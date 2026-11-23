// Presto entry point: brings up the hardware, wires LVGL to it and runs the
// portable app from core/.

#include <cstdio>

#include "board.hpp"
#include "hal.h"
#include "lvgl.h"
#include "net.hpp"
#include "pico/multicore.h"
#include "pico/platform/sections.h"
#include "pico/stdlib.h"
#include "st7701.hpp"

namespace {

// Scanned out by the display interrupts on core 1; must be SRAM.
__attribute__((section(".uninitialized_data"))) uint16_t front_buffer[st7701::kWidth * st7701::kHeight];

// LVGL renders strips here (SRAM, so drawing is fast), then they are copied
// into the front buffer between scan-out passes.
constexpr int kStripLines = BUDDY_STRIP_LINES;
__attribute__((aligned(4))) uint16_t strip[st7701::kWidth * kStripLines];

// LVGL's heap lives in PSRAM.
uint8_t __uninitialized_psram("lvgl") lv_pool[BUDDY_LV_POOL_BYTES];

// The SDK's default main stack is 2 KB in scratch RAM; LVGL needs far more.
constexpr size_t kAppStackWords = BUDDY_APP_STACK_BYTES / 4;
uint32_t app_stack[kAppStackWords];

void core1_main() {
    st7701::init(front_buffer);
    multicore_fifo_push_blocking(0);
    for (;;) __wfi();  // everything here runs from the display interrupts
}

void flush_cb(lv_display_t* disp, const lv_area_t* area, uint8_t* px) {
    const uint32_t t0 = time_us_32();
    st7701::blit((const uint16_t*)px, area->x1, area->y1, lv_area_get_width(area), lv_area_get_height(area));
    board::note_flush_time(time_us_32() - t0);
    lv_display_flush_ready(disp);
}

void touch_cb(lv_indev_t*, lv_indev_data_t* data) {
    const board::Touch t = board::read_touch();
    data->point.x = t.x;
    data->point.y = t.y;
    data->state = t.down ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

void lv_log_cb(lv_log_level_t, const char* buf) { fputs(buf, stdout); }

[[noreturn]] void app_main() {
    stdio_init_all();

    for (auto& p : front_buffer) p = 0;
    multicore_launch_core1(core1_main);
    multicore_fifo_pop_blocking();
    board::init();

    lv_init();
    lv_tick_set_cb(hal::millis);
    lv_log_register_print_cb(lv_log_cb);

    lv_display_t* disp = lv_display_create(st7701::kWidth, st7701::kHeight);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(disp, strip, nullptr, sizeof(strip), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(disp, flush_cb);

    lv_indev_t* indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, touch_cb);

    app_init();
    lv_timer_handler();  // paint the first frame before the backlight comes on
    st7701::set_backlight(200);

    net::init();

    for (;;) {
        net::poll();
        app_tick();
        const uint32_t wait = lv_timer_handler();
        if (wait > 1) sleep_us(500);
    }
}

__attribute__((naked, noreturn)) void switch_stack(void (*)(), uint32_t* /*top*/, uint32_t* /*limit*/) {
    __asm__("msr msplim, r2\n"
            "mov sp, r1\n"
            "bx r0\n");
}

}  // namespace

extern "C" void* buddy_lv_pool_alloc(size_t) { return lv_pool; }

int main() { switch_stack(app_main, app_stack + kAppStackWords, app_stack); }
