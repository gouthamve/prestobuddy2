// Desktop simulator. Runs the same core/ app as the Presto.
//
//   prestobuddy-sim                      interactive SDL window (if built with SDL)
//   prestobuddy-sim --headless --frames 300 --every 30 --out frames
//                                        virtual-time run that dumps PNG frames
//   --script "500:tap:240,400;1500:tap:60,440"   scripted touches (ms:tap:x,y / ms:down:x,y / ms:up)
//   --no-net                             do not look for buddyd
//   --demo                               start with sample goals and agents (no buddyd needed)
//   --demo-alert 3000                    with --demo: an agent starts waiting at this time (ms)
//   --inject '5000:{"t":"cmd",...}'       feed a protocol line at a time (ms); repeatable
//   --dump-at "900,2500"                 headless: also dump frames at these times (ms)
//
// Headless frames are 560x560: the 480x480 screen with the 7 rear LEDs drawn
// as a glow around it, so LED effects can be checked in the dumps too.

#include <arpa/inet.h>
#include <cerrno>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <string>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include <vector>

#include "hal.h"
#include "lvgl.h"
#include "png.hpp"

#ifdef BUDDY_SIM_SDL
#include <SDL.h>
#endif

namespace {

bool headless = false;
bool use_net = true;
bool demo = false;
std::vector<uint32_t> dump_at;
uint32_t virtual_ms = 0;
uint8_t leds[hal::kNumLeds][3];
uint16_t cur_freq = 0;
uint8_t cur_vol = 0;

uint32_t real_ms() {
    static timeval t0 = {0, 0};
    timeval tv;
    gettimeofday(&tv, nullptr);
    if (!t0.tv_sec) t0 = tv;
    return (uint32_t)((tv.tv_sec - t0.tv_sec) * 1000 + (tv.tv_usec - t0.tv_usec) / 1000);
}

// ---- scripted touch ----
struct TouchEvent {
    uint32_t at;
    bool down;
    int x, y;
};
std::vector<TouchEvent> script;
bool touch_down = false;
int touch_x = 240, touch_y = 240;

void parse_script(const char* s) {
    std::string str(s);
    size_t pos = 0;
    while (pos < str.size()) {
        size_t end = str.find(';', pos);
        if (end == std::string::npos) end = str.size();
        std::string item = str.substr(pos, end - pos);
        pos = end + 1;
        unsigned at = 0;
        int x = 0, y = 0;
        char kind[16] = {};
        int n = sscanf(item.c_str(), " %u:%15[a-z]:%d,%d", &at, kind, &x, &y);
        if (n < 2) continue;
        if (!strcmp(kind, "tap")) {
            script.push_back({at, true, x, y});
            script.push_back({at + 80, false, x, y});
        } else if (!strcmp(kind, "down")) {
            script.push_back({at, true, x, y});
        } else if (!strcmp(kind, "up")) {
            script.push_back({at, false, touch_x, touch_y});
        }
    }
}

void headless_touch_cb(lv_indev_t*, lv_indev_data_t* data) {
    const uint32_t now = hal::millis();
    for (auto& e : script) {
        if (e.at != UINT32_MAX && e.at <= now) {
            touch_down = e.down;
            if (e.down) {
                touch_x = e.x;
                touch_y = e.y;
            }
            e.at = UINT32_MAX;
        }
    }
    data->point.x = touch_x;
    data->point.y = touch_y;
    data->state = touch_down ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

// ---- headless display ----
std::vector<uint16_t> screen(hal::kScreenW * hal::kScreenH);

void headless_flush(lv_display_t* disp, const lv_area_t* area, uint8_t* px) {
    const int w = lv_area_get_width(area);
    const uint16_t* src = (const uint16_t*)px;
    for (int y = area->y1; y <= area->y2; y++) {
        memcpy(&screen[y * hal::kScreenW + area->x1], src, w * 2);
        src += w;
    }
    lv_display_flush_ready(disp);
}

void dump_frame(const char* dir, int index) {
    const int M = 40, W = hal::kScreenW + 2 * M, H = hal::kScreenH + 2 * M;
    std::vector<uint8_t> rgb((size_t)W * H * 3, 0);
    // LED positions as seen from the front (see hal.h for the index order).
    const float pos[hal::kNumLeds][2] = {{W - 20.f, H - 60.f}, {W - 20.f, H / 2.f}, {W - 20.f, 60.f}, {W / 2.f, 20.f},
                                         {20.f, 60.f},         {20.f, H / 2.f},     {20.f, H - 60.f}};
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            float acc[3] = {8, 8, 10};
            for (int i = 0; i < hal::kNumLeds; i++) {
                const float dx = x - pos[i][0], dy = y - pos[i][1];
                const float g = expf(-(dx * dx + dy * dy) / (2 * 70.f * 70.f));
                for (int c = 0; c < 3; c++) acc[c] += leds[i][c] * g;
            }
            for (int c = 0; c < 3; c++) rgb[((size_t)y * W + x) * 3 + c] = (uint8_t)(acc[c] > 255 ? 255 : acc[c]);
        }
    }
    for (int y = 0; y < hal::kScreenH; y++) {
        for (int x = 0; x < hal::kScreenW; x++) {
            const uint16_t p = screen[y * hal::kScreenW + x];
            uint8_t* d = &rgb[((size_t)(y + M) * W + x + M) * 3];
            d[0] = (uint8_t)(((p >> 11) & 0x1F) * 255 / 31);
            d[1] = (uint8_t)(((p >> 5) & 0x3F) * 255 / 63);
            d[2] = (uint8_t)((p & 0x1F) * 255 / 31);
        }
    }
    char path[512];
    snprintf(path, sizeof(path), "%s/frame_%04d.png", dir, index);
    png::write(path, rgb.data(), W, H);
}

// ---- link_st to buddyd over BSD sockets ----
hal::LinkState link_st = hal::LinkState::Searching;
int udp_fd = -1, tcp_fd = -1;
std::string rx;
hal::LineHandler handler = nullptr;
void* handler_ctx = nullptr;
uint32_t last_rx = 0;

void set_nonblock(int fd) { fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK); }

void net_init() {
    udp_fd = socket(AF_INET, SOCK_DGRAM, 0);
    int one = 1;
    setsockopt(udp_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
#ifdef SO_REUSEPORT
    setsockopt(udp_fd, SOL_SOCKET, SO_REUSEPORT, &one, sizeof(one));
#endif
    sockaddr_in a = {};
    a.sin_family = AF_INET;
    a.sin_port = htons(47800);
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(udp_fd, (sockaddr*)&a, sizeof(a)) != 0) hal::log("sim: cannot bind beacon port: %s", strerror(errno));
    set_nonblock(udp_fd);
}

void net_drop() {
    if (tcp_fd >= 0) close(tcp_fd);
    tcp_fd = -1;
    rx.clear();
    link_st = hal::LinkState::Searching;
}

void net_poll() {
    if (udp_fd < 0) return;
    const uint32_t now = real_ms();
    if (link_st == hal::LinkState::Searching) {
        char msg[64] = {};
        sockaddr_in from = {};
        socklen_t fl = sizeof(from);
        ssize_t n = recvfrom(udp_fd, msg, sizeof(msg) - 1, 0, (sockaddr*)&from, &fl);
        if (n > 8 && !strncmp(msg, "PBUDDY1 ", 8)) {
            tcp_fd = socket(AF_INET, SOCK_STREAM, 0);
            int one = 1;
            setsockopt(tcp_fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
            from.sin_port = htons((uint16_t)atoi(msg + 8));
            if (connect(tcp_fd, (sockaddr*)&from, sizeof(from)) == 0) {
                set_nonblock(tcp_fd);
                link_st = hal::LinkState::Connected;
                last_rx = now;
                hal::log("sim: connected to buddyd");
            } else {
                net_drop();
            }
        }
        return;
    }
    char buf[4096];
    for (;;) {
        ssize_t n = recv(tcp_fd, buf, sizeof(buf), 0);
        if (n > 0) {
            last_rx = now;
            rx.append(buf, (size_t)n);
            size_t nl;
            while ((nl = rx.find('\n')) != std::string::npos) {
                std::string line = rx.substr(0, nl);
                rx.erase(0, nl + 1);
                if (handler && !line.empty()) handler(line.c_str(), line.size(), handler_ctx);
            }
        } else if (n == 0 || (errno != EAGAIN && errno != EWOULDBLOCK)) {
            hal::log("sim: buddyd connection closed");
            net_drop();
            return;
        } else {
            break;
        }
    }
    if (now - last_rx > 12000) net_drop();
}

}  // namespace

namespace hal {

uint32_t millis() { return headless ? virtual_ms : real_ms(); }

void set_leds(const uint8_t rgb[kNumLeds][3]) { memcpy(leds, rgb, sizeof(leds)); }

void tone(uint16_t freq_hz, uint8_t volume) {
    if (volume == 0) freq_hz = 0;
    if (freq_hz == cur_freq && volume == cur_vol) return;
    cur_freq = freq_hz;
    cur_vol = volume;
    if (headless) printf("[%6u] tone %u vol %u\n", (unsigned)millis(), freq_hz, volume);
}

void set_backlight(uint8_t level) {
    if (headless) printf("[%6u] backlight %u\n", (unsigned)millis(), level);
}

LinkState link_state() { return demo ? LinkState::Connected : (use_net ? link_st : LinkState::NoWifi); }

bool link_send(const char* line) {
    if (link_st != LinkState::Connected || tcp_fd < 0) return false;
    std::string s(line);
    s.push_back('\n');
    return send(tcp_fd, s.data(), s.size(), 0) == (ssize_t)s.size();
}

void link_set_handler(LineHandler h, void* ctx) {
    handler = h;
    handler_ctx = ctx;
}

void log(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    printf("[%6u] ", (unsigned)millis());
    vprintf(fmt, ap);
    va_end(ap);
    putchar('\n');
    fflush(stdout);
}

Stats take_stats() { return {0, 0}; }

}  // namespace hal

#ifdef BUDDY_SIM_SDL
namespace {

uint32_t sdl_audio_phase = 0;
void audio_cb(void*, Uint8* stream, int len) {
    int16_t* out = (int16_t*)stream;
    const int n = len / 2;
    for (int i = 0; i < n; i++) {
        if (cur_freq == 0) {
            out[i] = 0;
            continue;
        }
        sdl_audio_phase += (uint32_t)((uint64_t)cur_freq * 0x10000 / 44100);
        out[i] = ((sdl_audio_phase & 0x8000) ? 1 : -1) * (int16_t)(cur_vol * 12);
    }
}

// The LEDs get their own small window under the screen.
SDL_Window* led_win = nullptr;
SDL_Renderer* led_ren = nullptr;
void draw_leds() {
    if (!led_ren) return;
    SDL_SetRenderDrawColor(led_ren, 10, 10, 12, 255);
    SDL_RenderClear(led_ren);
    // Laid out left-to-right as they wrap around the back: 4,5,6 | 3 | 2,1,0
    const int order[hal::kNumLeds] = {6, 5, 4, 3, 2, 1, 0};
    for (int i = 0; i < hal::kNumLeds; i++) {
        const uint8_t* c = leds[order[i]];
        SDL_SetRenderDrawColor(led_ren, c[0], c[1], c[2], 255);
        SDL_Rect r = {8 + i * 68, 8, 60, 34};
        SDL_RenderFillRect(led_ren, &r);
    }
    SDL_RenderPresent(led_ren);
}

}  // namespace
#endif

namespace {
// Sample data so every screen has something to show without buddyd.
// 2026-11-23 10:42 local time.
const char* kDemoState =
    "{\"t\":\"state\",\"time\":1795430520,\"tz\":3600,\"pomos_today\":2,"
    "\"goals\":["
    "{\"id\":\"g1\",\"title\":\"Review the remote-write PR\",\"done\":true,\"est\":1,\"pomos\":1},"
    "{\"id\":\"g2\",\"title\":\"Write the Q1 roadmap draft\",\"done\":false,\"est\":3,\"pomos\":1},"
    "{\"id\":\"g3\",\"title\":\"Fix the flaky ingester test and backport it to the release branch\",\"done\":false,\"est\":2,\"pomos\":0},"
    "{\"id\":\"g4\",\"title\":\"Reply to the conference organisers\",\"done\":false,\"est\":0,\"pomos\":0},"
    "{\"id\":\"g5\",\"title\":\"Book train tickets\",\"done\":false,\"est\":0,\"pomos\":0}],"
    "\"agents\":["
    "{\"id\":\"a1\",\"name\":\"fix-auth-refresh\",\"kind\":\"claude\",\"state\":\"working\",\"since\":1795430100},"
    "{\"id\":\"a2\",\"name\":\"docs-rewrite\",\"kind\":\"codex\",\"state\":\"working\",\"since\":1795428000},"
    "{\"id\":\"a3\",\"name\":\"bump-deps\",\"kind\":\"claude\",\"state\":\"done\",\"since\":1795429000}],"
    "\"settings\":{\"focus_min\":25,\"led\":70,\"volume\":50}}";
const char* kDemoAlert =
    "{\"t\":\"state\",\"time\":1795430530,\"tz\":3600,"
    "\"agents\":["
    "{\"id\":\"a1\",\"name\":\"fix-auth-refresh\",\"kind\":\"claude\",\"state\":\"waiting\",\"since\":1795430528},"
    "{\"id\":\"a2\",\"name\":\"docs-rewrite\",\"kind\":\"codex\",\"state\":\"working\",\"since\":1795428000},"
    "{\"id\":\"a3\",\"name\":\"bump-deps\",\"kind\":\"claude\",\"state\":\"done\",\"since\":1795429000}]}";
struct Inject {
    uint32_t at;
    std::string line;
};
std::vector<Inject> injects;
}  // namespace

int main(int argc, char** argv) {
    int frames = 300, every = 30;
    const char* out = "frames";
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--headless")) headless = true;
        else if (!strcmp(argv[i], "--no-net")) use_net = false;
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc) frames = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--every") && i + 1 < argc) every = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--out") && i + 1 < argc) out = argv[++i];
        else if (!strcmp(argv[i], "--script") && i + 1 < argc) parse_script(argv[++i]);
        else if (!strcmp(argv[i], "--demo")) demo = true;
        else if (!strcmp(argv[i], "--demo-alert") && i + 1 < argc) injects.push_back({(uint32_t)atoi(argv[++i]), kDemoAlert});
        else if (!strcmp(argv[i], "--inject") && i + 1 < argc) {  // "ms:{json line}"
            const char* a = argv[++i];
            const char* colon = strchr(a, ':');
            if (colon) injects.push_back({(uint32_t)atoi(a), colon + 1});
        }
        else if (!strcmp(argv[i], "--dump-at") && i + 1 < argc) {
            for (char* tok = strtok(argv[++i], ","); tok; tok = strtok(nullptr, ",")) dump_at.push_back((uint32_t)atoi(tok));
        }
    }
#ifndef BUDDY_SIM_SDL
    headless = true;
#endif

    lv_init();
    lv_tick_set_cb(hal::millis);
    if (use_net) net_init();

    if (headless) {
        static uint16_t strip[hal::kScreenW * 24];
        lv_display_t* disp = lv_display_create(hal::kScreenW, hal::kScreenH);
        lv_display_set_buffers(disp, strip, nullptr, sizeof(strip), LV_DISPLAY_RENDER_MODE_PARTIAL);
        lv_display_set_flush_cb(disp, headless_flush);
        lv_indev_t* indev = lv_indev_create();
        lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
        lv_indev_set_read_cb(indev, headless_touch_cb);

        mkdir(out, 0755);
        app_init();
        if (demo && handler) handler(kDemoState, strlen(kDemoState), handler_ctx);
        int dumped = 0;
        for (int f = 0; f < frames; f++) {
            if (use_net) net_poll();
            for (auto& in : injects) {
                if (in.at != UINT32_MAX && virtual_ms >= in.at && handler) {
                    handler(in.line.c_str(), in.line.size(), handler_ctx);
                    in.at = UINT32_MAX;
                }
            }
            app_tick();
            lv_timer_handler();
            bool due = every > 0 && f % every == every - 1;
            for (auto& t : dump_at) {
                if (t != UINT32_MAX && virtual_ms >= t) {
                    due = true;
                    t = UINT32_MAX;
                }
            }
            if (due) dump_frame(out, dumped++);
            virtual_ms += 16;
        }
        printf("wrote %d frames to %s\n", dumped, out);
        return 0;
    }

#ifdef BUDDY_SIM_SDL
    lv_display_t* disp = lv_sdl_window_create(hal::kScreenW, hal::kScreenH);
    lv_sdl_window_set_title(disp, "prestobuddy2");
    lv_sdl_mouse_create();

    SDL_InitSubSystem(SDL_INIT_AUDIO);
    SDL_AudioSpec want = {};
    want.freq = 44100;
    want.format = AUDIO_S16SYS;
    want.channels = 1;
    want.samples = 512;
    want.callback = audio_cb;
    if (SDL_OpenAudio(&want, nullptr) == 0) SDL_PauseAudio(0);

    led_win = SDL_CreateWindow("rear LEDs", SDL_WINDOWPOS_CENTERED, 40, 492, 50, 0);
    if (led_win) led_ren = SDL_CreateRenderer(led_win, -1, 0);

    app_init();
    if (demo && handler) handler(kDemoState, strlen(kDemoState), handler_ctx);
    for (;;) {
        if (use_net) net_poll();
        app_tick();
        lv_timer_handler();
        draw_leds();
        usleep(4000);
    }
#endif
    return 0;
}
