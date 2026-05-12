#pragma once
#include <cstdint>
#include <cstring>
#include <cmath>

// KNXnet/IP constants
#define KNXIP_MULTICAST      "224.0.23.12"
#define KNXIP_PORT           3671
#define KNXIP_HEADER_SIZE    6

// Service types
#define KNXIP_ROUTING_INDICATION  0x0530

// CEMI
#define CEMI_L_DATA_IND  0x29

// APCI commands (masked from APCI bytes)
#define APCI_GROUP_READ      0x0000
#define APCI_GROUP_RESPONSE  0x0040
#define APCI_GROUP_WRITE     0x0080
#define APCI_CMD_MASK        0x03C0

namespace esphome {
namespace knxip {

// ─── Group/Individual address helpers ────────────────────────────────────────

inline uint16_t ga_parse(const char* s) {
    unsigned a, b, c;
    if (sscanf(s, "%u/%u/%u", &a, &b, &c) == 3)
        return (uint16_t)((a << 11) | (b << 8) | c);
    if (sscanf(s, "%u/%u", &a, &b) == 2)
        return (uint16_t)((a << 11) | b);
    return 0;
}

inline uint16_t ia_parse(const char* s) {
    unsigned a, l, m;
    if (sscanf(s, "%u.%u.%u", &a, &l, &m) == 3)
        return (uint16_t)((a << 12) | (l << 8) | m);
    return 0;
}

// ─── DPT encode/decode ───────────────────────────────────────────────────────

struct DPT {
    // DPT 1.x  1-bit bool
    static bool     d1(uint8_t apci_lo)           { return (apci_lo & 0x01) != 0; }
    static uint8_t  e1(bool v)                     { return v ? 1 : 0; }

    // DPT 5.x  8-bit unsigned (0-255)
    static float    d5(uint8_t b)                  { return (float)b; }
    static uint8_t  e5(float v)                    { return (uint8_t)fminf(fmaxf(v,0),255); }

    // DPT 9.x  2-byte KNX float
    static float    d9(uint8_t hi, uint8_t lo) {
        bool    sgn = (hi & 0x80) != 0;
        int     exp = (hi >> 3) & 0x0F;
        int16_t m   = (int16_t)(((hi & 0x07) << 8) | lo);
        if (sgn) m |= 0xF800;          // sign extend 11→16 bit
        return 0.01f * (float)m * (float)(1 << exp);
    }
    static void     e9(float v, uint8_t& hi, uint8_t& lo) {
        bool   sgn = v < 0.0f;
        float  m   = fabsf(v) / 0.01f;
        int    exp = 0;
        while (m > 2047.0f && exp < 15) { m /= 2.0f; exp++; }
        if (m > 2047.0f) m = 2047.0f;
        int16_t mi = sgn ? -(int16_t)roundf(m) : (int16_t)roundf(m);
        hi = (sgn ? 0x80 : 0x00) | ((exp & 0x0F) << 3) | ((mi >> 8) & 0x07);
        lo = (uint8_t)(mi & 0xFF);
    }

    // DPT 14.x  4-byte IEEE754
    static float    d14(const uint8_t* d) {
        uint32_t r = ((uint32_t)d[0]<<24)|((uint32_t)d[1]<<16)|((uint32_t)d[2]<<8)|d[3];
        float v; memcpy(&v, &r, 4); return v;
    }
    static void     e14(float v, uint8_t* d) {
        uint32_t r; memcpy(&r, &v, 4);
        d[0]=(r>>24)&0xFF; d[1]=(r>>16)&0xFF; d[2]=(r>>8)&0xFF; d[3]=r&0xFF;
    }
};

// ─── CEMI frame builder/parser ───────────────────────────────────────────────

// Returns total frame length written into buf[]
// For "small" values (1-bit, etc.): data_len=0, small_val used
// For multi-byte: data != nullptr, data_len > 0
inline size_t build_routing_frame(uint8_t* buf,
                                   uint16_t src, uint16_t dst,
                                   uint16_t apci_cmd,
                                   const uint8_t* data, uint8_t data_len,
                                   uint8_t small_val = 0) {
    // KNXnet/IP header
    buf[0] = 0x06; buf[1] = 0x10;
    buf[2] = (KNXIP_ROUTING_INDICATION >> 8) & 0xFF;
    buf[3] =  KNXIP_ROUTING_INDICATION & 0xFF;

    uint8_t* c = buf + 6;               // CEMI
    c[0] = CEMI_L_DATA_IND;
    c[1] = 0x00;                        // no additional info
    c[2] = 0xBC;                        // ctrl1: standard, normal prio
    c[3] = 0xE0;                        // ctrl2: group addr, hop=6
    c[4] = (src >> 8) & 0xFF; c[5] = src & 0xFF;
    c[6] = (dst >> 8) & 0xFF; c[7] = dst & 0xFF;

    // APDU (starts at c[9]):
    //   c[9]  = TPCI | APCI[9:8]
    //   c[10] = APCI[7:0]  (for small: | small_val[5:0])
    //   c[11+]= data bytes
    c[9]  = 0x00 | (uint8_t)((apci_cmd >> 8) & 0x03);
    c[10] = (uint8_t)(apci_cmd & 0xFF);

    if (data_len == 0) {
        // small value (≤6 bit) encoded in APCI low byte
        c[10] |= (small_val & 0x3F);
        c[8]   = 0x01;          // APDU len - 1 = 1 (just TPCI+APCI)
    } else {
        memcpy(&c[11], data, data_len);
        c[8] = 1 + data_len;    // APDU len - 1
    }

    size_t cemi_len = 9 + 1 + (data_len == 0 ? 0 : data_len) + 1;
    // Actually: c[0..8] = 9 bytes fixed, c[9..10] = TPCI+APCI = 2, c[11+] = data
    cemi_len = 11 + data_len;
    size_t total = 6 + cemi_len;
    buf[4] = (total >> 8) & 0xFF;
    buf[5] =  total & 0xFF;
    return total;
}

struct ParsedCEMI {
    bool    valid     = false;
    uint16_t src      = 0;
    uint16_t dst      = 0;
    uint16_t apci_cmd = 0;     // APCI_GROUP_READ / RESPONSE / WRITE
    uint8_t  data[8]  = {};    // decoded value bytes
    uint8_t  data_len = 0;
    uint8_t  small_val = 0;    // for data_len==0 (6-bit) values
};

inline ParsedCEMI parse_routing_frame(const uint8_t* buf, int len) {
    ParsedCEMI r;
    if (len < KNXIP_HEADER_SIZE) return r;
    if (buf[0] != 0x06 || buf[1] != 0x10) return r;
    uint16_t svc = ((uint16_t)buf[2] << 8) | buf[3];
    if (svc != KNXIP_ROUTING_INDICATION) return r;

    const uint8_t* c = buf + 6;            // CEMI
    if (len < 6 + 11) return r;

    uint8_t add = c[1];                    // additional info length
    int o = 2 + add;                       // offset to standard CEMI
    if (len < 6 + o + 9) return r;

    r.src = ((uint16_t)c[o+2] << 8) | c[o+3];
    r.dst = ((uint16_t)c[o+4] << 8) | c[o+5];

    uint8_t apdu_len = c[o+6];             // APDU length - 1
    const uint8_t* apdu = &c[o+7];

    uint8_t tpci = apdu[0];
    if ((tpci & 0xFC) != 0x00) return r;  // only Data Group, seq=0

    r.apci_cmd = (uint16_t)((apdu[0] & 0x03) << 8) | apdu[1];
    r.apci_cmd &= APCI_CMD_MASK;

    if (apdu_len <= 1) {
        // small value (6-bit) in low bits of apdu[1]
        r.small_val = apdu[1] & 0x3F;
        r.data[0]   = r.small_val;
        r.data_len  = 0;            // signal: small value
    } else {
        r.data_len = apdu_len - 1;
        if (r.data_len > sizeof(r.data)) r.data_len = sizeof(r.data);
        memcpy(r.data, &apdu[2], r.data_len);
    }

    r.valid = true;
    return r;
}

}  // namespace knxip
}  // namespace esphome
