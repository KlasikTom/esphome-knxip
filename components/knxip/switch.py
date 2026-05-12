import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import switch
from esphome.const import CONF_ID, CONF_PIN
from esphome import pins
from . import knxip_ns, KNXIPComponent

KNXIPSwitch   = knxip_ns.class_('KNXIPSwitch', switch.Switch, cg.Component)

CONF_GA_COMMAND = "group_address_command"
CONF_GA_STATE   = "group_address_state"
CONF_KNXIP_ID   = "knxip_id"
DEPENDENCIES    = ['knxip', 'switch']
AUTO_LOAD       = ['switch']

CONFIG_SCHEMA = switch.switch_schema(KNXIPSwitch).extend({
    cv.Required(CONF_GA_COMMAND): cv.string,
    cv.Optional(CONF_GA_STATE):   cv.string,
    cv.Optional(CONF_PIN):        pins.gpio_output_pin_schema,
    cv.GenerateID(CONF_KNXIP_ID): cv.use_id(KNXIPComponent),
}).extend(cv.COMPONENT_SCHEMA)

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await switch.register_switch(var, config)
    cg.add(var.set_ga_command(config[CONF_GA_COMMAND]))
    if CONF_GA_STATE in config:
        cg.add(var.set_ga_state(config[CONF_GA_STATE]))
    if CONF_PIN in config:
        pin = await cg.gpio_pin_expression(config[CONF_PIN])
        cg.add(var.set_gpio_pin(pin))
    knxip = await cg.get_variable(config[CONF_KNXIP_ID])
    cg.add(var.set_knxip(knxip))
