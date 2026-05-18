#pragma once
// NS2009 I2C touchscreen driver pro ESPHome
// I2C adresa: 0x4D (Olimex MOD-LCD2.8RTP na ESP32-EVB)
//
// Použití:
//   esphome:
//     includes: [ns2009_touch.h]
//
//   i2c:
//     id: i2c_bus_id
//     scl_pin: GPIO16
//     sda_pin: GPIO13
//
//   interval:
//     - interval: 50ms
//       then:
//         - lambda: |-
//             static esphome::ns2009_touch::NS2009 touch(i2c_bus_id);
//             auto tp = touch.read();
//             if (tp.touched) { ... }

#include "esphome/components/i2c/i2c.h"
#include "esphome/core/log.h"

namespace esphome {
namespace ns2009_touch {

// NS2009 registry (send 1 byte cmd, read 2 bytes výsledek)
static const uint8_t NS2009_ADDR = 0x4D;  // Olimex MOD-LCD
static const uint8_t NS2009_X    = 0xC0;  // X position
static const uint8_t NS2009_Y    = 0xD0;  // Y position
static const uint8_t NS2009_Z1   = 0xE0;  // Z1 pressure (detekce dotyku)

struct TouchPoint {
    int16_t x       = 0;
    int16_t y       = 0;
    int16_t z       = 0;
    bool    touched = false;
};

class NS2009 {
 public:
    explicit NS2009(i2c::I2CBus* bus) : bus_(bus) {}

    // Přečti dotyk a vrať kalibrované souřadnice
    // Parametry kalibrace: raw ADC hodnoty pro okraje displeje
    TouchPoint read(int16_t x_min = 200, int16_t x_max = 3900,
                    int16_t y_min = 200, int16_t y_max = 3900,
                    int16_t z_threshold = 100,
                    int16_t out_w = 240, int16_t out_h = 320) {
        TouchPoint tp;

        uint16_t rz = read_reg_(NS2009_Z1);
        tp.z = (int16_t)(rz >> 4);
        tp.touched = (tp.z > z_threshold);

        if (!tp.touched) return tp;

        uint16_t rx = read_reg_(NS2009_X);
        uint16_t ry = read_reg_(NS2009_Y);

        tp.x = map_val_((int16_t)(rx >> 4), x_min, x_max, 0, out_w);
        tp.y = map_val_((int16_t)(ry >> 4), y_min, y_max, 0, out_h);

        // Clamp na výstupní rozměry
        tp.x = std::max((int16_t)0, std::min(tp.x, out_w));
        tp.y = std::max((int16_t)0, std::min(tp.y, out_h));

        ESP_LOGD("ns2009", "x=%d y=%d z=%d (raw x=%d y=%d)",
                 tp.x, tp.y, tp.z, rx >> 4, ry >> 4);
        return tp;
    }

 private:
    i2c::I2CBus* bus_;

    uint16_t read_reg_(uint8_t reg) {
        // NS2009: pošli 1 byte příkaz, přečti 2 byty výsledku
        i2c::ErrorCode err;
        uint8_t cmd = reg;
        uint8_t buf[2] = {0, 0};

        err = bus_->write(NS2009_ADDR, &cmd, 1, true);
        if (err != i2c::ERROR_OK) return 0;

        err = bus_->read(NS2009_ADDR, buf, 2);
        if (err != i2c::ERROR_OK) return 0;

        return ((uint16_t)buf[0] << 8) | buf[1];
    }

    int16_t map_val_(int16_t val, int16_t in_min, int16_t in_max,
                     int16_t out_min, int16_t out_max) {
        if (in_max == in_min) return out_min;
        return (int16_t)((int32_t)(val - in_min) * (out_max - out_min)
                         / (in_max - in_min) + out_min);
    }
};

}  // namespace ns2009_touch
}  // namespace esphome
