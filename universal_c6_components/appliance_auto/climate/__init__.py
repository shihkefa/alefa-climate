import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import climate
from esphome.const import CONF_ID
from .. import ApplianceAuto, AutoClimate
DEPENDENCIES = ['appliance_auto']
CONFIG_SCHEMA = climate.climate_schema(AutoClimate).extend({cv.Required('appliance_id'): cv.use_id(ApplianceAuto)})
async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await climate.register_climate(var, config)
    parent = await cg.get_variable(config['appliance_id'])
    cg.add(var.set_parent(parent))
    cg.add(parent.set_climate(var))
