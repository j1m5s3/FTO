"""
FTO's building kit: modular walls, roofline and rooftop pieces, house parts, shop signs, furniture for every kind
of interior, street dressing and trees. All in-house, built from code.

  blender -b --factory-startup -P Tools/Blender/build_kit.py -- --out Art/Source/Kit [--preview <dir>] [--only A,B]

Conventions (metres). Every piece faces +X:
- Wall panels have their outer face on x = 0 and run back to x = -T (the inside), are centred on y = 0 and stand on
  z = 0. Panels are P wide; ground-floor panels are G tall, upper-floor panels U tall. The game lays them edge to
  edge round a footprint (Source/FTO/City/FTOCityKit.h mirrors these numbers).
- Furniture and props stand on z = 0. Free-standing pieces are centred on the origin with their front (where you'd
  stand to use them) toward +X; wall-hugging pieces have their back on x = 0 and stick out toward +X.
- Glazed ground-floor panels come with a matching *_Glass mesh in the same frame: the translucent pane is kept
  separate so the walls themselves can be Nanite.
Vertex alpha 1 marks what the game tints per instance: building paint, awning stripes, upholstery, signs, rugs.
Collision is UCX boxes (doorways stay open; windows don't).
"""
import json
import math
import os
import random
import sys

import bmesh
import bpy
from mathutils import Matrix, Vector

sys.path.append(os.path.dirname(os.path.abspath(__file__)))
import fto_blender as fb  # noqa: E402
from fto_shapes import (BODY, GLASS, GLOW, MATERIALS, Frame, ball, beam, box, collision_boxes, cone, cyl,  # noqa: E402
                        export_static, lettering, prism, ring, star)

# Kit grid (mirrored in FTOCityKit.h)
P = 2.0     # panel width
G = 4.0     # ground floor height
U = 3.2     # upper floor height
T = 0.3     # wall thickness

# Seats are an ordinary 0.45 m, which is what the cast (Epic's mannequin proportions) sit on in their Sit clip (hip joints
# 0.53 m up): feet flat on the floor, hands at the 0.75 m tables. Stools stay tall (feet dangle, as on any bar stool).
# FTOCityInteriors.cpp seats people to these numbers.
SEAT = 0.45

# Colours (sRGB)
PAINT = (0.85, 0.85, 0.85)          # tinted in game
INNER = (0.94, 0.91, 0.84)          # interior plaster
PLINTH = (0.40, 0.38, 0.38)
FRAME = (0.96, 0.95, 0.92)
SILL = (0.82, 0.80, 0.76)
DARK_GLASS = (0.20, 0.30, 0.44)     # opaque upper-floor glass
SHINE = (0.62, 0.78, 0.92)
GLASS_TINT = (0.55, 0.72, 0.85)     # preview only; the game renders M_FTOGlass
TRIM = (0.32, 0.32, 0.34)
METAL = (0.58, 0.60, 0.63)
CHROME = (0.78, 0.79, 0.81)
DARK = (0.12, 0.12, 0.13)
BLACK = (0.04, 0.04, 0.05)
WHITE = (0.96, 0.96, 0.96)
WOOD = (0.58, 0.38, 0.20)
WOOD_DARK = (0.34, 0.20, 0.10)
WOOD_LIGHT = (0.78, 0.60, 0.38)
RED = (0.85, 0.12, 0.10)
GREEN = (0.20, 0.55, 0.22)
LEAVES = (0.20, 0.60, 0.22)
LEAVES_DARK = (0.12, 0.42, 0.16)
TRUNK = (0.40, 0.24, 0.12)
BRICK = (0.66, 0.28, 0.20)
CONCRETE = (0.62, 0.62, 0.60)
TERRACOTTA = (0.78, 0.42, 0.25)
BLUE = (0.16, 0.32, 0.80)
POLICE_BLUE = (0.10, 0.20, 0.55)
GOLD = (1.0, 0.78, 0.18)
OLIVE = (0.36, 0.40, 0.20)
YELLOW = (1.0, 0.80, 0.10)
ORANGE = (0.98, 0.52, 0.12)
FELT = (0.10, 0.50, 0.26)
SCREEN = (0.35, 0.72, 1.0)
WARM_LIGHT = (1.0, 0.92, 0.72)
CARDBOARD = (0.72, 0.55, 0.34)
MONEY = (0.62, 0.52, 0.32)
PRODUCT_COLOURS = [(0.90, 0.20, 0.20), (0.20, 0.50, 0.90), (0.98, 0.80, 0.15), (0.25, 0.75, 0.35), (0.95, 0.55, 0.15),
                   (0.70, 0.30, 0.80), (0.95, 0.95, 0.95), (0.30, 0.85, 0.85), (0.95, 0.45, 0.65)]

# --------------------------------------------------------------------------------------
# Building helpers
# --------------------------------------------------------------------------------------
def slab(y0, y1, z0, z1, outer=PAINT, inner=INNER, t=T, tint=True, bevel=0.012):
    """A stretch of wall: tinted outer skin on x in [-t/2, 0], plaster inner skin behind it."""
    if y1 - y0 < 1e-4 or z1 - z0 < 1e-4:
        return []
    cy, cz, w, h = (y0 + y1) / 2, (z0 + z1) / 2, y1 - y0, z1 - z0
    return [box(outer, (-t / 4, cy, cz), (t / 2, w, h), bevel=bevel, tint=tint),
            box(inner, (-3 * t / 4, cy, cz), (t / 2, w, h), bevel=bevel)]


def wall(height, opening=None, width=P, outer=PAINT, t=T, tint=True):
    """A wall panel with at most one rectangular opening (y0, y1, z0, z1)."""
    h = width / 2
    if not opening:
        return slab(-h, h, 0, height, outer=outer, t=t, tint=tint)
    y0, y1, z0, z1 = opening
    return (slab(-h, y0, 0, height, outer=outer, t=t, tint=tint) + slab(y1, h, 0, height, outer=outer, t=t, tint=tint)
            + slab(y0, y1, 0, z0, outer=outer, t=t, tint=tint) + slab(y0, y1, z1, height, outer=outer, t=t, tint=tint))


def frame(y0, y1, z0, z1, color=FRAME, w=0.08, d=0.05, sill=True, bottom=True):
    """Moulding round an opening on the outer face, with a sill that sticks out a little further."""
    p = [box(color, (d / 2, y0 - w / 2, (z0 + z1) / 2), (d, w, z1 - z0 + w), bevel=0.01),
         box(color, (d / 2, y1 + w / 2, (z0 + z1) / 2), (d, w, z1 - z0 + w), bevel=0.01),
         box(color, (d / 2, (y0 + y1) / 2, z1 + w / 2), (d, y1 - y0 + 2 * w, w), bevel=0.01)]
    if sill:
        p.append(box(SILL, (0.06, (y0 + y1) / 2, z0 - 0.04), (0.14, y1 - y0 + 0.24, 0.08), bevel=0.015))
    elif bottom:
        p.append(box(color, (d / 2, (y0 + y1) / 2, z0 - w / 2), (d, y1 - y0 + 2 * w, w), bevel=0.01))
    return p


def plinth(y0, y1, height=0.35):
    return [box(PLINTH, (0.012, (y0 + y1) / 2, height / 2), (0.024, y1 - y0, height), bevel=0.006)] if y1 > y0 else []


def glazing(y0, y1, z0, z1, x=-0.1):
    return [box(GLASS_TINT, (x, (y0 + y1) / 2, (z0 + z1) / 2), (0.02, y1 - y0, z1 - z0), bevel=0.0, material=GLASS)]


def full_panel_collision(height, width=P, t=T):
    return [((-t / 2, 0, height / 2), (t, width, height))]


def frame_collision(height, opening, width=P, t=T):
    """Wall round an opening; the glass piece blocks the opening itself (walkers, but not line of sight)."""
    y0, y1, z0, z1 = opening
    h = width / 2
    return [((-t / 2, (-h + y0) / 2, height / 2), (t, y0 + h, height)),
            ((-t / 2, (y1 + h) / 2, height / 2), (t, h - y1, height)),
            ((-t / 2, (y0 + y1) / 2, z0 / 2), (t, y1 - y0, z0)),
            ((-t / 2, (y0 + y1) / 2, (z1 + height) / 2), (t, y1 - y0, height - z1))]


def glass_collision(opening, x=-0.1):
    y0, y1, z0, z1 = opening
    return [((x, (y0 + y1) / 2, (z0 + z1) / 2), (0.1, y1 - y0, z1 - z0))]


def door_collision(height, y0, y1, z1, width=P, t=T):
    h = width / 2
    return [((-t / 2, (-h + y0) / 2, height / 2), (t, y0 + h, height)),
            ((-t / 2, (y1 + h) / 2, height / 2), (t, h - y1, height)),
            ((-t / 2, (y0 + y1) / 2, (z1 + height) / 2), (t, y1 - y0, height - z1))]


# Openings, as (y0, y1, z0, z1)
WINDOW_G = (-0.6, 0.6, 1.0, 2.6)
DOOR_G = (-0.75, 0.75, 0.0, 2.7)
SHOP_G = (-0.85, 0.85, 0.45, 2.75)
WINDOW_U = (-0.6, 0.6, 0.8, 2.5)
WIDE_U = (-0.85, 0.85, 0.7, 2.6)
IDOOR = (-0.7, 0.7, 0.0, 2.6)


def wall_g_plain():
    return wall(G) + plinth(-P / 2, P / 2), full_panel_collision(G)


def wall_g_window():
    y0, y1, z0, z1 = WINDOW_G
    return wall(G, WINDOW_G) + frame(y0, y1, z0, z1) + plinth(-P / 2, P / 2), frame_collision(G, WINDOW_G)


def wall_g_window_glass():
    return glazing(*WINDOW_G), glass_collision(WINDOW_G)


def wall_g_door():
    y0, y1, z0, z1 = DOOR_G
    p = wall(G, DOOR_G) + frame(y0, y1, z0, z1, sill=False, bottom=False) + plinth(-P / 2, y0) + plinth(y1, P / 2)
    p.append(box(CONCRETE, (0.15, 0, 0.03), (0.3, 1.9, 0.06), bevel=0.01))          # front step
    p.append(box(WARM_LIGHT, (0.06, 0, z1 + 0.25), (0.06, 0.3, 0.15), bevel=0.02, material=GLOW))  # porch light
    return p, door_collision(G, y0, y1, z1)


def wall_g_shop():
    y0, y1, z0, z1 = SHOP_G
    p = wall(G, SHOP_G) + frame(y0, y1, z0, z1, color=DARK, w=0.07, sill=False)
    p.append(box(PLINTH, (0.02, 0, z0 / 2), (0.04, P, z0), bevel=0.008))                          # kick plate
    return p, frame_collision(G, SHOP_G)


def wall_g_shop_glass():
    return glazing(*SHOP_G), glass_collision(SHOP_G)


def wall_g_shopdoor():
    y0, y1, z0, z1 = DOOR_G
    p = wall(G, DOOR_G) + frame(y0, y1, z0, z1, color=DARK, w=0.07, sill=False, bottom=False)
    p += [box(DARK, (0.02, -0.82, 0.225), (0.04, 0.16, 0.45), bevel=0.008),
          box(DARK, (0.02, 0.82, 0.225), (0.04, 0.16, 0.45), bevel=0.008),
          box(CONCRETE, (0.15, 0, 0.03), (0.3, 1.9, 0.06), bevel=0.01)]
    return p, door_collision(G, y0, y1, z1)


def wall_g_roller():
    """Warehouse loading door, two panels wide, rolled up."""
    width = 2 * P
    y0, y1, z0, z1 = -1.8, 1.8, 0.0, 3.4
    p = wall(G, (y0, y1, z0, z1), width=width)
    p += frame(y0, y1, z0, z1, color=YELLOW, w=0.12, sill=False, bottom=False)
    p.append(cyl(METAL, (0.25, 0, 3.7), 0.22, 3.8, axis='y', segments=14))             # rolled-up door drum
    for i in range(6):                                                                   # hazard stripes on the jambs
        p.append(box(BLACK, (0.065, y0 - 0.06, 0.25 + i * 0.55), (0.02, 0.13, 0.22), bevel=0.0, rot=(35, 0, 0)))
        p.append(box(BLACK, (0.065, y1 + 0.06, 0.25 + i * 0.55), (0.02, 0.13, 0.22), bevel=0.0, rot=(35, 0, 0)))
    return p, door_collision(G, y0, y1, z1, width=width)


def floor_band(width=P):
    return [box(FRAME, (0.03, 0, 0.1), (0.06, width, 0.2), bevel=0.012)]


def upper_window(opening):
    y0, y1, z0, z1 = opening
    p = wall(U, opening) + frame(y0, y1, z0, z1) + floor_band()
    p.append(box(DARK_GLASS, (-0.12, (y0 + y1) / 2, (z0 + z1) / 2), (0.03, y1 - y0, z1 - z0), bevel=0.0))
    p.append(box(SHINE, (-0.10, y0 + (y1 - y0) * 0.3, (z0 + z1) / 2 + 0.2), (0.01, 0.12, (z1 - z0) * 0.7), bevel=0.0,
                 rot=(20, 0, 0)))
    p.append(box(FRAME, (-0.1, (y0 + y1) / 2, (z0 + z1) / 2), (0.03, 0.05, z1 - z0), bevel=0.0))  # mullion
    return p, full_panel_collision(U)


def wall_u_plain():
    return wall(U) + floor_band(), full_panel_collision(U)


def wall_u_window():
    return upper_window(WINDOW_U)


def wall_u_wide():
    return upper_window(WIDE_U)


def iwall_plain():
    t = 0.15
    return ([box(PAINT, (0, 0, G / 2), (t, P, G), bevel=0.01, tint=True)],
            [((0, 0, G / 2), (t, P, G))])


def iwall_door():
    t = 0.15
    y0, y1, z0, z1 = IDOOR
    h = P / 2
    p = [box(PAINT, (0, (-h + y0) / 2, G / 2), (t, y0 + h, G), bevel=0.01, tint=True),
         box(PAINT, (0, (y1 + h) / 2, G / 2), (t, h - y1, G), bevel=0.01, tint=True),
         box(PAINT, (0, 0, (z1 + G) / 2), (t, y1 - y0, G - z1), bevel=0.01, tint=True)]
    for side in (1, -1):  # frame on both faces
        x = side * (t / 2 + 0.015)
        p += [box(FRAME, (x, y0 - 0.04, z1 / 2), (0.03, 0.08, z1), bevel=0.005),
              box(FRAME, (x, y1 + 0.04, z1 / 2), (0.03, 0.08, z1), bevel=0.005),
              box(FRAME, (x, 0, z1 + 0.04), (0.03, y1 - y0 + 0.16, 0.08), bevel=0.005)]
    return p, [((0, (-h + y0) / 2, G / 2), (t, y0 + h, G)), ((0, (y1 + h) / 2, G / 2), (t, h - y1, G)),
               ((0, 0, (z1 + G) / 2), (t, y1 - y0, G - z1))]


def corner(height, base=True):
    """Pilaster on a building corner (the pivot is the corner itself); hides where two walls meet."""
    s = 0.44
    p = [box(PAINT, (-s / 2 + 0.07, -s / 2 + 0.07, height / 2), (s, s, height), bevel=0.03, tint=True)]
    if base:
        p.append(box(PLINTH, (-s / 2 + 0.08, -s / 2 + 0.08, 0.2), (s + 0.02, s + 0.02, 0.4), bevel=0.02))
    else:
        p.append(box(FRAME, (-s / 2 + 0.08, -s / 2 + 0.08, 0.1), (s + 0.03, s + 0.03, 0.2), bevel=0.012))
    return p, [((-s / 2 + 0.07, -s / 2 + 0.07, height / 2), (s, s, height))]


def corner_g():
    return corner(G)


def corner_u():
    return corner(U, base=False)


def parapet():
    return ([box(PAINT, (-T / 2, 0, 0.45), (T, P, 0.9), bevel=0.015, tint=True),
             box(FRAME, (-0.08, 0, 0.97), (0.5, P, 0.14), bevel=0.025),                 # coping
             box(FRAME, (0.05, 0, 0.1), (0.1, P, 0.2), bevel=0.02)],                     # cornice moulding
            [((-T / 2, 0, 0.5), (T, P, 1.0))])


def parapet_corner():
    return [box(FRAME, (-0.1, -0.1, 0.52), (0.62, 0.62, 1.04), bevel=0.04)], [((-0.1, -0.1, 0.52), (0.62, 0.62, 1.04))]


def cornice():
    """Band dividing the shop level from the floors above (sits at z = G on the facade)."""
    return [box(FRAME, (0.08, 0, -0.1), (0.16, P, 0.2), bevel=0.03),
            box(FRAME, (0.04, 0, -0.26), (0.08, P, 0.12), bevel=0.02)], []


def awning():
    """Striped canopy over a shopfront: stripes alternate the tint (the shop's colour) and white."""
    p = []
    stripes = 8
    width = P / stripes
    for i in range(stripes):
        y = -P / 2 + width * (i + 0.5)
        tinted = i % 2 == 0
        col = PAINT if tinted else WHITE
        p.append(beam(col, (0.0, y, 3.35), (1.3, y, 2.85), width, 0.04, bevel=0.0, tint=tinted))
        p.append(box(col, (1.32, y, 2.72), (0.04, width, 0.26), bevel=0.0, tint=tinted))  # valance
    p.append(box(DARK, (0.66, -P / 2 + 0.02, 3.08), (1.32, 0.03, 0.04), bevel=0.0, rot=(0, 21, 0)))
    p.append(box(DARK, (0.66, P / 2 - 0.02, 3.08), (1.32, 0.03, 0.04), bevel=0.0, rot=(0, 21, 0)))
    return p, []


def sign(text, board=PAINT, letters=WHITE, width=3.6):
    """Shop sign on the sign band above the door (back on the wall, pivot at the wall at floor level)."""
    p = [box(board, (0.05, 0, 3.4), (0.1, width, 0.72), bevel=0.03, tint=True),
         box(letters, (0.105, 0, 3.4), (0.01, width - 0.12, 0.6), bevel=0.0),
         box(board, (0.108, 0, 3.4), (0.01, width - 0.2, 0.52), bevel=0.0, tint=True),
         lettering(text, letters, (0.12, 0, 3.4), '+x', 0.42)]
    return p, []


SIGNS = {
    "Diner": "DINER", "Bar": "BAR", "Pizza": "PIZZA", "Donuts": "DONUTS", "Coffee": "COFFEE", "Market": "MARKET",
    "Books": "BOOKS", "Toys": "TOYS", "Laundry": "LAUNDRY", "Offices": "OFFICES", "Bank": "BANK", "Police": "POLICE",
}

# --------------------------------------------------------------------------------------
# Rooftop
# --------------------------------------------------------------------------------------
def roof_ac():
    p = [box(METAL, (0, 0, 0.45), (1.4, 1.0, 0.8), bevel=0.05),
         cyl(DARK, (0.25, 0, 0.86), 0.32, 0.04, segments=16),
         box(TRIM, (0.25, 0, 0.88), (0.6, 0.05, 0.02), bevel=0.0),
         box(TRIM, (0.25, 0, 0.88), (0.05, 0.6, 0.02), bevel=0.0),
         box(TRIM, (0, 0, 0.03), (1.5, 1.1, 0.06), bevel=0.01)]
    for i in range(4):
        p.append(box(TRIM, (-0.4, -0.51, 0.25 + i * 0.13), (0.5, 0.02, 0.05), bevel=0.0))
    return p, [((0, 0, 0.45), (1.4, 1.0, 0.9))]


def roof_vent():
    return [cyl(METAL, (0, 0, 0.35), 0.15, 0.7, segments=12),
            cone(METAL, (0, 0, 0.8), 0.3, 0.25, segments=12),
            cyl(DARK, (0, 0, 0.66), 0.2, 0.06, segments=12)], [((0, 0, 0.4), (0.4, 0.4, 0.8))]


def roof_water_tower():
    """The classic wooden rooftop water tank on stilts."""
    p = [cyl(WOOD, (0, 0, 3.2), 1.2, 2.4, segments=20),
         cone(WOOD_DARK, (0, 0, 4.75), 1.35, 0.7, segments=20),
         ring(DARK, (0, 0, 2.4), 1.23, 0.04, segments=28, rings=6),
         ring(DARK, (0, 0, 3.4), 1.23, 0.04, segments=28, rings=6),
         ring(DARK, (0, 0, 4.2), 1.23, 0.04, segments=28, rings=6),
         cyl(DARK, (0, 0, 1.97), 1.3, 0.1, segments=20)]
    for sx in (1, -1):
        for sy in (1, -1):
            p.append(box(DARK, (sx * 0.8, sy * 0.8, 1.0), (0.12, 0.12, 2.0), bevel=0.02))
    p.append(beam(DARK, (0.8, 0.8, 0.3), (-0.8, -0.8, 1.8), 0.05, 0.05, bevel=0.0))
    p.append(beam(DARK, (-0.8, 0.8, 0.3), (0.8, -0.8, 1.8), 0.05, 0.05, bevel=0.0))
    return p, [((0, 0, 2.5), (2.4, 2.4, 5.0))]


def roof_hut():
    p = [box(PAINT, (0, 0, 1.2), (2.4, 2.0, 2.4), bevel=0.04, tint=True),
         box(FRAME, (0, 0, 2.46), (2.56, 2.16, 0.12), bevel=0.03),
         box(TRIM, (1.21, 0, 1.0), (0.04, 0.9, 2.0), bevel=0.01),
         cyl(CHROME, (1.25, 0.3, 1.0), 0.04, 0.06, axis='x', segments=8)]
    return p, [((0, 0, 1.25), (2.5, 2.1, 2.5))]


def roof_antenna():
    p = [cyl(METAL, (0, 0, 2.0), 0.05, 4.0, segments=8), cyl(DARK, (0, 0, 0.1), 0.25, 0.2, segments=10)]
    for i, w in enumerate((1.2, 0.9, 0.6)):
        p.append(box(METAL, (0, 0, 2.6 + i * 0.5), (0.04, w, 0.04), bevel=0.0))
    p.append(ball((1.0, 0.2, 0.1), (0, 0, 4.05), 0.12, material=GLOW))
    return p, [((0, 0, 0.5), (0.3, 0.3, 1.0))]

# --------------------------------------------------------------------------------------
# Houses
# --------------------------------------------------------------------------------------
def gable_roof():
    """
    Unit gable roof, scaled by the game to fit each house: 1 x 1 footprint (eaves overhang included), ridge along Y
    at z = 1. Roof tiles take the tint; the gable ends are plaster.
    """
    p = [prism(INNER, [(-0.46, 0.0), (0.46, 0.0), (0.0, 0.92)], -0.47, 0.47),
         beam(PAINT, (-0.52, 0, -0.02), (0.0, 0, 1.0), 1.02, 0.05, bevel=0.0, tint=True),
         beam(PAINT, (0.52, 0, -0.02), (0.0, 0, 1.0), 1.02, 0.05, bevel=0.0, tint=True),
         box(FRAME, (0, 0, 1.0), (0.08, 1.04, 0.06), bevel=0.0)]
    return p, []


def chimney():
    return [box(BRICK, (0, 0, 1.0), (0.6, 0.6, 2.0), bevel=0.03),
            box(CONCRETE, (0, 0, 2.05), (0.72, 0.72, 0.12), bevel=0.02),
            cyl(DARK, (0, 0, 2.15), 0.14, 0.12, segments=10)], []


def porch():
    """Little roof over the front door on two posts, with steps (pivot on the wall at the door)."""
    p = [box(PAINT, (0.75, 0, 2.95), (1.6, 2.4, 0.12), bevel=0.03, tint=True),
         box(FRAME, (1.5, 0, 2.85), (0.1, 2.44, 0.14), bevel=0.02),
         box(CONCRETE, (0.55, 0, 0.08), (1.1, 2.2, 0.16), bevel=0.02),
         box(CONCRETE, (1.25, 0, 0.04), (0.35, 1.8, 0.08), bevel=0.02)]
    for y in (-1.05, 1.05):
        p.append(box(FRAME, (1.45, y, 1.45), (0.12, 0.12, 2.9), bevel=0.02))
    return p, [((1.45, -1.05, 1.45), (0.14, 0.14, 2.9)), ((1.45, 1.05, 1.45), (0.14, 0.14, 2.9))]


def fence():
    p = [box(WHITE, (0, 0, 0.35), (0.04, P, 0.07), bevel=0.0), box(WHITE, (0, 0, 0.75), (0.04, P, 0.07), bevel=0.0)]
    for i in range(10):
        y = -P / 2 + 0.1 + i * 0.2
        p.append(box(WHITE, (0.03, y, 0.5), (0.03, 0.09, 1.0), bevel=0.0))
        p.append(prism(WHITE, [(0.015, 1.0), (0.045, 1.0), (0.03, 1.08)], y - 0.045, y + 0.045))
    return p, [((0, 0, 0.5), (0.1, P, 1.0))]


def house_mailbox():
    return [box(WOOD, (0, 0, 0.55), (0.08, 0.08, 1.1), bevel=0.01),
            box(PAINT, (0, 0, 1.18), (0.45, 0.22, 0.2), bevel=0.06, tint=True),
            box(RED, (-0.05, 0.13, 1.28), (0.03, 0.02, 0.18), bevel=0.0)], [((0, 0, 0.6), (0.4, 0.25, 1.2))]

# --------------------------------------------------------------------------------------
# Interiors: shop, diner, bar, office, home, warehouse, bank, precinct
# --------------------------------------------------------------------------------------
def ceiling_light():
    """Glowing ceiling panel; pivot on the ceiling, hanging below it."""
    return [box(FRAME, (0, 0, -0.03), (1.3, 0.7, 0.06), bevel=0.01),
            box(WARM_LIGHT, (0, 0, -0.065), (1.2, 0.6, 0.02), bevel=0.0, material=GLOW)], []


def products(rng, x, y0, y1, z, depth, facing=1):
    """A shelf's worth of colourful boxes and cans."""
    p = []
    y = y0 + 0.05
    while y < y1 - 0.1:
        w = rng.uniform(0.1, 0.2)
        h = rng.uniform(0.12, 0.3)
        col = rng.choice(PRODUCT_COLOURS)
        if rng.random() < 0.4:
            p.append(cyl(col, (x, y + w / 2, z + h / 2), w * 0.4, h, segments=8))
        else:
            p.append(box(col, (x, y + w / 2, z + h / 2), (depth * 0.8, w * 0.9, h), bevel=0.005))
        y += w + 0.02
    return p


def shelf():
    """Double-sided shop gondola (long side along Y)."""
    rng = random.Random("shelf")
    p = [box(WHITE, (0, 0, 0.05), (0.9, 2.0, 0.1), bevel=0.01),
         box(WHITE, (0, 0, 0.9), (0.06, 2.0, 1.7), bevel=0.01),
         box(WHITE, (0, -1.0, 0.85), (0.9, 0.06, 1.7), bevel=0.01),
         box(WHITE, (0, 1.0, 0.85), (0.9, 0.06, 1.7), bevel=0.01)]
    for side in (1, -1):
        for level, z in enumerate((0.15, 0.55, 0.95, 1.35)):
            p.append(box(METAL, (side * 0.22, 0, z), (0.4, 1.94, 0.03), bevel=0.0))
            p += products(rng, side * 0.24, -0.95, 0.95, z + 0.015, 0.36)
    return p, [((0, 0, 0.85), (0.9, 2.1, 1.7))]


def wall_shelf():
    rng = random.Random("wall_shelf")
    p = [box(WHITE, (0.03, 0, 1.0), (0.06, 2.0, 2.0), bevel=0.01),
         box(WHITE, (0.2, -1.0, 1.0), (0.4, 0.05, 2.0), bevel=0.01),
         box(WHITE, (0.2, 1.0, 1.0), (0.4, 0.05, 2.0), bevel=0.01)]
    for z in (0.1, 0.5, 0.9, 1.3, 1.7):
        p.append(box(METAL, (0.22, 0, z), (0.36, 1.95, 0.03), bevel=0.0))
        p += products(rng, 0.24, -0.95, 0.95, z + 0.015, 0.3)
    return p, [((0.2, 0, 1.0), (0.4, 2.05, 2.0))]


def shop_counter():
    """Clerk stands behind (-X), customers in front (+X)."""
    rng = random.Random("counter")
    p = [box(PAINT, (0, 0, 0.5), (0.7, 2.2, 1.0), bevel=0.03, tint=True),
         box(WOOD_LIGHT, (0, 0, 1.02), (0.8, 2.3, 0.05), bevel=0.02),
         box(DARK, (-0.05, 0.6, 1.15), (0.35, 0.4, 0.2), bevel=0.03),                  # register
         box(SCREEN, (-0.23, 0.6, 1.28), (0.02, 0.25, 0.14), bevel=0.0, material=GLOW),
         box(DARK, (-0.05, 0.6, 1.31), (0.05, 0.04, 0.12), bevel=0.0),
         box(TRIM, (0.25, -0.5, 1.2), (0.25, 0.7, 0.3), bevel=0.02)]                    # candy rack
    for i in range(6):
        p.append(box(rng.choice(PRODUCT_COLOURS), (0.3, -0.8 + i * 0.12, 1.2), (0.1, 0.1, 0.22), bevel=0.01))
    p.append(cyl(GLASS_TINT, (0.2, 0.0, 1.13), 0.07, 0.16, segments=10, material=GLASS))       # tip jar
    return p, [((0, 0, 0.6), (0.8, 2.3, 1.2))]


def cabinet(color, depth, width, height, back=None):
    """An open-fronted carcass (back, sides, top and bottom) with its back on x = 0 and its open side toward +X."""
    t = 0.04
    return [box(back or color, (t / 2, 0, height / 2), (t, width, height), bevel=0.0),
            box(color, (depth / 2, -width / 2 + t / 2, height / 2), (depth, t, height), bevel=0.01),
            box(color, (depth / 2, width / 2 - t / 2, height / 2), (depth, t, height), bevel=0.01),
            box(color, (depth / 2, 0, height - t / 2), (depth, width, t), bevel=0.01),
            box(color, (depth / 2, 0, 0.05), (depth, width, 0.1), bevel=0.01)]


def drinks_fridge():
    rng = random.Random("fridge")
    p = cabinet(WHITE, 0.7, 1.0, 2.0, back=WARM_LIGHT)
    p[0] = box(WARM_LIGHT, (0.03, 0, 1.0), (0.04, 0.96, 1.96), bevel=0.0, material=GLOW)   # glowing back wall
    p += [box(GLASS_TINT, (0.69, 0, 1.03), (0.02, 0.92, 1.8), bevel=0.0, material=GLASS),
          box(RED, (0.71, 0, 1.92), (0.02, 0.96, 0.14), bevel=0.0),
          box(CHROME, (0.72, 0.4, 1.05), (0.03, 0.04, 0.6), bevel=0.0)]
    for z in (0.35, 0.8, 1.25, 1.65):
        p.append(box(METAL, (0.35, 0, z), (0.6, 0.9, 0.02), bevel=0.0))
        for i in range(6):
            p.append(cyl(rng.choice(PRODUCT_COLOURS), (0.4, -0.35 + i * 0.14, z + 0.13), 0.04, 0.24, segments=8))
    return p, [((0.35, 0, 1.0), (0.72, 1.02, 2.0))]


def booth():
    """Diner booth: its end against the wall (x = 0), table between two benches that face each other."""
    p = [box(CHROME, (0.75, 0, 0.36), (0.1, 0.1, 0.72), bevel=0.0),
         box(WHITE, (0.75, 0, 0.74), (1.3, 0.9, 0.05), bevel=0.02),
         box(RED, (0.75, 0, 0.765), (1.3, 0.9, 0.012), bevel=0.0),
         cyl(CHROME, (0.3, 0.2, 0.83), 0.04, 0.12, segments=8),
         cyl(RED, (0.3, 0.3, 0.83), 0.035, 0.14, segments=8),
         cyl(YELLOW, (0.3, 0.38, 0.83), 0.035, 0.14, segments=8)]
    for side in (1, -1):
        y = side * 0.78
        p += [box(PAINT, (0.75, y, (SEAT + 0.01) / 2), (1.4, 0.5, SEAT + 0.01), bevel=0.05, tint=True),
              box(PAINT, (0.75, y + side * 0.22, 0.65), (1.4, 0.16, 0.8), bevel=0.05, tint=True),
              box(CHROME, (0.75, y + side * 0.3, 1.07), (1.4, 0.06, 0.05), bevel=0.01)]
    return p, [((0.75, 0, 0.6), (1.45, 2.1, 1.2))]


def diner_counter():
    p = [box(PAINT, (0, 0, 0.5), (0.75, 3.0, 1.0), bevel=0.03, tint=True),
         box(CHROME, (0.37, 0, 0.1), (0.02, 3.0, 0.08), bevel=0.0),
         box(WHITE, (0, 0, 1.03), (0.85, 3.1, 0.06), bevel=0.02),
         cyl(WOOD_LIGHT, (-0.1, 0.9, 1.1), 0.2, 0.06, segments=16),                       # a pie...
         ball(GLASS_TINT, (-0.1, 0.9, 1.12), (0.44, 0.44, 0.36), material=GLASS),          # ...under a dome
         box(CHROME, (0.2, -0.7, 1.12), (0.1, 0.14, 0.1), bevel=0.01),                     # napkins
         cyl(RED, (0.2, -0.5, 1.14), 0.03, 0.16, segments=8),                              # ketchup
         cyl(YELLOW, (0.2, -0.42, 1.14), 0.03, 0.16, segments=8)]                          # mustard
    return p, [((0, 0, 0.55), (0.85, 3.1, 1.1))]


def stool():
    return [cyl(CHROME, (0, 0, 0.36), 0.04, 0.72, segments=8), cyl(CHROME, (0, 0, 0.02), 0.2, 0.04, segments=12),
            cyl(PAINT, (0, 0, 0.75), 0.2, 0.08, segments=14, tint=True)], [((0, 0, 0.4), (0.4, 0.4, 0.8))]


def jukebox():
    p = [box(WOOD_DARK, (0.25, 0, 0.6), (0.5, 0.8, 1.2), bevel=0.05),
         cyl(WOOD_DARK, (0.25, 0, 1.2), 0.4, 0.5, axis='x', segments=16),
         cyl(ORANGE, (0.51, 0, 1.2), 0.32, 0.02, axis='x', segments=16, material=GLOW),
         box(SCREEN, (0.51, 0, 0.75), (0.02, 0.5, 0.3), bevel=0.0, material=GLOW),
         box((0.95, 0.3, 0.6), (0.51, -0.33, 0.6), (0.02, 0.06, 1.0), bevel=0.0, material=GLOW),
         box((0.95, 0.3, 0.6), (0.51, 0.33, 0.6), (0.02, 0.06, 1.0), bevel=0.0, material=GLOW)]
    return p, [((0.25, 0, 0.8), (0.5, 0.8, 1.6))]


def bar_counter():
    p = [box(WOOD_DARK, (0, 0, 0.55), (0.8, 3.0, 1.1), bevel=0.03),
         box(WOOD, (0, 0, 1.12), (0.9, 3.1, 0.06), bevel=0.02),
         box(GOLD, (0.5, 0, 0.2), (0.04, 3.0, 0.04), bevel=0.0)]
    for y in (-0.6, -0.3, 0.0):
        p += [cyl(CHROME, (-0.2, y, 1.3), 0.03, 0.3, segments=8),
              box(PAINT, (-0.2, y, 1.5), (0.05, 0.05, 0.14), bevel=0.01, tint=True)]
    for y in (0.6, 0.8):
        p += [cyl(GLASS_TINT, (0.2, y, 1.2), 0.04, 0.12, segments=8, material=GLASS),
              cyl(YELLOW, (0.2, y, 1.18), 0.035, 0.08, segments=8)]
    return p, [((0, 0, 0.6), (0.9, 3.1, 1.2))]


def bottle_shelf():
    rng = random.Random("bottles")
    p = [box(WOOD_DARK, (0.05, 0, 1.0), (0.1, 3.0, 2.0), bevel=0.02),
         box(SHINE, (0.105, 0, 1.2), (0.01, 2.8, 1.3), bevel=0.0)]
    for z in (0.5, 1.0, 1.5):
        p.append(box(WOOD, (0.2, 0, z), (0.3, 3.0, 0.04), bevel=0.0))
        for i in range(14):
            col = rng.choice([(0.15, 0.45, 0.15), (0.45, 0.25, 0.10), (0.85, 0.85, 0.8), (0.6, 0.15, 0.1)])
            y = -1.35 + i * 0.2 + rng.uniform(-0.03, 0.03)
            h = rng.uniform(0.22, 0.32)
            p += [cyl(col, (0.22, y, z + 0.02 + h / 2), 0.04, h, segments=8),
                  cyl(col, (0.22, y, z + 0.02 + h + 0.05), 0.015, 0.1, segments=6)]
    return p, [((0.18, 0, 1.0), (0.36, 3.0, 2.0))]


def round_table():
    return [cyl(DARK, (0, 0, 0.03), 0.25, 0.06, segments=12), cyl(DARK, (0, 0, 0.38), 0.04, 0.7, segments=8),
            cyl(WOOD, (0, 0, 0.75), 0.42, 0.05, segments=18)], [((0, 0, 0.4), (0.8, 0.8, 0.8))]


def chair():
    """A wooden chair; whoever sits in it faces +X."""
    leg = SEAT - 0.05
    p = [box(WOOD, (0, 0, SEAT - 0.025), (0.45, 0.45, 0.05), bevel=0.01),
         box(WOOD, (-0.2, 0, SEAT + 0.3), (0.05, 0.42, 0.6), bevel=0.01)]
    for sx in (1, -1):
        for sy in (1, -1):
            p.append(box(WOOD_DARK, (sx * 0.19, sy * 0.19, leg / 2), (0.04, 0.04, leg), bevel=0.0))
    return p, [((0, 0, (SEAT + 0.6) / 2), (0.45, 0.45, SEAT + 0.6))]


def pool_table():
    p = [box(WOOD_DARK, (0, 0, 0.62), (2.5, 1.4, 0.2), bevel=0.04),
         box(FELT, (0, 0, 0.73), (2.3, 1.2, 0.03), bevel=0.0)]
    for sx in (1, -1):
        for sy in (1, -1):
            p.append(box(WOOD_DARK, (sx * 1.05, sy * 0.55, 0.28), (0.14, 0.14, 0.56), bevel=0.02))
            p.append(cyl(BLACK, (sx * 1.14, sy * 0.59, 0.74), 0.06, 0.02, segments=10))
    for i, col in enumerate([WHITE, RED, YELLOW, BLUE, BLACK, GREEN, ORANGE]):
        p.append(ball(col, (-0.6 + (i % 4) * 0.12 + (0.5 if i else 0), -0.2 + (i // 4) * 0.12, 0.77), 0.06))
    p.append(beam(WOOD_LIGHT, (-0.9, 0.3, 0.79), (0.4, 0.45, 0.79), 0.03, 0.03, bevel=0.0))
    return p, [((0, 0, 0.45), (2.5, 1.4, 0.9))]


def dartboard():
    p = [box(DARK, (0.02, 0, 1.73), (0.04, 0.7, 0.7), bevel=0.01),
         cyl(BLACK, (0.05, 0, 1.73), 0.24, 0.03, axis='x', segments=20)]
    for r, col in ((0.2, (0.9, 0.85, 0.6)), (0.14, RED), (0.1, (0.9, 0.85, 0.6)), (0.05, GREEN), (0.02, RED)):
        p.append(cyl(col, (0.066 + (0.2 - r) * 0.01, 0, 1.73), r, 0.005, axis='x', segments=20))
    return p, []


def desk():
    """Office desk: the monitor at the back, whoever works at it sits at +X."""
    p = [box(WHITE, (0, 0, 0.73), (0.8, 1.6, 0.05), bevel=0.01),
         box(METAL, (0, -0.75, 0.36), (0.7, 0.05, 0.72), bevel=0.0),
         box(METAL, (0, 0.75, 0.36), (0.7, 0.05, 0.72), bevel=0.0),
         box(PAINT, (-0.2, 0.5, 0.36), (0.4, 0.45, 0.7), bevel=0.02, tint=True),        # drawers
         box(DARK, (-0.25, 0, 0.78), (0.2, 0.25, 0.04), bevel=0.01),
         box(DARK, (-0.25, 0, 0.9), (0.04, 0.05, 0.2), bevel=0.0),
         box(DARK, (-0.24, 0, 1.12), (0.04, 0.62, 0.38), bevel=0.01),
         box(SCREEN, (-0.215, 0, 1.12), (0.01, 0.56, 0.32), bevel=0.0, material=GLOW),
         box(DARK, (0.1, 0, 0.77), (0.18, 0.5, 0.02), bevel=0.005),
         box(WHITE, (0.05, -0.55, 0.77), (0.3, 0.22, 0.02), bevel=0.0, rot=(0, 0, 12)),
         cyl(RED, (0.15, 0.6, 0.8), 0.04, 0.1, segments=8)]
    return p, [((0, 0, 0.4), (0.8, 1.6, 0.8))]


def office_chair():
    top = SEAT + 0.02
    p = [cyl(DARK, (0, 0, 0.05), 0.3, 0.05, segments=5),
         cyl(CHROME, (0, 0, (top - 0.1) / 2 + 0.04), 0.03, top - 0.1, segments=8),
         box(PAINT, (0, 0, top - 0.05), (0.5, 0.5, 0.1), bevel=0.04, tint=True),
         box(PAINT, (-0.23, 0, top + 0.3), (0.08, 0.46, 0.6), bevel=0.04, tint=True)]
    return p, [((0, 0, (top + 0.6) / 2), (0.5, 0.5, top + 0.6))]


def filing_cabinet():
    p = [box(METAL, (0.3, 0, 0.65), (0.6, 0.5, 1.3), bevel=0.02)]
    for i in range(3):
        z = 0.25 + i * 0.4
        p += [box(TRIM, (0.605, 0, z + 0.18), (0.01, 0.46, 0.01), bevel=0.0),
              box(CHROME, (0.62, 0, z), (0.02, 0.16, 0.04), bevel=0.0)]
    return p, [((0.3, 0, 0.65), (0.6, 0.5, 1.3))]


def water_cooler():
    return [box(WHITE, (0.2, 0, 0.5), (0.35, 0.35, 1.0), bevel=0.03),
            cyl((0.55, 0.78, 0.95), (0.2, 0, 1.25), 0.15, 0.45, segments=12),
            box(BLUE, (0.39, 0.06, 0.8), (0.03, 0.05, 0.06), bevel=0.0),
            box(RED, (0.39, -0.06, 0.8), (0.03, 0.05, 0.06), bevel=0.0)], [((0.2, 0, 0.7), (0.4, 0.4, 1.4))]


def plant():
    return [cyl(TERRACOTTA, (0, 0, 0.25), 0.22, 0.5, segments=12),
            ball(LEAVES, (0, 0, 0.8), (0.6, 0.6, 0.7)),
            ball(LEAVES_DARK, (0.12, 0.1, 1.05), (0.4, 0.4, 0.45)),
            ball(LEAVES, (-0.1, -0.08, 1.15), (0.3, 0.3, 0.35))], [((0, 0, 0.5), (0.5, 0.5, 1.0))]


def reception_desk():
    p = [box(PAINT, (0.1, 0, 0.6), (0.2, 2.4, 1.2), bevel=0.03, tint=True),
         box(WOOD_LIGHT, (0.12, 0, 1.22), (0.3, 2.5, 0.05), bevel=0.02),
         box(WHITE, (-0.3, 0, 0.74), (0.7, 2.4, 0.05), bevel=0.01),
         box(DARK, (-0.45, 0.4, 0.98), (0.04, 0.5, 0.32), bevel=0.01),
         box(SCREEN, (-0.425, 0.4, 0.98), (0.01, 0.45, 0.27), bevel=0.0, material=GLOW),
         ball(GOLD, (0.15, -0.8, 1.29), (0.1, 0.1, 0.07))]                                       # the bell
    return p, [((-0.1, 0, 0.6), (0.8, 2.5, 1.2))]


def sofa():
    top = SEAT + 0.02                       # cushion top
    p = [box(PAINT, (0, 0, 0.15), (0.9, 2.2, 0.14), bevel=0.05, tint=True),
         box(PAINT, (-0.33, 0, 0.525), (0.24, 2.2, 0.65), bevel=0.08, tint=True),
         box(PAINT, (0, -1.0, 0.325), (0.9, 0.22, 0.45), bevel=0.08, tint=True),
         box(PAINT, (0, 1.0, 0.325), (0.9, 0.22, 0.45), bevel=0.08, tint=True)]
    for y in (-0.45, 0.45):
        p.append(box(PAINT, (0.05, y, top - 0.065), (0.75, 0.86, 0.13), bevel=0.06, tint=True))
    for sx in (1, -1):
        for sy in (1, -1):
            p.append(cyl(WOOD_DARK, (sx * 0.35, sy * 1.0, 0.04), 0.04, 0.08, segments=8))
    p.append(box(YELLOW, (-0.15, 0.6, top + 0.2), (0.12, 0.4, 0.35), bevel=0.06, rot=(0, -15, 0)))  # a cushion
    return p, [((0, 0, 0.43), (0.9, 2.2, 0.86))]


def armchair():
    top = SEAT + 0.02
    p = [box(PAINT, (0, 0, 0.13), (0.9, 0.9, 0.14), bevel=0.05, tint=True),
         box(PAINT, (-0.33, 0, 0.54), (0.24, 0.9, 0.68), bevel=0.08, tint=True),
         box(PAINT, (0, -0.4, 0.34), (0.9, 0.18, 0.48), bevel=0.07, tint=True),
         box(PAINT, (0, 0.4, 0.34), (0.9, 0.18, 0.48), bevel=0.07, tint=True),
         box(PAINT, (0.05, 0, top - 0.07), (0.75, 0.6, 0.14), bevel=0.06, tint=True)]
    return p, [((0, 0, 0.44), (0.9, 0.9, 0.88))]


def tv_stand():
    return [box(WOOD_DARK, (0.23, 0, 0.25), (0.45, 1.6, 0.5), bevel=0.02),
            box(DARK, (0.2, 0, 0.53), (0.25, 0.4, 0.06), bevel=0.01),
            box(DARK, (0.2, 0, 0.62), (0.06, 0.1, 0.14), bevel=0.0),
            box(DARK, (0.2, 0, 1.05), (0.07, 1.3, 0.78), bevel=0.02),
            box(SCREEN, (0.24, 0, 1.05), (0.01, 1.2, 0.68), bevel=0.0, material=GLOW),
            box(BLACK, (0.3, 0.55, 0.52), (0.12, 0.05, 0.02), bevel=0.0)], [((0.23, 0, 0.7), (0.5, 1.6, 1.4))]


def coffee_table():
    return [box(WOOD, (0, 0, 0.38), (0.6, 1.1, 0.05), bevel=0.02),
            box(WOOD_DARK, (0, -0.45, 0.18), (0.5, 0.06, 0.36), bevel=0.0),
            box(WOOD_DARK, (0, 0.45, 0.18), (0.5, 0.06, 0.36), bevel=0.0),
            cyl(WHITE, (0.1, 0.25, 0.45), 0.05, 0.1, segments=8),
            box(RED, (-0.1, -0.2, 0.42), (0.25, 0.18, 0.03), bevel=0.0, rot=(0, 0, 20))], [((0, 0, 0.2), (0.6, 1.1, 0.4))]


def rug():
    return [box(PAINT, (0, 0, 0.006), (1.6, 2.4, 0.012), bevel=0.0, tint=True),
            box(WHITE, (0, 0, 0.0125), (1.3, 2.1, 0.002), bevel=0.0),
            box(PAINT, (0, 0, 0.0135), (1.2, 2.0, 0.002), bevel=0.0, tint=True)], []


def floor_lamp():
    return [cyl(DARK, (0, 0, 0.02), 0.18, 0.04, segments=12), cyl(CHROME, (0, 0, 0.8), 0.02, 1.6, segments=8),
            cone(WARM_LIGHT, (0, 0, 1.68), 0.24, 0.32, rot=(180, 0, 0), segments=14, material=GLOW)], \
        [((0, 0, 0.8), (0.3, 0.3, 1.6))]


def kitchen_counter():
    p = [box(WHITE, (0.32, 0, 0.44), (0.62, 2.4, 0.88), bevel=0.02),
         box(DARK, (0.33, 0, 0.9), (0.66, 2.44, 0.05), bevel=0.01),
         box(CHROME, (0.3, 0.6, 0.91), (0.45, 0.6, 0.02), bevel=0.0),
         box(DARK, (0.3, 0.6, 0.905), (0.38, 0.5, 0.02), bevel=0.0),
         cyl(CHROME, (0.1, 0.6, 1.05), 0.02, 0.28, segments=8),
         box(BLACK, (0.32, -0.6, 0.92), (0.55, 0.6, 0.02), bevel=0.0),
         box(WHITE, (0.18, 0, 1.85), (0.35, 2.4, 0.7), bevel=0.02)]
    for y in (-0.8, -0.4, 0.0, 0.4, 0.8):
        p.append(box(CHROME, (0.64, y, 0.7), (0.02, 0.12, 0.02), bevel=0.0))
    for y, x in ((-0.75, 0.2), (-0.45, 0.2), (-0.75, 0.45), (-0.45, 0.45)):
        p.append(ring(TRIM, (x, y, 0.935), 0.1, 0.012, segments=16, rings=6))
    return p, [((0.32, 0, 0.45), (0.66, 2.44, 0.9))]


def fridge():
    return [box(WHITE, (0.35, 0, 0.95), (0.7, 0.8, 1.9), bevel=0.05),
            box(TRIM, (0.705, 0, 1.25), (0.01, 0.78, 0.01), bevel=0.0),
            box(CHROME, (0.72, 0.33, 1.55), (0.03, 0.04, 0.4), bevel=0.0),
            box(CHROME, (0.72, 0.33, 0.9), (0.03, 0.04, 0.4), bevel=0.0),
            box(RED, (0.71, -0.1, 1.6), (0.01, 0.08, 0.08), bevel=0.0),
            box(BLUE, (0.71, 0.05, 1.45), (0.01, 0.1, 0.06), bevel=0.0)], [((0.35, 0, 0.95), (0.72, 0.82, 1.9))]


def dining_table():
    p = [box(WOOD, (0, 0, 0.74), (0.9, 1.6, 0.05), bevel=0.02)]
    for sx in (1, -1):
        for sy in (1, -1):
            p.append(box(WOOD_DARK, (sx * 0.38, sy * 0.72, 0.36), (0.06, 0.06, 0.72), bevel=0.0))
    for y in (-0.4, 0.4):
        for side, x in ((1, -0.7), (-1, 0.7)):
            fr = Frame((x, y, 0), (0, 0, 0 if side > 0 else 180))
            for part in chair_parts(fr):
                p.append(part)
    p.append(cyl(WHITE, (0, -0.3, 0.8), 0.12, 0.02, segments=12))
    p.append(cyl(WHITE, (0, 0.3, 0.8), 0.12, 0.02, segments=12))
    return p, [((0, 0, 0.45), (1.9, 1.7, 0.9))]


def chair_parts(fr):
    """A chair at a frame (for sets like the dining table)."""
    leg = SEAT - 0.05
    p = [box(WOOD, fr.at((0, 0, SEAT - 0.025)), (0.42, 0.42, 0.05), rot=fr.rot, bevel=0.01),
         box(WOOD, fr.at((-0.19, 0, SEAT + 0.28)), (0.05, 0.4, 0.55), rot=fr.rot, bevel=0.01)]
    for sx in (1, -1):
        for sy in (1, -1):
            p.append(box(WOOD_DARK, fr.at((sx * 0.17, sy * 0.17, leg / 2)), (0.04, 0.04, leg), rot=fr.rot, bevel=0.0))
    return p


def bookshelf():
    rng = random.Random("books")
    p = cabinet(WOOD, 0.34, 1.0, 2.0)
    for z in (0.1, 0.48, 0.86, 1.24, 1.6):
        p.append(box(WOOD_DARK, (0.2, 0, z), (0.3, 0.9, 0.03), bevel=0.0))
        y = -0.42
        while y < 0.38:
            w = rng.uniform(0.04, 0.08)
            h = rng.uniform(0.22, 0.33)
            p.append(box(rng.choice(PRODUCT_COLOURS), (0.22, y + w / 2, z + 0.015 + h / 2), (0.22, w * 0.9, h), bevel=0.0))
            y += w
    return p, [((0.17, 0, 1.0), (0.34, 1.0, 2.0))]


def pallet_rack():
    """Double-sided warehouse racking, long side along Y."""
    rng = random.Random("rack")
    p = []
    for y in (-1.4, 0.0, 1.4):
        for x in (-0.5, 0.5):
            p.append(box(ORANGE, (x, y, 1.8), (0.08, 0.08, 3.6), bevel=0.0))
    for z in (0.15, 1.35, 2.55):
        for x in (-0.5, 0.5):
            p.append(box(BLUE, (x, 0, z), (0.1, 2.9, 0.12), bevel=0.0))
        for y in (-0.7, 0.7):
            if rng.random() < 0.85:
                p.append(box(WOOD_LIGHT, (0, y, z + 0.1), (1.0, 1.1, 0.12), bevel=0.0))
                h = rng.uniform(0.5, 0.9)
                col = CARDBOARD if rng.random() < 0.7 else rng.choice(PRODUCT_COLOURS)
                p.append(box(col, (0, y, z + 0.16 + h / 2), (0.9, 1.0, h), bevel=0.02))
    return p, [((0, 0, 1.8), (1.1, 2.9, 3.6))]


def crate():
    p = [box(WOOD_LIGHT, (0, 0, 0.5), (1.0, 1.0, 1.0), bevel=0.02)]
    for z in (0.05, 0.95):
        for side in (1, -1):
            p.append(box(WOOD, (side * 0.505, 0, z), (0.02, 1.0, 0.1), bevel=0.0))
            p.append(box(WOOD, (0, side * 0.505, z), (1.0, 0.02, 0.1), bevel=0.0))
    for side in (1, -1):
        p.append(box(WOOD, (side * 0.51, 0, 0.5), (0.02, 1.0, 0.1), bevel=0.0, rot=(45, 0, 0)))
    return p, [((0, 0, 0.5), (1.0, 1.0, 1.0))]


def pallet():
    p = [box(WOOD_LIGHT, (0, 0, 0.07), (1.2, 1.0, 0.14), bevel=0.01)]
    for i, (x, y, z, s) in enumerate(((-0.28, -0.24, 0.14, 0.5), (0.28, -0.24, 0.14, 0.5), (-0.28, 0.24, 0.14, 0.45),
                                      (0.28, 0.24, 0.14, 0.5), (0.0, 0.0, 0.64, 0.42))):
        p.append(box(CARDBOARD, (x, y, z + s / 2), (s, s * 0.9, s), bevel=0.015))
        p.append(box((0.8, 0.7, 0.5), (x, y, z + s + 0.002), (s * 0.95, 0.06, 0.004), bevel=0.0))
    return p, [((0, 0, 0.55), (1.2, 1.0, 1.1))]


def forklift():
    """Cartoon forklift, forks toward +X."""
    p = [box(YELLOW, (-0.3, 0, 0.6), (1.6, 1.1, 0.7), bevel=0.08),
         box(YELLOW, (-0.9, 0, 0.9), (0.5, 1.1, 0.8), bevel=0.08),                        # counterweight
         box(DARK, (-0.3, 0, 1.0), (0.5, 0.6, 0.12), bevel=0.03),                          # seat
         box(DARK, (-0.5, 0, 1.3), (0.1, 0.6, 0.5), bevel=0.03),
         box(DARK, (0.3, 0, 1.2), (0.2, 0.5, 0.35), bevel=0.03)]                           # steering column
    for x in (-0.15, 0.55):
        for y in (-0.56, 0.56):
            p.append(cyl(BLACK, (x, y, 0.3), 0.3, 0.22, axis='y', segments=14))
    for y in (-0.5, 0.5):
        p.append(box(DARK, (0.3, y, 1.6), (0.06, 0.06, 1.4), bevel=0.0))                   # overhead guard
        p.append(box(DARK, (-0.6, y, 1.6), (0.06, 0.06, 1.2), bevel=0.0))
        p.append(box(DARK, (0.65, y * 0.8, 1.2), (0.08, 0.08, 2.2), bevel=0.0))            # mast
        p.append(box(METAL, (1.15, y * 0.5, 0.1), (1.0, 0.12, 0.05), bevel=0.0))            # forks
    p.append(box(DARK, (-0.15, 0, 2.3), (0.95, 1.05, 0.05), bevel=0.0))
    p.append(box(DARK, (0.7, 0, 0.5), (0.06, 0.9, 0.8), bevel=0.0))
    return p, [((0.0, 0, 1.1), (2.3, 1.3, 2.2))]


def barrel():
    return [cyl(BLUE, (0, 0, 0.45), 0.3, 0.9, segments=16), ring(DARK, (0, 0, 0.3), 0.31, 0.02, segments=20, rings=6),
            ring(DARK, (0, 0, 0.6), 0.31, 0.02, segments=20, rings=6), cyl(DARK, (0, 0, 0.9), 0.28, 0.02, segments=16)], \
        [((0, 0, 0.45), (0.6, 0.6, 0.9))]


def teller_counter():
    """Bank counter with glass screens; tellers behind (-X), customers in front (+X)."""
    p = [box((0.86, 0.84, 0.80), (0, 0, 0.55), (0.8, 3.0, 1.1), bevel=0.03),
         box(WOOD_DARK, (0, 0, 1.13), (0.9, 3.1, 0.06), bevel=0.02)]
    for y in (-1.5, -0.5, 0.5, 1.5):
        p.append(box(GOLD, (0.1, y, 1.55), (0.06, 0.06, 0.8), bevel=0.0))
    for y in (-1.0, 0.0, 1.0):
        p.append(box(GLASS_TINT, (0.1, y, 1.6), (0.02, 0.94, 0.7), bevel=0.0, material=GLASS))
        p.append(box(DARK, (0.1, y, 1.2), (0.02, 0.3, 0.1), bevel=0.0))
    p.append(box(GOLD, (0.1, 0, 1.98), (0.08, 3.0, 0.06), bevel=0.0))
    return p, [((0, 0, 1.0), (0.9, 3.1, 2.0))]


def vault_wall():
    """
    The vault: a thick wall two panels wide with the great round door swung open toward +X (the banking hall
    side). Pivot on the hall-side face like other walls; the wall runs back to x = -0.8.
    """
    width, height, t = 2 * P, G, 0.8
    y0, y1, z0, z1 = -0.8, 0.8, 0.0, 2.4
    h = width / 2
    p = [box(METAL, (-t / 2, (-h + y0) / 2, height / 2), (t, y0 + h, height), bevel=0.03),
         box(METAL, (-t / 2, (y1 + h) / 2, height / 2), (t, h - y1, height), bevel=0.03),
         box(METAL, (-t / 2, 0, (z1 + height) / 2), (t, y1 - y0, height - z1), bevel=0.03),
         ring(CHROME, (0.03, 0, 1.3), 1.25, 0.09, rot=(0, 90, 0), segments=32, rings=8)]
    door = Frame((0.03, 1.25, 1.3), (0, 0, 100))                  # hinged on the right, swung open
    p += [cyl(CHROME, door.at((0, -1.2, 0)), 1.15, 0.45, rot=(0, 90, 100), segments=32),
          cyl(METAL, door.at((0.24, -1.2, 0)), 0.95, 0.04, rot=(0, 90, 100), segments=28),
          ring(DARK, door.at((0.3, -1.2, 0)), 0.45, 0.04, rot=(0, 90, 100), segments=20, rings=6),
          cyl(DARK, door.at((0.3, -1.2, 0)), 0.06, 0.12, rot=(0, 90, 100), segments=10)]
    for a in range(0, 360, 45):
        r = math.radians(a)
        p.append(cyl(DARK, door.at((-0.15, -1.2 + math.cos(r) * 1.0, math.sin(r) * 1.0)), 0.06, 0.22,
                     rot=(0, 90, 100), segments=8))
    return p, [((-t / 2, (-h + y0) / 2, height / 2), (t, y0 + h, height)),
               ((-t / 2, (y1 + h) / 2, height / 2), (t, h - y1, height)),
               ((-t / 2, 0, (z1 + height) / 2), (t, y1 - y0, height - z1)),
               (tuple(door.at((0, -1.2, 0))), (2.2, 0.5, 2.3))]                  # the open door, edge on


def vault_shelf():
    p = cabinet(METAL, 0.5, 2.0, 2.0)
    for z in (0.45, 1.05, 1.65):
        p.append(box(METAL, (0.28, 0, z), (0.46, 1.95, 0.04), bevel=0.0))
    for i, y in enumerate((-0.7, -0.2, 0.3, 0.75)):
        p.append(ball(MONEY, (0.3, y, 0.62), (0.35, 0.35, 0.34)))
        p.append(lettering("$", (0.2, 0.45, 0.2), (0.48, y, 0.62), '+x', 0.18))
    for i in range(5):
        p.append(prism(GOLD, [(0.08, 1.07), (0.48, 1.07), (0.44, 1.17), (0.12, 1.17)], -0.9 + i * 0.36, -0.62 + i * 0.36))
    for i, y in enumerate((-0.6, 0.0, 0.6)):
        p.append(ball(MONEY, (0.3, y, 1.82), (0.32, 0.32, 0.3)))
    return p, [((0.25, 0, 1.0), (0.5, 2.0, 2.0))]


def queue_post():
    return [cyl(CHROME, (0, 0, 0.02), 0.16, 0.04, segments=12), cyl(CHROME, (0, 0, 0.47), 0.03, 0.94, segments=8),
            ball(CHROME, (0, 0, 0.96), 0.07),
            beam(RED, (0, 0.03, 0.88), (0, 0.6, 0.72), 0.03, 0.03, bevel=0.0),
            beam(RED, (0, 0.6, 0.72), (0, 1.17, 0.88), 0.03, 0.03, bevel=0.0)], []


def bench():
    """Slatted bench; whoever sits on it faces +X."""
    top = SEAT + 0.02
    p = []
    for i in range(3):
        p.append(box(WOOD, (-0.12 + i * 0.13, 0, top - 0.02), (0.11, 1.8, 0.04), bevel=0.01))
    for i in range(2):
        p.append(box(WOOD, (-0.22, 0, top + 0.17 + i * 0.14), (0.04, 1.8, 0.1), bevel=0.01, rot=(0, -12, 0)))
    for y in (-0.75, 0.75):
        p += [box(DARK, (0, y, top - 0.08), (0.45, 0.06, 0.06), bevel=0.0),
              box(DARK, (-0.18, y, (top + 0.45) / 2), (0.05, 0.06, top + 0.45), bevel=0.0),
              box(DARK, (0.15, y, (top - 0.04) / 2), (0.05, 0.06, top - 0.04), bevel=0.0)]
    return p, [((0, 0, (top + 0.45) / 2), (0.5, 1.8, top + 0.45))]


def front_desk():
    """Precinct reception: a tall desk with POLICE across the front; the desk sergeant sits behind (-X)."""
    p = [box(POLICE_BLUE, (0.1, 0, 0.6), (0.2, 4.0, 1.2), bevel=0.04),
         box(WOOD_LIGHT, (0.1, 0, 1.23), (0.34, 4.1, 0.06), bevel=0.02),
         box(WHITE, (-0.35, 0, 0.76), (0.7, 4.0, 0.05), bevel=0.01),
         lettering("POLICE", WHITE, (0.21, 0.35, 0.72), '+x', 0.32),
         box(DARK, (-0.5, 1.2, 1.0), (0.04, 0.55, 0.34), bevel=0.01),
         box(SCREEN, (-0.475, 1.2, 1.0), (0.01, 0.5, 0.29), bevel=0.0, material=GLOW),
         ball(GOLD, (0.15, -1.5, 1.3), (0.1, 0.1, 0.07))]
    p += star_facing_x(GOLD, 0.2, -1.35, 0.72, 0.2, 0.085)
    return p, [((-0.1, 0, 0.6), (0.9, 4.1, 1.2))]


def star_facing_x(color, x, cy, cz, r_out, r_in, depth=0.02):
    """A star on a +X face at x: star() builds in the XZ plane, so swing it round a quarter turn."""
    parts = star(color, cy, cz, -x - depth, -x, r_out=r_out, r_in=r_in)
    quarter = Matrix.Rotation(math.radians(90), 4, 'Z')  # (x, y, z) -> (-y, x, z)
    for part in parts:
        bmesh.ops.transform(part.bm, matrix=quarter, verts=part.bm.verts)
    return parts


def briefing_chair():
    leg = SEAT - 0.05
    p = [box(BLUE, (0, 0, SEAT - 0.025), (0.45, 0.45, 0.05), bevel=0.02),
         box(BLUE, (-0.2, 0, SEAT + 0.28), (0.05, 0.42, 0.45), bevel=0.02),
         box(WOOD_LIGHT, (0.1, 0.28, SEAT + 0.3), (0.4, 0.25, 0.03), bevel=0.01)]     # writing tablet
    for sx in (1, -1):
        for sy in (1, -1):
            p.append(box(CHROME, (sx * 0.18, sy * 0.18, leg / 2), (0.03, 0.03, leg), bevel=0.0))
    return p, [((0, 0, (SEAT + 0.5) / 2), (0.5, 0.55, SEAT + 0.5))]


def whiteboard():
    p = [box(METAL, (0.03, 0, 1.6), (0.05, 2.5, 1.5), bevel=0.01),
         box(WHITE, (0.058, 0, 1.6), (0.01, 2.36, 1.36), bevel=0.0),
         box(METAL, (0.1, 0, 0.9), (0.1, 2.4, 0.04), bevel=0.0)]
    # A hand-drawn city map: roads, a big red X, arrows.
    for i in range(4):
        p.append(box(BLUE, (0.064, -0.9 + i * 0.3, 1.55), (0.004, 0.03, 0.9), bevel=0.0))
        p.append(box(BLUE, (0.064, -0.45, 1.2 + i * 0.22), (0.004, 1.0, 0.03), bevel=0.0))
    p.append(box(RED, (0.066, -0.3, 1.6), (0.004, 0.3, 0.04), bevel=0.0, rot=(45, 0, 0)))
    p.append(box(RED, (0.066, -0.3, 1.6), (0.004, 0.3, 0.04), bevel=0.0, rot=(-45, 0, 0)))
    p.append(lettering("DON'T PANIC", RED, (0.068, 0.62, 1.95), '+x', 0.14))
    p.append(lettering("donuts @ 3", BLUE, (0.068, 0.62, 1.55), '+x', 0.1))
    return p, []


def podium():
    p = [box(WOOD, (0, 0, 0.55), (0.5, 0.6, 1.1), bevel=0.03),
         box(WOOD_DARK, (0, 0, 1.15), (0.6, 0.7, 0.08), bevel=0.02, rot=(0, -12, 0))]
    p += star_facing_x(GOLD, 0.25, 0.0, 0.7, 0.14, 0.06)
    return p, [((0, 0, 0.6), (0.6, 0.7, 1.2))]


def lockers():
    p = [box(BLUE, (0.25, 0, 0.95), (0.5, 2.0, 1.9), bevel=0.02)]
    for i in range(5):
        y = -0.8 + i * 0.4
        p += [box(TRIM, (0.505, y + 0.2, 0.95), (0.01, 0.01, 1.8), bevel=0.0),
              box(CHROME, (0.515, y + 0.12, 1.0), (0.02, 0.03, 0.12), bevel=0.0)]
        for j in range(3):
            p.append(box(TRIM, (0.505, y, 1.6 + j * 0.05), (0.01, 0.22, 0.015), bevel=0.0))
    return p, [((0.25, 0, 0.95), (0.5, 2.0, 1.9))]


def long_gun(fr, kind):
    """A cartoon long gun standing upright in a rack frame (muzzle up)."""
    p = [box(WOOD, fr.at((0, 0, 0.18)), (0.05, 0.1, 0.36), rot=fr.rot, bevel=0.01),       # stock
         box(DARK, fr.at((0, 0, 0.46)), (0.06, 0.07, 0.22), rot=fr.rot, bevel=0.01)]       # receiver
    if kind == 'shotgun':
        p += [cyl(DARK, fr.at((0, 0.01, 0.85)), 0.018, 0.6, rot=fr.rot, segments=8),
              cyl(DARK, fr.at((0, -0.025, 0.75)), 0.016, 0.4, rot=fr.rot, segments=8),
              box(WOOD, fr.at((0, -0.02, 0.7)), (0.05, 0.05, 0.14), rot=fr.rot, bevel=0.01)]
    else:
        p += [cyl(DARK, fr.at((0, 0, 0.9)), 0.013, 0.7, rot=fr.rot, segments=8),
              box(DARK, fr.at((0, -0.05, 0.42)), (0.04, 0.05, 0.16), rot=fr.rot, bevel=0.0),
              box(DARK, fr.at((0, 0.03, 0.62)), (0.04, 0.04, 0.12), rot=fr.rot, bevel=0.0)]
    return p


def gun_rack():
    """The armory wall rack: long guns stood upright, pistols on pegs (the armory hands these out)."""
    p = [box(WOOD_DARK, (0.03, 0, 1.45), (0.06, 2.0, 1.5), bevel=0.02),
         box(WOOD, (0.15, 0, 0.72), (0.25, 2.0, 0.06), bevel=0.01),
         box(WOOD, (0.1, 0, 1.55), (0.12, 2.0, 0.05), bevel=0.01)]
    for i, kind in enumerate(('shotgun', 'rifle', 'shotgun', 'rifle')):
        p += long_gun(Frame((0.13, -0.75 + i * 0.3, 0.75), (0, 0, 0)), kind)
    for i in range(3):
        y = 0.35 + i * 0.25
        p += [cyl(CHROME, (0.08, y, 1.9), 0.01, 0.1, axis='x', segments=6),
              box(DARK, (0.12, y, 1.83), (0.04, 0.16, 0.08), bevel=0.01),
              box(DARK, (0.12, y + 0.05, 1.74), (0.04, 0.05, 0.12), bevel=0.01)]
    return p, [((0.15, 0, 1.2), (0.3, 2.0, 2.2))]


def ammo_crate():
    return [box(OLIVE, (0, 0, 0.22), (0.5, 0.8, 0.44), bevel=0.02),
            box((0.30, 0.34, 0.16), (0, 0, 0.45), (0.52, 0.82, 0.04), bevel=0.01),
            lettering("AMMO", YELLOW, (0.255, 0, 0.24), '+x', 0.14)], [((0, 0, 0.23), (0.5, 0.8, 0.46))]


def cell_bars(door=False):
    """Holding-cell front: a wall of bars, optionally with its door swung open (toward -X, into the cell)."""
    p = [box(DARK, (0, 0, 0.03), (0.1, P, 0.06), bevel=0.0), box(DARK, (0, 0, 3.55), (0.1, P, 0.1), bevel=0.0),
         box(DARK, (0, 0, 1.9), (0.06, P, 0.06), bevel=0.0)]
    gap = (-0.55, 0.55) if door else (9, 9)
    for i in range(14):
        y = -P / 2 + 0.07 + i * (P - 0.14) / 13
        if gap[0] < y < gap[1]:
            continue
        p.append(cyl(METAL, (0, y, 1.8), 0.025, 3.5, segments=8))
    col = [((0, 0, 1.8), (0.12, P, 3.6))]
    if door:
        fr = Frame((0, 0.55, 0), (0, 0, 0))                               # hinged, swung open into the cell
        p += [box(DARK, fr.at((-0.55, 0, 0.06)), (1.1, 0.06, 0.06), rot=fr.rot, bevel=0.0),
              box(DARK, fr.at((-0.55, 0, 2.4)), (1.1, 0.06, 0.06), rot=fr.rot, bevel=0.0),
              box(DARK, fr.at((-0.55, 0, 1.2)), (1.1, 0.06, 0.06), rot=fr.rot, bevel=0.0)]
        for i in range(6):
            p.append(cyl(METAL, fr.at((-0.1 - i * 0.18, 0, 1.23)), 0.022, 2.4, rot=fr.rot, segments=8))
        p.append(box(DARK, (0, 0, 2.47), (0.1, 1.2, 0.08), bevel=0.0))
        col = [((0, (-P / 2 - 0.55) / 2, 1.8), (0.12, P / 2 - 0.55, 3.6)),
               ((0, (P / 2 + 0.55) / 2, 1.8), (0.12, P / 2 - 0.55, 3.6)),
               ((0, 0, 3.0), (0.12, 1.1, 1.2)),
               (tuple(fr.at((-0.55, 0, 1.2))), (1.1, 0.1, 2.4))]
    return p, col


def cell_bench():
    """Steel bench bolted to the wall (x = 0); the prisoner sits with their back to it, facing +X."""
    top = SEAT + 0.02
    return [box(METAL, (0.25, 0, top - 0.03), (0.5, 1.8, 0.06), bevel=0.01),
            box(METAL, (0.25, -0.8, (top - 0.06) / 2), (0.45, 0.06, top - 0.06), bevel=0.0),
            box(METAL, (0.25, 0.8, (top - 0.06) / 2), (0.45, 0.06, top - 0.06), bevel=0.0)], \
        [((0.25, 0, top / 2), (0.5, 1.8, top))]


def toilet():
    return [box(CHROME, (0.2, 0, 0.4), (0.4, 0.45, 0.8), bevel=0.04),
            cyl(CHROME, (0.45, 0, 0.3), 0.2, 0.3, segments=14),
            ring(CHROME, (0.45, 0, 0.46), 0.2, 0.03, segments=16, rings=6)], [((0.3, 0, 0.35), (0.6, 0.45, 0.7))]


def coffee_station():
    p = [box(WOOD, (0.25, 0, 0.45), (0.5, 1.2, 0.9), bevel=0.02),
         box(DARK, (0.2, -0.3, 1.1), (0.3, 0.3, 0.4), bevel=0.03),
         cyl(GLASS_TINT, (0.3, -0.3, 0.98), 0.08, 0.16, segments=10, material=GLASS),
         box(RED, (0.36, -0.2, 1.2), (0.02, 0.03, 0.03), bevel=0.0, material=GLOW),
         box((1.0, 0.62, 0.78), (0.25, 0.3, 0.95), (0.35, 0.35, 0.08), bevel=0.01)]
    for i, (x, y) in enumerate(((0.18, 0.2), (0.32, 0.2), (0.18, 0.38), (0.32, 0.38))):
        p.append(ring((0.95, 0.55, 0.75) if i % 2 else (0.55, 0.32, 0.18), (x, y, 1.0), 0.06, 0.025, segments=12, rings=6))
    return p, [((0.25, 0, 0.5), (0.5, 1.2, 1.0))]


def wanted_board():
    p = [box(WOOD_DARK, (0.02, 0, 1.55), (0.04, 1.7, 1.1), bevel=0.01),
         box((0.72, 0.55, 0.36), (0.045, 0, 1.55), (0.01, 1.6, 1.0), bevel=0.0)]
    for i, (y, z) in enumerate(((-0.5, 1.75), (0.05, 1.8), (0.55, 1.7), (-0.3, 1.3), (0.35, 1.3))):
        p += [box(WHITE, (0.052, y, z), (0.005, 0.36, 0.44), bevel=0.0, rot=(4 - i * 3, 0, 0)),
              ball((0.9, 0.7, 0.55), (0.056, y, z + 0.03), (0.01, 0.16, 0.18)),
              box(BLACK, (0.057, y, z + 0.16), (0.004, 0.28, 0.06), bevel=0.0)]
    p.append(lettering("WANTED", RED, (0.06, 0.0, 2.02), '+x', 0.11))
    return p, []


def doormat():
    return [box(PAINT, (0, 0, 0.008), (0.7, 1.2, 0.016), bevel=0.004, tint=True),
            fb.make_text("HI!", (0.95, 0.9, 0.8), loc=(0, 0, 0.017), rot=(0, 0, 90), size=0.25, depth=0.004)], []

# --------------------------------------------------------------------------------------
# Street
# --------------------------------------------------------------------------------------
def lamp_post():
    """Street light: pole at the origin, the arm reaching out over the road (+X)."""
    return [cyl(DARK, (0, 0, 0.3), 0.14, 0.6, segments=12),
            cyl(DARK, (0, 0, 3.2), 0.07, 5.6, segments=10),
            beam(DARK, (0, 0, 5.9), (0.6, 0, 6.25), 0.08, 0.08, bevel=0.01),
            beam(DARK, (0.6, 0, 6.25), (1.3, 0, 6.3), 0.08, 0.08, bevel=0.01),
            box(DARK, (1.45, 0, 6.25), (0.6, 0.35, 0.15), bevel=0.05),
            box(WARM_LIGHT, (1.45, 0, 6.17), (0.5, 0.28, 0.03), bevel=0.0, material=GLOW)], \
        [((0, 0, 3.0), (0.3, 0.3, 6.0))]


def traffic_light():
    """Corner signal: the pole at the origin, the arm out along +Y, the lamps facing +X."""
    p = [cyl(DARK, (0, 0, 0.2), 0.16, 0.4, segments=12),
         cyl(YELLOW, (0, 0, 2.6), 0.09, 5.2, segments=10),
         beam(YELLOW, (0, 0, 5.0), (0, 3.6, 5.0), 0.1, 0.1, bevel=0.01),
         box(DARK, (0.0, 3.4, 4.45), (0.36, 0.36, 1.05), bevel=0.04),
         box(DARK, (0.0, 0.05, 2.9), (0.3, 0.3, 0.8), bevel=0.04)]                         # pedestrian signal
    for i, col in enumerate(((0.95, 0.15, 0.1), (1.0, 0.7, 0.1), (0.2, 0.9, 0.3))):
        p.append(cyl(col, (0.19, 3.4, 4.8 - i * 0.33), 0.12, 0.03, axis='x', segments=14))
        p.append(box(DARK, (0.24, 3.4, 4.9 - i * 0.33), (0.1, 0.3, 0.03), bevel=0.0))       # visor
    p.append(box(WHITE, (0.16, 0.05, 2.9), (0.01, 0.2, 0.2), bevel=0.0))
    return p, [((0, 0, 2.6), (0.3, 0.3, 5.2))]


def hydrant():
    return [cyl(RED, (0, 0, 0.35), 0.13, 0.6, segments=12), ball(RED, (0, 0, 0.66), (0.26, 0.26, 0.2)),
            cyl(RED, (0, 0, 0.78), 0.04, 0.08, segments=8), cyl(RED, (0, 0, 0.04), 0.18, 0.08, segments=12),
            cyl(CHROME, (0, 0, 0.45), 0.05, 0.4, axis='y', segments=8),
            cyl(CHROME, (0.14, 0, 0.45), 0.06, 0.1, axis='x', segments=8)], [((0, 0, 0.4), (0.35, 0.35, 0.8))]


def street_bench():
    p, col = bench()
    return p, col


def bin_():
    return [cyl(GREEN, (0, 0, 0.45), 0.28, 0.9, segments=14), cyl(DARK, (0, 0, 0.93), 0.3, 0.08, segments=14),
            ring(DARK, (0, 0, 0.55), 0.29, 0.02, segments=18, rings=6), cyl(BLACK, (0, 0, 0.98), 0.12, 0.04, segments=10)], \
        [((0, 0, 0.45), (0.6, 0.6, 0.9))]


def bus_stop():
    """Shelter open toward the road (+X): glass back and sides, a bench, and the BUS sign."""
    p = [box(DARK, (0.0, 0, 2.55), (1.5, 3.2, 0.1), bevel=0.02),
         box(GLASS_TINT, (-0.65, 0, 1.35), (0.02, 3.0, 2.3), bevel=0.0, material=GLASS),
         box(GLASS_TINT, (0, -1.55, 1.35), (1.3, 0.02, 2.3), bevel=0.0, material=GLASS),
         box(GLASS_TINT, (0, 1.55, 1.35), (1.3, 0.02, 2.3), bevel=0.0, material=GLASS),
         box(YELLOW, (0.7, 0, 2.63), (0.06, 3.2, 0.12), bevel=0.0)]
    for x in (-0.65, 0.65):
        for y in (-1.55, 1.55):
            p.append(box(DARK, (x, y, 1.27), (0.08, 0.08, 2.55), bevel=0.0))
    for i in range(3):
        p.append(box(WOOD, (-0.35 + i * 0.12, 0, SEAT), (0.1, 2.0, 0.04), bevel=0.01))
    for y in (-0.9, 0.9):
        p.append(box(DARK, (-0.22, y, (SEAT - 0.02) / 2), (0.4, 0.06, SEAT - 0.02), bevel=0.0))
    p += [cyl(DARK, (0.9, 1.35, 1.4), 0.04, 2.8, segments=8),
          cyl(YELLOW, (0.9, 1.35, 2.85), 0.3, 0.04, axis='x', segments=16),
          lettering("BUS", DARK, (0.93, 1.35, 2.85), '+x', 0.2),
          lettering("BUS", DARK, (0.87, 1.35, 2.85), '-x', 0.2)]
    return p, [((-0.65, 0, 1.3), (0.1, 3.2, 2.6)), ((0, -1.55, 1.3), (1.4, 0.1, 2.6)), ((0, 1.55, 1.3), (1.4, 0.1, 2.6)),
               ((-0.22, 0, (SEAT + 0.02) / 2), (0.45, 2.0, SEAT + 0.02))]


def mailbox():
    return [box(BLUE, (0, 0, 0.75), (0.5, 0.5, 0.8), bevel=0.04),
            cyl(BLUE, (0, 0, 1.15), 0.25, 0.5, axis='y', segments=16),
            box(DARK, (0.26, 0, 1.05), (0.02, 0.3, 0.05), bevel=0.0),
            box(WHITE, (0.255, 0, 0.8), (0.01, 0.26, 0.14), bevel=0.0)]\
        + [box(BLUE, (sx * 0.2, sy * 0.2, 0.18), (0.06, 0.06, 0.36), bevel=0.0) for sx in (1, -1) for sy in (1, -1)], \
        [((0, 0, 0.7), (0.5, 0.5, 1.4))]


def news_box():
    return [box(RED, (0, 0, 0.8), (0.4, 0.5, 0.6), bevel=0.03),
            box(GLASS_TINT, (0.205, 0, 0.9), (0.01, 0.36, 0.3), bevel=0.0, material=GLASS),
            box(WHITE, (0.19, 0, 0.9), (0.01, 0.34, 0.28), bevel=0.0),
            box(DARK, (0, 0, 0.25), (0.08, 0.08, 0.5), bevel=0.0),
            box(DARK, (0, 0, 0.02), (0.3, 0.3, 0.04), bevel=0.0)], [((0, 0, 0.55), (0.4, 0.5, 1.1))]


def planter():
    return [box(CONCRETE, (0, 0, 0.25), (1.2, 1.2, 0.5), bevel=0.05),
            box(TRUNK, (0, 0, 0.48), (1.05, 1.05, 0.04), bevel=0.0),
            ball(LEAVES, (0, 0, 0.8), (0.9, 0.9, 0.7)), ball(LEAVES_DARK, (0.25, 0.2, 0.95), (0.5, 0.5, 0.45)),
            ball((1.0, 0.45, 0.6), (-0.2, 0.25, 1.0), 0.12), ball(YELLOW, (0.15, -0.3, 1.0), 0.1)], \
        [((0, 0, 0.3), (1.2, 1.2, 0.6))]


def parking_meter():
    return [cyl(METAL, (0, 0, 0.55), 0.035, 1.1, segments=8),
            box(METAL, (0, 0, 1.25), (0.2, 0.22, 0.35), bevel=0.05),
            box(GLASS_TINT, (0.1, 0, 1.3), (0.01, 0.14, 0.12), bevel=0.0, material=GLASS),
            box(RED, (0.1, 0, 1.3), (0.005, 0.1, 0.05), bevel=0.0)], [((0, 0, 0.7), (0.25, 0.25, 1.4))]


def curb():
    return [box(CONCRETE, (-0.12, 0, 0.1), (0.24, P, 0.2), bevel=0.04)], []


def tree_round():
    return [cone(TRUNK, (0, 0, 1.4), 0.2, 2.8, segments=10),
            ball(LEAVES, (0, 0, 3.4), (2.4, 2.4, 2.1), segments=16, rings=10),
            ball(LEAVES_DARK, (0.6, 0.4, 3.0), (1.5, 1.5, 1.3)),
            ball(LEAVES, (-0.6, -0.3, 3.1), (1.6, 1.6, 1.4)),
            ball(LEAVES, (0.1, -0.5, 4.2), (1.4, 1.4, 1.2))], [((0, 0, 1.2), (0.4, 0.4, 2.4))]


def tree_tall():
    return [cone(TRUNK, (0, 0, 2.0), 0.18, 4.0, segments=10),
            ball(LEAVES_DARK, (0, 0, 3.0), (1.8, 1.8, 1.6)),
            ball(LEAVES, (0, 0, 4.2), (1.6, 1.6, 1.5)),
            ball(LEAVES, (0, 0, 5.2), (1.2, 1.2, 1.2))], [((0, 0, 1.2), (0.36, 0.36, 2.4))]


def tree_pine():
    return [cyl(TRUNK, (0, 0, 0.6), 0.15, 1.2, segments=8),
            cone(LEAVES_DARK, (0, 0, 1.9), 1.4, 2.0, segments=12),
            cone(LEAVES_DARK, (0, 0, 3.0), 1.1, 1.8, segments=12),
            cone(LEAVES, (0, 0, 4.0), 0.8, 1.6, segments=12)], [((0, 0, 1.0), (0.3, 0.3, 2.0))]


def bush():
    return [ball(LEAVES, (0, 0, 0.45), (1.2, 1.1, 0.9)), ball(LEAVES_DARK, (0.4, 0.2, 0.4), (0.7, 0.7, 0.6)),
            ball(LEAVES, (-0.35, -0.2, 0.45), (0.8, 0.8, 0.65))], [((0, 0, 0.4), (1.2, 1.1, 0.8))]


# --------------------------------------------------------------------------------------
# Registry
# --------------------------------------------------------------------------------------
KIT = {
    # Building shell
    "SM_Wall_G_Plain": wall_g_plain, "SM_Wall_G_Window": wall_g_window, "SM_Wall_G_Window_Glass": wall_g_window_glass,
    "SM_Wall_G_Door": wall_g_door, "SM_Wall_G_Shop": wall_g_shop, "SM_Wall_G_Shop_Glass": wall_g_shop_glass,
    "SM_Wall_G_ShopDoor": wall_g_shopdoor, "SM_Wall_G_Roller": wall_g_roller,
    "SM_Wall_U_Plain": wall_u_plain, "SM_Wall_U_Window": wall_u_window, "SM_Wall_U_Wide": wall_u_wide,
    "SM_IWall_Plain": iwall_plain, "SM_IWall_Door": iwall_door,
    "SM_Corner_G": corner_g, "SM_Corner_U": corner_u, "SM_Parapet": parapet, "SM_ParapetCorner": parapet_corner,
    "SM_Cornice": cornice, "SM_Awning": awning, "SM_VaultWall": vault_wall,
    # Rooftop
    "SM_Roof_AC": roof_ac, "SM_Roof_Vent": roof_vent, "SM_Roof_WaterTower": roof_water_tower, "SM_Roof_Hut": roof_hut,
    "SM_Roof_Antenna": roof_antenna,
    # Houses
    "SM_GableRoof": gable_roof, "SM_Chimney": chimney, "SM_Porch": porch, "SM_Fence": fence,
    "SM_HouseMailbox": house_mailbox,
    # Interiors
    "SM_CeilingLight": ceiling_light, "SM_Shelf": shelf, "SM_WallShelf": wall_shelf, "SM_ShopCounter": shop_counter,
    "SM_DrinksFridge": drinks_fridge, "SM_Booth": booth, "SM_DinerCounter": diner_counter, "SM_Stool": stool,
    "SM_Jukebox": jukebox, "SM_BarCounter": bar_counter, "SM_BottleShelf": bottle_shelf, "SM_RoundTable": round_table,
    "SM_Chair": chair, "SM_PoolTable": pool_table, "SM_Dartboard": dartboard, "SM_Desk": desk,
    "SM_OfficeChair": office_chair, "SM_FilingCabinet": filing_cabinet, "SM_WaterCooler": water_cooler,
    "SM_Plant": plant, "SM_ReceptionDesk": reception_desk, "SM_Sofa": sofa, "SM_Armchair": armchair,
    "SM_TVStand": tv_stand, "SM_CoffeeTable": coffee_table, "SM_Rug": rug, "SM_FloorLamp": floor_lamp,
    "SM_KitchenCounter": kitchen_counter, "SM_Fridge": fridge, "SM_DiningTable": dining_table,
    "SM_Bookshelf": bookshelf, "SM_PalletRack": pallet_rack, "SM_Crate": crate, "SM_Pallet": pallet,
    "SM_Forklift": forklift, "SM_Barrel": barrel, "SM_TellerCounter": teller_counter, "SM_VaultShelf": vault_shelf,
    "SM_QueuePost": queue_post, "SM_Bench": bench, "SM_FrontDesk": front_desk, "SM_BriefingChair": briefing_chair,
    "SM_Whiteboard": whiteboard, "SM_Podium": podium, "SM_Lockers": lockers, "SM_GunRack": gun_rack,
    "SM_AmmoCrate": ammo_crate, "SM_CellBars": lambda: cell_bars(False), "SM_CellBarsDoor": lambda: cell_bars(True),
    "SM_CellBench": cell_bench, "SM_Toilet": toilet, "SM_CoffeeStation": coffee_station,
    "SM_WantedBoard": wanted_board, "SM_Doormat": doormat,
    # Street
    "SM_LampPost": lamp_post, "SM_TrafficLight": traffic_light, "SM_Hydrant": hydrant,
    "SM_StreetBench": street_bench, "SM_Bin": bin_, "SM_BusStop": bus_stop, "SM_Mailbox": mailbox,
    "SM_NewsBox": news_box, "SM_Planter": planter, "SM_ParkingMeter": parking_meter, "SM_Curb": curb,
    # Nature
    "SM_Tree_Round": tree_round, "SM_Tree_Tall": tree_tall, "SM_Tree_Pine": tree_pine, "SM_Bush": bush,
}
for _key, _text in SIGNS.items():
    KIT[f"SM_Sign_{_key}"] = (lambda text: (lambda: sign(text)))(_text)


# --------------------------------------------------------------------------------------
# Export + preview
# --------------------------------------------------------------------------------------
def mesh_bounds(obj):
    xs = [v.co.x / fb.UNIT for v in obj.data.vertices]
    ys = [v.co.y / fb.UNIT for v in obj.data.vertices]
    zs = [v.co.z / fb.UNIT for v in obj.data.vertices]
    return Vector((min(xs), min(ys), min(zs))), Vector((max(xs), max(ys), max(zs)))


def main():
    args = fb.script_args()
    out_dir = os.path.abspath(args.get("out", "Art/Source/Kit"))
    preview_dir = os.path.abspath(args["preview"]) if isinstance(args.get("preview"), str) else None
    only = set(args["only"].split(",")) if isinstance(args.get("only"), str) else None

    manifest_path = os.path.join(out_dir, "kit_manifest.json")
    manifest = {}
    if only and os.path.exists(manifest_path):
        with open(manifest_path) as f:
            manifest = json.load(f)

    for name, build in KIT.items():
        if only and name not in only:
            continue
        fb.reset_scene()
        parts, collision = build()
        obj = fb.build_mesh_object(name, parts, materials=MATERIALS)
        # Keep only the slots a piece uses; the manifest tells the importer which pieces have glass (and so
        # can't be Nanite).
        fb.set_active(obj)
        bpy.ops.object.material_slot_remove_unused()
        manifest[name] = {"slots": [m.name for m in obj.data.materials]}
        objects = [obj] + collision_boxes(name, collision)
        print(f"FTO: {name} {len(obj.data.polygons)} faces, {len(collision)} collision boxes")
        export_static(os.path.join(out_dir, f"{name}.fbx"), objects)

        if preview_dir:
            for col in objects[1:]:
                col.hide_render = True
            lo, hi = mesh_bounds(obj)
            centre = (lo + hi) / 2
            size = max((hi - lo).length, 0.5)
            cam = fb.setup_preview((360, 360))
            bpy.context.scene.display.shading.show_backface_culling = True
            eye = centre + Vector((1.0, -0.8, 0.6)).normalized() * size * 3
            fb.render_view(os.path.join(preview_dir, f"{name}.png"), cam, tuple(eye), tuple(centre), size * 1.15)

    with open(manifest_path, "w") as f:
        json.dump(dict(sorted(manifest.items())), f, indent=1)
    print(f"FTO: wrote {manifest_path}")


main()
