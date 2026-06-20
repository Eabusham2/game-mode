#!/usr/bin/env python3
"""Generate assets/icon.ico for GameMode (no third-party deps).

Draws a green "power" symbol on a dark rounded tile at 16/32/48 px, writes a
multi-image 32bpp ICO. Run:  python3 scripts/make_icon.py
"""
import math
import os
import struct

SIZES = (16, 32, 48)
GREEN = (0x37, 0xd6, 0x7a)   # R, G, B
DARK = (0x1e, 0x22, 0x30)


def pixel(x, y, s):
    """Return (B, G, R, A) for pixel (x, y) of an s*s icon."""
    cx = cy = (s - 1) / 2.0
    dx, dy = x - cx, y - cy
    dist = math.hypot(dx, dy)
    r_out = s * 0.40
    r_in = s * 0.26
    bar_w = max(1.0, s * 0.11)
    corner = s * 0.20

    # rounded-rectangle background mask (transparent outside the rounding)
    inside = True
    if x < corner and y < corner:
        inside = math.hypot(corner - x, corner - y) <= corner
    elif x > s - 1 - corner and y < corner:
        inside = math.hypot(x - (s - 1 - corner), corner - y) <= corner
    elif x < corner and y > s - 1 - corner:
        inside = math.hypot(corner - x, y - (s - 1 - corner)) <= corner
    elif x > s - 1 - corner and y > s - 1 - corner:
        inside = math.hypot(x - (s - 1 - corner), y - (s - 1 - corner)) <= corner
    if not inside:
        return (0, 0, 0, 0)

    # power symbol = ring (with a gap at top) + a vertical bar through the gap
    on_ring = (r_in <= dist <= r_out) and not (dy < 0 and abs(dx) < bar_w)
    on_bar = abs(dx) <= bar_w / 2.0 and (cy - r_out * 1.02) <= y <= cy + 1
    if on_ring or on_bar:
        r, g, b = GREEN
    else:
        r, g, b = DARK
    return (b, g, r, 255)


def image_bytes(s):
    # BITMAPINFOHEADER (height doubled: XOR image + AND mask)
    hdr = struct.pack("<IiiHHIIiiII", 40, s, s * 2, 1, 32, 0, 0, 0, 0, 0, 0)
    xor = bytearray()
    for y in range(s - 1, -1, -1):           # bottom-up
        for x in range(s):
            xor += bytes(pixel(x, y, s))
    # AND mask: all zero (alpha channel drives transparency), rows 4-byte aligned
    and_row = ((s + 31) // 32) * 4
    andmask = bytes(and_row * s)
    return hdr + bytes(xor) + andmask


def main():
    here = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    out = os.path.join(here, "assets", "icon.ico")
    os.makedirs(os.path.dirname(out), exist_ok=True)

    images = [image_bytes(s) for s in SIZES]
    header = struct.pack("<HHH", 0, 1, len(SIZES))
    offset = 6 + 16 * len(SIZES)
    entries = b""
    for s, img in zip(SIZES, images):
        w = 0 if s >= 256 else s
        entries += struct.pack("<BBBBHHII", w, w, 0, 0, 1, 32, len(img), offset)
        offset += len(img)
    with open(out, "wb") as fh:
        fh.write(header + entries + b"".join(images))
    print("wrote", out, "(", os.path.getsize(out), "bytes )")


if __name__ == "__main__":
    main()
