Alpha Universal C6 v0.6.11 — Matter over Thread 多功能版

ESP32-C6 SuperMini 4MB，USB 更新；UART TX=GPIO19、RX=GPIO20。
Factory BIN 請在 ESPHome Web 選擇 alefa-universal.factory.bin，位址 0x0。
按住 BOOT 再接 USB，完成後放開 BOOT；正常開機不要按住 BOOT。

公開原始碼使用 `CHANGE_ME_SETUP_PASSWORD` 作為設定入口的佔位值。部署前請在本機將 `setup_password` 改成自己的 8–63 字元密碼；請勿把個人密碼或編譯產物提交到公開倉庫。Matter 配對碼由每台裝置獨立產生。
Wi-Fi 提供 ESPHome API 與設定頁；Matter 使用 BLE 配對加入 Thread。配對期間會暫停 Wi-Fi，Matter／Thread 配對完成後 Wi-Fi 會停用，這是預期行為。
空調尚未完成 UART 握手前，設定 AP 會保持啟用，可先連線進行設定；RGB 綠燈必須等到收到有效且最新的空調狀態封包後才會亮起。
Matter 配對完成後 Wi-Fi 會停用。若要重新設定，請長按 BOOT 約 10 秒清除 Wi-Fi 與 Matter 配對，再重新配網／配對。
空調支援冷氣、暖氣、送風、自動、除濕，16–30°C；除濕機使用 Fan 類型。HA 會建立除濕機的風速、計時、濕度／強度、模式、提示音、童鎖、除霉等多個實體。
本版設定頁在設備完成通訊握手後會顯示「設備控制」區塊：空調顯示電源、模式、16–30°C、風速、上下／左右擺風、定時與 TaiSEIA 服務；除濕機顯示電源、風速、模式、目標濕度、除濕強度與服務。頁面會依自動辨識結果只顯示對應設備。
通電的 ESP32-C6 使用 OpenThread FTD，具備成為 Thread Router／mesh 中繼的條件；實際角色由 Thread 網路動態決定，仍需家中 Apple／Google Thread Border Router。

v0.6.11 修正 Matter FanControl 風速寫入（FanMode、PercentSetting、SpeedSetting）與上下擺風支援，並掛上空調 FanControl delegate；新增空調功率、累計能耗、電流、電壓與蜂鳴器 HA 實體。Matter 同端點 FanControl／上下擺風，RGB（GRB 通道）狀態燈在空調握手成功時優先顯示綠色；無即時空調連線但已完成 Matter 配對時顯示藍色，未配網時顯示紅色。Thread 配對完成後 Wi-Fi 停用，避免 2.4 GHz 射頻互相干擾。保留每台裝置獨立 QR／PAKE 資料與既有 NVS。
v0.6.11 將 AC FanControl／上下擺風移到空調端點 1；若從 v0.6.8 或更早版本更新，先從 Matter 控制器移除舊裝置，再用 Factory BIN 燒錄並重新配對。

