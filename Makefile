# All cross-builds run inside the toolchain image; see docker/Dockerfile.
IMAGE := prestobuddy2-build
DOCKER := docker run --rm -v "$(CURDIR)":/work -w /work $(IMAGE)

.PHONY: image firmware flash clean shell sim-headless frames sim assets server run test

image:
	docker build -t $(IMAGE) docker

firmware:
	$(DOCKER) sh -c 'cmake -S . -B build/presto -G Ninja -DBUDDY_TARGET=presto >/dev/null && cmake --build build/presto'

flash: firmware
	tools/flash.sh build/presto/prestobuddy.uf2

# Regenerate core/gen/ (mascot sprites, icons, fonts).
assets:
	$(DOCKER) python3 tools/assetc.py

# Headless simulator, built and run in Docker. FRAMES/EVERY/SCRIPT are optional.
FRAMES ?= 300
EVERY ?= 30
SCRIPT ?=
sim-headless:
	$(DOCKER) sh -c 'cmake -S . -B build/sim-docker -G Ninja -DBUDDY_TARGET=sim >/dev/null && cmake --build build/sim-docker'

frames: sim-headless
	rm -rf frames
	$(DOCKER) build/sim-docker/prestobuddy-sim --headless --no-net --frames $(FRAMES) --every $(EVERY) --out frames --script "$(SCRIPT)"

# Interactive simulator window, built on the host with the system compiler.
sim:
	cmake -S . -B build/sim-host -DBUDDY_TARGET=sim -DBUDDY_SIM_SDL=ON >/dev/null && cmake --build build/sim-host -j8
	build/sim-host/prestobuddy-sim

# The laptop server (uses the Go already on the host).
server:
	cd server && go build -o ../build/buddyd .

run: server
	build/buddyd

test:
	cd server && go test ./...

shell:
	docker run --rm -it -v "$(CURDIR)":/work -w /work $(IMAGE) bash

clean:
	rm -rf build
