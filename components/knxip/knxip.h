#pragma once
#include <cstdint>
#include <cstring>
#include <cmath>

// KNXnet/IP constants
#define KNXIP_MULTICAST      "224.0.23.12"
#define KNXIP_PORT           3671
#define KNXIP_HEADER_SIZE    6

// Service types — Routing
#define KNXIP_ROUTING_INDICATION     0x0530
#define KNXIP_ROUTING_LOST_MESSAGE   0x0531
#define KNXIP_ROUTING_BUSY           0x0532

// Service types — Core (Search, Description, Connect)
#define KNXIP_SEARCH_REQUEST         0x0201
#define KNXIP_SEARCH_RESPONSE        0x0202
#define KNXIP_DESCRIPTION_REQUEST    0x0203
#define KNXIP_DESCRIPTION_RESPONSE   0x0204
#define KNXIP_CONNECT_REQUEST        0x0205
#define KNXIP_CONNECT_RESPONSE       0x0206
#define KNXIP_CONNECTIONSTATE_REQUEST  0x0207
#define KNXIP_CONNECTIONSTATE_RESPONSE 0x0208
#define KNXIP_DISCONNECT_REQUEST     0x0209
#define KNXIP_DISCONNECT_RESPONSE    0x020A

// Service types — Tunneling
#define KNXIP_TUNNELING_REQUEST      0x0420
#define KNXIP_TUNNELING_ACK          0x0421

// CEMI message codes
#define CEMI_L_DATA_REQ  0x11   // request (from ETS via tunnel)
#define CEMI_L_DATA_IND  0x29   // indication (received from bus)
#define CEMI_L_DATA_CON  0x2E   // confirmation (TX result)

// APCI commands
#define APCI_GROUP_READ      0x0000
#define APCI_GROUP_RESPONSE  0x0040
#define APCI_GROUP_WRITE     0x0080
#define APCI_CMD_MASK        0x03C0

// DIB description types
#define DIB_DEVICE_INFO        0x01
#define DIB_SUPP_SVC_FAMILIES  0x02
#define DIB_IP_CONFIG          0x03
#define DIB_IP_CUR_CONFIG      0x04
#define DIB_KNX_ADDRESSES      0x05

// Connection types
#define CRI_TUNNEL_CONNECTION  0x04
#define CRI_DEVICE_MGMT_CONN   0x03

// Tunnel layer types
#define TUNNEL_LINKLAYER   0x02
#define TUNNEL_RAW         0x04
#define TUNNEL_BUSMONITOR  0x80

// KNX medium
#define KNX_MEDIUM_TP   0x02
#define KNX_MEDIUM_IP   0x20

// KNXnet/IP error codes
#define E_NO_ERROR              0x00
#define E_HOST_PROT_TYPE        0x01
#define E_VERSION_NOT_SUPPORTED 0x02
#define E_SEQUENCE_NUMBER       0x04
#define E_CONNECTION_ID         0x21
#define E_CONN_TYPE             0x22
#define E_CONN_OPTION           0x23
#define E_NO_MORE_CONNECTIONS   0x24
#define E_DATA_CONNECTION       0x26
#define E_KNX_CONNECTION        0x27
#define E_TUNNELING_LAYER       0x29

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

// ─── ParsedCEMI ──────────────────────────────────────────────────────────────

struct ParsedCEMI {
    bool     valid       = false;
    uint8_t  msg_code    = CEMI_L_DATA_IND;  // CEMI_L_DATA_REQ/IND/CON
    bool     is_group    = true;              // group vs individual addressing
    uint16_t src         = 0;
    uint16_t dst         = 0;
    uint16_t apci_cmd    = 0;       // APCI_GROUP_READ / RESPONSE / WRITE
    uint8_t  data[8]     = {};      // decoded value bytes
    uint8_t  data_len    = 0;
    uint8_t  small_val   = 0;       // for data_len==0 (6-bit) values
    // Raw CEMI bytes for transparent forwarding (used in tunneling/TP bridge)
    uint8_t  cemi_raw[32] = {};
    uint8_t  cemi_raw_len = 0;
};

// ─── KNXnet/IP header helpers ────────────────────────────────────────────────

inline void write_knxip_header(uint8_t* buf, uint16_t service, uint16_t total_len) {
    buf[0] = 0x06; buf[1] = 0x10;
    buf[2] = (service >> 8) & 0xFF;
    buf[3] =  service & 0xFF;
    buf[4] = (total_len >> 8) & 0xFF;
    buf[5] =  total_len & 0xFF;
}

inline uint16_t read_knxip_service(const uint8_t* buf, int len) {
    if (len < 6 || buf[0] != 0x06 || buf[1] != 0x10) return 0;
    return ((uint16_t)buf[2] << 8) | buf[3];
}

// ─── CEMI frame builder for routing ──────────────────────────────────────────

// Returns total frame length written into buf[]
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

    c[9]  = 0x00 | (uint8_t)((apci_cmd >> 8) & 0x03);
    c[10] = (uint8_t)(apci_cmd & 0xFF);

    if (data_len == 0) {
        c[10] |= (small_val & 0x3F);
        c[8]   = 0x01;
    } else {
        memcpy(&c[11], data, data_len);
        c[8] = 1 + data_len;
    }

    size_t cemi_len = 11 + data_len;
    size_t total = 6 + cemi_len;
    buf[4] = (total >> 8) & 0xFF;
    buf[5] =  total & 0xFF;
    return total;
}

// ─── CEMI frame parser (general — handles any CEMI, not just routing) ─────────

inline ParsedCEMI parse_cemi_frame(const uint8_t* cemi, int cemi_len) {
    ParsedCEMI r;
    if (cemi_len < 9) return r;

    r.msg_code = cemi[0];
    uint8_t add = cemi[1];
    int o = 2 + add;            // offset to standard CEMI fields
    if (cemi_len < o + 7) return r;

    // ctrl2 bit 7: 1 = group addressing, 0 = individual addressing
    r.is_group = (cemi[o+1] & 0x80) != 0;
    r.src = ((uint16_t)cemi[o+2] << 8) | cemi[o+3];
    r.dst = ((uint16_t)cemi[o+4] << 8) | cemi[o+5];

    uint8_t apdu_len = cemi[o+6];       // APDU length - 1
    if (cemi_len < o + 7 + 1 + (int)apdu_len) return r;
    const uint8_t* apdu = &cemi[o+7];

    uint8_t tpci = apdu[0];
    if ((tpci & 0xC0) == 0x00) {
        // Group data (T_DATA_GROUP)
        r.apci_cmd = (uint16_t)((apdu[0] & 0x03) << 8) | apdu[1];
        r.apci_cmd &= APCI_CMD_MASK;

        if (apdu_len <= 1) {
            r.small_val = apdu[1] & 0x3F;
            r.data[0]   = r.small_val;
            r.data_len  = 0;
        } else {
            r.data_len = apdu_len - 1;
            if (r.data_len > (uint8_t)sizeof(r.data)) r.data_len = sizeof(r.data);
            memcpy(r.data, &apdu[2], r.data_len);
        }
    }
    // Management/connected frames: apci_cmd stays 0, data_len stays 0
    // but cemi_raw carries the full frame for transparent forwarding

    // Store raw CEMI for tunneling forwarding
    r.cemi_raw_len = (cemi_len < (int)sizeof(r.cemi_raw)) ? (uint8_t)cemi_len : (uint8_t)sizeof(r.cemi_raw);
    memcpy(r.cemi_raw, cemi, r.cemi_raw_len);

    r.valid = true;
    return r;
}

// ─── Routing frame parser ─────────────────────────────────────────────────────

inline ParsedCEMI parse_routing_frame(const uint8_t* buf, int len) {
    ParsedCEMI r;
    if (len < KNXIP_HEADER_SIZE) return r;
    if (buf[0] != 0x06 || buf[1] != 0x10) return r;
    uint16_t svc = ((uint16_t)buf[2] << 8) | buf[3];
    if (svc != KNXIP_ROUTING_INDICATION) return r;
    if (len < 6 + 9) return r;

    return parse_cemi_frame(buf + 6, len - 6);
}

}  // namespace knxip
}  // namespace esphome
