// ST7701 driver for the Pimoroni Presto. See st7701.hpp for provenance:
// the scan-out machinery and panel init are Pimoroni's (MIT).

#include "st7701.hpp"

#include <cstdio>
#include <cstring>

#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/pio.h"
#include "hardware/pwm.h"
#include "hardware/spi.h"
#include "pico/stdlib.h"
#include "pico/sync.h"

#include "st7701_parallel.pio.h"
#include "st7701_timing.pio.h"

namespace st7701 {
namespace {

// Presto wiring (matches boards/pimoroni_presto.h).
constexpr uint kPinD0 = 1;
constexpr uint kPinHsync = 19;
constexpr uint kPinVsync = 20;
constexpr uint kPinDe = 21;
constexpr uint kPinDotClk = 22;
constexpr uint kPinSpiClk = 26;
constexpr uint kPinSpiDat = 27;
constexpr uint kPinSpiCs = 28;
constexpr uint kPinBacklight = 45;
#define LCD_SPI spi1

constexpr uint32_t kSpiBaud = 8'000'000;
constexpr uint32_t kBacklightPwmTop = 6200;

constexpr int kTimingVPulse = 8;
constexpr int kTimingVBack = 5 + kTimingVPulse;
constexpr int kTimingVDisplay = kHeight + kTimingVBack;
constexpr int kTimingVFront = 5 + kTimingVDisplay;
constexpr int kTimingHFront = 4;
constexpr int kTimingHPulse = 16;
constexpr int kTimingHBack = 30;
constexpr int kTimingHDisplay = 480;

PIO st_pio = pio1;
uint parallel_sm, timing_sm;
uint parallel_offset, timing_offset;
uint st_dma, st_dma2;

uint16_t timing_row = 0;
uint16_t timing_phase = 0;
volatile bool waiting_for_vsync = false;

uint16_t* framebuffer = nullptr;
uint16_t* volatile next_line_addr = nullptr;
volatile int display_row = kHeight;  // kHeight == in vertical blanking

// Triggered whenever the timing SM's FIFO is not full.
void __isr __no_inline_not_in_flash_func(timing_isr)() {
    while (!pio_sm_is_tx_fifo_full(st_pio, timing_sm)) {
        uint32_t instr;
        switch (timing_phase) {
            case 0:  // Front porch
                instr = 0x4000B042u;  // HSYNC high, NOP
                if (timing_row >= kTimingVPulse) instr |= 0x80000000u;
                instr |= (kTimingHFront - 3) << 16;
                break;
            case 1:  // HSYNC
                instr = 0x0000B042u;  // HSYNC low, NOP
                if (timing_row >= kTimingVPulse) instr |= 0x80000000u;
                instr |= (kTimingHPulse - 3) << 16;
                break;
            case 2:  // Back porch, trigger the pixel SM inside the display window
                instr = 0x40000000u;
                if (timing_row >= kTimingVPulse) instr |= 0x80000000u;
                if (timing_row >= kTimingVBack && timing_row < kTimingVDisplay) instr |= 0xD004u;  // irq 4
                else instr |= 0xB042u;
                instr |= (kTimingHBack - 3) << 16;
                break;
            default:  // Display; queue the next line or the next frame
                instr = 0x40000000u;
                if (timing_row == kTimingVDisplay) instr |= 0xD001u;  // irq 1: new frame
                else if (timing_row >= kTimingVBack - 1 && timing_row < kTimingVDisplay) instr |= 0xD000u;  // irq 0: new line
                else instr |= 0xB042u;
                if (timing_row >= kTimingVPulse) instr |= 0x80000000u;
                instr |= (kTimingHDisplay - 3) << 16;
                if (++timing_row >= kTimingVFront) timing_row = 0;
                break;
        }
        pio_sm_put(st_pio, timing_sm, instr);
        timing_phase = (timing_phase + 1) & 3;
    }
}

inline void __not_in_flash_func(start_line_xfer)() {
    hw_clear_bits(&st_pio->irq, 0x1);
    int row = display_row + 1;
    display_row = row;
    if (row >= kHeight) next_line_addr = nullptr;
    else next_line_addr = &framebuffer[kWidth * row];
}

inline void __not_in_flash_func(start_frame_xfer)() {
    hw_clear_bits(&st_pio->irq, 0x2);

    next_line_addr = nullptr;
    dma_channel_abort(st_dma);
    dma_channel_wait_for_finish_blocking(st_dma);
    pio_sm_set_enabled(st_pio, parallel_sm, false);
    pio_sm_clear_fifos(st_pio, parallel_sm);
    // pio_sm_restart empties OSR/ISR without a stallable exec (see Pimoroni's
    // note in the original driver); X/Y survive so the width counter is kept.
    pio_sm_restart(st_pio, parallel_sm);
    pio_sm_exec(st_pio, parallel_sm, pio_encode_jmp(parallel_offset));
    pio_sm_set_enabled(st_pio, parallel_sm, true);
    display_row = 0;
    next_line_addr = framebuffer;
    dma_channel_set_read_addr(st_dma, framebuffer, true);

    waiting_for_vsync = false;
    __sev();
}

// Triggered at the end of each line transferred.
void __isr __no_inline_not_in_flash_func(end_of_line_isr)() {
    if (st_pio->irq & 0x2) start_frame_xfer();
    else start_line_xfer();
}

void command(uint8_t cmd, size_t len = 0, const char* data = nullptr) {
    static uint16_t buf[20];
    gpio_put(kPinSpiCs, 0);
    uint16_t c = cmd;  // 9th bit low = command
    spi_write16_blocking(LCD_SPI, &c, 1);
    if (data) {
        for (size_t i = 0; i < len; i++) buf[i] = 0x100 | (uint8_t)data[i];  // 9th bit high = data
        spi_write16_blocking(LCD_SPI, buf, len);
    }
    gpio_put(kPinSpiCs, 1);
}

void bank(uint8_t sel) {
    const char v[5] = {0x77, 0x01, 0x00, 0x00, (char)sel};
    command(0xFF, 5, v);
}

void panel_init() {
    pwm_config cfg = pwm_get_default_config();
    pwm_config_set_wrap(&cfg, kBacklightPwmTop);
    pwm_init(pwm_gpio_to_slice_num(kPinBacklight), &cfg, true);
    gpio_set_function(kPinBacklight, GPIO_FUNC_PWM);
    set_backlight(0);

    command(0x01);  // SWRESET
    sleep_ms(150);

    bank(0x10);  // Command2 BK0
    command(0xC0, 2, "\x3b\x00");  // LNESET: (59 + 1) * 8 = 480 lines
    command(0xC1, 2, "\x0d\x02");  // PORCTRL
    command(0xC2, 2, "\x31\x01");  // INVSET
    command(0xCD, 1, "\x08");      // COLCTRL
    command(0xB0, 16, "\x00\x11\x18\x0e\x11\x06\x07\x08\x07\x22\x04\x12\x0f\xaa\x31\x18");
    command(0xB1, 16, "\x00\x11\x19\x0e\x12\x07\x08\x08\x08\x22\x04\x11\x11\xa9\x32\x18");
    command(0xC3, 3, "\x80\x2e\x0e");  // RGBCTRL: HV mode

    bank(0x11);  // Command2 BK1: voltages
    command(0xB0, 1, "\x60");
    command(0xB1, 1, "\x32");
    command(0xB2, 1, "\x07");
    command(0xB3, 1, "\x80");
    command(0xB5, 1, "\x49");
    command(0xB7, 1, "\x85");
    command(0xB8, 1, "\x21");
    command(0xC1, 1, "\x78");
    command(0xC2, 1, "\x78");

    // Panel-specific sequence (undocumented; the display does not work without it).
    command(0xE0, 3, "\x00\x1b\x02");
    command(0xE1, 11, "\x08\xa0\x00\x00\x07\xa0\x00\x00\x00\x44\x44");
    command(0xE2, 12, "\x11\x11\x44\x44\xed\xa0\x00\x00\xec\xa0\x00\x00");
    command(0xE3, 4, "\x00\x00\x11\x11");
    command(0xE4, 2, "\x44\x44");
    command(0xE5, 16, "\x0a\xe9\xd8\xa0\x0c\xeb\xd8\xa0\x0e\xed\xd8\xa0\x10\xef\xd8\xa0");
    command(0xE6, 4, "\x00\x00\x11\x11");
    command(0xE7, 2, "\x44\x44");
    command(0xE8, 16, "\x09\xe8\xd8\xa0\x0b\xea\xd8\xa0\x0d\xec\xd8\xa0\x0f\xee\xd8\xa0");
    command(0xEB, 7, "\x02\x00\xe4\xe4\x88\x00\x40");
    command(0xEC, 2, "\x3c\x00");
    command(0xED, 16, "\xab\x89\x76\x54\x02\xff\xff\xff\xff\xff\xff\x20\x45\x67\x98\xba");
    command(0x36, 1, "\x00");

    bank(0x13);  // Command2 BK3
    command(0xE5, 1, "\xe4");

    bank(0x00);  // Disable Command2
    command(0x3A, 1, "\x66");  // COLMOD: 18 bits per pixel

    command(0x36, 1, "\x00");  // MADCTL: no rotation
    bank(0x10);
    command(0xC7, 1, "\x00");  // SDIR
    bank(0x00);

    command(0x21);  // INVON
    sleep_ms(1);
    command(0x11);  // SLPOUT
    sleep_ms(120);
    command(0x29);  // DISPON
    sleep_ms(100);
}

}  // namespace

void init(uint16_t* fb) {
    framebuffer = fb;

    parallel_sm = pio_claim_unused_sm(st_pio, true);
    timing_sm = pio_claim_unused_sm(st_pio, true);
    timing_offset = pio_add_program(st_pio, &st7701_timing_program);  // has .origin 0
    parallel_offset = pio_add_program(st_pio, &st7701_parallel_program);

    spi_init(LCD_SPI, kSpiBaud);
    gpio_init(kPinSpiCs);
    gpio_set_dir(kPinSpiCs, GPIO_OUT);
    gpio_put(kPinSpiCs, 1);
    gpio_set_function(kPinSpiDat, GPIO_FUNC_SPI);
    gpio_set_function(kPinSpiClk, GPIO_FUNC_SPI);
    // 3-line serial interface: 9-bit frames, 9th bit is D/CX.
    spi_set_format(LCD_SPI, 9, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);

    pio_gpio_init(st_pio, kPinHsync);
    pio_gpio_init(st_pio, kPinVsync);
    pio_gpio_init(st_pio, kPinDe);
    pio_gpio_init(st_pio, kPinDotClk);
    for (uint i = 0; i < 16; i++) pio_gpio_init(st_pio, kPinD0 + i);
    // The panel is wired for 18 bits; the two extra lines are held low.
    for (uint i = 16; i < 18; i++) {
        gpio_init(kPinD0 + i);
        gpio_set_dir(kPinD0 + i, GPIO_OUT);
        gpio_put(kPinD0 + i, false);
    }

    pio_sm_set_consecutive_pindirs(st_pio, parallel_sm, kPinD0, 16, true);
    pio_sm_set_consecutive_pindirs(st_pio, parallel_sm, kPinHsync, 4, true);

    pio_sm_config c = st7701_parallel_program_get_default_config(parallel_offset);
    sm_config_set_out_pins(&c, kPinD0, 16);
    sm_config_set_sideset_pins(&c, kPinDe);
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);
    sm_config_set_out_shift(&c, true, true, 32);
    sm_config_set_in_shift(&c, false, false, 32);

    const uint32_t max_pio_clk = 34 * MHZ;
    const uint32_t sys_clk_hz = clock_get_hz(clk_sys);
    uint32_t clk_div = (sys_clk_hz + max_pio_clk - 1) / max_pio_clk;
    // The pixel SM runs at double the rate of the timing SM at full resolution.
    if (clk_div & 1) clk_div += 1;
    sm_config_set_clkdiv(&c, clk_div >> 1);

    pio_sm_init(st_pio, parallel_sm, parallel_offset, &c);
    pio_sm_exec(st_pio, parallel_sm, pio_encode_out(pio_y, 32));
    pio_sm_put(st_pio, parallel_sm, (kWidth >> 1) - 1);
    pio_sm_set_enabled(st_pio, parallel_sm, true);

    c = st7701_timing_program_get_default_config(timing_offset);
    sm_config_set_out_pins(&c, kPinHsync, 2);
    sm_config_set_sideset_pins(&c, kPinDotClk);
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);
    sm_config_set_out_shift(&c, false, true, 32);
    sm_config_set_clkdiv(&c, clk_div);
    pio_sm_init(st_pio, timing_sm, timing_offset, &c);
    pio_sm_set_enabled(st_pio, timing_sm, true);

    st_dma = dma_claim_unused_channel(true);
    st_dma2 = dma_claim_unused_channel(true);

    // Channel 1 streams one line of pixel pairs to the PIO; channel 2 then
    // reloads channel 1's read address from next_line_addr and retriggers it.
    dma_channel_config config = dma_channel_get_default_config(st_dma);
    channel_config_set_transfer_data_size(&config, DMA_SIZE_32);
    channel_config_set_dreq(&config, pio_get_dreq(st_pio, parallel_sm, true));
    channel_config_set_bswap(&config, true);
    channel_config_set_chain_to(&config, st_dma2);
    dma_channel_configure(st_dma, &config, &st_pio->txf[parallel_sm], nullptr, kWidth >> 1, false);

    config = dma_channel_get_default_config(st_dma2);
    channel_config_set_transfer_data_size(&config, DMA_SIZE_32);
    channel_config_set_read_increment(&config, false);
    dma_channel_configure(st_dma2, &config, &dma_hw->ch[st_dma].al3_read_addr_trig, &next_line_addr, 1, false);

    panel_init();

    hw_set_bits(&st_pio->inte1, 0x010 << timing_sm);  // TX not full
    irq_set_exclusive_handler(pio_get_irq_num(st_pio, 1), timing_isr);
    irq_set_priority(pio_get_irq_num(st_pio, 1), 0x40);
    irq_set_enabled(pio_get_irq_num(st_pio, 1), true);

    hw_set_bits(&st_pio->inte0, 0x300);  // PIO IRQ flags 0 and 1
    irq_set_exclusive_handler(pio_get_irq_num(st_pio, 0), end_of_line_isr);
    irq_set_priority(pio_get_irq_num(st_pio, 0), 0x40);
    irq_set_enabled(pio_get_irq_num(st_pio, 0), true);
}

void set_backlight(uint8_t brightness) {
    uint16_t value;
    if (brightness == 0) value = 0;
    else if (brightness == 255) value = kBacklightPwmTop;
    else value = 181 + (brightness * brightness) / 85;
    pwm_set_gpio_level(kPinBacklight, value);
}

void __no_inline_not_in_flash_func(wait_for_vsync)() {
    waiting_for_vsync = true;
    while (waiting_for_vsync) __wfe();
}

int scan_row() {
    int row = display_row;
    return row >= kHeight ? -1 : row;
}

void __not_in_flash_func(blit)(const uint16_t* src, int x, int y, int w, int h) {
    // Scan-out reads row r while display_row == r (the DMA for row r+1 is
    // queued a line ahead). Wait until the beam is clear of this block; a
    // strip copies in well under the time the beam takes to return.
    const int guard = 3;
    for (;;) {
        int row = display_row;
        if (row >= kHeight || row + guard < y || row >= y + h) break;
        tight_loop_contents();
    }
    for (int r = 0; r < h; r++) {
        uint16_t* dst = &framebuffer[(y + r) * kWidth + x];
        const uint16_t* s = src + r * w;
        for (int i = 0; i < w; i++) dst[i] = __builtin_bswap16(s[i]);
    }
}

}  // namespace st7701
