"""
Builds FTO's cartoon vehicles as static meshes (no rig): traffic cars and the police cruiser.

  blender -b --factory-startup -P Tools/Blender/build_vehicles.py -- --out Art/Source/Vehicles [--preview <dir>]

Vehicles face +X (Unreal forward) with the wheels on the ground at Z=0. They are shells, not blocks: doors,
floor and dashboard around real seats and a steering wheel, behind see-through glass, so whoever is driving
shows. A seated FTO cartoon is 1.3 m from seat to hat (build_officer.car_legs), which is what sets the tall,
bubbly cabins.

Material slots: "Body" (vertex colours; vertex alpha 1 marks paint the game tints per car), "Glass"
(translucent) and "Glow" (lights, dials and screens). Sockets (SOCKET_* empties):
  Wheel_FL/FR/RL/RR                  wheel hubs
  Seat_Driver/Passenger/RearL/RearR  where a seated character's root (feet origin) goes, facing +X
  Cam_Driver/Cam_Passenger           eye point for the interior camera
  Lightbar                           cruiser: centre of the light bar (the game lays glowing lenses on it)

--preview also renders a roofless cutaway with posed occupants and the driver's-eye view, to check the fit.
--dented also exports each body's beaten-up variant (<name>_Dented.fbx); --dented_only exports just those.
--only SM_Car_Sedan,SM_Car_Taxi limits a run to the named meshes.
"""
import math
import os
import random
import sys

import bmesh
import bpy
from mathutils import Vector

sys.path.append(os.path.dirname(os.path.abspath(__file__)))
import fto_blender as fb  # noqa: E402
from fto_shapes import (BODY, GLASS, GLOW, MATERIALS, Frame, add_sockets, ball, beam, both_flanks, box, cyl,  # noqa: E402
                        export_static, lettering, prism, star)

# Material slots

# Colours (sRGB)
PAINT = (0.85, 0.85, 0.85)          # tinted in game
GLASS_TINT = (0.55, 0.72, 0.85)     # preview only; the game renders M_FTOGlass
TRIM = (0.12, 0.12, 0.13)
CHROME = (0.75, 0.76, 0.78)
HEADLIGHT = (1.0, 0.95, 0.75)
TAILLIGHT = (0.95, 0.10, 0.08)
TYRE = (0.05, 0.05, 0.05)
HUB = (0.65, 0.66, 0.70)
WHITE = (0.96, 0.96, 0.96)
BLACK = (0.04, 0.04, 0.05)
YELLOW = (1.0, 0.80, 0.10)
PLATE = (0.98, 0.86, 0.30)
PINK = (1.0, 0.62, 0.78)
GOLD_STAR = (1.0, 0.78, 0.18)
SIREN_RED = (1.0, 0.1, 0.1)
SIREN_BLUE = (0.1, 0.3, 1.0)
DASH = (0.17, 0.17, 0.19)
CABIN = (0.30, 0.30, 0.33)          # door cards, console
LINING = (0.80, 0.78, 0.74)
CARPET = (0.11, 0.11, 0.12)
SEAT_BLACK = (0.10, 0.10, 0.11)
SEAT_TAN = (0.66, 0.50, 0.33)
SEAT_GREY = (0.42, 0.42, 0.46)
SEAT_RED = (0.58, 0.14, 0.12)
PLASTIC = (0.20, 0.20, 0.22)        # cruiser back seat: hard and wipe-clean
GAUGE = (0.55, 0.95, 1.0)
SCREEN = (0.30, 0.70, 1.0)
SCREEN_TEXT = (0.85, 0.98, 1.0)
LED_GREEN = (0.30, 1.0, 0.40)
LED_RED = (1.0, 0.15, 0.10)
LED_AMBER = (1.0, 0.62, 0.10)
WOOD = (0.45, 0.28, 0.14)
CARDBOARD = (0.72, 0.55, 0.34)
DOUGH = (0.86, 0.60, 0.30)

WHEEL_RADIUS = 0.38
LIFT = WHEEL_RADIUS * 0.75          # underside of the body panels

# A seated cartoon (build_officer.car_legs), relative to the character root:
SOLE = 0.15                         # soles
BUTT = 0.33                         # seat cushion top
BACK = 0.29                         # back of the torso, behind the root
HANDS = (0.48, 0.99)                # steering-wheel centre: forward, height (hands 0.245 either side)
EYES = (0.16, 1.44)                 # interior camera: forward, height (a touch above the eyes)
HEADROOM = 1.72                     # roof lining above the root: clears the tallest hat or hairdo
SEAT_Y = 0.48                       # front seats sit this far either side of the centre line


# --------------------------------------------------------------------------------------
# Body layout
# --------------------------------------------------------------------------------------
class Body:
    """
    Proportions of one vehicle, in metres. The cabin runs from the windscreen base (cf) back to the
    rear glass base (cr); everything inside is placed relative to the front seat root (sx).
    """

    def __init__(self, **kw):
        self.L, self.W = 4.5, 2.16
        self.floor = 0.25               # cabin floor top
        self.belt = 1.0                 # door tops / window sills
        self.extra_head = 0.0           # taller roofs (vans)
        self.sx = 0.10                  # front seat root x
        self.rear = -0.80               # rear seat root x, or None for one row
        self.bx = None                  # B-pillar x (default: behind the front seats when there's a rear row)
        self.cf = None                  # windscreen base x (default: just past the dashboard)
        self.rake_front = 0.68          # windscreen run from base to roof
        self.cr = -1.45                 # rear glass base x
        self.rake_rear = 0.22
        self.nose_z = 0.80
        self.tail_z = 0.95
        self.trunk = True               # False: something else fills the back (pickup bed, ice cream box)
        self.rear_glass = True
        self.cargo = False              # windowless load area behind the B-pillar (vans)
        self.paint = PAINT
        self.tint = True
        self.roof_paint = None
        self.door_paint = None
        self.seat = SEAT_GREY
        self.rear_seat = None
        self.__dict__.update(kw)
        if self.cf is None:
            self.cf = self.sx + 1.10
        if self.bx is None and self.rear is not None:
            self.bx = self.sx - 0.45
        self.hw = self.W / 2
        self.root_z = self.floor - SOLE
        self.lining = self.root_z + HEADROOM + self.extra_head
        self.roof = self.lining + 0.08
        self.rf = self.cf - self.rake_front
        self.rr = self.cr + self.rake_rear

    def along_front(self, z):
        """x of the windscreen / A-pillar line at height z."""
        return self.cf + (self.rf - self.cf) * (z - self.belt) / (self.lining - self.belt)

    def along_rear(self, z):
        return self.cr + (self.rr - self.cr) * (z - self.belt) / (self.lining - self.belt)


def shell(s, cut):
    """Hood, trunk, doors, floor, pillars, roof and glass. cut=True leaves off the roof and glass (preview)."""
    L, W, hw = s.L, s.W, s.hw
    roof_paint = s.roof_paint or s.paint
    roof_tint = s.tint and s.roof_paint is None
    door_paint = s.door_paint or s.paint
    door_tint = s.tint and s.door_paint is None
    p = [
        # Hood, sloping up to the windscreen.
        prism(s.paint, [(s.cf, LIFT), (L / 2, LIFT), (L / 2, s.nose_z), (L / 2 - 0.3, s.nose_z + 0.1), (s.cf, s.belt)],
              -hw, hw, bevel=0.09, tint=s.tint),
    ]
    if s.trunk:
        p.append(prism(s.paint, [(-L / 2, LIFT), (s.cr, LIFT), (s.cr, s.belt), (-L / 2 + 0.25, s.tail_z),
                                 (-L / 2, s.tail_z - 0.12)], -hw, hw, bevel=0.09, tint=s.tint))

    # Doors either side of the cabin, carpeted floor between them.
    mid, span = (s.cf + s.cr) / 2, s.cf - s.cr
    for side in (1, -1):
        p.append(box(door_paint, (mid, side * (hw - 0.05), (LIFT + s.belt) / 2), (span + 0.02, 0.10, s.belt - LIFT),
                     tint=door_tint))
    p.append(box(CARPET, (mid, 0, (s.floor + LIFT - 0.08) / 2), (span, W - 0.16, s.floor - LIFT + 0.08), bevel=0.0))

    # Pillars
    yp = hw - 0.06
    for side in (1, -1):
        y = side * yp
        p.append(beam(roof_paint, (s.cf, y, s.belt - 0.02), (s.rf, y, s.lining + 0.03), 0.10, 0.09, tint=roof_tint))
        p.append(beam(roof_paint, (s.cr, y, s.belt - 0.02), (s.rr, y, s.lining + 0.03), 0.10, 0.09, tint=roof_tint))
        if s.bx is not None:
            p.append(box(roof_paint, (s.bx, y, (s.belt + s.lining) / 2), (0.10, 0.10, s.lining - s.belt + 0.04),
                         bevel=0.02, tint=roof_tint))
        if s.cargo:
            # Solid load-area sides from the B-pillar back.
            p.append(box(door_paint, ((s.bx + s.cr) / 2, y, (s.belt + s.lining) / 2),
                         (s.bx - s.cr, 0.09, s.lining - s.belt + 0.02), bevel=0.02, tint=door_tint))
    if cut:
        return p

    # Roof and headliner
    p.append(box(roof_paint, ((s.rf + s.rr) / 2, 0, s.lining + 0.04), (s.rf - s.rr + 0.14, W - 0.03, 0.09), bevel=0.04,
                 tint=roof_tint))
    p.append(box(LINING, ((s.rf + s.rr) / 2, 0, s.lining - 0.006), (s.rf - s.rr - 0.02, W - 0.24, 0.012), bevel=0.0))

    # Glass: windscreen, rear window, and side windows either side of the B-pillar.
    glass_w = 2 * yp - 0.06
    p.append(beam(GLASS_TINT, (s.cf - 0.01, 0, s.belt), (s.rf, 0, s.lining), glass_w, 0.02, bevel=0.0, material=GLASS))
    if s.cargo:
        # Van back doors: a panel with two little windows.
        p.append(box(door_paint, (s.cr - 0.02, 0, (s.belt + s.lining) / 2), (0.06, W - 0.1, s.lining - s.belt), bevel=0.02,
                     tint=door_tint))
        for side in (1, -1):
            p.append(box(GLASS_TINT, (s.cr - 0.055, side * hw * 0.45, s.belt + 0.45), (0.02, hw * 0.7, 0.5), bevel=0.0,
                         material=GLASS))
    elif s.rear_glass:
        p.append(beam(GLASS_TINT, (s.cr + 0.01, 0, s.belt), (s.rr, 0, s.lining), glass_w, 0.02, bevel=0.0, material=GLASS))
    z0, z1 = s.belt, s.lining
    for side in (1, -1):
        y0, y1 = side * yp - 0.01, side * yp + 0.01
        if s.bx is not None:
            windows = [[(s.along_front(z0), z0), (s.bx, z0), (s.bx, z1), (s.along_front(z1), z1)]]
            if not s.cargo:
                windows.append([(s.bx, z0), (s.along_rear(z0), z0), (s.along_rear(z1), z1), (s.bx, z1)])
        else:
            windows = [[(s.along_front(z0), z0), (s.along_rear(z0), z0), (s.along_rear(z1), z1), (s.along_front(z1), z1)]]
        for outline in windows:
            p.append(prism(GLASS_TINT, outline, y0, y1, material=GLASS))
    return p


def bucket_seat(x, y, s, color):
    top = s.root_z + BUTT
    back = Frame((x - BACK - 0.065, y, top), (0, -8, 0))  # reclined a touch
    return [
        box(color, (x - 0.03, y, (s.floor + top) / 2), (0.54, 0.52, top - s.floor), bevel=0.05),
        box(color, back.at((0, 0, 0.37)), (0.13, 0.52, 0.74), rot=back.rot, bevel=0.05),
        box(color, back.at((-0.015, 0, 0.98)), (0.11, 0.30, 0.20), rot=back.rot, bevel=0.045),
        box(CHROME, back.at((-0.015, 0.08, 0.81)), (0.02, 0.02, 0.15), rot=back.rot, bevel=0.0),
        box(CHROME, back.at((-0.015, -0.08, 0.81)), (0.02, 0.02, 0.15), rot=back.rot, bevel=0.0),
    ]


def bench_seat(x, s, color):
    top = s.root_z + BUTT
    width = s.W - 0.30
    back = Frame((x - BACK - 0.065, 0, top), (0, -8, 0))
    return [
        box(color, (x - 0.03, 0, (s.floor + top) / 2), (0.54, width, top - s.floor), bevel=0.05),
        box(color, back.at((0, 0, 0.35)), (0.13, width, 0.70), rot=back.rot, bevel=0.05),
        # Parcel shelf behind it.
        box(DASH, ((x - 0.44 + s.cr) / 2, 0, s.belt - 0.02), (x - 0.44 - s.cr, s.W - 0.25, 0.03), bevel=0.0),
    ]


def interior(s):
    """Door cards, dashboard, gauges, steering wheel, console, seats, mirror and sun visors."""
    p = []
    hw, d, rz = s.hw, s.sx, s.root_z
    face = d + 0.64                     # dashboard face, just past the steering wheel
    mid, span = (s.cf + s.cr) / 2, s.cf - s.cr
    for side in (1, -1):
        p.append(box(CABIN, (mid, side * (hw - 0.115), (s.floor + s.belt) / 2 + 0.01), (span - 0.04, 0.03, s.belt - s.floor),
                     bevel=0.01))
        p.append(box(DASH, (mid, side * (hw - 0.15), rz + 0.66), (span - 0.5, 0.06, 0.05), bevel=0.015))  # armrest

    # Dashboard, sloping up to the windscreen, with the gauge binnacle in front of the wheel.
    p.append(prism(DASH, [(s.cf, s.floor), (s.cf, s.belt + 0.03), (face + 0.18, s.belt + 0.10), (face, s.belt + 0.06),
                          (face, s.belt - 0.20), (face + 0.18, s.floor + 0.22), (face + 0.32, s.floor)],
                   -(hw - 0.12), hw - 0.12, bevel=0.03))
    p.append(box(DASH, (face + 0.07, SEAT_Y, s.belt + 0.12), (0.16, 0.44, 0.12), bevel=0.04))
    for off in (-0.1, 0.1):
        p.append(cyl(GAUGE, (face - 0.012, SEAT_Y + off, s.belt + 0.115), 0.055, 0.012, axis='x', segments=16,
                     material=GLOW))
    p.append(box(BLACK, (face - 0.005, 0.0, s.belt - 0.08), (0.02, 0.30, 0.10), bevel=0.01))            # centre vents
    p.append(box(CABIN, (face - 0.005, -SEAT_Y, s.belt - 0.12), (0.02, 0.42, 0.14), bevel=0.01))        # glovebox

    # Steering wheel: rim at the driver's hands, tilted toward them, column into the dash.
    grip = Frame((d + HANDS[0], SEAT_Y, rz + HANDS[1]), (0, -65, 0))
    p += [
        fb.make_part('torus', BLACK, loc=grip.center, rot=grip.rot, scale=(0.54, 0.54, 0.54), minor=0.046,
                     segments=28, rings=8),
        cyl(DASH, grip.center, 0.075, 0.07, rot=grip.rot, segments=16),
        box(BLACK, grip.center, (0.03, 0.46, 0.025), rot=grip.rot, bevel=0.008),
        box(BLACK, grip.at((-0.12, 0, 0)), (0.24, 0.035, 0.025), rot=grip.rot, bevel=0.008),
        beam(DASH, grip.at((0, 0, -0.03)), grip.at((0, 0, -0.32)), 0.07, 0.07),
    ]

    # Centre console and gear stick.
    p += [
        box(CABIN, ((d - 0.25 + face) / 2, 0, (s.floor + rz + 0.46) / 2), (face - d + 0.25, 0.22, rz + 0.46 - s.floor)),
        cyl(CHROME, (d + 0.42, 0, rz + 0.52), 0.015, 0.14),
        ball(BLACK, (d + 0.42, 0, rz + 0.60), 0.07),
    ]

    # Seats
    for y in (SEAT_Y, -SEAT_Y):
        p += bucket_seat(d, y, s, s.seat)
    if s.rear is not None:
        p += bench_seat(s.rear, s, s.rear_seat or s.seat)

    # Mirror and sun visors under the roof.
    top = s.lining
    p += [
        box(DASH, (s.rf - 0.04, 0, top - 0.12), (0.03, 0.26, 0.08), bevel=0.015),
        box(DASH, (s.rf - 0.03, 0, top - 0.05), (0.02, 0.02, 0.10), bevel=0.0),
    ]
    for y in (SEAT_Y, -SEAT_Y):
        p.append(box(LINING, (s.rf - 0.12, y, top - 0.025), (0.22, 0.42, 0.025), bevel=0.01))
    return p


def trim(s, plates=True):
    """Bumpers, grille, glowing lights, wheel arches, sills, mirrors, door handles and plates."""
    L, hw = s.L, s.hw
    p = [
        box(TRIM, (L / 2 + 0.03, 0, LIFT + 0.12), (0.18, s.W + 0.04, 0.24), bevel=0.07),
        box(TRIM, (-L / 2 - 0.03, 0, LIFT + 0.12), (0.18, s.W + 0.04, 0.24), bevel=0.07),
        box(BLACK, (L / 2 + 0.01, 0, s.nose_z - 0.17), (0.04, s.W * 0.36, 0.14), bevel=0.02),
    ]
    handles = [s.bx + 0.25, s.cr + 0.3] if s.bx is not None and not s.cargo else [s.cf - 0.55]
    for side in (1, -1):
        p += [
            box(HEADLIGHT, (L / 2 + 0.005, side * hw * 0.68, s.nose_z - 0.15), (0.06, 0.34, 0.16), bevel=0.04,
                material=GLOW),
            box(TAILLIGHT, (-L / 2 - 0.005, side * hw * 0.72, s.tail_z - 0.24), (0.06, 0.30, 0.15), bevel=0.03,
                material=GLOW),
            box(TRIM, (L * 0.3, side * (hw - 0.02), LIFT + 0.27), (1.0, 0.12, 0.36), bevel=0.05),
            box(TRIM, (-L * 0.3, side * (hw - 0.02), LIFT + 0.27), (1.0, 0.12, 0.36), bevel=0.05),
            box(TRIM, ((s.cf + s.cr) / 2, side * (hw - 0.03), s.belt), (s.cf - s.cr + 0.04, 0.07, 0.04), bevel=0.012),
            box(s.door_paint or s.paint, (s.cf - 0.06, side * (hw + 0.10), s.belt + 0.10), (0.12, 0.18, 0.11),
                bevel=0.03, tint=s.tint and s.door_paint is None),
            box(TRIM, (s.cf - 0.08, side * (hw + 0.02), s.belt + 0.06), (0.05, 0.08, 0.04), bevel=0.01),
        ]
        for x in handles:
            p.append(box(CHROME, (x, side * (hw + 0.005), s.belt - 0.12), (0.14, 0.03, 0.035), bevel=0.01))
    if plates:
        p.append(box(WHITE, (L / 2 + 0.125, 0, LIFT + 0.12), (0.02, 0.40, 0.12), bevel=0.0))
        p.append(box(PLATE, (-L / 2 - 0.125, 0, LIFT + 0.12), (0.02, 0.40, 0.12), bevel=0.0))
    return p


def wheel_sockets(s):
    x, y = s.L * 0.3, s.W * 0.5
    return {
        "Wheel_FL": (x, y, WHEEL_RADIUS),
        "Wheel_FR": (x, -y, WHEEL_RADIUS),
        "Wheel_RL": (-x, y, WHEEL_RADIUS),
        "Wheel_RR": (-x, -y, WHEEL_RADIUS),
    }


def seat_sockets(s):
    rz = s.root_z
    sockets = {
        "Seat_Driver": (s.sx, SEAT_Y, rz),
        "Seat_Passenger": (s.sx, -SEAT_Y, rz),
        "Cam_Driver": (s.sx + EYES[0], SEAT_Y, rz + EYES[1]),
        "Cam_Passenger": (s.sx + EYES[0], -SEAT_Y, rz + EYES[1]),
    }
    if s.rear is not None:
        sockets["Seat_RearL"] = (s.rear, 0.50, rz)
        sockets["Seat_RearR"] = (s.rear, -0.50, rz)
    return sockets


def assemble(s, cut, *extras):
    parts = shell(s, cut) + interior(s) + trim(s)
    sockets = {**wheel_sockets(s), **seat_sockets(s)}
    for extra in extras:
        extra_parts, extra_sockets = extra(s, cut)
        parts += extra_parts
        sockets.update(extra_sockets)
    return parts, sockets


# --------------------------------------------------------------------------------------
# Vehicles
# --------------------------------------------------------------------------------------
def sedan(cut=False):
    return assemble(Body(L=4.5, seat=SEAT_GREY), cut)


def hatchback(cut=False):
    return assemble(Body(L=3.9, W=2.1, sx=0.25, rear=-0.72, cr=-1.40, rake_rear=0.30, tail_z=1.0, seat=SEAT_TAN), cut)


def van(cut=False):
    s = Body(L=4.9, W=2.2, sx=0.85, rear=None, bx=0.33, cr=-2.43, rake_front=0.55, rake_rear=0.0, extra_head=0.35,
             nose_z=0.85, tail_z=1.0, trunk=False, cargo=True, seat=SEAT_BLACK)

    def load(s, cut):
        # Bulkhead behind the seats, and parcels in the back (you can see them through the rear windows).
        p = [box(CABIN, (s.bx - 0.12, 0, (s.floor + s.lining) / 2), (0.04, s.W - 0.24, s.lining - s.floor), bevel=0.0)]
        for x, y, z, size in ((-1.9, 0.5, 0.0, 0.55), (-1.9, 0.5, 0.55, 0.4), (-1.9, -0.45, 0.0, 0.5),
                              (-1.35, 0.55, 0.0, 0.45), (-0.9, -0.5, 0.0, 0.5)):
            p.append(box(CARDBOARD, (x, y, s.floor + z + size / 2), (size, size * 0.9, size), bevel=0.02))
        p += both_flanks("SPEEDY MOVERS", BLACK, (s.bx + s.cr) / 2, 1.55, s.hw + 0.005, 0.24)
        p += both_flanks("We lift. Mostly.", BLACK, (s.bx + s.cr) / 2, 1.25, s.hw + 0.005, 0.12)
        return p, {}
    return assemble(s, cut, load)


def pickup(cut=False):
    s = Body(L=4.8, sx=0.60, rear=None, cr=-0.02, rake_rear=0.0, rake_front=0.6, trunk=False, seat=SEAT_RED)

    def bed(s, cut):
        L, hw = s.L, s.hw
        length = s.cr + L / 2
        mid = (s.cr - L / 2) / 2
        top = 0.62
        p = [
            box(PAINT, (mid, 0, (LIFT + top) / 2), (length, s.W, top - LIFT), bevel=0.06, tint=True),
            box(BLACK, (mid, 0, top + 0.005), (length - 0.2, s.W - 0.26, 0.01), bevel=0.0),
            box(PAINT, (-L / 2 + 0.05, 0, (top + s.belt) / 2), (0.10, s.W, s.belt - top), bevel=0.03, tint=True),
            box(CHROME, (s.cr - 0.2, 0, top + 0.18), (0.3, s.W - 0.3, 0.3), bevel=0.03),       # tool box
            box(WOOD, (-1.3, 0.35, top + 0.2), (0.5, 0.45, 0.4), bevel=0.02),                  # crate
        ]
        for side in (1, -1):
            p.append(box(PAINT, (mid, side * (hw - 0.05), (top + s.belt) / 2), (length, 0.10, s.belt - top), bevel=0.03,
                         tint=True))
        return p, {}
    return assemble(s, cut, bed)


def taxi(cut=False):
    s = Body(L=4.5, paint=YELLOW, tint=False, seat=SEAT_BLACK)

    def livery(s, cut):
        p = [
            # Glowing roof sign and the meter on the dash.
            box(DASH, (-0.2, 0, s.roof + 0.02), (0.5, 0.16, 0.04), bevel=0.01),
            box((1.0, 0.95, 0.7), (-0.2, 0, s.roof + 0.14), (0.55, 0.30, 0.2), bevel=0.05, material=GLOW),
            box(DASH, (s.sx + 0.84, -0.12, s.belt + 0.14), (0.10, 0.22, 0.08), bevel=0.015),
            box(LED_RED, (s.sx + 0.785, -0.12, s.belt + 0.145), (0.01, 0.16, 0.035), bevel=0.0, material=GLOW),
        ]
        for i in range(15):
            for side in (1, -1):
                color = BLACK if i % 2 else WHITE
                p.append(box(color, (-2.1 + i * 0.3, side * (s.hw + 0.005), 0.72), (0.3, 0.02, 0.12), bevel=0.0))
        if not cut:
            p.append(lettering("TAXI", BLACK, (0.08, 0, s.roof + 0.14), '+x', 0.12))
            p.append(lettering("TAXI", BLACK, (-0.48, 0, s.roof + 0.14), '-x', 0.12))
        return p, {}
    return assemble(s, cut, livery)


def ice_cream(cut=False):
    s = Body(L=5.1, W=2.2, sx=1.05, rear=None, cr=0.05, rake_rear=0.0, rake_front=0.5, trunk=False, rear_glass=False,
             paint=WHITE, tint=False, seat=SEAT_RED, tail_z=1.0)

    def parlour(s, cut):
        L, hw = s.L, s.hw
        front, back = s.cr, -L / 2
        mid, length = (front + back) / 2, front - back
        top = 2.65
        hatch_x = mid + 0.1
        hatch_front, hatch_back = hatch_x + 0.75, hatch_x - 0.75
        p = [
            # The box: walls around an open serving hatch on the kerb (right) side.
            box(WHITE, (mid, 0, s.floor - 0.03), (length, s.W, 0.06), bevel=0.02),
            box(WHITE, (front, 0, (LIFT + top) / 2), (0.08, s.W, top - LIFT), bevel=0.03),
            box(WHITE, (back + 0.04, 0, (LIFT + top) / 2), (0.08, s.W, top - LIFT), bevel=0.03),
            box(WHITE, (mid, hw - 0.04, (LIFT + top) / 2), (length, 0.08, top - LIFT), bevel=0.03),
            box(WHITE, (mid, -hw + 0.04, (LIFT + 1.05) / 2), (length, 0.08, 1.05 - LIFT), bevel=0.03),
            box(WHITE, (mid, -hw + 0.04, (1.80 + top) / 2), (length, 0.08, top - 1.80), bevel=0.03),
            box(WHITE, ((front + hatch_front) / 2, -hw + 0.04, 1.43), (front - hatch_front, 0.08, 0.76), bevel=0.02),
            box(WHITE, ((hatch_back + back) / 2, -hw + 0.04, 1.43), (hatch_back - back, 0.08, 0.76), bevel=0.02),
            box(WHITE, (mid, 0, top), (length + 0.06, s.W + 0.04, 0.08), bevel=0.03),
            box(PINK, (mid, 0, 1.20), (length + 0.02, s.W + 0.02, 0.18), bevel=0.02),
            box(GLASS_TINT, (hatch_x, -hw + 0.02, 1.43), (1.5, 0.02, 0.7), bevel=0.0, material=GLASS) if not cut else None,
            # Striped awning over the hatch.
            box(PINK, (hatch_x, -hw - 0.25, 1.95), (1.7, 0.5, 0.04), bevel=0.01, rot=(-12, 0, 0)),
            # Inside: counter with tubs of ice cream, a menu board.
            box(CABIN, (hatch_x, -hw + 0.35, 0.95), (1.5, 0.5, 0.1), bevel=0.02),
            box(WHITE, (hatch_x, -hw + 0.35, (s.floor + 0.9) / 2), (1.5, 0.5, 0.9 - s.floor), bevel=0.02),
            box(BLACK, (mid, hw - 0.09, 1.7), (1.4, 0.02, 0.6), bevel=0.01),
            # The giant cone on the roof.
            fb.make_part('cone', (0.93, 0.78, 0.50), loc=(mid, 0, top + 0.45), rot=(180, 0, 0), scale=(0.55, 0.55, 0.8),
                         segments=12),
            ball(PINK, (mid, 0, top + 0.95), (0.62, 0.62, 0.55)),
            ball((0.55, 0.30, 0.15), (mid, 0, top + 1.25), (0.3, 0.3, 0.25)),
        ]
        for i, color in enumerate(((1.0, 0.95, 0.85), PINK, (0.55, 0.3, 0.15), (0.6, 0.9, 0.6))):
            p.append(cyl(color, (hatch_x - 0.5 + i * 0.33, -hw + 0.35, 1.02), 0.11, 0.06, segments=12))
        for i in range(4):
            p.append(box(PINK if i % 2 else WHITE, (hatch_x - 0.64 + i * 0.425, -hw - 0.25, 1.97), (0.21, 0.5, 0.05),
                         bevel=0.0, rot=(-12, 0, 0)))
        cocoa = (0.55, 0.30, 0.15)
        p.append(lettering("ICE CREAM", cocoa, (mid, -hw - 0.005, 2.3), '-y', 0.3))
        p.append(lettering("ICE CREAM", cocoa, (mid, hw + 0.005, 1.95), '+y', 0.34))
        p.append(lettering("Brain freeze guaranteed", cocoa, (mid, hw + 0.005, 1.6), '+y', 0.13))
        return [part for part in p if part is not None], {}
    return assemble(s, cut, parlour)


def cruiser(cut=False):
    """Black-and-white with a light bar and the full police kit inside. The door stripe takes the driver's colour."""
    s = Body(L=4.6, rear=-0.92, cr=-1.52, paint=BLACK, tint=False, door_paint=WHITE, roof_paint=WHITE,
             seat=SEAT_BLACK, rear_seat=PLASTIC)
    return assemble(s, cut, police_outside, police_inside)


def police_outside(s, cut):
    L, hw = s.L, s.hw
    mid = (s.cf + s.cr) / 2
    bar_x = (s.rf + s.rr) / 2 - 0.1
    p = []
    for side in (1, -1):
        y = side * (hw + 0.006)
        p.append(box(PAINT, (mid, y, 0.62), (s.cf - s.cr, 0.02, 0.12), bevel=0.0, tint=True))   # badge-colour stripe
        p += star(GOLD_STAR, s.bx + 0.45, 0.84, y - 0.01, y + 0.01)
    p += both_flanks("POLICE", BLACK, (s.bx + s.cr) / 2 + 0.05, 0.83, hw + 0.012, 0.2)
    p.append(lettering("POLICE", WHITE, (-L / 2 - 0.004, 0, s.tail_z - 0.29), '-x', 0.1))
    p += [
        # Push bar
        box(TRIM, (L / 2 + 0.22, 0, LIFT + 0.35), (0.08, 1.2, 0.5), bevel=0.02),
        box(TRIM, (L / 2 + 0.13, 0.45, LIFT + 0.35), (0.2, 0.08, 0.5), bevel=0.02),
        box(TRIM, (L / 2 + 0.13, -0.45, LIFT + 0.35), (0.2, 0.08, 0.5), bevel=0.02),
        # Driver's A-pillar spotlight
        cyl(CHROME, (s.cf - 0.05, hw + 0.08, s.belt + 0.14), 0.07, 0.16, axis='x', segments=14),
        cyl(HEADLIGHT, (s.cf + 0.035, hw + 0.08, s.belt + 0.14), 0.06, 0.01, axis='x', segments=14, material=GLOW),
        box(CHROME, (s.cf - 0.14, hw - 0.02, s.belt + 0.1), (0.04, 0.2, 0.03), bevel=0.0),
        # Whip antenna on the trunk
        cyl(BLACK, (-L / 2 + 0.35, -0.5, s.tail_z + 0.4), 0.008, 0.8, segments=6),
    ]
    if not cut:
        p += [
            box(TRIM, (bar_x, 0, s.roof + 0.05), (0.5, 1.6, 0.1), bevel=0.03),
            box(SIREN_RED, (bar_x, 0.4, s.roof + 0.18), (0.4, 0.6, 0.16), bevel=0.04),
            box(SIREN_BLUE, (bar_x, -0.4, s.roof + 0.18), (0.4, 0.6, 0.16), bevel=0.04),
            box(WHITE, (bar_x, 0, s.roof + 0.16), (0.36, 0.18, 0.12), bevel=0.03, material=GLOW),
        ]
    return p, {"Lightbar": (bar_x, 0, s.roof + 0.18)}


def police_inside(s, cut):
    d, rz, hw = s.sx, s.root_z, s.hw
    face = d + 0.64
    console_top = rz + 0.46
    p = []

    # MDT: a rugged laptop on a pole mount between the seats, swung toward the driver.
    mount = Vector((d + 0.47, -0.06, 0.0))
    p.append(cyl(CHROME, (mount.x, mount.y, (console_top + rz + 0.80) / 2), 0.02, rz + 0.80 - console_top))
    kb = Frame((mount.x, mount.y, rz + 0.82), (0, 0, -22))
    p.append(box(DASH, kb.center, (0.26, 0.36, 0.035), rot=kb.rot, bevel=0.012))
    p.append(box(BLACK, kb.at((-0.02, 0, 0.02)), (0.16, 0.30, 0.006), rot=kb.rot, bevel=0.0))       # keys
    lid = Frame(kb.at((0.12, 0, 0.018)), (0, 10, -22))                                              # hinge, leaning back
    p.append(box(DASH, lid.at((0, 0, 0.12)), (0.025, 0.36, 0.24), rot=lid.rot, bevel=0.012))
    p.append(box(SCREEN, lid.at((-0.014, 0, 0.125)), (0.004, 0.31, 0.19), rot=lid.rot, bevel=0.0, material=GLOW))
    for i, width in enumerate((0.22, 0.16, 0.2, 0.1)):
        p.append(box(SCREEN_TEXT, lid.at((-0.017, 0.11 - width / 2, 0.19 - i * 0.035)), (0.003, width, 0.014), rot=lid.rot,
                     bevel=0.0, material=GLOW))

    # Police radio on the console with a glowing channel display, handset clipped to the dash.
    p += [
        box(BLACK, (d + 0.23, 0, console_top + 0.03), (0.22, 0.17, 0.06), bevel=0.01),
        box(LED_GREEN, (d + 0.25, 0, console_top + 0.061), (0.08, 0.10, 0.004), bevel=0.0, material=GLOW),
        cyl(CHROME, (d + 0.16, 0.05, console_top + 0.07), 0.018, 0.03),
        cyl(CHROME, (d + 0.16, -0.05, console_top + 0.07), 0.018, 0.03),
        box(BLACK, (face - 0.03, 0.18, s.belt - 0.12), (0.04, 0.06, 0.10), bevel=0.012),
        cyl(BLACK, (face - 0.03, 0.18, s.belt - 0.25), 0.012, 0.16, segments=6),
    ]

    # Lightbar control panel behind the radio: a row of lit switches.
    p.append(box(DASH, (d - 0.02, 0, console_top + 0.025), (0.16, 0.18, 0.05), bevel=0.01))
    for i, color in enumerate((LED_RED, SIREN_BLUE, LED_AMBER, WHITE)):
        p.append(box(color, (d - 0.02, 0.06 - i * 0.04, console_top + 0.055), (0.05, 0.028, 0.012), bevel=0.0,
                     material=GLOW))

    # Radar: the unit on the dash (speed readout toward the driver) and its antenna at the windscreen.
    p += [
        box(DASH, (face + 0.24, -0.10, s.belt + 0.155), (0.14, 0.24, 0.09), bevel=0.015),
        box(LED_RED, (face + 0.169, -0.10, s.belt + 0.16), (0.004, 0.16, 0.04), bevel=0.0, material=GLOW),
        cyl(BLACK, (s.cf - 0.12, 0.22, s.belt + 0.11), 0.055, 0.18, axis='x', segments=14),
        box(DASH, (s.cf - 0.16, 0.22, s.belt + 0.06), (0.06, 0.04, 0.06), bevel=0.01),
    ]

    # Coffee in the cup holder and a box of donuts on the dash. It's a cop car.
    p += [
        cyl(WHITE, (d + 0.08, 0, console_top + 0.06), 0.04, 0.12, segments=12),
        cyl(BLACK, (d + 0.08, 0, console_top + 0.125), 0.042, 0.012, segments=12),
        box(PINK, (face + 0.30, -0.55, s.belt + 0.14), (0.26, 0.26, 0.06), bevel=0.01),
        fb.make_part('torus', (1.0, 0.55, 0.75), loc=(face + 0.30, -0.55, s.belt + 0.19), scale=(0.15, 0.15, 0.15),
                     minor=0.17, segments=16, rings=8),
        fb.make_part('torus', DOUGH, loc=(face + 0.30, -0.55, s.belt + 0.18), scale=(0.155, 0.155, 0.12),
                     minor=0.17, segments=16, rings=8),
    ]

    # Dash cam behind the mirror.
    p.append(box(BLACK, (s.rf - 0.02, -0.12, s.lining - 0.1), (0.05, 0.07, 0.06), bevel=0.01))

    # Shotgun locked upright in its rack between the seats, in front of the cage.
    gx = d - 0.44
    p += [
        box(BLACK, (gx, 0, console_top + 0.03), (0.1, 0.12, 0.06), bevel=0.01),                      # rack base
        box(WOOD, (gx, 0, console_top + 0.2), (0.07, 0.045, 0.28), bevel=0.012),                    # stock
        box(BLACK, (gx, 0, console_top + 0.42), (0.06, 0.04, 0.18), bevel=0.008),                   # receiver
        cyl(DASH, (gx + 0.012, 0, console_top + 0.85), 0.012, 0.72, segments=8),                    # barrel
        cyl(DASH, (gx - 0.014, 0, console_top + 0.75), 0.013, 0.52, segments=8),                    # magazine tube
        box(WOOD, (gx - 0.01, 0, console_top + 0.8), (0.055, 0.05, 0.14), bevel=0.012),             # pump
        box(CHROME, (gx - 0.04, 0, console_top + 0.62), (0.04, 0.09, 0.03), bevel=0.0),            # clamps
        box(CHROME, (gx - 0.04, 0, console_top + 1.02), (0.04, 0.09, 0.03), bevel=0.0),
    ]

    # Cage: solid lower panel, bars above, between the front seats and the back seat.
    cage_x = d - 0.58
    inner = hw - 0.13
    top = s.lining - 0.02
    p += [
        box(PLASTIC, (cage_x, 0, (s.floor + s.belt) / 2), (0.03, 2 * inner, s.belt - s.floor), bevel=0.0),
        box(BLACK, (cage_x, 0, s.belt), (0.05, 2 * inner, 0.05), bevel=0.01),
        box(BLACK, (cage_x, 0, top - 0.02), (0.05, 2 * inner, 0.05), bevel=0.01),
        box(BLACK, (cage_x, 0, (s.belt + top) / 2 + 0.05), (0.04, 2 * inner, 0.03), bevel=0.0),
    ]
    for side in (1, -1):
        p.append(box(BLACK, (cage_x, side * inner, (s.belt + top) / 2), (0.05, 0.05, top - s.belt), bevel=0.01))
    bars = 14
    for i in range(bars):
        y = -inner + (i + 1) * 2 * inner / (bars + 1)
        p.append(cyl(DASH, (cage_x, y, (s.belt + top) / 2), 0.011, top - s.belt, segments=6))
    return p, {}


def dice(mesh_obj, max_edge=0.14):
    """
    Split every edge longer than max_edge (metres) until none are, so flat panels have vertices in the middle. The game
    dents cars where they're hit by pushing vertices about (UFTOVehicleDamage and M_FTOVehicle), and a bonnet that's
    one quad has nothing in the middle to push. Colours, slots and shapes are unchanged.
    """
    bm = bmesh.new()
    bm.from_mesh(mesh_obj.data)
    limit = max_edge * fb.UNIT
    for _ in range(7):
        long_edges = [e for e in bm.edges if e.calc_length() > limit]
        if not long_edges:
            break
        bmesh.ops.subdivide_edges(bm, edges=long_edges, cuts=1, use_grid_fill=True)
    bm.to_mesh(mesh_obj.data)
    bm.free()
    mesh_obj.data.update()


def dent(mesh_obj, seed):
    """
    The same car after a hard day: nose and tail crumpled in, a couple of knocks along the doors, and a roof that's
    been sat on. Vertices only move (nothing is added or removed), so the material slots and the sockets (seats,
    wheels, cameras) still fit: the game swaps this in when a car's badly damaged.
    """
    rng = random.Random(seed)
    # (The body's already diced into small faces (dice()), so there's something in the middle of each panel to push.)
    verts = mesh_obj.data.vertices
    xs = [v.co.x for v in verts]
    x0, x1 = min(xs), max(xs)
    hw = max(abs(v.co.y) for v in verts)
    top = max(v.co.z for v in verts)
    length = x1 - x0
    zone = length * 0.18
    u = fb.UNIT
    knocks = [(rng.uniform(x0 + length * 0.25, x1 - length * 0.25), rng.choice((-1.0, 1.0)), rng.uniform(0.6, 1.0),
               rng.uniform(0.45, 0.8) * u) for _ in range(3)]
    for v in verts:
        x, y, z = v.co
        # Crumple zones: the last stretch at each end folds in, unevenly across the width, and buckles down a touch.
        front = max(0.0, (x - (x1 - zone)) / zone)
        rear = max(0.0, ((x0 + zone) - x) / zone)
        ripple = 0.6 + 0.4 * (0.5 + 0.5 * math.sin(y * 0.09 + z * 0.05))
        x -= front ** 1.5 * 0.28 * u * ripple
        x += rear ** 1.5 * 0.20 * u * ripple
        if z > 0.4 * u:
            z -= (front + rear) ** 2 * 0.05 * u
        # The bonnet buckles up into a tent, cartoon style.
        hood = max(0.0, (x - (x1 - zone * 2.2)) / (zone * 2.2))
        if z > 0.6 * u and hood > 0.0:
            z += 0.13 * u * math.sin(math.pi * min(1.0, hood * 1.4)) * max(0.0, 1.0 - (y / hw) ** 2)
        # Knocks along the doors.
        for kx, side, depth, kz in knocks:
            if y * side > hw - 0.25 * u:
                d = math.hypot((x - kx) / (0.45 * u), (z - kz) / (0.30 * u))
                if d < 1.0:
                    y -= side * depth * 0.14 * u * (1.0 - d * d)
        # Somebody sat on the roof.
        if z > top - 0.12 * u:
            z -= 0.035 * u * max(0.0, 1.0 - (y / hw) ** 2)
        v.co = (x, y, z)
    mesh_obj.data.update()


def wheel(cut=False):
    """One wheel centred on its axle, axle along Y (the car's side axis)."""
    parts = [
        fb.make_part('cylinder', TYRE, rot=(90, 0, 0), scale=(WHEEL_RADIUS * 2, WHEEL_RADIUS * 2, 0.3), segments=18),
        fb.make_part('cylinder', HUB, rot=(90, 0, 0), scale=(WHEEL_RADIUS * 1.1, WHEEL_RADIUS * 1.1, 0.32), segments=12),
    ]
    return parts, {}


VEHICLES = {
    "SM_Car_Sedan": sedan,
    "SM_Car_Hatchback": hatchback,
    "SM_Car_Van": van,
    "SM_Car_Pickup": pickup,
    "SM_Car_Taxi": taxi,
    "SM_Car_IceCream": ice_cream,
    "SM_Car_Cruiser": cruiser,
    "SM_Wheel": wheel,
}


# --------------------------------------------------------------------------------------
# Export + preview
# --------------------------------------------------------------------------------------
def seat_occupant(name, pose_fn, loc):
    """A posed officer sat at a seat socket (preview only)."""
    import build_officer as bo
    arm = fb.build_armature(bo.BONES, name=f"Rig_{name}")
    body = fb.build_mesh_object(f"Occupant_{name}", bo.officer_parts(), bo.BONE_NAMES)
    fb.bind(body, arm)
    pose = pose_fn(0.0)
    for pb in arm.pose.bones:
        spec = pose.get(pb.name, {})
        pb.rotation_euler = [math.radians(a) for a in spec.get("rot", (0, 0, 0))]
        pb.location = [v * fb.UNIT for v in spec.get("loc", (0, 0, 0))]
    arm.location = Vector(loc) * fb.UNIT
    arm.rotation_euler = (0, 0, math.radians(90))  # rig faces -Y; seats face +X
    return [arm, body]


def preview(name, build, sockets, out_dir):
    import build_officer as bo
    for socket, loc in sockets.items():
        if socket.startswith("Wheel"):
            w = fb.build_mesh_object(f"preview_{socket}", wheel()[0])
            w.location = [v * fb.UNIT for v in loc]
    cam = fb.setup_preview((520, 360))
    bpy.context.scene.display.shading.show_backface_culling = True
    fb.render_view(os.path.join(out_dir, f"{name}.png"), cam, (7, -7, 4.8), (0, 0, 1.3), 7.6)

    # Cutaway: same vehicle without roof and glass, with people in the seats.
    bpy.data.objects[name].hide_render = True
    cut_parts, _ = build(cut=True)
    fb.build_mesh_object(name + "_cut", cut_parts, materials=MATERIALS)
    poses = {"Seat_Driver": bo.drive, "Seat_Passenger": bo.ride, "Seat_RearL": bo.ride, "Seat_RearR": bo.sit_cuffed}
    driver = []
    for socket, pose_fn in poses.items():
        if socket in sockets:
            objs = seat_occupant(socket, pose_fn, sockets[socket])
            if socket == "Seat_Driver":
                driver = objs
    fb.render_view(os.path.join(out_dir, f"{name}_cut_top.png"), cam, (4.5, -3.0, 6.5), (0, 0, 0.8), 6.0)
    fb.render_view(os.path.join(out_dir, f"{name}_cut_side.png"), cam, (0, -8, 1.4), (0, 0, 1.4), 6.6)

    # Driver's-eye view, as the interior camera sees it (the game hides the driver's own head).
    if "Cam_Driver" in sockets:
        for obj in driver:
            obj.hide_render = True
        bpy.data.objects[name].hide_render = False
        bpy.data.objects[name + "_cut"].hide_render = True
        cam.data.type = 'PERSP'
        cam.data.lens = 16
        eye = Vector(sockets["Cam_Driver"])
        fb.render_view(os.path.join(out_dir, f"{name}_eye.png"), cam, tuple(eye), tuple(eye + Vector((1.0, -0.12, -0.18))), 1.0)
        cam.data.type = 'ORTHO'


def main():
    args = fb.script_args()
    out_dir = os.path.abspath(args.get("out", "Art/Source/Vehicles"))
    preview_dir = os.path.abspath(args["preview"]) if isinstance(args.get("preview"), str) else None
    only = set(args["only"].split(",")) if isinstance(args.get("only"), str) else None

    for name, build in VEHICLES.items():
        if only and name not in only:
            continue
        fb.reset_scene()
        parts, sockets = build()
        mesh_obj = fb.build_mesh_object(name, parts, materials=None if name == "SM_Wheel" else MATERIALS)
        if name != "SM_Wheel":
            dice(mesh_obj)
        counts = {}
        for poly in mesh_obj.data.polygons:
            counts[poly.material_index] = counts.get(poly.material_index, 0) + 1
        print(f"FTO: {name} {len(mesh_obj.data.polygons)} faces, per slot {dict(sorted(counts.items()))}")
        objects = add_sockets(mesh_obj, sockets)
        if not args.get("preview_only") and not args.get("dented_only"):
            export_static(os.path.join(out_dir, f"{name}.fbx"), objects)
        if preview_dir and name != "SM_Wheel" and not args.get("dented_only"):
            preview(name, build, sockets, preview_dir)
        # The beaten-up variant (same sockets and slots).
        if name != "SM_Wheel" and (args.get("dented") or args.get("dented_only")):
            dent(mesh_obj, sum(ord(ch) for ch in name))
            if not args.get("preview_only"):
                export_static(os.path.join(out_dir, f"{name}_Dented.fbx"), objects)
            if preview_dir:
                cam = fb.setup_preview((520, 360))
                fb.render_view(os.path.join(preview_dir, f"{name}_Dented.png"), cam, (7, -7, 4.8), (0, 0, 1.3), 7.6)
                fb.render_view(os.path.join(preview_dir, f"{name}_Dented_nose.png"), cam, (6.5, -3.0, 2.0), (1.4, 0, 0.7), 3.4)


main()
