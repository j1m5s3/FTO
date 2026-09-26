"""
FTO's weapons: the taser every officer carries and the armory's pistol, shotgun and rifle. In-house, built from
code, chunky and cartoonish to match the cast (a touch oversized so they read at a distance).

  blender -b --factory-startup -P Tools/Blender/build_weapons.py -- --out Art/Source/Weapons [--preview <dir>]

Every weapon points its barrel along +X with the firing hand's grip at the origin: the game puts the grip in the
hand and turns the barrel along the aim. SOCKET_Muzzle is where shots leave the barrel.
"""
import os
import sys

import bpy
from mathutils import Vector

sys.path.append(os.path.dirname(os.path.abspath(__file__)))
import fto_blender as fb  # noqa: E402
from fto_shapes import MATERIALS, GLOW, add_sockets, ball, box, cyl, export_static, prism, ring  # noqa: E402

# Colours (sRGB)
GUNMETAL = (0.20, 0.21, 0.24)
BLACK = (0.07, 0.07, 0.08)
STEEL = (0.60, 0.62, 0.65)
WOOD = (0.58, 0.34, 0.16)
WOOD_DARK = (0.38, 0.21, 0.09)
OLIVE = (0.34, 0.38, 0.22)
TASER_YELLOW = (1.0, 0.80, 0.10)
LED_BLUE = (0.30, 0.70, 1.0)
LASER_RED = (1.0, 0.10, 0.08)

S = 1.3  # cartoon oversize


def v(*xyz):
    return tuple(c * S for c in xyz)


def trigger(x, z):
    """Trigger and its guard, just ahead of the grip."""
    return [ring(BLACK, v(x, 0, z), 0.02 * S, 0.004 * S, rot=(90, 0, 0), segments=16, rings=5),
            box(BLACK, v(x - 0.004, 0, z + 0.004), v(0.006, 0.006, 0.02), bevel=0.001)]


def pistol():
    p = [box(GUNMETAL, v(0.045, 0, 0.068), v(0.19, 0.032, 0.036), bevel=0.005),      # slide
         box(BLACK, v(0.035, 0, 0.042), v(0.16, 0.028, 0.02), bevel=0.004),          # frame
         box(BLACK, v(-0.012, 0, 0.0), v(0.034, 0.030, 0.11), bevel=0.006, rot=(0, 12, 0)),  # grip
         box(STEEL, v(0.13, 0, 0.09), v(0.008, 0.006, 0.01), bevel=0.0),             # front sight
         box(STEEL, v(-0.04, 0, 0.09), v(0.008, 0.02, 0.01), bevel=0.0),             # rear sight
         cyl(BLACK, v(0.141, 0, 0.068), 0.008 * S, 0.01 * S, axis='x', segments=10)]  # the business end
    p += trigger(0.03, 0.02)
    return p, {"Muzzle": v(0.15, 0, 0.068)}


def taser():
    p = [box(TASER_YELLOW, v(0.05, 0, 0.06), v(0.16, 0.04, 0.05), bevel=0.01),
         box(BLACK, v(0.145, 0, 0.06), v(0.04, 0.046, 0.056), bevel=0.006),          # cartridge
         box(TASER_YELLOW, v(0.166, 0, 0.06), v(0.004, 0.03, 0.04), bevel=0.0),      # cartridge face
         cyl(STEEL, v(0.168, 0, 0.072), 0.005 * S, 0.006 * S, axis='x', segments=8),  # probes
         cyl(STEEL, v(0.168, 0, 0.048), 0.005 * S, 0.006 * S, axis='x', segments=8),
         box(BLACK, v(-0.012, 0, 0.0), v(0.04, 0.036, 0.1), bevel=0.008, rot=(0, 12, 0)),
         box(LED_BLUE, v(-0.02, 0, 0.087), v(0.02, 0.012, 0.006), bevel=0.0, material=GLOW),
         box(LASER_RED, v(0.12, 0, 0.03), v(0.02, 0.01, 0.01), bevel=0.0, material=GLOW)]
    p += trigger(0.03, 0.02)
    return p, {"Muzzle": v(0.17, 0, 0.06)}


def shotgun():
    p = [box(GUNMETAL, v(0.07, 0, 0.07), v(0.2, 0.045, 0.07), bevel=0.008),         # receiver
         cyl(GUNMETAL, v(0.47, 0, 0.09), 0.018 * S, 0.62 * S, axis='x', segments=12),  # barrel
         cyl(GUNMETAL, v(0.42, 0, 0.056), 0.015 * S, 0.5 * S, axis='x', segments=10),  # magazine tube
         box(WOOD, v(0.40, 0, 0.056), v(0.2, 0.052, 0.046), bevel=0.01),             # pump
         ball(STEEL, v(0.775, 0, 0.11), 0.012 * S),                                   # bead sight
         cyl(BLACK, v(0.78, 0, 0.09), 0.012 * S, 0.01 * S, axis='x', segments=10)]
    # Wooden stock from the wrist (the grip, at the origin) back to the butt.
    p.append(prism(WOOD, [v(-0.01, 0, 0.1)[::2], v(-0.4, 0, 0.075)[::2], v(-0.43, 0, -0.07)[::2], v(-0.34, 0, -0.08)[::2],
                          v(-0.03, 0, -0.01)[::2]], -0.022 * S, 0.022 * S, bevel=0.006))
    p.append(box(WOOD_DARK, v(-0.432, 0, 0.0), v(0.02, 0.048, 0.15), bevel=0.006))  # butt pad
    p += trigger(0.04, 0.025)
    return p, {"Muzzle": v(0.79, 0, 0.09)}


def rifle():
    p = [box(BLACK, v(0.06, 0, 0.07), v(0.24, 0.05, 0.07), bevel=0.008),           # receiver
         box(OLIVE, v(0.31, 0, 0.075), v(0.26, 0.056, 0.06), bevel=0.012),           # handguard
         cyl(BLACK, v(0.56, 0, 0.078), 0.012 * S, 0.28 * S, axis='x', segments=10),  # barrel
         cyl(BLACK, v(0.71, 0, 0.078), 0.018 * S, 0.05 * S, axis='x', segments=10),  # muzzle brake
         box(BLACK, v(0.085, 0, -0.03), v(0.05, 0.036, 0.13), bevel=0.006, rot=(0, -14, 0)),  # magazine
         box(BLACK, v(-0.012, 0, 0.0), v(0.036, 0.036, 0.1), bevel=0.006, rot=(0, 12, 0)),    # pistol grip
         box(OLIVE, v(-0.22, 0, 0.055), v(0.34, 0.046, 0.08), bevel=0.012),          # stock
         box(BLACK, v(-0.4, 0, 0.05), v(0.03, 0.05, 0.11), bevel=0.006),             # butt pad
         box(BLACK, v(0.07, 0, 0.125), v(0.13, 0.022, 0.03), bevel=0.004),           # sight rail
         box(BLACK, v(0.52, 0, 0.11), v(0.012, 0.01, 0.05), bevel=0.0),              # front sight
         box(LASER_RED, v(0.1, 0, 0.145), v(0.02, 0.012, 0.012), bevel=0.0, material=GLOW)]  # red dot
    p += trigger(0.04, 0.025)
    return p, {"Muzzle": v(0.74, 0, 0.078)}


WEAPONS = {
    "SM_Taser": taser,
    "SM_Pistol": pistol,
    "SM_Shotgun": shotgun,
    "SM_Rifle": rifle,
}


def main():
    args = fb.script_args()
    out_dir = os.path.abspath(args.get("out", "Art/Source/Weapons"))
    preview_dir = os.path.abspath(args["preview"]) if isinstance(args.get("preview"), str) else None

    for name, build in WEAPONS.items():
        fb.reset_scene()
        parts, sockets = build()
        obj = fb.build_mesh_object(name, parts, materials=MATERIALS)
        fb.set_active(obj)
        bpy.ops.object.material_slot_remove_unused()
        objects = add_sockets(obj, sockets)
        print(f"FTO: {name} {len(obj.data.polygons)} faces, sockets {list(sockets)}")
        export_static(os.path.join(out_dir, f"{name}.fbx"), objects)

        if preview_dir:
            xs = [p.co.x / fb.UNIT for p in obj.data.vertices]
            zs = [p.co.z / fb.UNIT for p in obj.data.vertices]
            centre = Vector(((min(xs) + max(xs)) / 2, 0, (min(zs) + max(zs)) / 2))
            size = max(max(xs) - min(xs), 0.3)
            cam = fb.setup_preview((480, 320))
            fb.render_view(os.path.join(preview_dir, f"{name}.png"), cam, tuple(centre + Vector((0.2, -1.2, 0.35)) * size),
                           tuple(centre), size * 1.1)


main()
