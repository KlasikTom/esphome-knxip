import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor
from esphome.const import CONF_ID
from . import knxip_ns, KNXIPComponent

KNXIPSensor = knxip_ns.class_('KNXIPSensor', sensor.Sensor, cg.Component)
SensorDPT   = knxip_ns.enum('SensorDPT')

CONF_GA   = "group_address"
CONF_DPT  = "dpt"
CONF_KNXIP_ID = "knxip_id"

DEPENDENCIES = ['knxip']

DPT_MAP = {
    "9":  SensorDPT.DPT9,
    "14": SensorDPT.DPT14,
    "5":  SensorDPT.DPT5,
}

CONFIG_SCHEMA = sensor.sensor_schema(KNXIPSensor).extend({
    cv.Required(CONF_GA): cv.string,
    cv.Optional(CONF_DPT, default="9"): cv.one_of(*DPT_MAP.keys(), lower=True),
    cv.GenerateID(CONF_KNXIP_ID): cv.use_id(KNXIPComponent),
}).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await sensor.register_sensor(var, config)
    cg.add(var.set_ga(config[CONF_GA]))
    cg.add(var.set_dpt(DPT_MAP[config[CONF_DPT]]))
    knxip = await cg.get_variable(config[CONF_KNXIP_ID])
    cg.add(var.set_knxip(knxip))
