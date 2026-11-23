#include "board.hpp"

#include <cstdarg>
#include <cstdio>
#include <malloc.h>

#include "hal.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "hardware/pio.h"
#include "hardware/pwm.h"
#include "pico/stdlib.h"
#include "st7701.hpp"

#include "ws2812.pio.h"

extern "C" {
extern char __StackLimit, __bss_end__;
}

namespace {

constexpr uint kPinTouchSda = 30;
constexpr uint kPinTouchScl = 31;
constexpr uint kPinTouchInt = 32;
constexpr uint8_t kTouchAddr = 0x48;  // FT6236
#define TOUCH_I2C i2c1

constexpr uint kPinLeds = 33;
constexpr uint kPinBuzzer = 43;

PIO led_pio;
uint led_sm;
bool led_ok = false;

board::Touch last_touch = {false, 240, 240};
int touch_backoff = 0;

uint32_t flush_us_max = 0;

}  // namespace

namespace board {

void init() {
    // Touch
    i2c_init(TOUCH_I2C, 400 * 1000);
    gpio_set_function(kPinTouchSda, GPIO_FUNC_I2C);
    gpio_set_function(kPinTouchScl, GPIO_FUNC_I2C);
    gpio_pull_up(kPinTouchSda);
    gpio_pull_up(kPinTouchScl);
    gpio_init(kPinTouchInt);
    gpio_set_dir(kPinTouchInt, GPIO_IN);
    gpio_pull_up(kPinTouchInt);

    // LEDs: GPIO33 needs a PIO whose pin window covers GPIO 32+.
    uint offset;
    led_ok = pio_claim_free_sm_and_add_program_for_gpio_range(&ws2812_program, &led_pio, &led_sm, &offset,
                                                              kPinLeds, 1, true);
    if (led_ok) {
        pio_gpio_init(led_pio, kPinLeds);
        pio_sm_set_consecutive_pindirs(led_pio, led_sm, kPinLeds, 1, true);
        pio_sm_config c = ws2812_program_get_default_config(offset);
        sm_config_set_sideset_pins(&c, kPinLeds);
        sm_config_set_out_shift(&c, false, true, 24);
        sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);
        const int cycles_per_bit = ws2812_T1 + ws2812_T2 + ws2812_T3;
        sm_config_set_clkdiv(&c, (float)clock_get_hz(clk_sys) / (800000.0f * cycles_per_bit));
        pio_sm_init(led_pio, led_sm, offset, &c);
        pio_sm_set_enabled(led_pio, led_sm, true);
    }
    const uint8_t off[hal::kNumLeds][3] = {};
    hal::set_leds(off);

    // Piezo
    gpio_set_function(kPinBuzzer, GPIO_FUNC_PWM);
    pwm_set_enabled(pwm_gpio_to_slice_num(kPinBuzzer), false);
}

Touch read_touch() {
    if (touch_backoff > 0) {
        touch_backoff--;
        return last_touch;
    }
    // INT is low while the controller has a report; keep polling while a
    // finger is down so the release is always seen.
    if (gpio_get(kPinTouchInt) && !last_touch.down) return last_touch;

    uint8_t reg = 0;
    uint8_t buf[9];
    int w = i2c_write_timeout_us(TOUCH_I2C, kTouchAddr, &reg, 1, true, 2000);
    int r = w == 1 ? i2c_read_timeout_us(TOUCH_I2C, kTouchAddr, buf, sizeof(buf), false, 4000) : -1;
    if (r != (int)sizeof(buf)) {
        touch_backoff = 20;  // the controller NACKs now and then; don't stall the loop
        return last_touch;
    }
    const uint8_t touches = buf[2] & 0x0f;
    bool down = false;
    if (touches >= 1 && touches <= 2) {
        const uint8_t* d = &buf[3];
        const uint8_t event = d[0] >> 6;  // 0 down, 1 up, 2 contact, 3 none
        int x = ((d[0] & 0x0f) << 8) | d[1];
        int y = ((d[2] & 0x0f) << 8) | d[3];
        if (event == 0 || event == 2) {
            down = true;
            last_touch.x = x < 0 ? 0 : (x > 479 ? 479 : x);
            last_touch.y = y < 0 ? 0 : (y > 479 ? 479 : y);
        }
    }
    last_touch.down = down;
    return last_touch;
}

void note_flush_time(uint32_t us) {
    if (us > flush_us_max) flush_us_max = us;
}

}  // namespace board

namespace hal {

uint32_t millis() { return to_ms_since_boot(get_absolute_time()); }

void set_leds(const uint8_t rgb[kNumLeds][3]) {
    if (!led_ok) return;
    // The joined TX FIFO holds 8 words, so 7 LEDs never block. Skip the frame
    // if the previous one is still draining.
    if (!pio_sm_is_tx_fifo_empty(led_pio, led_sm)) return;
    for (int i = 0; i < kNumLeds; i++) {
        uint32_t grb = ((uint32_t)rgb[i][1] << 16) | ((uint32_t)rgb[i][0] << 8) | rgb[i][2];
        pio_sm_put(led_pio, led_sm, grb << 8);
    }
}

void tone(uint16_t freq_hz, uint8_t volume) {
    static uint16_t cur_freq = 0;
    static uint8_t cur_vol = 0;
    if (freq_hz == cur_freq && volume == cur_vol) return;
    cur_freq = freq_hz;
    cur_vol = volume;
    const uint slice = pwm_gpio_to_slice_num(kPinBuzzer);
    if (freq_hz < 40 || volume == 0) {
        pwm_set_gpio_level(kPinBuzzer, 0);
        pwm_set_enabled(slice, false);
        return;
    }
    const uint32_t clk = clock_get_hz(clk_sys);
    uint32_t div = clk / (freq_hz * 65536u) + 1;
    uint32_t wrap = clk / (div * freq_hz) - 1;
    pwm_set_clkdiv_int_frac(slice, div, 0);
    pwm_set_wrap(slice, wrap);
    // 50% duty is loudest; scale duty with volume for a rough loudness control.
    pwm_set_gpio_level(kPinBuzzer, (wrap * volume) / 510);
    pwm_set_enabled(slice, true);
}

void set_backlight(uint8_t level) { st7701::set_backlight(level); }

void log(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    putchar('\n');
}

Stats take_stats() {
    struct mallinfo m = mallinfo();
    Stats s;
    s.flush_us_max = flush_us_max;
    s.free_sram = (uint32_t)(&__StackLimit - &__bss_end__) - m.uordblks;
    flush_us_max = 0;
    return s;
}

}  // namespace hal
