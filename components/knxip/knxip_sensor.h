#pragma once
#include "esphome/components/sensor/sensor.h"
#include "esphome/core/component.h"
#include "knxip_component.h"

namespace esphome {
namespace knxip {

enum class SensorDPT { DPT9, DPT14, DPT5 };

class KNXIPSensor : public sensor::Sensor, public Component {
 public:
    void set_ga(const std::string& s)   { ga_ = ga_parse(s.c_str()); }
    void set_dpt(SensorDPT d)           { dpt_ = d; }
    void set_knxip(KNXIPComponent* k)   { knxip_ = k; }

    void setup() override {
        knxip_->add_listener(ga_, [this](const ParsedCEMI& f) {
            if (f.apci_cmd == APCI_GROUP_WRITE || f.apci_cmd == APCI_GROUP_RESPONSE) {
                float v = decode(f);
                ESP_LOGD("knxip.sensor", "GA=0x%04X → %.2f", ga_, v);
                publish_state(v);
            }
        });
        ESP_LOGI("knxip.sensor", "Sensor GA=0x%04X registrovan", ga_);
    }

    float get_setup_priority() const override { return setup_priority::AFTER_WIFI - 1.0f; }

 private:
    KNXIPComponent* knxip_{nullptr};
    uint16_t ga_{0};
    SensorDPT dpt_{SensorDPT::DPT9};

    float decode(const ParsedCEMI& f) {
        switch (dpt_) {
            case SensorDPT::DPT9:
                if (f.data_len >= 2) return DPT::d9(f.data[0], f.data[1]);
                return DPT::d9(0, f.small_val);
            case SensorDPT::DPT14:
                if (f.data_len >= 4) return DPT::d14(f.data);
                return 0.0f;
            case SensorDPT::DPT5:
                return f.data_len ? DPT::d5(f.data[0]) : DPT::d5(f.small_val);
        }
        return 0.0f;
    }
};

}  // namespace knxip
}  // namespace esphome
