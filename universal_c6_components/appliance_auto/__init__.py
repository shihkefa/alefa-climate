import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import uart, text_sensor
from esphome.components.matter import MatterComponent
from esphome.const import CONF_ID
DEPENDENCIES = ['uart', 'matter']
AUTO_LOAD = ['climate', 'fan', 'text_sensor', 'switch', 'number', 'binary_sensor', 'select', 'sensor']
ns = cg.esphome_ns.namespace('appliance_auto')
ApplianceAuto = ns.class_('ApplianceAuto', cg.Component, uart.UARTDevice)
AutoClimate = ns.class_('AutoClimate', cg.esphome_ns.namespace('climate').class_('Climate'))
AutoFan = ns.class_('AutoFan', cg.esphome_ns.namespace('fan').class_('Fan'))
AutoServiceSwitch = ns.class_('AutoServiceSwitch', cg.esphome_ns.namespace('switch').class_('Switch'), cg.Component)
AutoServiceNumber = ns.class_('AutoServiceNumber', cg.esphome_ns.namespace('number').class_('Number'), cg.Component)
AutoServiceBinarySensor = ns.class_('AutoServiceBinarySensor', cg.esphome_ns.namespace('binary_sensor').class_('BinarySensor'), cg.Component)
AutoServiceSelect = ns.class_('AutoServiceSelect', cg.esphome_ns.namespace('select').class_('Select'), cg.Component)
AutoServiceSensor = ns.class_('AutoServiceSensor', cg.esphome_ns.namespace('sensor').class_('Sensor'), cg.Component)
CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(ApplianceAuto),
    cv.Required('matter_id'): cv.use_id(MatterComponent),
    **{cv.Optional(k): text_sensor.text_sensor_schema() for k in ['status','brand','model','protocol']},
}).extend(cv.COMPONENT_SCHEMA).extend(uart.UART_DEVICE_SCHEMA)
async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)
    cg.add(var.set_matter(await cg.get_variable(config['matter_id'])))
    for k in ['status','brand','model','protocol']:
        if k in config:
            sensor = await text_sensor.new_text_sensor(config[k])
            cg.add(getattr(var, 'set_'+k+'_sensor')(sensor))
