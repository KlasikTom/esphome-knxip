#pragma once
#include "esphome/core/component.h"
#include "esphome/core/log.h"
#include "esphome/components/wifi/wifi_component.h"
#include <WiFiUdp.h>
#include <functional>
#include <vector>
#include "knxip.h"

static const char* const TAG_KNXIP = "knxip";

namespace esphome {
namespace knxip {

using GACallback = std::function<void(const ParsedCEMI&)>;

struct GAListener {
    uint16_t ga;
    GACallback cb;
};

class KNXIPComponent : public Component {
 public:
    void set_individual_address(const std::string& s) {
        ia_ = ia_parse(s.c_str());
    }
    void set_multicast_group(const std::string& s) {
        mcast_.fromString(s.c_str());
    }
    void set_port(uint16_t p) { port_ = p; }

    // Sensors/switches call this in setup()
    void add_listener(uint16_t ga, GACallback cb) {
        listeners_.push_back({ga, cb});
    }

    // ── Send helpers ─────────────────────────────────────────────────────────

    void send_bool(uint16_t ga, bool v) {
        uint8_t buf[32]; size_t len;
        len = build_routing_frame(buf, ia_, ga, APCI_GROUP_WRITE, nullptr, 0, DPT::e1(v));
        tx(buf, len);
        ESP_LOGI(TAG_KNXIP, "TX bool GA=0x%04X val=%d", ga, v);
    }

    void send_dpt9(uint16_t ga, float v) {
        uint8_t d[2]; DPT::e9(v, d[0], d[1]);
        uint8_t buf[32]; size_t len;
        len = build_routing_frame(buf, ia_, ga, APCI_GROUP_WRITE, d, 2);
        tx(buf, len);
        ESP_LOGI(TAG_KNXIP, "TX DPT9 GA=0x%04X val=%.2f", ga, v);
    }

    void send_dpt5(uint16_t ga, float v) {
        uint8_t buf[32]; size_t len;
        len = build_routing_frame(buf, ia_, ga, APCI_GROUP_WRITE, nullptr, 0, DPT::e5(v));
        tx(buf, len);
    }

    void send_dpt14(uint16_t ga, float v) {
        uint8_t d[4]; DPT::e14(v, d);
        uint8_t buf[32]; size_t len;
        len = build_routing_frame(buf, ia_, ga, APCI_GROUP_WRITE, d, 4);
        tx(buf, len);
    }

    bool is_started() const { return started_; }

    // ── ESPHome lifecycle ────────────────────────────────────────────────────

    void setup() override {
        ESP_LOGI(TAG_KNXIP, "KNX/IP setup IA=0x%04X", ia_);
    }

    void loop() override {
        if (!started_) {
            auto* w = wifi::global_wifi_component;
            if (!w || !w->is_connected()) { wifi_ms_ = 0; return; }
            if (!wifi_ms_) { wifi_ms_ = millis(); return; }
            if (millis() - wifi_ms_ < 500) return;
            if (udp_.beginMulticast(mcast_, port_)) {
                started_ = true;
                ESP_LOGI(TAG_KNXIP, "KNX/IP aktivan %s:%d", KNXIP_MULTICAST, port_);
            } else {
                ESP_LOGE(TAG_KNXIP, "beginMulticast selhal, retry...");
                wifi_ms_ = millis() - 4500;
            }
            return;
        }

        int pkt = udp_.parsePacket();
        if (pkt <= 0) return;

        uint8_t buf[256];
        int len = udp_.read(buf, sizeof(buf));
        if (len < KNXIP_HEADER_SIZE) return;

        ParsedCEMI frame = parse_routing_frame(buf, len);
        if (!frame.valid) return;

        ESP_LOGD(TAG_KNXIP, "RX src=0x%04X dst=0x%04X apci=0x%03X dlen=%d",
                 frame.src, frame.dst, frame.apci_cmd, frame.data_len);

        for (auto& l : listeners_) {
            if (l.ga == frame.dst) l.cb(frame);
        }
    }

    float get_setup_priority() const override { return setup_priority::AFTER_WIFI; }

 private:
    WiFiUDP  udp_;
    IPAddress mcast_{224, 0, 23, 12};
    uint16_t  port_     = KNXIP_PORT;
    uint16_t  ia_       = 0x1132;   // 1.1.50
    bool      started_  = false;
    uint32_t  wifi_ms_  = 0;
    std::vector<GAListener> listeners_;

    void tx(const uint8_t* buf, size_t len) {
        if (!started_) return;
        udp_.beginMulticastPacket();
        udp_.write(buf, len);
        udp_.endPacket();
    }
};

// Accessible by sensors/switches
static KNXIPComponent* knxip_global = nullptr;

}  // namespace knxip
}  // namespace esphome
