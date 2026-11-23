// Platform interface. core/ talks to hardware only through this header, so
// the same app runs on the Presto and in the desktop simulator.
#pragma once
#include <stddef.h>
#include <stdint.h>

// Large static data. On the Presto SRAM is almost entirely the display's
// front buffer, so anything sizeable is placed in PSRAM instead.
#if defined(PICO_RP2350)
#define BUDDY_BIG_DATA __attribute__((section(".psram_uninitialised.app")))
#else
#define BUDDY_BIG_DATA
#endif

namespace hal {

constexpr int kNumLeds = 7;
constexpr int kScreenW = 480;
constexpr int kScreenH = 480;

// Milliseconds since boot (monotonic).
uint32_t millis();

// Rear ambient LEDs, 0-255 per channel. Index order follows Pimoroni:
// 0 bottom-right, 1 right, 2 top-right, 3 top, 4 top-left, 5 left, 6 bottom-left
// (as seen from the front).
void set_leds(const uint8_t rgb[kNumLeds][3]);

// Piezo. freq_hz == 0 or volume == 0 silences it. volume is 0-255.
void tone(uint16_t freq_hz, uint8_t volume);

void set_backlight(uint8_t level);

// Link to buddyd (line-delimited JSON over TCP, discovered by UDP beacon).
enum class LinkState : uint8_t { NoWifi, Joining, Searching, Connecting, Connected };
LinkState link_state();
// Sends one line (newline appended). Returns false if not connected or busy.
bool link_send(const char* line);
typedef void (*LineHandler)(const char* line, size_t len, void* ctx);
void link_set_handler(LineHandler handler, void* ctx);

void log(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

// Diagnostics for the bring-up screen.
struct Stats {
    uint32_t flush_us_max;   // slowest single flush since last read
    uint32_t free_sram;      // bytes of libc heap headroom (0 if unknown)
};
Stats take_stats();

}  // namespace hal

// Implemented by core/, called by each platform's main loop.
void app_init();
void app_tick();
