import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import output
from esphome.const import CONF_ID
from . import knxip_ns, KNXIPComponent

KNXIPOutput   = knxip_ns.class_('KNXIPOutput', output.FloatOutput, cg.Component)

CONF_GA       = "group_address"
CONF_KNXIP_ID = "knxip_id"
DEPENDENCIES  = ['knxip', 'output']
AUTO_LOAD     = ['output']

CONFIG_SCHEMA = output.float_output_schema(KNXIPOutput).extend({
    cv.Required(CONF_GA): cv.string,
    cv.GenerateID(CONF_KNXIP_ID): cv.use_id(KNXIPComponent),
}).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await output.register_output(var, config)
    cg.add(var.set_ga(config[CONF_GA]))
    knxip = await cg.get_variable(config[CONF_KNXIP_ID])
    cg.add(var.set_knxip(knxip))
