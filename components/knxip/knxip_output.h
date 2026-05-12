#pragma once
#include "esphome/components/output/float_output.h"
#include "esphome/core/component.h"
#include "knxip_component.h"

namespace esphome {
namespace knxip {

// DPT5 output: 0.0-1.0 mapped to 0-255 (e.g. dimmer)
class KNXIPOutput : public output::FloatOutput, public Component {
 public:
    void set_ga(const std::string& s) { ga_ = ga_parse(s.c_str()); }
    void set_knxip(KNXIPComponent* k) { knxip_ = k; }

    void setup() override {
        ESP_LOGI("knxip.output", "Output GA=0x%04X", ga_);
    }

    float get_setup_priority() const override { return setup_priority::AFTER_WIFI - 1.0f; }

 protected:
    void write_state(float v) override {
        knxip_->send_dpt5(ga_, v * 255.0f);
    }

 private:
    KNXIPComponent* knxip_{nullptr};
    uint16_t ga_{0};
};

}  // namespace knxip
}  // namespace esphome
