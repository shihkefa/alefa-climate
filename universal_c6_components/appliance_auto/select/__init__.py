import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import select
from esphome.const import CONF_ID, CONF_OPTIONS
from .. import ApplianceAuto, AutoServiceSelect

DEPENDENCIES = ['appliance_auto']
CONF_SERVICE = 'service'
CONF_PROFILE = 'profile'

CONFIG_SCHEMA = select.select_schema(AutoServiceSelect).extend({
    cv.Required('appliance_id'): cv.use_id(ApplianceAuto),
    cv.Required(CONF_SERVICE): cv.int_range(min=0, max=0x7f),
    cv.Required(CONF_OPTIONS): cv.All(cv.ensure_list(cv.string_strict), cv.Length(min=1, max=32)),
    cv.Optional(CONF_PROFILE, default='any'): cv.one_of('any', 'ac', 'dehumidifier', lower=True),
})

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await select.register_select(var, config, options=config[CONF_OPTIONS])
    await cg.register_component(var, config)
    parent = await cg.get_variable(config['appliance_id'])
    cg.add(var.set_parent(parent))
    cg.add(var.set_service(config[CONF_SERVICE]))
    cg.add(var.set_profile({'any': 0, 'ac': 1, 'dehumidifier': 2}[config[CONF_PROFILE]]))
    cg.add(parent.register_service_select(var))
