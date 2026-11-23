"""The mascot's artwork, as code.

Garder is an original little royal guardsman: bearskin hat, round face,
red tunic with white cross-belts, light blue trousers. Every body part is
drawn on a shared 240x360 canvas (so they line up), then the asset compiler
crops each part to its own sprite and records where it sits.

parts() returns [(name, svg_fragment, pivot_xy)], back to front.
"""
import math

W, H = 240, 360

SKIN = "#FFD8B5"
SKIN_SHADE = "#F2BE96"
CHEEK = "#FF8FA0"
HAT = "#231F2B"
HAT_LIGHT = "#4A4358"
TUNIC = "#D93A45"
TUNIC_SHADE = "#B62B3A"
BELT = "#FFFFFF"
BELT_SHADE = "#DCE3EE"
GOLD = "#FFC94A"
TROUSER = "#8CC6F2"
TROUSER_SHADE = "#6FAEE0"
BOOT = "#25222E"
INK = "#2A2533"


def _fuzzy_hat():
    # Tall bearskin: a rounded dome whose outline is broken up with tufts.
    cx, top, bottom = 120, 20, 142
    rx_top, rx_bot = 62, 56
    body = (f'<path d="M {cx-rx_bot} {bottom} '
            f'C {cx-rx_bot-8} {bottom-50}, {cx-rx_top-4} {top+34}, {cx-rx_top+14} {top+16} '
            f'C {cx-30} {top-8}, {cx+30} {top-8}, {cx+rx_top-14} {top+16} '
            f'C {cx+rx_top+4} {top+34}, {cx+rx_bot+8} {bottom-50}, {cx+rx_bot} {bottom} '
            f'Q {cx} {bottom+10} {cx-rx_bot} {bottom} Z" fill="url(#hatg)"/>')
    tufts = []
    n = 34
    for i in range(n):
        t = math.pi * (i + 0.5) / n  # left side, over the top, right side
        x = cx - math.cos(t) * 61
        y = 84 - math.sin(t) * 66 + (1 - math.sin(t)) * 34
        r = 7.5 + 2.0 * math.sin(i * 2.3)
        tufts.append(f'<circle cx="{x:.1f}" cy="{y:.1f}" r="{r:.1f}" fill="{HAT}"/>')
    # Soft highlight strokes that read as fur.
    fur = []
    for i, (x, y, l) in enumerate([(92, 44, 26), (104, 34, 30), (84, 70, 22), (98, 78, 18), (112, 58, 16)]):
        fur.append(f'<path d="M {x} {y} q 4 {l/2} 1 {l}" stroke="{HAT_LIGHT}" stroke-width="4" '
                   f'stroke-linecap="round" fill="none" opacity="0.5"/>')
    brim = (f'<path d="M {cx-rx_bot+2} {bottom-4} Q {cx} {bottom+12} {cx+rx_bot-2} {bottom-4}" '
            f'stroke="#15121B" stroke-width="5" fill="none" stroke-linecap="round" opacity="0.6"/>')
    defs = (f'<defs><linearGradient id="hatg" x1="0" y1="0" x2="1" y2="1">'
            f'<stop offset="0" stop-color="#3A3447"/><stop offset="0.55" stop-color="{HAT}"/>'
            f'<stop offset="1" stop-color="#17141D"/></linearGradient></defs>')
    # Rim light along the upper left so the dark hat reads on dark backgrounds.
    rim = (f'<path d="M {cx-rx_bot-3} {bottom-30} C {cx-rx_bot-9} {bottom-70}, {cx-rx_top-5} {top+36}, {cx-rx_top+12} {top+15} '
           f'C {cx-34} {top-6}, {cx+10} {top-9}, {cx+34} {top+2}" stroke="#8C84A6" stroke-width="3.5" '
           f'fill="none" stroke-linecap="round" opacity="0.7"/>')
    return defs + "".join(tufts) + body + "".join(fur) + brim + rim


def _head():
    return (
        # ears
        f'<circle cx="67" cy="166" r="11" fill="{SKIN}"/><circle cx="173" cy="166" r="11" fill="{SKIN}"/>'
        f'<circle cx="67" cy="166" r="5" fill="{SKIN_SHADE}"/><circle cx="173" cy="166" r="5" fill="{SKIN_SHADE}"/>'
        # face
        f'<ellipse cx="120" cy="160" rx="55" ry="52" fill="{SKIN}"/>'
        # chin strap, the way a bearskin is worn
        f'<path d="M 70 150 Q 78 205 120 209 Q 162 205 170 150" stroke="{GOLD}" stroke-width="5" '
        f'fill="none" stroke-linecap="round"/>'
        f'<ellipse cx="87" cy="180" rx="12" ry="8" fill="{CHEEK}" opacity="0.55"/>'
        f'<ellipse cx="153" cy="180" rx="12" ry="8" fill="{CHEEK}" opacity="0.55"/>'
    )


def _eyes(kind):
    lx, rx, y = 99, 141, 166
    if kind == "open":
        e = lambda x: (f'<ellipse cx="{x}" cy="{y}" rx="8" ry="11" fill="{INK}"/>'
                       f'<circle cx="{x+3}" cy="{y-4}" r="3.2" fill="#FFFFFF"/>')
    elif kind == "wide":
        e = lambda x: (f'<ellipse cx="{x}" cy="{y}" rx="10" ry="13" fill="{INK}"/>'
                       f'<circle cx="{x+3.5}" cy="{y-5}" r="4" fill="#FFFFFF"/>'
                       f'<circle cx="{x-3}" cy="{y+5}" r="1.8" fill="#FFFFFF"/>')
    elif kind == "half":
        e = lambda x: (f'<path d="M {x-8} {y} A 8 9 0 0 0 {x+8} {y} Z" fill="{INK}"/>'
                       f'<path d="M {x-10} {y} L {x+10} {y}" stroke="{INK}" stroke-width="4" stroke-linecap="round"/>')
    elif kind == "closed":
        e = lambda x: (f'<path d="M {x-9} {y} Q {x} {y+8} {x+9} {y}" stroke="{INK}" stroke-width="4.5" '
                       f'fill="none" stroke-linecap="round"/>')
    elif kind == "happy":
        e = lambda x: (f'<path d="M {x-9} {y+3} Q {x} {y-10} {x+9} {y+3}" stroke="{INK}" stroke-width="4.5" '
                       f'fill="none" stroke-linecap="round"/>')
    else:
        raise ValueError(kind)
    return e(lx) + e(rx)


def _mouth(kind):
    x, y = 120, 188
    if kind == "smile":
        return (f'<path d="M {x-9} {y} Q {x} {y+9} {x+9} {y}" stroke="{INK}" stroke-width="4" fill="none" '
                f'stroke-linecap="round"/>')
    if kind == "grin":
        return (f'<path d="M {x-12} {y-2} Q {x} {y+18} {x+12} {y-2} Z" fill="{INK}"/>'
                f'<path d="M {x-6} {y+7} Q {x} {y+12} {x+6} {y+7} Q {x} {y+4} {x-6} {y+7} Z" fill="#FF7C8E"/>')
    if kind == "o":
        return f'<ellipse cx="{x}" cy="{y+3}" rx="6" ry="7.5" fill="{INK}"/>'
    if kind == "flat":
        return f'<path d="M {x-7} {y+3} L {x+7} {y+3}" stroke="{INK}" stroke-width="4" stroke-linecap="round"/>'
    raise ValueError(kind)


def _body():
    return (
        f'<defs><linearGradient id="tun" x1="0" y1="0" x2="1" y2="0">'
        f'<stop offset="0" stop-color="{TUNIC}"/><stop offset="1" stop-color="{TUNIC_SHADE}"/></linearGradient>'
        f'<clipPath id="tclip"><path d="M 80 222 Q 80 198 104 198 L 136 198 Q 160 198 160 222 L 164 276 '
        f'Q 164 286 154 286 L 86 286 Q 76 286 76 276 Z"/></clipPath></defs>'
        f'<path d="M 80 222 Q 80 198 104 198 L 136 198 Q 160 198 160 222 L 164 276 Q 164 286 154 286 '
        f'L 86 286 Q 76 286 76 276 Z" fill="url(#tun)"/>'
        f'<g clip-path="url(#tclip)">'
        # white cross-belts
        f'<path d="M 82 196 L 162 276" stroke="{BELT}" stroke-width="13"/>'
        f'<path d="M 158 196 L 78 276" stroke="{BELT_SHADE}" stroke-width="13"/>'
        # waist belt
        f'<rect x="70" y="262" width="100" height="13" fill="{BELT}"/>'
        f'</g>'
        f'<rect x="111" y="259" width="18" height="19" rx="4" fill="{GOLD}"/>'
        f'<rect x="116" y="264" width="8" height="9" rx="2" fill="#E0A21F"/>'
        f'<circle cx="120" cy="236" r="6.5" fill="{GOLD}"/>'
        # collar
        f'<path d="M 100 198 Q 120 212 140 198 L 140 204 Q 120 220 100 204 Z" fill="#2B3A67"/>'
    )


def _arm(side):
    # Drawn hanging straight down from the shoulder; rotated at runtime.
    x = 70 if side == "l" else 170
    return (
        f'<rect x="{x-12}" y="204" width="24" height="60" rx="12" fill="{TUNIC if side == "l" else TUNIC_SHADE}"/>'
        f'<rect x="{x-12}" y="246" width="24" height="9" fill="#2B3A67"/>'
        f'<circle cx="{x}" cy="268" r="13" fill="#FFFFFF"/>'
        f'<circle cx="{x}" cy="268" r="13" fill="none" stroke="{BELT_SHADE}" stroke-width="2"/>'
    )


def _leg(side):
    x = 102 if side == "l" else 138
    stripe_x = x - 13 if side == "l" else x + 8
    toe = -6 if side == "l" else 6
    return (
        f'<rect x="{x-15}" y="272" width="30" height="62" rx="10" fill="{TROUSER if side == "l" else TROUSER_SHADE}"/>'
        f'<rect x="{stripe_x}" y="278" width="5" height="50" rx="2" fill="#FFFFFF"/>'
        f'<path d="M {x-17+min(toe,0)} 334 Q {x-17+min(toe,0)} 322 {x-10} 322 L {x+10} 322 '
        f'Q {x+17+max(toe,0)} 322 {x+17+max(toe,0)} 334 L {x+17+max(toe,0)} 340 Q {x+17+max(toe,0)} 346 {x+11+max(toe,0)} 346 '
        f'L {x-11+min(toe,0)} 346 Q {x-17+min(toe,0)} 346 {x-17+min(toe,0)} 340 Z" fill="{BOOT}"/>'
    )


def _shadow():
    return '<ellipse cx="120" cy="347" rx="62" ry="9" fill="#000000" opacity="0.28"/>'


def parts():
    out = [
        ("shadow", _shadow(), (120, 347)),
        ("leg_l", _leg("l"), (102, 276)),
        ("leg_r", _leg("r"), (138, 276)),
        ("arm_r", _arm("r"), (170, 210)),
        ("body", _body(), (120, 286)),
        ("arm_l", _arm("l"), (70, 210)),
        ("head", _head(), (120, 208)),
    ]
    for k in ("open", "wide", "half", "closed", "happy"):
        out.append((f"eyes_{k}", _eyes(k), (120, 166)))
    for k in ("smile", "grin", "o", "flat"):
        out.append((f"mouth_{k}", _mouth(k), (120, 190)))
    out.append(("hat", _fuzzy_hat(), (120, 142)))
    return out


def svg(fragment, scale=1.0):
    return (f'<svg xmlns="http://www.w3.org/2000/svg" width="{W*scale:.0f}" height="{H*scale:.0f}" '
            f'viewBox="0 0 {W} {H}">{fragment}</svg>')


if __name__ == "__main__":
    # Preview: the default pose and a sheet of expressions.
    import io, sys
    import cairosvg
    from PIL import Image

    out = sys.argv[1] if len(sys.argv) > 1 else "mascot_preview.png"
    ps = dict((n, f) for n, f, _ in parts())

    def pose(eyes, mouth, bg):
        order = ["shadow", "leg_l", "leg_r", "arm_r", "body", "arm_l", "head", f"eyes_{eyes}", f"mouth_{mouth}", "hat"]
        frag = "".join(f"<g>{ps[n]}</g>" for n in order)
        png = cairosvg.svg2png(bytestring=svg(frag, 1.0).encode())
        im = Image.open(io.BytesIO(png)).convert("RGBA")
        base = Image.new("RGBA", im.size, bg)
        base.alpha_composite(im)
        return base

    combos = [("open", "smile"), ("happy", "grin"), ("wide", "o"), ("half", "flat"), ("closed", "smile")]
    sheet = Image.new("RGBA", (W * len(combos), H), "#10162B")
    for i, (e, m) in enumerate(combos):
        sheet.paste(pose(e, m, "#10162B"), (i * W, 0))
    sheet.save(out)
    print("wrote", out)
