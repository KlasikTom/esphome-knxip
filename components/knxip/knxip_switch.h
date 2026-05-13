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
        if (pin_) { pin_->setup(); pin_->digital_write(false); }

        // Poslouchej na state GA (feedback ze sběrnice)
        if (ga_state_) {
            knxip_->add_listener(ga_state_, [this](const ParsedCEMI& f) {
                if (f.apci_cmd == APCI_GROUP_WRITE || f.apci_cmd == APCI_GROUP_RESPONSE) {
                    bool v = DPT::d1(f.small_val);
                    if (pin_) pin_->digital_write(v);
                    publish_state(v);
                    ESP_LOGD("knxip.switch", "State GA feedback: %d", v);
                }
            });
        }

        // Poslouchej i na command GA (příkazy od jiných zdrojů)
        if (ga_cmd_ && ga_cmd_ != ga_state_) {
            knxip_->add_listener(ga_cmd_, [this](const ParsedCEMI& f) {
                if (f.apci_cmd == APCI_GROUP_WRITE) {
                    bool v = DPT::d1(f.small_val);
                    if (pin_) pin_->digital_write(v);
                    // Optimistic: publish ihned bez čekání na state GA
                    publish_state(v);
                    ESP_LOGD("knxip.switch", "Cmd GA: %d", v);
                }
            });
        }

        ESP_LOGI("knxip.switch", "Switch cmd=0x%04X state=0x%04X gpio=%s",
                 ga_cmd_, ga_state_, pin_ ? "yes" : "no");
    }

    float get_setup_priority() const override { return setup_priority::AFTER_WIFI - 1.0f; }

 protected:
    void write_state(bool v) override {
        // Okamžitě aplikuj lokálně (optimistic) - nečekej na KNX feedback
        if (pin_) pin_->digital_write(v);
        publish_state(v);  // ← klíčové: publish PŘED odesláním na KNX
        // Pak odešli telegram na sběrnici
        knxip_->send_bool(ga_cmd_, v);
        ESP_LOGI("knxip.switch", "write_state: %d → GA=0x%04X", v, ga_cmd_);
    }

 private:
    KNXIPComponent* knxip_{nullptr};
    GPIOPin*        pin_{nullptr};
    uint16_t        ga_cmd_{0};
    uint16_t        ga_state_{0};
};

}  // namespace knxip
}  // namespace esphome
