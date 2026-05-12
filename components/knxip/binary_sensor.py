import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor
from esphome.const import CONF_ID
from . import knxip_ns, KNXIPComponent

KNXIPBinarySensor = knxip_ns.class_('KNXIPBinarySensor', binary_sensor.BinarySensor, cg.Component)

CONF_GA       = "group_address"
CONF_KNXIP_ID = "knxip_id"
DEPENDENCIES  = ['knxip', 'binary_sensor']
AUTO_LOAD     = ['binary_sensor']

CONFIG_SCHEMA = binary_sensor.binary_sensor_schema(KNXIPBinarySensor).extend({
    cv.Required(CONF_GA): cv.string,
    cv.GenerateID(CONF_KNXIP_ID): cv.use_id(KNXIPComponent),
}).extend(cv.COMPONENT_SCHEMA)

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await binary_sensor.register_binary_sensor(var, config)
    cg.add(var.set_ga(config[CONF_GA]))
    knxip = await cg.get_variable(config[CONF_KNXIP_ID])
    cg.add(var.set_knxip(knxip))
