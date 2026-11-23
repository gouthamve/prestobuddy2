// WiFi + link to buddyd.
//
// buddyd broadcasts "PBUDDY1 <tcp-port>" over UDP every couple of seconds.
// We listen for it, connect back to the sender over TCP and exchange
// newline-delimited JSON.

#include "net.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "hal.h"
#include "lwip/pbuf.h"
#include "lwip/tcp.h"
#include "lwip/udp.h"
#include "pico/cyw43_arch.h"
#include "pico/platform/sections.h"
#include "secrets.h"

namespace {

constexpr uint16_t kBeaconPort = 47800;
constexpr uint32_t kRxTimeoutMs = 12000;  // buddyd pings every 3 s
constexpr size_t kLineMax = 16 * 1024;

hal::LinkState state = hal::LinkState::NoWifi;
bool chip_ok = false;
uint32_t state_since = 0;
uint32_t last_rx = 0;

udp_pcb* beacon_pcb = nullptr;
tcp_pcb* conn = nullptr;
ip_addr_t server_addr;
uint16_t server_port = 0;
bool beacon_seen = false;
uint32_t beacons = 0, udp_packets = 0, last_report = 0;

char __uninitialized_psram("net") line_buf[kLineMax];
size_t line_len = 0;
bool line_overflow = false;

hal::LineHandler handler = nullptr;
void* handler_ctx = nullptr;

void set_state(hal::LinkState s) {
    if (s == state) return;
    state = s;
    state_since = hal::millis();
    static const char* names[] = {"no-wifi", "joining", "searching", "connecting", "connected"};
    hal::log("net: %s", names[(int)s]);
}

void close_conn(bool abort) {
    if (!conn) return;
    tcp_arg(conn, nullptr);
    tcp_recv(conn, nullptr);
    tcp_err(conn, nullptr);
    tcp_sent(conn, nullptr);
    if (abort || tcp_close(conn) != ERR_OK) tcp_abort(conn);
    conn = nullptr;
    line_len = 0;
    line_overflow = false;
}

void on_beacon(void*, udp_pcb*, pbuf* p, const ip_addr_t* addr, u16_t) {
    udp_packets++;
    char msg[32] = {};
    pbuf_copy_partial(p, msg, sizeof(msg) - 1, 0);
    pbuf_free(p);
    if (strncmp(msg, "PBUDDY1 ", 8) != 0) return;
    int port = atoi(msg + 8);
    if (port <= 0 || port > 65535) return;
    beacons++;
    server_addr = *addr;
    server_port = (uint16_t)port;
    beacon_seen = true;
}

err_t on_recv(void*, tcp_pcb* pcb, pbuf* p, err_t) {
    if (!p) {  // closed by the server
        close_conn(false);
        set_state(hal::LinkState::Searching);
        return ERR_OK;
    }
    last_rx = hal::millis();
    for (pbuf* q = p; q; q = q->next) {
        const char* d = (const char*)q->payload;
        for (u16_t i = 0; i < q->len; i++) {
            char c = d[i];
            if (c == '\n') {
                if (!line_overflow && handler && line_len > 0) {
                    line_buf[line_len] = 0;
                    handler(line_buf, line_len, handler_ctx);
                }
                line_len = 0;
                line_overflow = false;
            } else if (line_len < kLineMax - 1) {
                line_buf[line_len++] = c;
            } else {
                line_overflow = true;
            }
        }
    }
    tcp_recved(pcb, p->tot_len);
    pbuf_free(p);
    return ERR_OK;
}

void on_err(void*, err_t) {
    conn = nullptr;  // lwIP has already freed the pcb
    line_len = 0;
    set_state(hal::LinkState::Searching);
}

err_t on_connected(void*, tcp_pcb*, err_t err) {
    if (err != ERR_OK) return err;
    last_rx = hal::millis();
    set_state(hal::LinkState::Connected);
    return ERR_OK;
}

void start_connect() {
    conn = tcp_new_ip_type(IPADDR_TYPE_V4);
    if (!conn) return;
    tcp_nagle_disable(conn);
    tcp_recv(conn, on_recv);
    tcp_err(conn, on_err);
    if (tcp_connect(conn, &server_addr, server_port, on_connected) != ERR_OK) {
        close_conn(true);
        return;
    }
    set_state(hal::LinkState::Connecting);
}

void join() {
    cyw43_arch_wifi_connect_async(WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK);
    set_state(hal::LinkState::Joining);
    state_since = hal::millis();
}

}  // namespace

namespace net {

void init() {
    if (cyw43_arch_init() != 0) {
        hal::log("net: wifi chip init failed");
        return;
    }
    chip_ok = true;
    cyw43_arch_enable_sta_mode();
    // Default power-save adds ~100 ms of latency to incoming packets.
    cyw43_wifi_pm(&cyw43_state, CYW43_PERFORMANCE_PM);
    netif_set_hostname(&cyw43_state.netif[CYW43_ITF_STA], "prestobuddy");

    beacon_pcb = udp_new_ip_type(IPADDR_TYPE_V4);
    if (beacon_pcb) {
        ip_set_option(beacon_pcb, SOF_BROADCAST);
        udp_bind(beacon_pcb, IP_ANY_TYPE, kBeaconPort);
        udp_recv(beacon_pcb, on_beacon, nullptr);
    }
    join();
}

void poll() {
    if (!chip_ok) return;
    cyw43_arch_poll();

    const uint32_t now = hal::millis();
    const int link = cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA);
    if (now - last_report > 10000) {
        last_report = now;
        hal::log("net: state %d, wifi link %d, ip %s, udp packets %u, beacons %u", (int)state, link,
                 ip4addr_ntoa(netif_ip4_addr(&cyw43_state.netif[CYW43_ITF_STA])), (unsigned)udp_packets, (unsigned)beacons);
    }

    if (state == hal::LinkState::Joining) {
        if (link == CYW43_LINK_UP) {
            hal::log("net: ip %s", ip4addr_ntoa(netif_ip4_addr(&cyw43_state.netif[CYW43_ITF_STA])));
            beacon_seen = false;
            set_state(hal::LinkState::Searching);
        } else if (link < 0 || now - state_since > 20000) {
            hal::log("net: join failed (%d), retrying", link);
            join();
        }
        return;
    }

    if (link != CYW43_LINK_UP) {  // dropped off the network
        close_conn(true);
        join();
        return;
    }

    switch (state) {
        case hal::LinkState::Searching:
            if (beacon_seen) {
                beacon_seen = false;
                start_connect();
            }
            break;
        case hal::LinkState::Connecting:
            if (now - state_since > 5000) {
                close_conn(true);
                set_state(hal::LinkState::Searching);
            }
            break;
        case hal::LinkState::Connected:
            if (now - last_rx > kRxTimeoutMs) {
                hal::log("net: server went quiet");
                close_conn(true);
                beacon_seen = false;
                set_state(hal::LinkState::Searching);
            }
            break;
        default:
            break;
    }
}

}  // namespace net

namespace hal {

LinkState link_state() { return state; }

bool link_send(const char* line) {
    if (state != LinkState::Connected || !conn) return false;
    const size_t n = strlen(line);
    if (tcp_sndbuf(conn) < n + 1) return false;
    if (tcp_write(conn, line, n, TCP_WRITE_FLAG_COPY | TCP_WRITE_FLAG_MORE) != ERR_OK) return false;
    if (tcp_write(conn, "\n", 1, TCP_WRITE_FLAG_COPY) != ERR_OK) return false;
    tcp_output(conn);
    return true;
}

void link_set_handler(LineHandler h, void* ctx) {
    handler = h;
    handler_ctx = ctx;
}

}  // namespace hal
