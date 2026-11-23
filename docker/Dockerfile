# Build toolchain for prestobuddy2: firmware cross-compile, asset pipeline,
# headless simulator and tests. Nothing here is needed on the host.
FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential cmake ninja-build git ca-certificates pkg-config \
        gcc-arm-none-eabi libnewlib-arm-none-eabi libstdc++-arm-none-eabi-newlib \
        python3 python3-pil python3-cairosvg python3-numpy \
        nodejs npm libusb-1.0-0-dev \
    && rm -rf /var/lib/apt/lists/*

# LVGL's font converter (TTF -> LVGL C font with 4-bit anti-aliasing).
# renovate: datasource=npm depName=lv_font_conv
ARG LV_FONT_CONV_VERSION=1.5.3
RUN npm install -g lv_font_conv@${LV_FONT_CONV_VERSION} && npm cache clean --force

# Repos are bind-mounted from macOS with a different owner.
RUN git config --system --add safe.directory '*'

WORKDIR /work

# Font subsetting/instancing for the asset pipeline.
RUN apt-get update && apt-get install -y --no-install-recommends python3-fonttools python3-brotli curl \
    && rm -rf /var/lib/apt/lists/*
