#pragma once
#include "esphome/core/component.h"
#include "esphome/core/log.h"
#include "esphome/components/wifi/wifi_component.h"
#include <WiFiUdp.h>
#include <WiFi.h>
#include <functional>
#include <vector>
#include "knxip.h"
#include "knxip_tunnel.h"

static const char* const TAG_KNXIP = "knxip";

namespace esphome {
namespace knxip {

using GACallback = std::function<void(const ParsedCEMI&)>;
struct GAListener { uint16_t ga; GACallback cb; };

class KNXIPComponent : public Component {
 public:
    // ── Configuration setters ─────────────────────────────────────────────────
    void set_individual_address(const std::string& s) { ia_ = ia_parse(s.c_str()); }
    void set_multicast_group(const std::string& s)    { mcast_.fromString(s.c_str()); }
    void set_port(uint16_t p)                         { port_ = p; }
    void set_friendly_name(const char* n)             { name_ = n; }

    uint16_t get_ia() const { return ia_; }
    bool is_started()  const { return started_; }

    // ── Listener / forwarder registration ────────────────────────────────────
    void add_listener(uint16_t ga, GACallback cb) { listeners_.push_back({ga, cb}); }

    // Forwarder receives EVERY telegram (for IP→TP bridge)
    void add_forwarder(GACallback cb) { forwarders_.push_back(cb); }

    // ── Tunneling (ETS programming) ───────────────────────────────────────────
    // Optional: callback when ETS sends a frame via tunnel → forward to TP bus
    void set_on_tunneling(KNXIPTunnelServer::TunnelingCallback cb) {
        tunnel_cb_ = cb;
    }

    // Forward a bus frame to connected ETS (call from TP receive callback)
    void tunnel_forward_to_ets(const ParsedCEMI& frame) {
        tunnel_.forward_to_ets(frame);
    }

    bool has_ets_session() const { return tunnel_.has_active_session(); }

    // ── Send — uint16_t GA ───────────────────────────────────────────────────
    void send_bool(uint16_t ga, bool v) {
        uint8_t buf[32];
        size_t len = build_routing_frame(buf, ia_, ga, APCI_GROUP_WRITE, nullptr, 0, DPT::e1(v));
        tx_multicast_(buf, len);
        ESP_LOGI(TAG_KNXIP, "TX bool  GA=0x%04X val=%d", ga, v);
    }
    void send_dpt9(uint16_t ga, float v) {
        uint8_t d[2]; DPT::e9(v, d[0], d[1]);
        uint8_t buf[32];
        size_t len = build_routing_frame(buf, ia_, ga, APCI_GROUP_WRITE, d, 2);
        tx_multicast_(buf, len);
        ESP_LOGI(TAG_KNXIP, "TX DPT9  GA=0x%04X val=%.2f", ga, v);
    }
    void send_dpt5(uint16_t ga, float v) {
        uint8_t buf[32];
        size_t len = build_routing_frame(buf, ia_, ga, APCI_GROUP_WRITE, nullptr, 0, DPT::e5(v));
        tx_multicast_(buf, len);
    }
    void send_dpt14(uint16_t ga, float v) {
        uint8_t d[4]; DPT::e14(v, d);
        uint8_t buf[32];
        size_t len = build_routing_frame(buf, ia_, ga, APCI_GROUP_WRITE, d, 4);
        tx_multicast_(buf, len);
    }

    // ── Send — string GA "1/2/10" ────────────────────────────────────────────
    void send_bool(const std::string& s, bool v)   { send_bool(ga_parse(s.c_str()), v); }
    void send_dpt9(const std::string& s, float v)  { send_dpt9(ga_parse(s.c_str()), v); }
    void send_dpt5(const std::string& s, float v)  { send_dpt5(ga_parse(s.c_str()), v); }
    void send_dpt14(const std::string& s, float v) { send_dpt14(ga_parse(s.c_str()), v); }

    // ── Read request (Group Read) ─────────────────────────────────────────────
    void send_read(uint16_t ga) {
        uint8_t buf[32];
        size_t len = build_routing_frame(buf, ia_, ga, APCI_GROUP_READ, nullptr, 0, 0);
        tx_multicast_(buf, len);
        ESP_LOGI(TAG_KNXIP, "TX READ  GA=0x%04X", ga);
    }

    // ── ESPHome lifecycle ────────────────────────────────────────────────────
    void setup() override {
        ESP_LOGI(TAG_KNXIP, "KNX/IP setup IA=0x%04X", ia_);
    }

    void loop() override {
        if (!started_) {
            start_udp_();
            return;
        }

        // Check for session timeout
        tunnel_.loop(millis());

        // Process incoming packets
        int pkt = udp_.parsePacket();
        if (pkt <= 0) return;

        uint8_t buf[512];
        int len = udp_.read(buf, sizeof(buf));
        if (len < KNXIP_HEADER_SIZE) return;

        // Get sender info
        IPAddress remote = udp_.remoteIP();
        uint16_t  rport  = udp_.remotePort();
        uint8_t   remote_ip[4] = {remote[0], remote[1], remote[2], remote[3]};

        uint16_t svc = read_knxip_service(buf, len);

        switch (svc) {
            case KNXIP_ROUTING_INDICATION:
                handle_routing_(buf, len);
                break;

            case KNXIP_SEARCH_REQUEST:
            case KNXIP_DESCRIPTION_REQUEST:
            case KNXIP_CONNECT_REQUEST:
            case KNXIP_CONNECTIONSTATE_REQUEST:
            case KNXIP_DISCONNECT_REQUEST:
            case KNXIP_DISCONNECT_RESPONSE:
            case KNXIP_TUNNELING_REQUEST:
            case KNXIP_TUNNELING_ACK:
                tunnel_.handle_packet(buf, len, remote_ip, rport);
                break;

            default:
                ESP_LOGD(TAG_KNXIP, "RX unknown service 0x%04X len=%d", svc, len);
                break;
        }
    }

    float get_setup_priority() const override { return setup_priority::AFTER_WIFI; }

 private:
    WiFiUDP   udp_;
    IPAddress mcast_{224, 0, 23, 12};
    uint16_t  port_         = KNXIP_PORT;
    uint16_t  ia_           = 0x1132;
    bool      started_      = false;
    uint32_t  wifi_ms_      = 0;
    const char* name_       = "ESP32-KNX-IP";
    std::vector<GAListener> listeners_;
    std::vector<GACallback>  forwarders_;
    KNXIPTunnelServer        tunnel_;
    KNXIPTunnelServer::TunnelingCallback tunnel_cb_;

    void start_udp_() {
        auto* w = wifi::global_wifi_component;
        if (!w || !w->is_connected()) { wifi_ms_ = 0; return; }
        if (!wifi_ms_) { wifi_ms_ = millis(); return; }
        if (millis() - wifi_ms_ < 500) return;

        if (udp_.beginMulticast(mcast_, port_)) {
            started_ = true;

            // Configure tunnel server with local IP and MAC
            IPAddress local = WiFi.localIP();
            uint8_t local_ip[4] = {local[0], local[1], local[2], local[3]};
            uint8_t mac[6];
            WiFi.macAddress(mac);

            tunnel_.set_local(local_ip, port_);
            tunnel_.set_ia(ia_);
            tunnel_.set_name(name_);
            tunnel_.set_mac(mac);
            tunnel_.set_udp(&udp_);
            if (tunnel_cb_) tunnel_.set_on_tunneling(tunnel_cb_);

            ESP_LOGI(TAG_KNXIP,
                     "KNX/IP active %s:%d IA=0x%04X IP=%d.%d.%d.%d",
                     KNXIP_MULTICAST, port_, ia_,
                     local_ip[0], local_ip[1], local_ip[2], local_ip[3]);
        } else {
            ESP_LOGE(TAG_KNXIP, "beginMulticast failed, retry...");
            wifi_ms_ = millis() - 4500;
        }
    }

    void handle_routing_(const uint8_t* buf, int len) {
        ParsedCEMI frame = parse_routing_frame(buf, len);
        if (!frame.valid) return;

        // Ignore our own echo (src == our IA)
        if (frame.src == ia_) return;

        ESP_LOGD(TAG_KNXIP, "RX ROUTE src=0x%04X dst=0x%04X apci=0x%03X",
                 frame.src, frame.dst, frame.apci_cmd);

        // Forward to TP bridge if configured
        for (auto& fwd : forwarders_) fwd(frame);

        // Dispatch to listeners by GA
        for (auto& l : listeners_)
            if (l.ga == frame.dst) l.cb(frame);

        // Also forward to ETS if tunnel session active
        tunnel_.forward_to_ets(frame);
    }

    void tx_multicast_(const uint8_t* buf, size_t len) {
        if (!started_) return;
        udp_.beginMulticastPacket();
        udp_.write(buf, len);
        udp_.endPacket();
    }
};

}  // namespace knxip
}  // namespace esphome
