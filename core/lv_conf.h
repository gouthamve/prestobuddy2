// LVGL configuration shared by the Presto firmware and the simulator.
// Anything not set here takes LVGL's default.
#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_COLOR_DEPTH 16

// LVGL's own allocator on a fixed pool, identical on both platforms.
#define LV_USE_STDLIB_MALLOC LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_STRING LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_SPRINTF LV_STDLIB_BUILTIN
#ifdef BUDDY_LV_POOL_BYTES
// On the Presto the pool is a PSRAM array provided by platform/presto.
#define LV_MEM_SIZE BUDDY_LV_POOL_BYTES
#ifndef __ASSEMBLER__
#include <stddef.h>
#ifdef __cplusplus
extern "C"
#endif
void* buddy_lv_pool_alloc(size_t size);
#endif
#define LV_MEM_POOL_ALLOC buddy_lv_pool_alloc
#else
#define LV_MEM_SIZE (2 * 1024 * 1024)
#endif

#define LV_DEF_REFR_PERIOD 16
#define LV_DPI_DEF 160
#define LV_USE_OS LV_OS_NONE

#define LV_USE_FLOAT 1
#define LV_DRAW_SW_COMPLEX 1
#define LV_USE_DRAW_SW_COMPLEX_GRADIENTS 1
#define LV_GRADIENT_MAX_STOPS 4
#define LV_DRAW_SW_SUPPORT_RGB565_SWAPPED 0
#define LV_DRAW_SW_SUPPORT_RGB888 0
#define LV_DRAW_SW_SUPPORT_XRGB8888 1
#define LV_DRAW_SW_SUPPORT_ARGB8888_PREMULTIPLIED 0
#define LV_DRAW_SW_SUPPORT_L8 0
#define LV_DRAW_SW_SUPPORT_AL88 0
#define LV_DRAW_SW_SUPPORT_I1 0

#define LV_USE_LOG 1
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF 0

#define LV_USE_ASSERT_NULL 1
#define LV_USE_ASSERT_MALLOC 1

#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_DEFAULT &lv_font_montserrat_20

#define LV_USE_SNAPSHOT 1
#define LV_USE_SYSMON 0

#define LV_BUILD_EXAMPLES 0
#define LV_BUILD_DEMOS 0

#ifdef BUDDY_SIM_SDL
#define LV_USE_SDL 1
#define LV_SDL_INCLUDE_PATH <SDL.h>
#define LV_SDL_RENDER_MODE LV_DISPLAY_RENDER_MODE_PARTIAL
#define LV_SDL_DIRECT_EXIT 1
#endif

#endif  // LV_CONF_H
