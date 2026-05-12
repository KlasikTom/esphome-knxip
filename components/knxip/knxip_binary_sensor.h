#pragma once
#include "esphome/core/component.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "knxip_component.h"

namespace esphome {
namespace knxip {

class KNXIPBinarySensor : public binary_sensor::BinarySensor, public Component {
 public:
    void set_ga(const std::string& s) { ga_ = ga_parse(s.c_str()); }
    void set_knxip(KNXIPComponent* k) { knxip_ = k; }

    void setup() override {
        knxip_->add_listener(ga_, [this](const ParsedCEMI& f) {
            if (f.apci_cmd == APCI_GROUP_WRITE || f.apci_cmd == APCI_GROUP_RESPONSE)
                publish_state(DPT::d1(f.small_val));
        });
        ESP_LOGI("knxip.bsensor", "BinarySensor GA=0x%04X", ga_);
    }
    float get_setup_priority() const override { return setup_priority::AFTER_WIFI - 1.0f; }

 private:
    KNXIPComponent* knxip_{nullptr};
    uint16_t ga_{0};
};

}  // namespace knxip
}  // namespace esphome
