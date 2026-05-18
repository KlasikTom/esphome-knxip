#pragma once
// KNX TP driver pro NCN5120 / TPUART2 přes UART
// Protokol: 19200 baud, 8N1
// NCN5120 datasheet: https://www.onsemi.com/pdf/datasheet/ncn5120-d.pdf

#include "esphome/core/component.h"
#include "esphome/core/log.h"
#include "esphome/core/helpers.h"
#include <HardwareSerial.h>
#include <functional>
#include <vector>
#include "knxip.h"  // sdílíme ParsedCEMI a build_routing_frame

static const char* const TAG_KNXTP = "knxtp";

// ── TPUART2 / NCN5120 příkazy ─────────────────────────────────────────────────
#define TPUART_RESET           0x01
#define TPUART_RESET_IND       0x03
#define TPUART_STATE_REQ       0x02
#define TPUART_ACTIVATE        0x31   // U_SetRepetition — aktivace normálního módu
#define TPUART_SET_ADDR_HI     0x28
#define TPUART_SET_ADDR_LO     0x29
#define TPUART_ACK_ADDR        0x11   // L_DATA_CONFIRM addressed
#define TPUART_ACK_NOT_ADDR    0x10   // L_DATA_CONFIRM not addressed
#define TPUART_ACK_BUSY        0x12
#define TPUART_DATA_CONF_OK    0x8B   // L_DATA.con positive
#define TPUART_DATA_CONF_FAIL  0x0B   // L_DATA.con negative
#define TPUART_L_DATA_IND      0x29   // přijatý telegram (standard frame)
#define TPUART_L_DATA_STD      0x35   // odeslat standard frame

// TP frame max délka (standard frame): 23 bytů
#define TP_MAX_FRAME 23
#define TP_BAUD      19200

namespace esphome {
namespace knxip {

using TPCallback = std::function<void(const ParsedCEMI&)>;

class KNXTPDriver : public Component {
 public:
    void set_uart_num(int n)    { uart_num_ = n; }
    void set_rx_pin(int p)      { rx_pin_ = p; }
    void set_tx_pin(int p)      { tx_pin_ = p; }
    void set_ia(uint16_t ia)    { ia_ = ia; }
    void set_on_receive(TPCallback cb) { on_receive_ = cb; }

    void setup() override {
        uart_ = new HardwareSerial(uart_num_);
        uart_->begin(TP_BAUD, SERIAL_8N1, rx_pin_, tx_pin_);
        ESP_LOGI(TAG_KNXTP, "KNX TP UART%d RX=%d TX=%d", uart_num_, rx_pin_, tx_pin_);
        reset_();
    }

    void loop() override {
        // Zpracuj příchozí byty z TPUART
        while (uart_->available()) {
            uint8_t b = uart_->read();
            process_byte_(b);
        }

        // Timeout pro rozpracovaný frame (>10ms bez dalšího bytu = frame kompletní)
        if (rx_len_ > 0 && millis() - rx_last_ms_ > 10) {
            if (rx_len_ >= 7) dispatch_frame_();
            rx_len_ = 0;
        }
    }

    // Pošli CEMI frame na TP sběrnici
    void send(const ParsedCEMI& frame) {
        if (!ready_) {
            ESP_LOGW(TAG_KNXTP, "TP not ready, dropping frame");
            return;
        }

        // Sestav TP standard frame z CEMI
        // Format: ctrl1, ctrl2, src_hi, src_lo, dst_hi, dst_lo, len, tpci, apci[, data...]
        uint8_t buf[TP_MAX_FRAME];
        buf[0] = 0xBC;               // ctrl1: standard, group, prio=low, repeat allowed
        buf[1] = 0xE0;               // ctrl2: group addr, hop=6
        buf[2] = (ia_ >> 8) & 0xFF;
        buf[3] =  ia_ & 0xFF;
        buf[4] = (frame.dst >> 8) & 0xFF;
        buf[5] =  frame.dst & 0xFF;
        buf[6] = 1 + frame.data_len; // APDU length - 1
        buf[7] = (uint8_t)((frame.apci_cmd >> 8) & 0x03);
        buf[8] = (uint8_t)(frame.apci_cmd & 0xFF);
        if (frame.data_len == 0) {
            buf[8] |= frame.small_val & 0x3F;
        } else {
            memcpy(&buf[9], frame.data, frame.data_len);
        }
        uint8_t len = 9 + frame.data_len;

        // Výpočet checksum (XOR všech bytů, pak invertuj)
        uint8_t cs = 0xFF;
        for (int i = 0; i < len; i++) cs ^= buf[i];

        // Odešli přes TPUART: prefix 0x35 + frame + checksum
        uart_->write(TPUART_L_DATA_STD);
        uart_->write(buf, len);
        uart_->write(cs);

        ESP_LOGD(TAG_KNXTP, "TP TX dst=0x%04X apci=0x%03X len=%d", frame.dst, frame.apci_cmd, len);
    }

    bool is_ready() const { return ready_; }
    float get_setup_priority() const override { return setup_priority::AFTER_WIFI - 2.0f; }

 private:
    HardwareSerial* uart_{nullptr};
    int uart_num_{1}, rx_pin_{-1}, tx_pin_{-1};
    uint16_t ia_{0x1132};
    bool ready_{false};
    TPCallback on_receive_;

    uint8_t rx_buf_[TP_MAX_FRAME + 4];
    uint8_t rx_len_{0};
    uint32_t rx_last_ms_{0};
    uint32_t reset_ms_{0};
    bool wait_reset_ind_{false};

    void reset_() {
        ready_ = false;
        wait_reset_ind_ = true;
        uart_->write(TPUART_RESET);
        reset_ms_ = millis();
        ESP_LOGI(TAG_KNXTP, "TP reset odeslan...");
    }

    void init_() {
        // Nastav individuální adresu
        uart_->write(TPUART_SET_ADDR_HI);
        uart_->write((uint8_t)((ia_ >> 8) & 0xFF));
        uart_->write(TPUART_SET_ADDR_LO);
        uart_->write((uint8_t)(ia_ & 0xFF));
        // Aktivuj normální mód
        uart_->write(TPUART_ACTIVATE);
        ready_ = true;
        ESP_LOGI(TAG_KNXTP, "KNX TP ready, IA=0x%04X", ia_);
    }

    void process_byte_(uint8_t b) {
        // Čekáme na reset indication
        if (wait_reset_ind_) {
            if (b == TPUART_RESET_IND) {
                wait_reset_ind_ = false;
                init_();
            }
            return;
        }

        // Začátek nového framu
        if (rx_len_ == 0) {
            if (b == TPUART_L_DATA_IND || (b & 0xD3) == 0x90) {
                // L_DATA.ind nebo extended frame marker
                rx_buf_[rx_len_++] = b;
                rx_last_ms_ = millis();
            } else if (b == TPUART_DATA_CONF_OK) {
                ESP_LOGD(TAG_KNXTP, "TP TX confirmed OK");
            } else if (b == TPUART_DATA_CONF_FAIL) {
                ESP_LOGW(TAG_KNXTP, "TP TX failed");
            }
            return;
        }

        // Pokračování framu
        rx_buf_[rx_len_++] = b;
        rx_last_ms_ = millis();

        // Máme dost dat pro určení délky?
        if (rx_len_ >= 7) {
            uint8_t apdu_len = rx_buf_[6]; // APDU length - 1
            uint8_t expected = 7 + 1 + apdu_len; // header + TPCI + data + checksum
            if (rx_len_ >= expected) {
                dispatch_frame_();
                rx_len_ = 0;
            }
        }

        if (rx_len_ >= TP_MAX_FRAME) {
            ESP_LOGW(TAG_KNXTP, "TP RX frame too long, discarding");
            rx_len_ = 0;
        }
    }

    void dispatch_frame_() {
        // Sestav ParsedCEMI z TP framu
        // TP frame: ctrl1, ctrl2, src_hi, src_lo, dst_hi, dst_lo, apdu_len-1, tpci, apci, [data...]
        if (rx_len_ < 9) return;

        ParsedCEMI f;
        f.src = ((uint16_t)rx_buf_[2] << 8) | rx_buf_[3];
        f.dst = ((uint16_t)rx_buf_[4] << 8) | rx_buf_[5];
        uint8_t apdu_len = rx_buf_[6];

        f.apci_cmd = (uint16_t)((rx_buf_[7] & 0x03) << 8) | rx_buf_[8];
        f.apci_cmd &= APCI_CMD_MASK;

        if (apdu_len <= 1) {
            f.small_val = rx_buf_[8] & 0x3F;
            f.data[0]   = f.small_val;
            f.data_len  = 0;
        } else {
            f.data_len = apdu_len - 1;
            if (f.data_len > sizeof(f.data)) f.data_len = sizeof(f.data);
            memcpy(f.data, &rx_buf_[9], f.data_len);
        }
        f.valid = true;

        ESP_LOGD(TAG_KNXTP, "TP RX src=0x%04X dst=0x%04X apci=0x%03X",
                 f.src, f.dst, f.apci_cmd);

        if (on_receive_) on_receive_(f);
    }
};

}  // namespace knxip
}  // namespace esphome
