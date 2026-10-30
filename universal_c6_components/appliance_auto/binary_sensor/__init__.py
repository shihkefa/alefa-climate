import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor
from esphome.const import CONF_ID
from .. import ApplianceAuto, AutoServiceBinarySensor

DEPENDENCIES = ['appliance_auto']
CONF_SERVICE = 'service'
CONF_PROFILE = 'profile'

CONFIG_SCHEMA = binary_sensor.binary_sensor_schema(AutoServiceBinarySensor).extend({
    cv.Required('appliance_id'): cv.use_id(ApplianceAuto),
    cv.Required(CONF_SERVICE): cv.int_range(min=0, max=0x7f),
    cv.Optional(CONF_PROFILE, default='any'): cv.one_of('any', 'ac', 'dehumidifier', lower=True),
})

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await binary_sensor.register_binary_sensor(var, config)
    await cg.register_component(var, config)
    parent = await cg.get_variable(config['appliance_id'])
    cg.add(var.set_parent(parent))
    cg.add(var.set_service(config[CONF_SERVICE]))
    cg.add(var.set_profile({'any': 0, 'ac': 1, 'dehumidifier': 2}[config[CONF_PROFILE]]))
    cg.add(parent.register_service_binary_sensor(var))
