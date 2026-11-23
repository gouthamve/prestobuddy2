#!/usr/bin/env python3
"""Tile simulator frames into contact sheets: sheet.py frames out_prefix [per_sheet]"""
import glob, sys
from PIL import Image

src, out = sys.argv[1], sys.argv[2]
per = int(sys.argv[3]) if len(sys.argv) > 3 else 3
files = sorted(glob.glob(f"{src}/frame_*.png"))
for n, i in enumerate(range(0, len(files), per)):
    ims = [Image.open(f) for f in files[i:i + per]]
    sheet = Image.new("RGB", (sum(im.width for im in ims), ims[0].height))
    x = 0
    for im in ims:
        sheet.paste(im, (x, 0))
        x += im.width
    sheet.save(f"{out}_{n}.png")
    print(f"{out}_{n}.png")
