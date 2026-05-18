#pragma once
// KNXnet/IP Tunneling — ETS programming support
// Implements KNXnet/IP Core + Tunneling (ISO 22510 / EN 13321-2)
//
// Supported services:
//   SEARCH_REQUEST/RESPONSE          — ETS device discovery
//   DESCRIPTION_REQUEST/RESPONSE     — Interface capabilities
//   CONNECT_REQUEST/RESPONSE         — Open tunnel connection
//   CONNECTIONSTATE_REQUEST/RESPONSE — Heartbeat
//   DISCONNECT_REQUEST/RESPONSE      — Close connection
//   TUNNELING_REQUEST/ACK            — Data exchange for ETS programming
//
// Typical ETS flow:
//   1. ETS broadcasts SEARCH_REQUEST → we respond
//   2. ETS unicasts DESCRIPTION_REQUEST → we respond with DIBs
//   3. ETS sends CONNECT_REQUEST (tunnel type) → we assign channel
//   4. ETS sends TUNNELING_REQUEST (L_DATA.req CEMI) → forward to TP bus
//   5. TP bus response → we wrap in TUNNELING_REQUEST to ETS
//   6. CONNECTIONSTATE_REQUEST every 10s (heartbeat)
//   7. DISCONNECT_REQUEST → close session

#include <cstdint>
#include <cstring>
#include "knxip.h"

#ifdef ARDUINO
#include <WiFiUdp.h>
#include <WiFi.h>
#endif

namespace esphome {
namespace knxip {

// ─── Tunnel Session ───────────────────────────────────────────────────────────

struct TunnelSession {
    bool      active           = false;
    uint8_t   channel_id       = 0;
    uint8_t   seq_recv         = 0xFF;  // last received seq from ETS (0xFF = none yet)
    uint8_t   seq_send         = 0;    // our TX sequence counter
    uint32_t  last_activity_ms = 0;
    // Endpoints
    uint8_t   ctrl_ip[4]       = {};
    uint16_t  ctrl_port        = 0;
    uint8_t   data_ip[4]       = {};
    uint16_t  data_port        = 0;

    static const uint32_t HEARTBEAT_TIMEOUT_MS = 120000;  // 2 min

    bool timed_out(uint32_t now_ms) const {
        return active && (now_ms - last_activity_ms) > HEARTBEAT_TIMEOUT_MS;
    }
};

// ─── Frame building helpers ───────────────────────────────────────────────────

// Write HPAI (8 bytes: len, UDP, IP×4, port×2)
inline size_t write_hpai(uint8_t* buf, const uint8_t ip[4], uint16_t port) {
    buf[0] = 0x08;  // structure length
    buf[1] = 0x01;  // UDP, IPv4
    buf[2] = ip[0]; buf[3] = ip[1]; buf[4] = ip[2]; buf[5] = ip[3];
    buf[6] = (port >> 8) & 0xFF;
    buf[7] =  port & 0xFF;
    return 8;
}

// Write HPAI with zeros (NAT / not available)
inline size_t write_hpai_zero(uint8_t* buf) {
    const uint8_t zero[4] = {0, 0, 0, 0};
    return write_hpai(buf, zero, 0);
}

// Parse HPAI from buf[0..7], fill ip[4] and port
inline bool parse_hpai(const uint8_t* buf, int avail, uint8_t ip[4], uint16_t& port) {
    if (avail < 8 || buf[0] < 8) return false;
    if (buf[1] != 0x01) return false;  // only IPv4 UDP
    ip[0] = buf[2]; ip[1] = buf[3]; ip[2] = buf[4]; ip[3] = buf[5];
    port  = ((uint16_t)buf[6] << 8) | buf[7];
    return true;
}

// Write DIB_DEVICE_INFO (54 bytes)
inline size_t write_dib_device_info(uint8_t* buf,
                                     uint16_t ia,
                                     const uint8_t mac[6],
                                     const char* name,
                                     const uint8_t mcast_ip[4]) {
    memset(buf, 0, 54);
    buf[0] = 54;                // structure length
    buf[1] = DIB_DEVICE_INFO;  // 0x01
    buf[2] = KNX_MEDIUM_TP;    // TP medium
    buf[3] = 0x00;              // device status (not in programming mode)
    buf[4] = (ia >> 8) & 0xFF;
    buf[5] =  ia & 0xFF;
    buf[6] = 0x00; buf[7] = 0x00;  // project installation ID
    // Serial number: 6 bytes (derived from MAC)
    if (mac) { for (int i = 0; i < 6; i++) buf[8 + i] = mac[i]; }
    // Multicast routing address: 4 bytes
    if (mcast_ip) { for (int i = 0; i < 4; i++) buf[14 + i] = mcast_ip[i]; }
    // MAC address: 6 bytes
    if (mac) { for (int i = 0; i < 6; i++) buf[18 + i] = mac[i]; }
    // Friendly name: 30 bytes (null-terminated)
    strncpy((char*)&buf[24], name ? name : "ESP32-KNX-IP", 29);
    return 54;
}

// Write DIB_SUPP_SVC_FAMILIES — Core v1 + Tunneling v1 (6 bytes)
inline size_t write_dib_supp_svc(uint8_t* buf) {
    buf[0] = 0x06;
    buf[1] = DIB_SUPP_SVC_FAMILIES;
    buf[2] = 0x02; buf[3] = 0x01;  // KNXnet/IP Core v1
    buf[4] = 0x04; buf[5] = 0x01;  // KNXnet/IP Tunneling v1
    return 6;
}

// ─── Response builders ────────────────────────────────────────────────────────

// SEARCH_RESPONSE (unicast to discoverer)
inline size_t build_search_response(uint8_t* buf,
                                     const uint8_t local_ip[4], uint16_t port,
                                     uint16_t ia, const uint8_t mac[6],
                                     const char* name) {
    const uint8_t mcast[4] = {224, 0, 23, 12};
    size_t pos = 6;
    pos += write_hpai(&buf[pos], local_ip, port);
    pos += write_dib_device_info(&buf[pos], ia, mac, name, mcast);
    pos += write_dib_supp_svc(&buf[pos]);
    write_knxip_header(buf, KNXIP_SEARCH_RESPONSE, (uint16_t)pos);
    return pos;
}

// DESCRIPTION_RESPONSE
inline size_t build_description_response(uint8_t* buf,
                                          uint16_t ia, const uint8_t mac[6],
                                          const char* name) {
    const uint8_t mcast[4] = {224, 0, 23, 12};
    size_t pos = 6;
    pos += write_dib_device_info(&buf[pos], ia, mac, name, mcast);
    pos += write_dib_supp_svc(&buf[pos]);
    write_knxip_header(buf, KNXIP_DESCRIPTION_RESPONSE, (uint16_t)pos);
    return pos;
}

// CONNECT_RESPONSE — success
inline size_t build_connect_response_ok(uint8_t* buf,
                                         uint8_t channel_id,
                                         const uint8_t local_ip[4], uint16_t port,
                                         uint16_t ia) {
    size_t pos = 6;
    buf[pos++] = channel_id;
    buf[pos++] = E_NO_ERROR;
    pos += write_hpai(&buf[pos], local_ip, port);  // data endpoint
    // CRD: Tunnel Connection Response Data (4 bytes)
    buf[pos++] = 0x04;  // structure length
    buf[pos++] = CRI_TUNNEL_CONNECTION;
    buf[pos++] = (ia >> 8) & 0xFF;
    buf[pos++] =  ia & 0xFF;
    write_knxip_header(buf, KNXIP_CONNECT_RESPONSE, (uint16_t)pos);
    return pos;
}

// CONNECT_RESPONSE — error
inline size_t build_connect_response_err(uint8_t* buf, uint8_t error_code) {
    size_t pos = 6;
    buf[pos++] = 0x00;         // no channel
    buf[pos++] = error_code;
    pos += write_hpai_zero(&buf[pos]);  // zero HPAI
    buf[pos++] = 0x04; buf[pos++] = 0x00;  // empty CRD
    buf[pos++] = 0x00; buf[pos++] = 0x00;
    write_knxip_header(buf, KNXIP_CONNECT_RESPONSE, (uint16_t)pos);
    return pos;
}

// CONNECTIONSTATE_RESPONSE
inline size_t build_connstate_response(uint8_t* buf, uint8_t channel_id, uint8_t status) {
    size_t pos = 6;
    buf[pos++] = channel_id;
    buf[pos++] = status;
    write_knxip_header(buf, KNXIP_CONNECTIONSTATE_RESPONSE, (uint16_t)pos);
    return pos;
}

// DISCONNECT_RESPONSE
inline size_t build_disconnect_response(uint8_t* buf, uint8_t channel_id) {
    size_t pos = 6;
    buf[pos++] = channel_id;
    buf[pos++] = E_NO_ERROR;
    write_knxip_header(buf, KNXIP_DISCONNECT_RESPONSE, (uint16_t)pos);
    return pos;
}

// DISCONNECT_REQUEST (initiated by us, e.g. on timeout)
inline size_t build_disconnect_request(uint8_t* buf, uint8_t channel_id,
                                        const uint8_t local_ip[4], uint16_t port) {
    size_t pos = 6;
    buf[pos++] = channel_id;
    buf[pos++] = 0x00;  // reserved
    pos += write_hpai(&buf[pos], local_ip, port);
    write_knxip_header(buf, KNXIP_DISCONNECT_REQUEST, (uint16_t)pos);
    return pos;
}

// TUNNELING_ACK
inline size_t build_tunneling_ack(uint8_t* buf, uint8_t channel_id,
                                   uint8_t seq, uint8_t status = E_NO_ERROR) {
    size_t pos = 6;
    buf[pos++] = 0x04;      // tunnel header length
    buf[pos++] = channel_id;
    buf[pos++] = seq;
    buf[pos++] = status;
    write_knxip_header(buf, KNXIP_TUNNELING_ACK, (uint16_t)pos);
    return pos;
}

// TUNNELING_REQUEST — wrap a CEMI frame for sending to ETS
// Uses raw CEMI bytes if available, otherwise builds from ParsedCEMI
inline size_t build_tunneling_request(uint8_t* buf, uint8_t channel_id,
                                       uint8_t seq, const ParsedCEMI& frame,
                                       uint8_t msg_code_override = CEMI_L_DATA_IND) {
    size_t pos = 6;
    // Tunnel connection header (4 bytes)
    buf[pos++] = 0x04;      // header length
    buf[pos++] = channel_id;
    buf[pos++] = seq;
    buf[pos++] = 0x00;      // reserved

    // CEMI: prefer raw bytes if available, else build from ParsedCEMI
    if (frame.cemi_raw_len > 0) {
        // Use raw CEMI (already has correct message code)
        uint8_t raw_copy[32];
        memcpy(raw_copy, frame.cemi_raw, frame.cemi_raw_len);
        raw_copy[0] = msg_code_override;  // ensure correct msg code
        memcpy(&buf[pos], raw_copy, frame.cemi_raw_len);
        pos += frame.cemi_raw_len;
    } else {
        // Build CEMI from ParsedCEMI fields
        buf[pos++] = msg_code_override;  // message code
        buf[pos++] = 0x00;               // no additional info
        buf[pos++] = 0xBC;               // ctrl1
        buf[pos++] = 0xE0;               // ctrl2: group addr, hop=6
        buf[pos++] = (frame.src >> 8) & 0xFF;
        buf[pos++] =  frame.src & 0xFF;
        buf[pos++] = (frame.dst >> 8) & 0xFF;
        buf[pos++] =  frame.dst & 0xFF;
        // APDU
        if (frame.data_len == 0) {
            buf[pos++] = 0x01;
            buf[pos++] = (uint8_t)((frame.apci_cmd >> 8) & 0x03);
            buf[pos++] = (uint8_t)(frame.apci_cmd & 0xFF) | (frame.small_val & 0x3F);
        } else {
            buf[pos++] = 1 + frame.data_len;
            buf[pos++] = (uint8_t)((frame.apci_cmd >> 8) & 0x03);
            buf[pos++] = (uint8_t)(frame.apci_cmd & 0xFF);
            memcpy(&buf[pos], frame.data, frame.data_len);
            pos += frame.data_len;
        }
    }

    write_knxip_header(buf, KNXIP_TUNNELING_REQUEST, (uint16_t)pos);
    return pos;
}

// TUNNELING_REQUEST with L_DATA.con (TX confirmation to ETS)
inline size_t build_tunneling_con(uint8_t* buf, uint8_t channel_id,
                                   uint8_t seq, const ParsedCEMI& frame,
                                   bool success) {
    size_t pos = 6;
    buf[pos++] = 0x04;
    buf[pos++] = channel_id;
    buf[pos++] = seq;
    buf[pos++] = 0x00;

    // CEMI L_DATA.con
    buf[pos++] = CEMI_L_DATA_CON;
    buf[pos++] = 0x00;  // no additional info
    // ctrl1: bit 0 = confirm (1=error, 0=ok)
    buf[pos++] = success ? 0xBC : 0xBC | 0x01;
    buf[pos++] = 0xE0;
    buf[pos++] = (frame.src >> 8) & 0xFF;
    buf[pos++] =  frame.src & 0xFF;
    buf[pos++] = (frame.dst >> 8) & 0xFF;
    buf[pos++] =  frame.dst & 0xFF;
    // APDU (same as in request)
    if (frame.data_len == 0) {
        buf[pos++] = 0x01;
        buf[pos++] = (uint8_t)((frame.apci_cmd >> 8) & 0x03);
        buf[pos++] = (uint8_t)(frame.apci_cmd & 0xFF) | (frame.small_val & 0x3F);
    } else {
        buf[pos++] = 1 + frame.data_len;
        buf[pos++] = (uint8_t)((frame.apci_cmd >> 8) & 0x03);
        buf[pos++] = (uint8_t)(frame.apci_cmd & 0xFF);
        memcpy(&buf[pos], frame.data, frame.data_len);
        pos += frame.data_len;
    }

    write_knxip_header(buf, KNXIP_TUNNELING_REQUEST, (uint16_t)pos);
    return pos;
}

// ─── Request parsers ──────────────────────────────────────────────────────────

// Parse CONNECT_REQUEST, returns false on malformed packet
inline bool parse_connect_request(const uint8_t* buf, int len,
                                   uint8_t ctrl_ip[4], uint16_t& ctrl_port,
                                   uint8_t data_ip[4], uint16_t& data_port,
                                   uint8_t& conn_type, uint8_t& tunnel_layer) {
    if (len < 6 + 8 + 8 + 4) return false;
    if (!parse_hpai(&buf[6],  len - 6,  ctrl_ip, ctrl_port)) return false;
    if (!parse_hpai(&buf[14], len - 14, data_ip, data_port)) return false;
    if (len < 26) return false;
    // CRI: buf[22]=length, buf[23]=type, buf[24]=layer, buf[25]=reserved
    conn_type    = buf[23];
    tunnel_layer = (conn_type == CRI_TUNNEL_CONNECTION && len >= 27) ? buf[24] : TUNNEL_LINKLAYER;
    return true;
}

// Parse CONNECTIONSTATE_REQUEST
inline bool parse_connstate_request(const uint8_t* buf, int len,
                                     uint8_t& channel_id) {
    if (len < 6 + 2) return false;
    channel_id = buf[6];
    // buf[7] = reserved
    return true;
}

// Parse DISCONNECT_REQUEST
inline bool parse_disconnect_request(const uint8_t* buf, int len,
                                      uint8_t& channel_id) {
    if (len < 6 + 2) return false;
    channel_id = buf[6];
    return true;
}

// Parse TUNNELING_REQUEST — extract channel, seq, and CEMI
inline bool parse_tunneling_request(const uint8_t* buf, int len,
                                     uint8_t& channel_id, uint8_t& seq,
                                     ParsedCEMI& frame) {
    if (len < 6 + 4 + 9) return false;

    const uint8_t* th = buf + 6;  // tunnel connection header
    uint8_t th_len = th[0];
    if (th_len < 4 || len < 6 + th_len + 9) return false;

    channel_id = th[1];
    seq        = th[2];
    // th[3] = reserved

    const uint8_t* cemi = th + th_len;
    int cemi_avail = len - 6 - th_len;

    frame = parse_cemi_frame(cemi, cemi_avail);
    return frame.valid;
}

// Parse TUNNELING_ACK (from ETS acknowledging our TUNNELING_REQUEST)
inline bool parse_tunneling_ack(const uint8_t* buf, int len,
                                 uint8_t& channel_id, uint8_t& seq, uint8_t& status) {
    if (len < 6 + 4) return false;
    const uint8_t* th = buf + 6;
    if (th[0] < 4) return false;
    channel_id = th[1];
    seq        = th[2];
    status     = th[3];
    return true;
}

// ─── KNX Tunnel Server ────────────────────────────────────────────────────────

#ifdef ARDUINO

#include "esphome/core/log.h"
static const char* const TAG_TUNNEL = "knxip.tunnel";

class KNXIPTunnelServer {
 public:
    // Call once after UDP socket is ready
    void set_local(const uint8_t ip[4], uint16_t port) {
        memcpy(local_ip_, ip, 4);
        local_port_ = port;
    }
    void set_ia(uint16_t ia)           { ia_ = ia; }
    void set_name(const char* name)    { name_ = name; }
    void set_mac(const uint8_t mac[6]) { memcpy(mac_, mac, 6); }
    void set_udp(WiFiUDP* udp)         { udp_ = udp; }

    bool has_active_session() const { return session_.active; }
    const TunnelSession& session()  const { return session_; }

    // Handle incoming unicast packet (service already parsed)
    // buf = full KNXnet/IP packet, remote_ip/port = sender
    void handle_packet(const uint8_t* buf, int len,
                       const uint8_t remote_ip[4], uint16_t remote_port) {
        uint16_t svc = read_knxip_service(buf, len);

        switch (svc) {
            case KNXIP_SEARCH_REQUEST:
                handle_search_request_(buf, len, remote_ip, remote_port);
                break;
            case KNXIP_DESCRIPTION_REQUEST:
                handle_description_request_(buf, len, remote_ip, remote_port);
                break;
            case KNXIP_CONNECT_REQUEST:
                handle_connect_request_(buf, len, remote_ip, remote_port);
                break;
            case KNXIP_CONNECTIONSTATE_REQUEST:
                handle_connstate_request_(buf, len, remote_ip, remote_port);
                break;
            case KNXIP_DISCONNECT_REQUEST:
                handle_disconnect_request_(buf, len, remote_ip, remote_port);
                break;
            case KNXIP_TUNNELING_REQUEST:
                handle_tunneling_request_(buf, len, remote_ip, remote_port);
                break;
            case KNXIP_TUNNELING_ACK:
                handle_tunneling_ack_(buf, len);
                break;
            case KNXIP_DISCONNECT_RESPONSE:
                if (session_.active) close_session_("DISCONNECT_RESPONSE received");
                break;
            default:
                break;
        }
    }

    // Called when a frame is received from the KNX bus (TP or IP routing)
    // Forward it to ETS via TUNNELING_REQUEST
    void forward_to_ets(const ParsedCEMI& frame) {
        if (!session_.active) return;
        uint8_t buf[256];
        size_t len = build_tunneling_request(buf, session_.channel_id,
                                              session_.seq_send++, frame,
                                              CEMI_L_DATA_IND);
        send_to_data_endpoint_(buf, len);
        ESP_LOGD(TAG_TUNNEL, "→ETS TUNNELING_REQ seq=%d src=0x%04X dst=0x%04X",
                 (int)(session_.seq_send - 1), frame.src, frame.dst);
    }

    // Periodic call for session timeout check
    void loop(uint32_t now_ms) {
        if (session_.timed_out(now_ms)) {
            ESP_LOGW(TAG_TUNNEL, "Session ch=%d timed out — closing", session_.channel_id);
            close_session_("heartbeat timeout");
        }
    }

    // Callback: called when ETS sends a TUNNELING_REQUEST (frame from ETS → bus)
    // Returns the parsed frame for the caller to forward to the bus
    using TunnelingCallback = std::function<void(const ParsedCEMI&)>;
    void set_on_tunneling(TunnelingCallback cb) { on_tunneling_ = cb; }

 private:
    WiFiUDP*  udp_          = nullptr;
    uint16_t  ia_           = 0x1132;
    uint16_t  local_port_   = KNXIP_PORT;
    uint8_t   local_ip_[4]  = {0};
    uint8_t   mac_[6]       = {0};
    const char* name_       = "ESP32-KNX-IP";
    TunnelSession session_;
    TunnelingCallback on_tunneling_;
    static uint8_t next_channel_id_;

    void send_unicast_(const uint8_t* buf, size_t len,
                       const uint8_t ip[4], uint16_t port) {
        if (!udp_) return;
        udp_->beginPacket(IPAddress(ip[0], ip[1], ip[2], ip[3]), port);
        udp_->write(buf, len);
        udp_->endPacket();
    }

    void send_to_ctrl_endpoint_(const uint8_t* buf, size_t len) {
        send_unicast_(buf, len, session_.ctrl_ip, session_.ctrl_port);
    }

    void send_to_data_endpoint_(const uint8_t* buf, size_t len) {
        send_unicast_(buf, len, session_.data_ip, session_.data_port);
    }

    void close_session_(const char* reason) {
        ESP_LOGI(TAG_TUNNEL, "Session ch=%d closed: %s", session_.channel_id, reason);
        session_ = TunnelSession{};
    }

    void handle_search_request_(const uint8_t* buf, int len,
                                 const uint8_t remote_ip[4], uint16_t remote_port) {
        // Parse discovery endpoint HPAI from buf[6..13]
        uint8_t disc_ip[4] = {};
        uint16_t disc_port = 0;
        if (len >= 14) parse_hpai(&buf[6], len - 6, disc_ip, disc_port);

        // Reply to discovery endpoint (or sender if HPAI is zero/local)
        const uint8_t* reply_ip   = (disc_port && disc_ip[0]) ? disc_ip : remote_ip;
        uint16_t        reply_port = disc_port ? disc_port : remote_port;

        uint8_t resp[128];
        size_t rlen = build_search_response(resp, local_ip_, local_port_,
                                             ia_, mac_, name_);
        send_unicast_(resp, rlen, reply_ip, reply_port);
        ESP_LOGI(TAG_TUNNEL, "SEARCH_RESPONSE → %d.%d.%d.%d:%d",
                 reply_ip[0], reply_ip[1], reply_ip[2], reply_ip[3], reply_port);
    }

    void handle_description_request_(const uint8_t* buf, int len,
                                      const uint8_t remote_ip[4], uint16_t remote_port) {
        uint8_t resp[128];
        size_t rlen = build_description_response(resp, ia_, mac_, name_);
        send_unicast_(resp, rlen, remote_ip, remote_port);
        ESP_LOGI(TAG_TUNNEL, "DESCRIPTION_RESPONSE → %d.%d.%d.%d",
                 remote_ip[0], remote_ip[1], remote_ip[2], remote_ip[3]);
    }

    void handle_connect_request_(const uint8_t* buf, int len,
                                  const uint8_t remote_ip[4], uint16_t remote_port) {
        uint8_t ctrl_ip[4], data_ip[4];
        uint16_t ctrl_port, data_port;
        uint8_t conn_type, tunnel_layer;

        if (!parse_connect_request(buf, len, ctrl_ip, ctrl_port,
                                    data_ip, data_port, conn_type, tunnel_layer)) {
            ESP_LOGW(TAG_TUNNEL, "CONNECT_REQUEST malformed");
            uint8_t err[32];
            size_t elen = build_connect_response_err(err, E_HOST_PROT_TYPE);
            send_unicast_(err, elen, remote_ip, remote_port);
            return;
        }

        if (conn_type != CRI_TUNNEL_CONNECTION) {
            ESP_LOGW(TAG_TUNNEL, "CONNECT: unsupported type 0x%02X", conn_type);
            uint8_t err[32];
            size_t elen = build_connect_response_err(err, E_CONN_TYPE);
            send_unicast_(err, elen, remote_ip, remote_port);
            return;
        }

        if (session_.active) {
            ESP_LOGW(TAG_TUNNEL, "CONNECT: already have active session, rejecting");
            uint8_t err[32];
            size_t elen = build_connect_response_err(err, E_NO_MORE_CONNECTIONS);
            send_unicast_(err, elen, remote_ip, remote_port);
            return;
        }

        // Accept connection
        session_.active = true;
        session_.channel_id = ++next_channel_id_;
        if (session_.channel_id == 0) session_.channel_id = 1;
        memcpy(session_.ctrl_ip, ctrl_ip[0] ? ctrl_ip : remote_ip, 4);
        session_.ctrl_port = ctrl_port ? ctrl_port : remote_port;
        memcpy(session_.data_ip, data_ip[0] ? data_ip : remote_ip, 4);
        session_.data_port = data_port ? data_port : remote_port;
        session_.seq_recv  = 0xFF;
        session_.seq_send  = 0;
        session_.last_activity_ms = millis();

        uint8_t resp[32];
        size_t rlen = build_connect_response_ok(resp, session_.channel_id,
                                                 local_ip_, local_port_, ia_);
        send_unicast_(resp, rlen, remote_ip, remote_port);

        ESP_LOGI(TAG_TUNNEL,
                 "CONNECT ch=%d from %d.%d.%d.%d layer=0x%02X",
                 session_.channel_id,
                 ctrl_ip[0], ctrl_ip[1], ctrl_ip[2], ctrl_ip[3],
                 tunnel_layer);
    }

    void handle_connstate_request_(const uint8_t* buf, int len,
                                    const uint8_t remote_ip[4], uint16_t remote_port) {
        uint8_t ch = 0;
        parse_connstate_request(buf, len, ch);

        uint8_t status = E_NO_ERROR;
        if (!session_.active || session_.channel_id != ch) {
            status = E_CONNECTION_ID;
        } else {
            session_.last_activity_ms = millis();
        }

        uint8_t resp[16];
        size_t rlen = build_connstate_response(resp, ch, status);
        send_unicast_(resp, rlen, remote_ip, remote_port);
        ESP_LOGD(TAG_TUNNEL, "CONNSTATE ch=%d status=0x%02X", ch, status);
    }

    void handle_disconnect_request_(const uint8_t* buf, int len,
                                     const uint8_t remote_ip[4], uint16_t remote_port) {
        uint8_t ch = 0;
        parse_disconnect_request(buf, len, ch);

        uint8_t resp[16];
        size_t rlen = build_disconnect_response(resp, ch);
        send_unicast_(resp, rlen, remote_ip, remote_port);

        if (session_.active && session_.channel_id == ch) {
            close_session_("DISCONNECT_REQUEST from ETS");
        }
        ESP_LOGI(TAG_TUNNEL, "DISCONNECT ch=%d", ch);
    }

    void handle_tunneling_request_(const uint8_t* buf, int len,
                                    const uint8_t remote_ip[4], uint16_t remote_port) {
        uint8_t ch, seq;
        ParsedCEMI frame;

        if (!parse_tunneling_request(buf, len, ch, seq, frame)) {
            ESP_LOGW(TAG_TUNNEL, "TUNNELING_REQ malformed");
            return;
        }

        // Send ACK immediately
        uint8_t ack[16];
        uint8_t ack_status = E_NO_ERROR;
        if (!session_.active || session_.channel_id != ch) {
            ack_status = E_CONNECTION_ID;
        }
        size_t alen = build_tunneling_ack(ack, ch, seq, ack_status);
        send_unicast_(ack, alen, remote_ip, remote_port);

        if (ack_status != E_NO_ERROR) return;

        // Check sequence: accept same seq (duplicate) or next
        if (session_.seq_recv != 0xFF && seq != (uint8_t)(session_.seq_recv + 1)
            && seq != session_.seq_recv) {
            ESP_LOGW(TAG_TUNNEL, "TUNNELING_REQ unexpected seq %d (expected %d)",
                     seq, (uint8_t)(session_.seq_recv + 1));
        }
        session_.seq_recv = seq;
        session_.last_activity_ms = millis();

        // Forward frame to bus via callback
        if (on_tunneling_) {
            on_tunneling_(frame);
        }

        // Send L_DATA.con back to ETS confirming we received and are sending
        uint8_t con[256];
        size_t clen = build_tunneling_con(con, session_.channel_id,
                                           session_.seq_send++, frame, true);
        send_to_data_endpoint_(con, clen);

        ESP_LOGD(TAG_TUNNEL, "←ETS TUNNELING_REQ seq=%d src=0x%04X dst=0x%04X",
                 seq, frame.src, frame.dst);
    }

    void handle_tunneling_ack_(const uint8_t* buf, int len) {
        uint8_t ch, seq, status;
        if (!parse_tunneling_ack(buf, len, ch, seq, status)) return;
        ESP_LOGD(TAG_TUNNEL, "ETS ACK ch=%d seq=%d status=0x%02X", ch, seq, status);
        session_.last_activity_ms = millis();
    }
};

// Static member definition
uint8_t KNXIPTunnelServer::next_channel_id_ = 0;

#endif  // ARDUINO

}  // namespace knxip
}  // namespace esphome
