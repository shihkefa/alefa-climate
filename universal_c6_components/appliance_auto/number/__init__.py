import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import number
from esphome.const import CONF_ID, CONF_MAX_VALUE, CONF_MIN_VALUE, CONF_STEP
from .. import ApplianceAuto, AutoServiceNumber

DEPENDENCIES = ['appliance_auto']
CONF_SERVICE = 'service'
CONF_PROFILE = 'profile'

CONFIG_SCHEMA = number.number_schema(AutoServiceNumber).extend({
    cv.Required('appliance_id'): cv.use_id(ApplianceAuto),
    cv.Required(CONF_SERVICE): cv.int_range(min=0, max=0x7f),
    cv.Required(CONF_MIN_VALUE): cv.float_range(min=0),
    cv.Required(CONF_MAX_VALUE): cv.float_range(min=0),
    cv.Optional(CONF_STEP, default=1.0): cv.float_range(min=0.001),
    cv.Optional(CONF_PROFILE, default='any'): cv.one_of('any', 'ac', 'dehumidifier', lower=True),
})

async def to_code(config):
    var = await number.new_number(
        config,
        min_value=config[CONF_MIN_VALUE],
        max_value=config[CONF_MAX_VALUE],
        step=config[CONF_STEP],
    )
    await cg.register_component(var, config)
    parent = await cg.get_variable(config['appliance_id'])
    cg.add(var.set_parent(parent))
    cg.add(var.set_service(config[CONF_SERVICE]))
    cg.add(var.set_profile({'any': 0, 'ac': 1, 'dehumidifier': 2}[config[CONF_PROFILE]]))
    cg.add(parent.register_service_number(var))
