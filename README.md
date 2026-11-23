# prestobuddy2

A desk companion for the Pimoroni Presto: a little guardsman called Garder who keeps
your goals for the day, runs your pomodoro timer, watches your coding agents and
breathes with you.

```
 Browser (localhost:8787) ──HTTP/SSE──┐
 orca agent list --json ──poll 2s──► buddyd (Go, laptop) ◄──TCP, JSON lines──► Presto firmware (C++, LVGL)
                                           └── UDP beacon so the Presto finds the laptop
```

## Daily use

```sh
make run            # builds and starts buddyd, then open http://127.0.0.1:8787
```

Type goals on the page; they appear on the Presto. On the Presto, the dock switches
between Home, Goals, Focus, Agents and Breathe. The timer and breathing sessions keep
working when the laptop is asleep; goals and history live in
`~/Library/Application Support/prestobuddy/state.json`.

Agents come from the Orca CLI (`orca agent list --json`). Enable it in Orca under
Settings > Experimental > CLI. To try alerts without Orca:

```sh
build/buddyd -fake-agents     # the page then shows buttons to set an agent's state
```

## Building

Cross-builds run in Docker, so nothing is installed on the host.

| Command | What it does |
|---|---|
| `make image` | builds the toolchain image (once) |
| `make firmware` | builds `build/presto/prestobuddy.uf2` |
| `make flash` | builds and flashes over USB, no button press needed once our firmware is on |
| `make assets` | regenerates `core/gen/` (mascot sprites, icons, fonts) |
| `make frames` | runs the headless simulator in Docker and dumps PNG frames to `frames/` |
| `make sim` | builds and opens the interactive simulator window on the host |
| `make test` | server tests |

WiFi credentials are compiled in from `platform/presto/secrets.h` (gitignored; see
`secrets.example.h`). `tools/serial_log.py` prints the firmware's USB log.

To go back to MicroPython, hold BOOT, tap RESET and drop Pimoroni's UF2 onto the
`RP2350` drive.

## Layout

- `core/` portable app on LVGL 9: screens, mascot rig, timer, LED and sound effects, protocol.
- `platform/presto/` hardware: display driver (adapted from Pimoroni's, MIT), touch, LEDs, piezo, WiFi.
- `platform/sim/` desktop simulator, headless or SDL.
- `server/` `buddyd`: state, web page, device link, agent tracking.
- `tools/` asset compiler, mascot artwork (as code), flashing and log scripts.
- `third_party/` pico-sdk, LVGL and Pimoroni's presto repo as pinned submodules; cJSON vendored.

## Simulator tricks

```sh
build/sim-hl/prestobuddy-sim --headless --no-net --demo --frames 500 --every 0 \
    --dump-at "2000,4000" --script "2500:tap:144,444" --demo-alert 3000
```

`--demo` loads sample goals and agents, `--script` taps the screen at given times,
`--inject 'ms:{json}'` feeds a protocol line. Frames include the LED glow around the screen.
