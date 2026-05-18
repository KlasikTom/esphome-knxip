#pragma once
#include "esphome/components/switch/switch.h"
#include "esphome/core/component.h"
#include "esphome/core/gpio.h"
#include "knxip_component.h"

namespace esphome {
namespace knxip {

class KNXIPSwitch : public switch_::Switch, public Component {
 public:
    void set_ga_command(const std::string& s) { ga_cmd_   = ga_parse(s.c_str()); }
    void set_ga_state(const std::string& s)   { ga_state_ = ga_parse(s.c_str()); }
    void set_knxip(KNXIPComponent* k)         { knxip_ = k; }
    void set_gpio_pin(GPIOPin* p)             { pin_ = p; }

    void setup() override {
        // GPIO inicializace — stav přežije reboot (restore_state)
        if (pin_) {
            pin_->setup();
            pin_->digital_write(false);
        }

        // State GA — feedback ze sběrnice (NEPŘEPISUJE GPIO pokud právě probíhá lokální akce)
        if (ga_state_) {
            knxip_->add_listener(ga_state_, [this](const ParsedCEMI& f) {
                if (f.apci_cmd != APCI_GROUP_WRITE && f.apci_cmd != APCI_GROUP_RESPONSE) return;
                if (local_action_ms_ && millis() - local_action_ms_ < 500) return; // ignoruj echo 500ms
                bool v = DPT::d1(f.small_val);
                gpio_write_(v);
                publish_state(v);
                ESP_LOGD("knxip.switch", "KNX state GA: %d", v);
            });
        }

        // Command GA — příkazy od jiných KNX zdrojů
        if (ga_cmd_ && ga_cmd_ != ga_state_) {
            knxip_->add_listener(ga_cmd_, [this](const ParsedCEMI& f) {
                if (f.apci_cmd != APCI_GROUP_WRITE) return;
                if (local_action_ms_ && millis() - local_action_ms_ < 500) return;
                bool v = DPT::d1(f.small_val);
                gpio_write_(v);
                publish_state(v);
                ESP_LOGD("knxip.switch", "KNX cmd GA: %d", v);
            });
        }

        ESP_LOGI("knxip.switch", "Switch cmd=0x%04X state=0x%04X gpio=%s",
                 ga_cmd_, ga_state_, pin_ ? "yes" : "no");
    }

    float get_setup_priority() const override { return setup_priority::AFTER_WIFI - 1.0f; }

 protected:
    // Volá se z ESPHome (HA, web UI, lambda turn_on/off/toggle, BUT1 toggle)
    void write_state(bool v) override {
        local_action_ms_ = millis();  // označ lokální akci
        gpio_write_(v);               // ← GPIO PRVNÍ (bez ohledu na KNX)
        publish_state(v);             // ← stav do ESPHome/HA
        knxip_->send_bool(ga_cmd_, v); // ← pak pošli na sběrnici
        ESP_LOGI("knxip.switch", "write_state: %d GPIO=%s", v, pin_ ? "OK" : "N/A");
    }

 private:
    KNXIPComponent* knxip_{nullptr};
    GPIOPin*        pin_{nullptr};
    uint16_t        ga_cmd_{0};
    uint16_t        ga_state_{0};
    uint32_t        local_action_ms_{0};  // timestamp poslední lokální akce

    void gpio_write_(bool v) {
        if (pin_) {
            pin_->digital_write(v);
            ESP_LOGD("knxip.switch", "GPIO → %d", v);
        }
    }
};

}  // namespace knxip
}  // namespace esphome
