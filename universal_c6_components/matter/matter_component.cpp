#include "esphome/core/defines.h"
#ifdef USE_MATTER

#include "esphome/components/network/util.h"
#ifdef USE_WIFI
#include "esphome/components/wifi/wifi_component.h"
#endif // USE_WIFI
#ifdef USE_OPENTHREAD
#include "esphome/components/openthread/openthread.h"
#endif // USE_OPENTHREAD
#include "esphome/core/application.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include "esphome/core/preferences.h"
#include "matter_component.h"
#include "esphome/components/appliance_auto/appliance_auto.h"
#ifdef USE_MATTER_BLE_THREAD
#include <platform/ESP32/OpenthreadLauncher.h>
#endif

#include <algorithm>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <esp_matter_client.h>
#include <esp_random.h>
#include <esp_matter_providers.h>
#include <nvs.h>
#include <string>

#include <app/server/Server.h>
#include <crypto/CHIPCryptoPAL.h>
#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
#include <platform/ESP32/ThreadStackManagerImpl.h>
#endif // CHIP_DEVICE_CONFIG_ENABLE_THREAD
#include <lib/support/Base64.h>
#ifdef USE_OPENTHREAD
#include <openthread/ip6.h>
#endif // USE_OPENTHREAD
#include <setup_payload/ManualSetupPayloadGenerator.h>
#include <setup_payload/QRCodeSetupPayloadGenerator.h>
#include <setup_payload/SetupPayload.h>

static const char *const TAG = "matter";
static const char *const TAG_EVENT = "matter.event";

// Keys in the "chip-factory" NVS namespace, matching CHIP's ESP32Config key
// names.
static constexpr const char *NVS_NAMESPACE = "chip-factory";
static constexpr const char *NVS_KEY_DISCRIMINATOR = "discriminator";
static constexpr const char *NVS_KEY_PASSCODE = "pin-code";
static constexpr const char *NVS_KEY_ITERATION_COUNT = "iteration-count";
static constexpr const char *NVS_KEY_SALT = "salt";
static constexpr const char *NVS_KEY_VERIFIER = "verifier";

static constexpr uint32_t SPAKE2P_ITERATION_COUNT = 1000;
static constexpr size_t SPAKE2P_SALT_LENGTH = 32;

// The QR/manual payload and the PAKE verifier must come from the same
// commissionable-data provider.  Keep the derived values in RAM after loading
// them from NVS so ESP-Matter uses exactly the values shown by the portal.
static uint16_t g_commissioning_discriminator = 0;
static uint32_t g_commissioning_passcode = 0;
static uint32_t g_commissioning_iterations = SPAKE2P_ITERATION_COUNT;
static uint8_t g_commissioning_salt[SPAKE2P_SALT_LENGTH] = {};
static uint8_t g_commissioning_verifier[
    chip::Crypto::kSpake2p_VerifierSerialized_Length] = {};
static bool g_commissioning_data_ready = false;

static bool is_valid_passcode(uint32_t pin) {
  if (pin == 0 || pin > 99999998)
    return false;
  static constexpr uint32_t FORBIDDEN[] = {
      11111111, 22222222, 33333333, 44444444, 55555555, 66666666,
      77777777, 88888888, 99999999, 12345678, 87654321,
  };
  for (uint32_t f : FORBIDDEN) {
    if (pin == f)
      return false;
  }
  return true;
}

static bool generate_spake_verifier(
    uint32_t passcode, const uint8_t salt[SPAKE2P_SALT_LENGTH],
    uint8_t verifier_bytes[chip::Crypto::kSpake2p_VerifierSerialized_Length]) {
  chip::Crypto::Spake2pVerifier verifier;
  if (verifier.Generate(SPAKE2P_ITERATION_COUNT,
                        chip::ByteSpan(salt, SPAKE2P_SALT_LENGTH), passcode) !=
      CHIP_NO_ERROR) {
    return false;
  }
  chip::MutableByteSpan verifier_span(
      verifier_bytes, chip::Crypto::kSpake2p_VerifierSerialized_Length);
  return verifier.Serialize(verifier_span) == CHIP_NO_ERROR;
}

static void persist_spake_values(nvs_handle_t handle) {
  char salt_b64[BASE64_ENCODED_LEN(SPAKE2P_SALT_LENGTH) + 1];
  salt_b64[chip::Base64Encode32(g_commissioning_salt, SPAKE2P_SALT_LENGTH,
                                salt_b64)] = '\0';
  char verifier_b64[BASE64_ENCODED_LEN(
                        chip::Crypto::kSpake2p_VerifierSerialized_Length) +
                    1];
  verifier_b64[chip::Base64Encode32(
      g_commissioning_verifier,
      chip::Crypto::kSpake2p_VerifierSerialized_Length, verifier_b64)] = '\0';
  nvs_set_u32(handle, NVS_KEY_DISCRIMINATOR, g_commissioning_discriminator);
  nvs_set_u32(handle, NVS_KEY_PASSCODE, g_commissioning_passcode);
  nvs_set_u32(handle, NVS_KEY_ITERATION_COUNT, g_commissioning_iterations);
  nvs_set_str(handle, NVS_KEY_SALT, salt_b64);
  nvs_set_str(handle, NVS_KEY_VERIFIER, verifier_b64);
  nvs_commit(handle);
}

class EspHomeCommissionableDataProvider final
    : public chip::DeviceLayer::CommissionableDataProvider {
 public:
  CHIP_ERROR GetSetupDiscriminator(uint16_t &setupDiscriminator) override {
    if (!g_commissioning_data_ready)
      return CHIP_ERROR_INCORRECT_STATE;
    setupDiscriminator = g_commissioning_discriminator;
    return CHIP_NO_ERROR;
  }

  CHIP_ERROR SetSetupDiscriminator(uint16_t setupDiscriminator) override {
    if (setupDiscriminator > chip::kMaxDiscriminatorValue)
      return CHIP_ERROR_INVALID_ARGUMENT;
    g_commissioning_discriminator = setupDiscriminator;
    return CHIP_NO_ERROR;
  }

  CHIP_ERROR GetSpake2pIterationCount(uint32_t &iterationCount) override {
    if (!g_commissioning_data_ready)
      return CHIP_ERROR_INCORRECT_STATE;
    iterationCount = g_commissioning_iterations;
    return CHIP_NO_ERROR;
  }

  CHIP_ERROR GetSpake2pSalt(chip::MutableByteSpan &saltBuf) override {
    if (!g_commissioning_data_ready)
      return CHIP_ERROR_INCORRECT_STATE;
    if (saltBuf.size() < SPAKE2P_SALT_LENGTH)
      return CHIP_ERROR_BUFFER_TOO_SMALL;
    memcpy(saltBuf.data(), g_commissioning_salt, SPAKE2P_SALT_LENGTH);
    saltBuf = chip::MutableByteSpan(saltBuf.data(), SPAKE2P_SALT_LENGTH);
    return CHIP_NO_ERROR;
  }

  CHIP_ERROR GetSpake2pVerifier(chip::MutableByteSpan &verifierBuf,
                                size_t &outVerifierLen) override {
    outVerifierLen = chip::Crypto::kSpake2p_VerifierSerialized_Length;
    if (!g_commissioning_data_ready)
      return CHIP_ERROR_INCORRECT_STATE;
    if (verifierBuf.size() < outVerifierLen)
      return CHIP_ERROR_BUFFER_TOO_SMALL;
    memcpy(verifierBuf.data(), g_commissioning_verifier, outVerifierLen);
    verifierBuf = chip::MutableByteSpan(verifierBuf.data(), outVerifierLen);
    return CHIP_NO_ERROR;
  }

  CHIP_ERROR GetSetupPasscode(uint32_t &setupPasscode) override {
    if (!g_commissioning_data_ready)
      return CHIP_ERROR_INCORRECT_STATE;
    setupPasscode = g_commissioning_passcode;
    return CHIP_NO_ERROR;
  }

  CHIP_ERROR SetSetupPasscode(uint32_t setupPasscode) override {
    if (!is_valid_passcode(setupPasscode))
      return CHIP_ERROR_INVALID_ARGUMENT;
    g_commissioning_passcode = setupPasscode;
    return CHIP_NO_ERROR;
  }
};

static EspHomeCommissionableDataProvider g_commissionable_data_provider;

// Loads existing commissioning data from NVS, or generates random values and
// stores them. The custom provider below reads the same values, so the QR code
// and the PAKE verifier always describe the same commissioning identity.
static bool load_or_generate_commissioning_data(uint16_t &discriminator,
                                                uint32_t &passcode) {
  nvs_handle_t handle;
  if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) {
    ESP_LOGE(TAG, "Failed to open NVS namespace '%s'", NVS_NAMESPACE);
    return false;
  }

  uint32_t stored_discriminator, stored_passcode;
  bool already_provisioned =
      (nvs_get_u32(handle, NVS_KEY_DISCRIMINATOR, &stored_discriminator) ==
           ESP_OK &&
       nvs_get_u32(handle, NVS_KEY_PASSCODE, &stored_passcode) == ESP_OK);
  if (already_provisioned) {
    discriminator = static_cast<uint16_t>(stored_discriminator);
    passcode = stored_passcode;
    g_commissioning_discriminator = discriminator;
    g_commissioning_passcode = passcode;

    uint32_t stored_iterations = SPAKE2P_ITERATION_COUNT;
    nvs_get_u32(handle, NVS_KEY_ITERATION_COUNT, &stored_iterations);
    g_commissioning_iterations = stored_iterations;
    char salt_b64[BASE64_ENCODED_LEN(SPAKE2P_SALT_LENGTH) + 1] = {};
    char verifier_b64[BASE64_ENCODED_LEN(
                          chip::Crypto::kSpake2p_VerifierSerialized_Length) +
                      1] = {};
    size_t salt_len = sizeof(salt_b64);
    size_t verifier_len = sizeof(verifier_b64);
    bool loaded =
        nvs_get_str(handle, NVS_KEY_SALT, salt_b64, &salt_len) == ESP_OK &&
        nvs_get_str(handle, NVS_KEY_VERIFIER, verifier_b64, &verifier_len) ==
            ESP_OK &&
        chip::Base64Decode32(salt_b64, strlen(salt_b64),
                             g_commissioning_salt) == SPAKE2P_SALT_LENGTH &&
        chip::Base64Decode32(verifier_b64, strlen(verifier_b64),
                             g_commissioning_verifier) ==
            chip::Crypto::kSpake2p_VerifierSerialized_Length;
    if (!loaded) {
      for (size_t i = 0; i < SPAKE2P_SALT_LENGTH; i += sizeof(uint32_t)) {
        uint32_t r = esp_random();
        memcpy(g_commissioning_salt + i, &r,
               std::min(sizeof(uint32_t), SPAKE2P_SALT_LENGTH - i));
      }
      if (!generate_spake_verifier(g_commissioning_passcode,
                                   g_commissioning_salt,
                                   g_commissioning_verifier)) {
        nvs_close(handle);
        return false;
      }
      persist_spake_values(handle);
    }
    g_commissioning_data_ready = true;
    nvs_close(handle);
    return true;
  }

#ifdef MATTER_DISCRIMINATOR
  discriminator = MATTER_DISCRIMINATOR;
#else
  discriminator = (uint16_t)(esp_random() & 0x0FFFu);
#endif // MATTER_DISCRIMINATOR

#ifdef MATTER_PASSCODE
  passcode = MATTER_PASSCODE;
#else
  do {
    passcode = (esp_random() % 99999998u) + 1u;
  } while (!is_valid_passcode(passcode));
#endif // MATTER_PASSCODE

  // Generate random salt using the ESP32 hardware RNG.
  uint8_t salt[SPAKE2P_SALT_LENGTH];
  for (size_t i = 0; i < SPAKE2P_SALT_LENGTH; i += sizeof(uint32_t)) {
    uint32_t r = esp_random();
    size_t bytes = std::min(sizeof(uint32_t), SPAKE2P_SALT_LENGTH - i);
    memcpy(salt + i, &r, bytes);
  }

  g_commissioning_discriminator = discriminator;
  g_commissioning_passcode = passcode;
  g_commissioning_iterations = SPAKE2P_ITERATION_COUNT;
  memcpy(g_commissioning_salt, salt, sizeof(g_commissioning_salt));
  if (!generate_spake_verifier(g_commissioning_passcode,
                               g_commissioning_salt,
                               g_commissioning_verifier)) {
    ESP_LOGE(TAG, "Failed to generate Spake2p verifier");
    nvs_close(handle);
    return false;
  }
  persist_spake_values(handle);
  g_commissioning_data_ready = true;
  nvs_close(handle);

  return true;
}

namespace esphome::matter {

MatterComponent *global_matter_component =
    nullptr; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

static void event_callback(const ChipDeviceEvent *event, intptr_t arg) {
  if (global_matter_component != nullptr && global_matter_component->ready())
    global_matter_component->refresh_pairing_state();
  switch (event->Type) {
  case chip::DeviceLayer::DeviceEventType::kInterfaceIpAddressChanged:
    ESP_LOGD(TAG_EVENT, "Interface IP Address changed");
    break;
  case chip::DeviceLayer::DeviceEventType::kCommissioningComplete:
    ESP_LOGI(TAG_EVENT, "Commissioning complete");
    if (global_matter_component != nullptr)
      global_matter_component->request_wifi_resume_();
    break;
  case chip::DeviceLayer::DeviceEventType::kFailSafeTimerExpired:
    ESP_LOGI(TAG_EVENT, "Commissioning failed, fail safe timer expired");
    if (global_matter_component != nullptr)
      global_matter_component->request_wifi_resume_();
    break;
  case chip::DeviceLayer::DeviceEventType::kCommissioningSessionStarted:
    ESP_LOGI(TAG_EVENT, "Commissioning session started");
    if (global_matter_component != nullptr)
      global_matter_component->request_wifi_pause_();
    break;
  case chip::DeviceLayer::DeviceEventType::kCommissioningSessionStopped:
    ESP_LOGI(TAG_EVENT, "Commissioning session stopped");
    break;
  case chip::DeviceLayer::DeviceEventType::kCommissioningWindowOpened:
    ESP_LOGI(TAG_EVENT, "Commissioning window opened");
    break;
  case chip::DeviceLayer::DeviceEventType::kCommissioningWindowClosed:
    ESP_LOGI(TAG_EVENT, "Commissioning window closed");
    break;
  case chip::DeviceLayer::DeviceEventType::kFabricRemoved:
    ESP_LOGI(TAG_EVENT, "Fabric removed");
#ifdef USE_TEXT_SENSOR
    global_matter_component->schedule_fabric_sensor_update();
#endif
    // TODO: reopen commissioning window?
    break;
  case chip::DeviceLayer::DeviceEventType::kFabricWillBeRemoved:
    ESP_LOGI(TAG_EVENT, "Fabric will be removed");
    break;
  case chip::DeviceLayer::DeviceEventType::kFabricUpdated:
    ESP_LOGI(TAG_EVENT, "Fabric is updated");
#ifdef USE_TEXT_SENSOR
    global_matter_component->schedule_fabric_sensor_update();
#endif
    break;
  case chip::DeviceLayer::DeviceEventType::kFabricCommitted:
    ESP_LOGI(TAG_EVENT, "Fabric is committed");
#ifdef USE_TEXT_SENSOR
    global_matter_component->schedule_fabric_sensor_update();
#endif
    break;
  case chip::DeviceLayer::DeviceEventType::kDnssdRestartNeeded:
    ESP_LOGD(TAG_EVENT, "DNS-SD restart needed");
    break;
  case chip::DeviceLayer::DeviceEventType::kBindingsChangedViaCluster:
    ESP_LOGI(TAG_EVENT, "Bindings updated");
    break;
  case chip::DeviceLayer::DeviceEventType::kServerReady:
    ESP_LOGI(TAG_EVENT, "Server ready!");
    break;
  case chip::DeviceLayer::DeviceEventType::kSecureSessionEstablished: {
    const auto &s = event->SecureSessionEstablished;
    ESP_LOGI(TAG_EVENT,
             "Secure session established: %s node=0x%016" PRIx64
             " fabric=%u session=%u",
             s.SecureSessionType == 2   ? "CASE"
             : s.SecureSessionType == 1 ? "PASE"
                                        : "?",
             s.PeerNodeId, s.FabricIndex, s.LocalSessionId);
    break;
  }
  case chip::DeviceLayer::DeviceEventType::kWiFiConnectivityChange: // 0x8000
    ESP_LOGV(TAG_EVENT, "WiFiConnectivityChange");
    break;
  case chip::DeviceLayer::DeviceEventType::
      kThreadConnectivityChange: { // 0x8001
    switch (event->ThreadConnectivityChange.Result) {
    case chip::DeviceLayer::kConnectivity_Established:
      ESP_LOGI(TAG_EVENT, "ThreadConnectivityChange: established");
      break;
    case chip::DeviceLayer::kConnectivity_Lost:
      ESP_LOGW(TAG_EVENT, "ThreadConnectivityChange: lost");
      break;
    case chip::DeviceLayer::kConnectivity_NoChange:
      ESP_LOGV(TAG_EVENT, "ThreadConnectivityChange: no_change");
      break;
    default:
      ESP_LOGV(TAG_EVENT, "ThreadConnectivityChange: unknown (%d)",
               static_cast<int>(event->ThreadConnectivityChange.Result));
      break;
    }
    break;
  }
  case chip::DeviceLayer::DeviceEventType::
      kInternetConnectivityChange: // 0x8002
    ESP_LOGV(TAG_EVENT, "InternetConnectivityChange");
    break;
  case chip::DeviceLayer::DeviceEventType::kThreadStateChange: { // 0x800B
#ifdef USE_OPENTHREAD
    const auto &thread_state = event->ThreadStateChange;
    if (thread_state.RoleChanged) {
      auto lock = esphome::openthread::InstanceLock::try_acquire(1000);
      if (lock) {
        const otDeviceRole role = otThreadGetDeviceRole(lock.get_instance());
        ESP_LOGD(TAG_EVENT, "Thread role changed to %s",
                 otThreadDeviceRoleToString(role));
      } else {
        ESP_LOGW(TAG_EVENT, "Thread role changed; unable to acquire instance");
      }
    }
    if (thread_state.NetDataChanged)
      ESP_LOGV(TAG_EVENT, "Thread network data changed");
    if (thread_state.ChildNodesChanged)
      ESP_LOGV(TAG_EVENT, "Thread child nodes changed");
    if (thread_state.AddressChanged) {
      ESP_LOGD(TAG_EVENT, "Thread address(es) changed");
      auto lock = esphome::openthread::InstanceLock::try_acquire(1000);
      if (lock) {
        otInstance *instance = lock.get_instance();
        for (const otNetifAddress *addr = otIp6GetUnicastAddresses(instance);
             addr != nullptr; addr = addr->mNext) {
          char address[OT_IP6_ADDRESS_STRING_SIZE];
          otIp6AddressToString(&addr->mAddress, address, sizeof(address));
          if (addr->mValid)
            ESP_LOGD(TAG_EVENT, "  address %s/%u rloc=%s", address,
                     static_cast<unsigned>(addr->mPrefixLength),
                     YESNO(addr->mRloc));
          else
            ESP_LOGW(TAG_EVENT, "  [INVALID] address %s/%u rloc=%s", address,
                     static_cast<unsigned>(addr->mPrefixLength),
                     YESNO(addr->mRloc));
        }
      } else {
        ESP_LOGW(TAG_EVENT,
                 "ThreadStateChange: address list unavailable; failed to "
                 "acquire OpenThread lock");
      }
    }
    if (!thread_state.RoleChanged && !thread_state.AddressChanged &&
        !thread_state.NetDataChanged && !thread_state.ChildNodesChanged)
      ESP_LOGV(TAG_EVENT, "ThreadStateChange: flags=0x%08" PRIx32,
               thread_state.OpenThread.Flags);
#endif // USE_OPENTHREAD
    break;
  }
  case chip::DeviceLayer::DeviceEventType::kDnssdInitialized: // 0x8012
    ESP_LOGD(TAG_EVENT, "DnssdInitialized");
    break;
  default:
    ESP_LOGV(TAG_EVENT, "0x%04X", event->Type);
    break;
  }
}

void MatterComponent::setup() {
  global_matter_component = this;
  if (!appliance_auto::global_appliance->configured()) {
    ESP_LOGI(TAG, "Waiting for appliance identification before Matter commissioning");
    return;
  }
  uint16_t discriminator;
  uint32_t passcode;
  if (!load_or_generate_commissioning_data(discriminator, passcode)) {
    this->mark_failed();
    return;
  }
  this->discriminator_ = discriminator;
  this->passcode_ = passcode;
  this->generate_commissioning_codes_();

#ifdef USE_TEXT_SENSOR
  this->publish_commissioning_code_sensors_();
#endif

  // Always update device-name so it stays in sync if the ESPHome device name
  // changes. This is the DN TXT record in _matterc._udp — what controllers show
  // in their UI.
  {
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
      nvs_set_str(h, "device-name", appliance_auto::global_appliance->device_name());
      nvs_commit(h);
      nvs_close(h);
    }
  }

  esp_matter::node::config_t node_config;
  std::snprintf(node_config.root_node.basic_information.node_label,
                sizeof(node_config.root_node.basic_information.node_label), "%s",
                appliance_auto::global_appliance->device_name());
  esp_matter::node_t *node = esp_matter::node::create(
      &node_config, endpoint_attribute_update_cb, nullptr);
  if (node == nullptr) {
    ESP_LOGE(TAG, "Failed to create Matter node");
    this->mark_failed();
    return;
  }

  if (!this->create_endpoints_(node)) {
    this->mark_failed();
    return;
  }

#ifdef USE_OPENTHREAD
  // ESPHome owns the OpenThread task/stack. connectedhomeip still needs its
  // ThreadStackManager bound to that existing instance for Thread diagnostics
  // and other Thread helpers.
  if (chip::DeviceLayer::ThreadStackMgrImpl()._AttachToThreadStack() !=
      CHIP_NO_ERROR) {
    ESP_LOGE(TAG, "Failed to attach ESPHome OpenThread stack to "
                  "connectedhomeip ThreadStackManager");
    this->mark_failed();
    return;
  }
#endif // USE_OPENTHREAD

#ifdef USE_MATTER_BLE_THREAD
  esp_openthread_platform_config_t thread_config = {
      .radio_config = {.radio_mode = RADIO_MODE_NATIVE},
      .host_config = {.host_connection_mode = HOST_CONNECTION_MODE_NONE},
      .port_config = {.storage_partition_name = "nvs",
                      .netif_queue_size = 10,
                      .task_queue_size = 10},
  };
  if (set_openthread_platform_config(&thread_config) != ESP_OK) {
    this->mark_failed();
    return;
  }
#endif
#ifdef CONFIG_CUSTOM_COMMISSIONABLE_DATA_PROVIDER
  // Must be installed before esp_matter::start().  Otherwise the QR code can
  // contain our per-device passcode while CHIP still verifies its default
  // provider, making PASE fail immediately after scanning.
  esp_matter::set_custom_commissionable_data_provider(
      &g_commissionable_data_provider);
#endif
  /* Matter start */
  esp_err_t err = esp_matter::start(event_callback);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to start Matter: %s", esp_err_to_name(err));
    this->mark_failed();
    return;
  } else {
    ESP_LOGD(TAG, "Matter started successfully");
  }

  esp_matter::client::binding_manager_init();
  replay_attribute_triggers(this);
  this->initialize_endpoint_mappings_();
  this->ready_.store(true);
  auto snapshot_err = chip::DeviceLayer::PlatformMgr().ScheduleWork([](intptr_t arg) {
    reinterpret_cast<MatterComponent *>(arg)->refresh_pairing_state();
  }, reinterpret_cast<intptr_t>(this));
  if (snapshot_err != CHIP_NO_ERROR)
    ESP_LOGE(TAG, "Cannot refresh pairing state: %" CHIP_ERROR_FORMAT, snapshot_err.Format());
#ifdef USE_TEXT_SENSOR
  this->publish_fabric_sensors_();
#endif
}

void MatterComponent::loop() {
#ifdef USE_WIFI
  // The Matter event path normally resumes Wi-Fi. This guard is deliberately
  // longer than the CHIP fail-safe timer, so a cancelled or unusual
  // commissioning path cannot leave the ESPHome API offline indefinitely.
  if (this->wifi_paused_.load() &&
      static_cast<uint32_t>(millis() - this->wifi_pause_started_ms_) >=
          180000UL) {
    ESP_LOGW(TAG, "Matter commissioning Wi-Fi pause timed out; restoring Wi-Fi");
    this->request_wifi_resume_();
  }
#endif // USE_WIFI
}

void MatterComponent::request_wifi_pause_() {
#ifdef USE_WIFI
  if (!this->pause_wifi_during_commissioning_ || this->wifi_paused_.load())
    return;
  if (this->wifi_pause_pending_.exchange(true))
    return;
  this->defer([this]() {
    this->wifi_pause_pending_.store(false);
    this->apply_wifi_pause_();
  });
#endif // USE_WIFI
}

void MatterComponent::request_wifi_resume_() {
#ifdef USE_WIFI
  if (!this->wifi_paused_.load())
    return;
  if (this->wifi_resume_pending_.exchange(true))
    return;
  this->defer([this]() {
    this->wifi_resume_pending_.store(false);
    this->apply_wifi_resume_();
  });
#endif // USE_WIFI
}

void MatterComponent::apply_wifi_pause_() {
#ifdef USE_WIFI
  if (!this->pause_wifi_during_commissioning_ || this->wifi_paused_.load())
    return;
  auto *wifi = wifi::global_wifi_component;
  if (wifi == nullptr || wifi->is_disabled())
    return;
  wifi->disable();
  this->wifi_pause_started_ms_ = millis();
  this->wifi_paused_.store(true);
  ESP_LOGI(TAG, "Wi-Fi paused during Matter Thread commissioning");
#endif // USE_WIFI
}

void MatterComponent::apply_wifi_resume_() {
#ifdef USE_WIFI
  if (!this->wifi_paused_.load())
    return;
  auto *wifi = wifi::global_wifi_component;
  if (wifi != nullptr)
    wifi->enable();
  this->wifi_paused_.store(false);
  ESP_LOGI(TAG, "Wi-Fi resumed after Matter Thread commissioning");
#endif // USE_WIFI
}

void MatterComponent::factory_reset() {
  appliance_auto::global_appliance->clear_profile();
#ifdef USE_MATTER_BLE_THREAD
  if (this->ready()) {
  // SDK reset clears fabrics AND the runtime-provisioned Thread dataset.
  auto reset_err = chip::DeviceLayer::PlatformMgr().ScheduleWork([](intptr_t) {
    auto err = esp_matter::factory_reset();
    if (err != ESP_OK) ESP_LOGE(TAG, "Matter reset failed: %s", esp_err_to_name(err));
  });
  if (reset_err != CHIP_NO_ERROR)
    ESP_LOGE(TAG, "Cannot schedule Matter reset: %" CHIP_ERROR_FORMAT, reset_err.Format());
  return;
  }
#endif
  ESP_LOGW(TAG, "Matter factory reset. Erasing fabric data and rebooting");
  for (const char *ns : {"chip-config", "chip-counters", "CHIP_KVS", "openthread"}) {
    nvs_handle_t h;
    if (nvs_open(ns, NVS_READWRITE, &h) == ESP_OK) {
      nvs_erase_all(h);
      nvs_commit(h);
      nvs_close(h);
    }
  }
  App.safe_reboot();
}

void MatterComponent::factory_reset_all() {
  // Physical 10-second button action: also forget ESPHome Wi-Fi credentials.
  global_preferences->reset();
  this->factory_reset();
  // When Matter is already running, factory_reset() schedules the SDK erase
  // asynchronously and intentionally returns. Reboot after the erase has had
  // time to run so Wi-Fi starts its AP fallback with the cleared credentials.
  this->set_timeout("factory_reset_all_reboot", 2000, []() {
    ESP_LOGW(TAG, "Matter/Wi-Fi factory reset complete; rebooting into setup AP");
    App.safe_reboot();
  });
}

void MatterComponent::refresh_pairing_state() {
  auto &server = chip::Server::GetInstance();
  this->commissioned_.store(server.GetFabricTable().FabricCount() != 0);
  this->pairing_open_.store(server.GetCommissioningWindowManager().IsCommissioningWindowOpen());
}

void MatterComponent::reopen_pairing() {
  if (!this->ready()) return;
  auto schedule_err = chip::DeviceLayer::PlatformMgr().ScheduleWork([](intptr_t arg) {
    auto *self = reinterpret_cast<MatterComponent *>(arg);
    auto &server = chip::Server::GetInstance();
    if (server.GetFabricTable().FabricCount() == 0 &&
        !server.GetCommissioningWindowManager().IsCommissioningWindowOpen()) {
      auto err = server.GetCommissioningWindowManager().OpenBasicCommissioningWindow(
          chip::System::Clock::Seconds16(900), chip::CommissioningWindowAdvertisement::kAllSupported);
      if (err != CHIP_NO_ERROR) ESP_LOGE(TAG, "Cannot reopen commissioning: %" CHIP_ERROR_FORMAT, err.Format());
    }
    self->refresh_pairing_state();
  }, reinterpret_cast<intptr_t>(this));
  if (schedule_err != CHIP_NO_ERROR)
    ESP_LOGE(TAG, "Cannot schedule pairing: %" CHIP_ERROR_FORMAT, schedule_err.Format());
}

void MatterComponent::generate_commissioning_codes_() {
  chip::SetupPayload payload;
  payload.version = 0;
  payload.vendorID = CHIP_DEVICE_CONFIG_DEVICE_VENDOR_ID;
  payload.productID = CHIP_DEVICE_CONFIG_DEVICE_PRODUCT_ID;
  payload.commissioningFlow = chip::CommissioningFlow::kStandard;
#if defined(USE_MATTER_BLE_THREAD)
  payload.rendezvousInformation.SetValue(chip::RendezvousInformationFlag::kBLE);
#elif defined(USE_OPENTHREAD) || defined(USE_WIFI) || defined(USE_ETHERNET)
  payload.rendezvousInformation.SetValue(
      chip::RendezvousInformationFlag::kOnNetwork);
#else
  payload.rendezvousInformation.SetValue(chip::RendezvousInformationFlag::kBLE);
#endif // defined(USE_OPENTHREAD) || defined(USE_WIFI) || defined(USE_ETHERNET)
  payload.discriminator.SetLongValue(this->discriminator_);
  payload.setUpPINCode = this->passcode_;

  if (chip::QRCodeSetupPayloadGenerator(payload).payloadBase38Representation(
          this->qr_code_) != CHIP_NO_ERROR) {
    this->qr_code_.clear();
    ESP_LOGE(TAG, "  Failed to generate QR code");
  }

  if (chip::ManualSetupPayloadGenerator(payload)
          .payloadDecimalStringRepresentation(this->manual_pairing_code_) !=
      CHIP_NO_ERROR) {
    this->manual_pairing_code_.clear();
    ESP_LOGE(TAG, "  Failed to generate manual pairing code");
  }
}

void MatterComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "Matter:");
  if (this->is_failed()) {
    ESP_LOGE(TAG, "  Failed to initialize!");
    return;
  }
  // setup() deliberately returns before starting CHIP while identification is
  // pending. ESPHome still calls dump_config(), even with CONFIG logs disabled.
  // LockChipStack() requires the mutex created by the initialized CHIP stack.
  if (!this->ready()) {
    ESP_LOGCONFIG(TAG, "  Waiting for appliance identification; Matter not started");
    return;
  }

  if (!this->qr_code_.empty()) {
    ESP_LOGCONFIG(TAG, "  SetupQRCode: %s", this->qr_code_.c_str());
    ESP_LOGCONFIG(
        TAG,
        "  QR URL: "
        "https://project-chip.github.io/connectedhomeip/qrcode.html?data=%s",
        this->qr_code_.c_str());
  }
  if (!this->manual_pairing_code_.empty()) {
    ESP_LOGCONFIG(
        TAG, "  Manual pairing code: %s",
        format_manual_pairing_code(this->manual_pairing_code_).c_str());
  }

  chip::DeviceLayer::PlatformMgr().LockChipStack();
  auto &server = chip::Server::GetInstance();
  ESP_LOGCONFIG(
      TAG, "  Commissioning window: %s",
      server.GetCommissioningWindowManager().IsCommissioningWindowOpen()
          ? "open"
          : "closed");
  const auto &fabric_table = server.GetFabricTable();
  if (fabric_table.FabricCount() == 0) {
    ESP_LOGCONFIG(TAG, "  Fabrics: none");
  } else {
    ESP_LOGCONFIG(TAG, "  Fabrics:");
    for (const auto &fabric : fabric_table) {
      char label[chip::kFabricLabelMaxLengthInBytes + 1] = {0};
      auto label_span = fabric.GetFabricLabel();
      memcpy(label, label_span.data(),
             std::min(label_span.size(), sizeof(label) - 1));
      uint64_t compressed_fabric_id = fabric.GetCompressedFabricId();
      uint64_t node_id = fabric.GetNodeId();
      ESP_LOGCONFIG(
          TAG,
          "    [%u] CompressedFabricId: 0x%08" PRIx32 "%08" PRIx32
          ", NodeId: 0x%08" PRIx32 "%08" PRIx32 " (%" PRIu64 ")"
          ", VendorId: 0x%04x%s%s",
          fabric.GetFabricIndex(), (uint32_t)(compressed_fabric_id >> 32),
          (uint32_t)(compressed_fabric_id), (uint32_t)(node_id >> 32),
          (uint32_t)(node_id), node_id, (uint16_t)fabric.GetVendorId(),
          label[0] ? ", Label: " : "", label);
    }
  }
  chip::DeviceLayer::PlatformMgr().UnlockChipStack();
}

} // namespace esphome::matter

#endif // USE_MATTER
