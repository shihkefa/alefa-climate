import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import fan
from esphome.const import CONF_ID
from .. import ApplianceAuto, AutoFan
DEPENDENCIES = ['appliance_auto']
CONF_PROFILE = 'profile'
CONFIG_SCHEMA = fan.fan_schema(AutoFan).extend({
    cv.Required('appliance_id'): cv.use_id(ApplianceAuto),
    cv.Optional(CONF_PROFILE, default='any'): cv.one_of('any', 'ac', 'dehumidifier', lower=True),
})
async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await fan.register_fan(var, config)
    parent = await cg.get_variable(config['appliance_id'])
    cg.add(var.set_parent(parent))
    profile = config[CONF_PROFILE]
    cg.add(var.set_profile({'any': 0, 'ac': 1, 'dehumidifier': 2}[profile]))
    if profile == 'ac':
        cg.add(parent.set_ac_fan(var))
    else:
        cg.add(parent.set_fan(var))
