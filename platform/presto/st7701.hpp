// ST7701 480x480 parallel-RGB panel driver for the Pimoroni Presto.
//
// Adapted from Pimoroni's driver (github.com/pimoroni/presto, drivers/st7701,
// MIT licence, mainly by Michael Bell and Phil Howard). The PIO programs,
// timing ISR, DMA chain and panel init sequence are theirs; this copy drops
// the PicoGraphics dependency, palette mode and the 240x240 mode.
#pragma once
#include <stdint.h>

namespace st7701 {

constexpr int kWidth = 480;
constexpr int kHeight = 480;

// Starts scan-out of `framebuffer` (480x480 big-endian RGB565, must be in
// SRAM). Call on the core that should service the display interrupts.
void init(uint16_t* framebuffer);

void set_backlight(uint8_t brightness);
void wait_for_vsync();

// Row currently being fetched by scan-out (0..479), or -1 during blanking.
int scan_row();

// Copies a block of native little-endian RGB565 pixels into the framebuffer,
// byte-swapping on the way, and stays clear of the row being scanned out so
// the block never appears half-updated.
void blit(const uint16_t* src, int x, int y, int w, int h);

}  // namespace st7701
