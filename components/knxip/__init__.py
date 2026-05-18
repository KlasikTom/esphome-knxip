import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID

knxip_ns = cg.esphome_ns.namespace('knxip')
KNXIPComponent = knxip_ns.class_('KNXIPComponent', cg.Component)

CONF_INDIVIDUAL_ADDRESS = "individual_address"
CONF_MULTICAST_GROUP    = "multicast_group"
CONF_PORT               = "port"
CONF_FRIENDLY_NAME      = "friendly_name"
MULTI_CONF              = False
AUTO_LOAD               = ['binary_sensor', 'sensor', 'switch', 'output']

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(KNXIPComponent),
    cv.Required(CONF_INDIVIDUAL_ADDRESS): cv.string,
    cv.Optional(CONF_MULTICAST_GROUP, default="224.0.23.12"): cv.string,
    cv.Optional(CONF_PORT, default=3671): cv.port,
    cv.Optional(CONF_FRIENDLY_NAME, default="ESPHomeDevice"): cv.string,
}).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_individual_address(config[CONF_INDIVIDUAL_ADDRESS]))
    cg.add(var.set_multicast_group(config[CONF_MULTICAST_GROUP]))
    cg.add(var.set_port(config[CONF_PORT]))
    cg.add(var.set_friendly_name(config[CONF_FRIENDLY_NAME]))
    cg.add_library("WiFi", None)
    cg.add_build_flag("-Wno-unknown-pragmas")
    # POZOR: žádný cg.add_global s #include - způsobuje středník za direktivou


__all__ = ['knxip_ns', 'KNXIPComponent', 'CONF_INDIVIDUAL_ADDRESS']