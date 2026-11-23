#!/usr/bin/env python3
"""Print the Presto's USB serial log for N seconds (default 5). Run with .venv/bin/python."""
import glob, sys, time
import serial

secs = float(sys.argv[1]) if len(sys.argv) > 1 else 5
ports = glob.glob("/dev/cu.usbmodem*")
if not ports:
    sys.exit("no /dev/cu.usbmodem* port")
with serial.Serial(ports[0], 115200, timeout=0.2) as s:
    end = time.time() + secs
    while time.time() < end:
        data = s.read(4096)
        if data:
            sys.stdout.write(data.decode("utf-8", "replace"))
            sys.stdout.flush()
