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
    void set_gpio_pin(GPIOPin* p)             { pin_ = p; }  // optional GPIO

    void setup() override {
        if (pin_) { pin_->setup(); pin_->digital_write(false); }
        if (ga_state_) {
            knxip_->add_listener(ga_state_, [this](const ParsedCEMI& f) {
                if (f.apci_cmd == APCI_GROUP_WRITE || f.apci_cmd == APCI_GROUP_RESPONSE) {
                    bool v = DPT::d1(f.small_val);
                    if (pin_) pin_->digital_write(v);
                    publish_state(v);
                }
            });
        }
        // Poslouchej i na command GA (pokud jiný zdroj pošle příkaz)
        if (ga_cmd_ && ga_cmd_ != ga_state_) {
            knxip_->add_listener(ga_cmd_, [this](const ParsedCEMI& f) {
                if (f.apci_cmd == APCI_GROUP_WRITE)
                    write_state(DPT::d1(f.small_val));
            });
        }
        ESP_LOGI("knxip.switch", "Switch cmd=0x%04X state=0x%04X gpio=%s",
                 ga_cmd_, ga_state_, pin_ ? "yes" : "no");
    }

    float get_setup_priority() const override { return setup_priority::AFTER_WIFI - 1.0f; }

 protected:
    void write_state(bool v) override {
        if (pin_) pin_->digital_write(v);
        knxip_->send_bool(ga_cmd_, v);
        if (!ga_state_) publish_state(v);
    }

 private:
    KNXIPComponent* knxip_{nullptr};
    GPIOPin*        pin_{nullptr};
    uint16_t        ga_cmd_{0};
    uint16_t        ga_state_{0};
};

}  // namespace knxip
}  // namespace esphome
