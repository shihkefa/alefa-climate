import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import web_server_base
from esphome.components.matter.types import MatterComponent
from esphome.const import CONF_ID

AUTO_LOAD = ["web_server_base"]
DEPENDENCIES = ["wifi", "matter"]
ns = cg.esphome_ns.namespace("pairing_portal")
PairingPortal = ns.class_("PairingPortal", cg.Component)
CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(PairingPortal),
    cv.GenerateID(web_server_base.CONF_WEB_SERVER_BASE_ID): cv.use_id(web_server_base.WebServerBase),
    cv.Required("matter_id"): cv.use_id(MatterComponent),
    cv.Required("password"): cv.All(cv.string_strict, cv.Length(min=8, max=63)),
}).extend(cv.COMPONENT_SCHEMA)

def _final_validate(config):
    from esphome.components import socket
    socket.consume_sockets(4, "pairing_portal")(config)
    socket.consume_sockets(4, "matter", socket.SocketType.UDP)(config)

FINAL_VALIDATE_SCHEMA = _final_validate

async def to_code(config):
    base = await cg.get_variable(config[web_server_base.CONF_WEB_SERVER_BASE_ID])
    parent = await cg.get_variable(config["matter_id"])
    var = cg.new_Pvariable(config[CONF_ID], base, parent)
    await cg.register_component(var, config)
    cg.add(var.set_password(config["password"]))
    cg.add_define("USE_WEBSERVER_AUTH")
    cg.add_library("wjtje/qr-code-generator-library", "1.7.0")
