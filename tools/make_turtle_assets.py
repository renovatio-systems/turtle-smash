#!/usr/bin/env python3
"""Splits assets/tortoise.png (a finished picture) into game assets:

  src/meadow_image.c   360x240 background: sky, stars, grass, soil - no tortoise.
                       The 320x240 picture is widened to the game canvas by mirroring its
                       right-hand columns, so nothing in it is stretched.
  src/turtle_art.c     tortoise body sprite (cannon tube removed) + horizontal barrel sprite
  src/turtle_art.h     sizes and anchor points

usage: make_turtle_assets.py assets/tortoise.png src/ [scale]
Sprites use SPRITE_KEY (magenta) for transparent pixels. `scale` (default 1.0) shrinks
or grows the body/barrel sprites and their anchors, keeping the feet on the grass and
the left edge in place.
"""
import math
import sys
from collections import deque
from PIL import Image, ImageOps

W, H = 320, 240
MEADOW_W = 360                      # game canvas width (see render.h GAME_W)
KEY = 0xF81F
GRASS_Y = 200                       # grass starts here; feet overlap it
BBOX = (0, 74, 156, 212)            # x0, y0, x1, y1 (exclusive) around the tortoise
PIVOT = (100, 94)               # where the barrel turns, in picture pixels
ANGLE0 = 11                         # barrel angle in the picture, degrees above horizontal
U0, U1, VH = -3, 49, 9              # barrel strip: along-axis range, half thickness

src, outdir = sys.argv[1:3]
SCALE = float(sys.argv[3]) if len(sys.argv) > 3 else 1.0
im = Image.open(src).convert("RGB").resize((W, H), Image.BOX)
px = im.load()


def rgb565(c):
    v = ((c[0] & 0xF8) << 8) | ((c[1] & 0xFC) << 3) | (c[2] >> 3)
    return KEY ^ 1 if v == KEY else v   # a real pixel must never equal the key


def rgb565_to_rgb(v):
    return (((v >> 11) & 0x1F) * 255 // 31, ((v >> 5) & 0x3F) * 255 // 63, (v & 0x1F) * 255 // 31)


def scale_sprite(rows, new_w, new_h):
    """Resizes a KEY-transparent RGB565 sprite, alpha-aware so edges stay clean."""
    h, w = len(rows), len(rows[0])
    img = Image.new("RGBA", (w, h))
    for y in range(h):
        for x in range(w):
            v = rows[y][x]
            img.putpixel((x, y), (0, 0, 0, 0) if v == KEY else rgb565_to_rgb(v) + (255,))
    img = img.resize((new_w, new_h), Image.LANCZOS)
    out = [[KEY] * new_w for _ in range(new_h)]
    for y in range(new_h):
        for x in range(new_w):
            r, g, b, a = img.getpixel((x, y))
            if a >= 128:
                out[y][x] = rgb565((r, g, b))
    return out


def dark(c, limit=40):
    return max(c) < limit


def flood(is_open, w, h):
    """Cells reachable from the border through is_open(x, y)."""
    seen = [[False] * w for _ in range(h)]
    q = deque()
    for x in range(w):
        for y in (0, h - 1):
            q.append((x, y))
    for y in range(h):
        for x in (0, w - 1):
            q.append((x, y))
    while q:
        x, y = q.popleft()
        if not (0 <= x < w and 0 <= y < h) or seen[y][x] or not is_open(x, y):
            continue
        seen[y][x] = True
        q.extend(((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)))
    return seen


def grassy(c):
    return c[1] >= c[0] and c[1] >= c[2] + 15 and c[1] > 70


bx0, by0, bx1, by1 = BBOX
bw, bh = bx1 - bx0, by1 - by0

# Silhouette: sky is dark pixels connected to the box edge above the grass; in the
# grass band a pixel is tortoise when it is not grass-coloured.
sky = flood(lambda x, y: (by0 + y < GRASS_Y and dark(px[bx0 + x, by0 + y])) or
            (by0 + y >= GRASS_Y and grassy(px[bx0 + x, by0 + y])), bw, bh)
solid = [[not sky[y][x] for x in range(bw)] for y in range(bh)]

# Barrel strip in picture coordinates.
th = math.radians(ANGLE0)
ax, ay = math.cos(th), -math.sin(th)    # along the barrel
nx, ny = math.sin(th), math.cos(th)     # across it (downwards)


def uv(x, y):
    dx, dy = x - PIVOT[0], y - PIVOT[1]
    return dx * ax + dy * ay, dx * nx + dy * ny


in_barrel = [[False] * bw for _ in range(bh)]
for y in range(bh):
    for x in range(bw):
        u, v = uv(bx0 + x, by0 + y)
        in_barrel[y][x] = solid[y][x] and U0 <= u <= U1 and abs(v) <= VH

# Body sprite: silhouette minus the barrel strip.
body = [[rgb565(px[bx0 + x, by0 + y]) if solid[y][x] and not in_barrel[y][x] else KEY
         for x in range(bw)] for y in range(bh)]

# Barrel sprite: sample the picture along the axis, so it is horizontal.
bu, bv = U1 - U0 + 1, 2 * VH + 1
barrel_rgb = {}
for j in range(bv):
    for i in range(bu):
        u, v = U0 + i, j - VH
        x = round(PIVOT[0] + u * ax + v * nx)
        y = round(PIVOT[1] + u * ay + v * ny)
        barrel_rgb[i, j] = px[x, y]
barrel_sky = flood(lambda i, j: dark(barrel_rgb[i, j], 30), bu, bv)
barrel = [[KEY if barrel_sky[j][i] else rgb565(barrel_rgb[i, j]) for i in range(bu)] for j in range(bv)]

# Meadow: the picture with the tortoise painted out. Sky above the grass line is
# black; grass under the feet is copied from 160 px to the right (same rows).
mead = im.copy()
mp = mead.load()
for y in range(by0, by1):
    for x in range(bx0, bx1):
        if y < GRASS_Y:
            mp[x, y] = (0, 0, 0)  # the sky is black and no star sits behind the tortoise
        elif solid[y - by0][x - bx0]:
            mp[x, y] = px[x + 160, y]

if SCALE != 1.0:
    # Shrink/grow body + barrel around the feet (by1 stays put) and the left edge
    # (bx0 stays put), so the turtle keeps standing on the grass at the same spot.
    new_bw, new_bh = max(1, round(bw * SCALE)), max(1, round(bh * SCALE))
    new_U0, new_U1, new_VH = round(U0 * SCALE), round(U1 * SCALE), round(VH * SCALE)
    new_bu, new_bv = new_U1 - new_U0 + 1, 2 * new_VH + 1
    pivot_rel_x, pivot_rel_y = PIVOT[0] - bx0, PIVOT[1] - by0
    new_by0 = by1 - new_bh
    PIVOT = (bx0 + round(pivot_rel_x * SCALE), new_by0 + round(pivot_rel_y * SCALE))
    body = scale_sprite(body, new_bw, new_bh)
    barrel = scale_sprite(barrel, new_bu, new_bv)
    bw, bh, by0 = new_bw, new_bh, new_by0
    U0, U1, VH, bu, bv = new_U0, new_U1, new_VH, new_bu, new_bv

# Widen to the game canvas: the picture's right-hand columns, mirrored, fill the extra width.
wide = Image.new("RGB", (MEADOW_W, H))
wide.paste(mead, (0, 0))
wide.paste(ImageOps.mirror(mead.crop((W - (MEADOW_W - W), 0, W, H))), (W, 0))

with open(f"{outdir}/meadow_image.c", "w") as f:
    f.write("// Generated by tools/make_turtle_assets.py from tortoise.png. Do not edit.\n")
    f.write(f"#include <stdint.h>\n\nconst uint16_t meadow_image[{MEADOW_W} * {H}] = {{\n")
    mb = wide.tobytes()
    raw = [rgb565(tuple(mb[i:i + 3])) for i in range(0, MEADOW_W * H * 3, 3)]
    for i in range(0, len(raw), 12):
        f.write("    " + ",".join(f"0x{v:04X}" for v in raw[i:i + 12]) + ",\n")
    f.write("};\n")


def dump(f, name, rows):
    f.write(f"const uint16_t {name}[{len(rows)}][{len(rows[0])}] = {{\n")
    for r in rows:
        f.write("    {" + ",".join(f"0x{v:04X}" for v in r) + "},\n")
    f.write("};\n\n")


with open(f"{outdir}/turtle_art.c", "w") as f:
    f.write("// Generated by tools/make_turtle_assets.py from tortoise.png. Do not edit.\n")
    f.write('#include "turtle_art.h"\n\n')
    dump(f, "turtle_body", body)
    dump(f, "turtle_barrel", barrel)

with open(f"{outdir}/turtle_art.h", "w") as f:
    f.write(f"""// Generated by tools/make_turtle_assets.py. Do not edit.
#ifndef TURTLE_ART_H
#define TURTLE_ART_H

#include <stdint.h>

#define SPRITE_KEY 0x{KEY:04X} // transparent pixel

// Body sprite (barrel tube removed); its top-left corner is at (TURTLE_X0, TURTLE_Y0) on screen.
#define TURTLE_W  {bw}
#define TURTLE_H  {bh}
#define TURTLE_X0 {bx0}
#define TURTLE_Y0 {by0}
extern const uint16_t turtle_body[TURTLE_H][TURTLE_W];

// Barrel sprite drawn horizontally (pointing right). Pixel (i, j) is at distance
// BARREL_U0 + i along the barrel and j - BARREL_VH across it, from the pivot.
#define BARREL_W   {bu}
#define BARREL_H   {bv}
#define BARREL_U0  {U0}
#define BARREL_VH  {VH}
#define BARREL_LEN {U1} // muzzle distance from the pivot
#define PIVOT_X    {PIVOT[0]} // screen position of the pivot
#define PIVOT_Y    {PIVOT[1]}
#define BARREL_ANGLE0 {ANGLE0}  // angle of the barrel in the original picture
extern const uint16_t turtle_barrel[BARREL_H][BARREL_W];

#endif
""")

print("body", bw, "x", bh, " barrel", bu, "x", bv)
