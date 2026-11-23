// Presto peripherals other than the display: touch, LEDs, piezo.
#pragma once
#include <stdint.h>

namespace board {

void init();

struct Touch {
    bool down;
    int16_t x, y;
};
// Cheap to call every loop; only talks I2C when the controller has data.
Touch read_touch();

void note_flush_time(uint32_t us);

}  // namespace board
