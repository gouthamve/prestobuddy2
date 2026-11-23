cmake_minimum_required(VERSION 3.20)

# BUDDY_TARGET=presto builds the firmware (inside the Docker toolchain image);
# BUDDY_TARGET=sim builds the desktop simulator.
set(BUDDY_TARGET "presto" CACHE STRING "presto or sim")

if(BUDDY_TARGET STREQUAL "presto")
    set(PICO_BOARD pimoroni_presto)
    set(PICO_SDK_PATH ${CMAKE_CURRENT_LIST_DIR}/third_party/pico-sdk)
    include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)
endif()

project(prestobuddy2 C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# ---- LVGL (built from the pinned submodule with our lv_conf.h) ----
set(LVGL_DIR ${CMAKE_CURRENT_LIST_DIR}/third_party/lvgl)
file(GLOB_RECURSE LVGL_SOURCES CONFIGURE_DEPENDS ${LVGL_DIR}/src/*.c)
add_library(lvgl STATIC ${LVGL_SOURCES})
target_include_directories(lvgl PUBLIC ${LVGL_DIR} ${CMAKE_CURRENT_LIST_DIR}/core)
target_compile_definitions(lvgl PUBLIC LV_CONF_INCLUDE_SIMPLE=1)
target_compile_options(lvgl PRIVATE -O2 -w)

# ---- Portable app ----
file(GLOB_RECURSE CORE_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_LIST_DIR}/core/*.cpp ${CMAKE_CURRENT_LIST_DIR}/core/*.c)
add_library(buddy_core STATIC ${CORE_SOURCES} ${CMAKE_CURRENT_LIST_DIR}/third_party/cjson/cJSON.c)
target_include_directories(buddy_core PUBLIC ${CMAKE_CURRENT_LIST_DIR}/core ${CMAKE_CURRENT_LIST_DIR}/third_party/cjson)
target_link_libraries(buddy_core PUBLIC lvgl)
target_compile_options(buddy_core PRIVATE -Wall -Wextra -O2 -Wno-deprecated-declarations)

if(BUDDY_TARGET STREQUAL "presto")
    pico_sdk_init()

    # SRAM budget (512 KB): the 460.8 KB front buffer leaves about 51 KB.
    set(BUDDY_DEFS
        BUDDY_LV_POOL_BYTES=3145728      # LVGL heap, in PSRAM
        BUDDY_STRIP_LINES=24             # LVGL render strip, in SRAM (9.6 KB)
        BUDDY_APP_STACK_BYTES=12288      # main-loop stack, in SRAM
        # Same clocking as Pimoroni's firmware: 200 MHz, slower WiFi SPI clock.
        SYS_CLK_HZ=200000000
        PLL_SYS_VCO_FREQ_HZ=1200000000
        PLL_SYS_POSTDIV1=6
        PLL_SYS_POSTDIV2=1
        CYW43_PIO_CLOCK_DIV_INT=3
        PICO_CORE1_STACK_SIZE=0x800
    )
    target_compile_definitions(lvgl PUBLIC ${BUDDY_DEFS})
    target_link_libraries(lvgl PUBLIC pico_stdlib)

    add_executable(prestobuddy
        platform/presto/main.cpp
        platform/presto/board.cpp
        platform/presto/net.cpp
        platform/presto/st7701.cpp
    )
    pico_generate_pio_header(prestobuddy ${CMAKE_CURRENT_LIST_DIR}/platform/presto/st7701_parallel.pio)
    pico_generate_pio_header(prestobuddy ${CMAKE_CURRENT_LIST_DIR}/platform/presto/st7701_timing.pio)
    pico_generate_pio_header(prestobuddy ${CMAKE_CURRENT_LIST_DIR}/platform/presto/ws2812.pio)
    target_include_directories(prestobuddy PRIVATE platform/presto)
    target_compile_definitions(prestobuddy PRIVATE ${BUDDY_DEFS})
    target_compile_options(prestobuddy PRIVATE -Wall -Wextra -O2)
    target_link_libraries(prestobuddy PRIVATE
        buddy_core lvgl
        pico_stdlib pico_multicore
        hardware_pio hardware_dma hardware_pwm hardware_spi hardware_i2c hardware_psram
        pico_cyw43_arch_lwip_poll
    )
    pico_enable_stdio_usb(prestobuddy 1)
    pico_enable_stdio_uart(prestobuddy 0)
    pico_set_program_name(prestobuddy "prestobuddy2")
    pico_add_extra_outputs(prestobuddy)
endif()

if(BUDDY_TARGET STREQUAL "sim")
    option(BUDDY_SIM_SDL "Build the interactive SDL window (host only)" OFF)
    add_executable(prestobuddy-sim platform/sim/main.cpp)
    target_link_libraries(prestobuddy-sim PRIVATE buddy_core lvgl m)
    if(BUDDY_SIM_SDL)
        # Fetched and built inside the build folder; nothing is installed on the host.
        include(FetchContent)
        set(SDL_SHARED OFF CACHE BOOL "" FORCE)
        set(SDL_STATIC ON CACHE BOOL "" FORCE)
        set(SDL_TEST OFF CACHE BOOL "" FORCE)
        # renovate: datasource=github-releases depName=libsdl-org/SDL extractVersion=^release-(?<version>2\..+)$
        set(BUDDY_SDL_VERSION 2.32.10)
        FetchContent_Declare(SDL2 URL https://github.com/libsdl-org/SDL/releases/download/release-${BUDDY_SDL_VERSION}/SDL2-${BUDDY_SDL_VERSION}.tar.gz)
        FetchContent_MakeAvailable(SDL2)
        target_compile_definitions(lvgl PUBLIC BUDDY_SIM_SDL=1)
        target_link_libraries(lvgl PUBLIC SDL2::SDL2-static)
        target_link_libraries(prestobuddy-sim PRIVATE SDL2::SDL2main)
    endif()
endif()
