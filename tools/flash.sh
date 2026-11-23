#!/usr/bin/env bash
# Flash a UF2 onto the Presto from macOS with no extra tools.
#
# Puts the board into its USB bootloader (1200-baud touch for our firmware,
# `mpremote bootloader` for MicroPython), waits for the RP2350 drive to mount
# and copies the UF2 onto it. If neither works, hold BOOT and tap RESET.
set -euo pipefail

UF2="${1:-build/presto/prestobuddy.uf2}"
VOL="/Volumes/RP2350"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
[ -f "$UF2" ] || { echo "no such file: $UF2" >&2; exit 1; }

if [ ! -d "$VOL" ]; then
    PORT="$(ls /dev/cu.usbmodem* 2>/dev/null | head -1 || true)"
    if [ -n "$PORT" ]; then
        echo "Asking the board on $PORT to enter its bootloader..."
        stty -f "$PORT" 1200 2>/dev/null || true
        sleep 2
        if [ ! -d "$VOL" ] && [ -x "$ROOT/.venv/bin/mpremote" ] && [ -e "$PORT" ]; then
            "$ROOT/.venv/bin/mpremote" connect "$PORT" bootloader >/dev/null 2>&1 || true
        fi
    fi
fi

for _ in $(seq 1 30); do
    [ -d "$VOL" ] && break
    sleep 0.5
done
if [ ! -d "$VOL" ]; then
    echo "The RP2350 drive did not appear. Hold BOOT, tap RESET, release BOOT, then rerun." >&2
    exit 1
fi

echo "Copying $(basename "$UF2") to $VOL..."
# The board reboots as soon as the last block lands, which can make cp report
# an error after a successful write.
cp -X "$UF2" "$VOL/" 2>/dev/null || true
for _ in $(seq 1 20); do
    [ -d "$VOL" ] || { echo "Flashed; the board has rebooted."; exit 0; }
    sleep 0.5
done
echo "The drive is still mounted; the copy may not have completed." >&2
exit 1
