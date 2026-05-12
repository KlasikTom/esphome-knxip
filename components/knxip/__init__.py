import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID

knxip_ns = cg.esphome_ns.namespace('knxip')
KNXIPComponent = knxip_ns.class_('KNXIPComponent', cg.Component)

CONF_INDIVIDUAL_ADDRESS = "individual_address"
CONF_MULTICAST_GROUP    = "multicast_group"
CONF_PORT               = "port"
MULTI_CONF              = False

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(KNXIPComponent),
    cv.Required(CONF_INDIVIDUAL_ADDRESS): cv.string,
    cv.Optional(CONF_MULTICAST_GROUP, default="224.0.23.12"): cv.string,
    cv.Optional(CONF_PORT, default=3671): cv.port,
}).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    cg.add(var.set_individual_address(config[CONF_INDIVIDUAL_ADDRESS]))
    cg.add(var.set_multicast_group(config[CONF_MULTICAST_GROUP]))
    cg.add(var.set_port(config[CONF_PORT]))

    cg.add_library("WiFi", None)
    cg.add_build_flag("-Wno-unknown-pragmas")


__all__ = ['knxip_ns', 'KNXIPComponent', 'CONF_INDIVIDUAL_ADDRESS']
