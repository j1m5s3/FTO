"""
Builds FTO's cartoon vehicles as static meshes (no rig): traffic cars and the police cruiser.

  blender -b --factory-startup -P Tools/Blender/build_vehicles.py -- --out Art/Source/Vehicles [--preview <dir>]

Vehicles are modelled facing +X (Unreal forward), wheels sit on the ground at Z=0.
Paint panels have vertex alpha 1 so the game gives each traffic car its own colour (and tints
the cruiser's door stripe with the driver's badge colour). Wheel positions are exported as
SOCKET_Wheel_FL/FR/RL/RR empties, which Unreal imports as mesh sockets.
"""
import math
import os
import sys

import bpy

sys.path.append(os.path.dirname(os.path.abspath(__file__)))
import fto_blender as fb  # noqa: E402

PAINT = (0.85, 0.85, 0.85)      # tinted in game
GLASS = (0.10, 0.16, 0.26)
TRIM = (0.12, 0.12, 0.13)
CHROME = (0.75, 0.76, 0.78)
HEADLIGHT = (1.0, 0.95, 0.75)
TAILLIGHT = (0.9, 0.08, 0.08)
TYRE = (0.05, 0.05, 0.05)
HUB = (0.65, 0.66, 0.70)
WHITE = (0.96, 0.96, 0.96)
BLACK = (0.04, 0.04, 0.05)
YELLOW = (1.0, 0.80, 0.10)
PINK = (1.0, 0.62, 0.78)
SIREN_RED = (1.0, 0.1, 0.1)
SIREN_BLUE = (0.1, 0.3, 1.0)

WHEEL_RADIUS = 0.38


def box(color, center, size, bevel=0.06, tint=False, rot=(0, 0, 0)):
    return fb.make_part('cube', color, loc=center, rot=rot, scale=size, bevel=bevel, tint=tint, smooth=False)


def car_body(length, width, body_h, cabin_len, cabin_h, cabin_x, paint=PAINT, tint=True, cabin_paint=None):
    """Chunky two-box car: lower body + cabin with window bands, lights and bumpers."""
    lift = WHEEL_RADIUS * 0.75
    body_z = lift + body_h / 2
    cabin_z = lift + body_h + cabin_h / 2 - 0.02
    cabin_color = cabin_paint or paint
    parts = [
        box(paint, (0, 0, body_z), (length, width, body_h), bevel=0.12, tint=tint),
        box(cabin_color, (cabin_x, 0, cabin_z), (cabin_len, width * 0.9, cabin_h), bevel=0.1, tint=tint),
        # Windows: a glass band slightly proud of the cabin sides, plus windscreen/rear glass.
        box(GLASS, (cabin_x, 0, cabin_z + 0.03), (cabin_len * 0.92, width * 0.92, cabin_h * 0.6), bevel=0.04),
        box(GLASS, (cabin_x + cabin_len / 2 - 0.02, 0, cabin_z + 0.02), (0.06, width * 0.8, cabin_h * 0.62), bevel=0.02),
        box(GLASS, (cabin_x - cabin_len / 2 + 0.02, 0, cabin_z + 0.02), (0.06, width * 0.8, cabin_h * 0.62), bevel=0.02),
        # Bumpers
        box(TRIM, (length / 2 + 0.02, 0, lift + 0.12), (0.16, width * 1.02, 0.22), bevel=0.05),
        box(TRIM, (-length / 2 - 0.02, 0, lift + 0.12), (0.16, width * 1.02, 0.22), bevel=0.05),
    ]
    for side in (1, -1):
        parts += [
            box(HEADLIGHT, (length / 2 + 0.01, side * width * 0.33, body_z + 0.08), (0.06, 0.34, 0.16), bevel=0.02),
            box(TAILLIGHT, (-length / 2 - 0.01, side * width * 0.36, body_z + 0.08), (0.06, 0.3, 0.14), bevel=0.02),
            # Wheel arches
            box(TRIM, (length * 0.3, side * width * 0.48, lift + 0.2), (1.0, 0.1, 0.34), bevel=0.04),
            box(TRIM, (-length * 0.3, side * width * 0.48, lift + 0.2), (1.0, 0.1, 0.34), bevel=0.04),
        ]
    return parts, lift


def wheel_sockets(length, width):
    x = length * 0.3
    y = width * 0.5
    return {
        "Wheel_FL": (x, y, WHEEL_RADIUS),
        "Wheel_FR": (x, -y, WHEEL_RADIUS),
        "Wheel_RL": (-x, y, WHEEL_RADIUS),
        "Wheel_RR": (-x, -y, WHEEL_RADIUS),
    }


def sedan():
    parts, _ = car_body(4.2, 1.95, 0.62, 2.2, 0.62, -0.2)
    return parts, wheel_sockets(4.2, 1.95)


def hatchback():
    parts, _ = car_body(3.5, 1.85, 0.62, 2.0, 0.66, -0.45)
    return parts, wheel_sockets(3.5, 1.85)


def van():
    parts, lift = car_body(4.6, 2.05, 0.7, 3.5, 0.95, -0.45)
    return parts, wheel_sockets(4.6, 2.05)


def pickup():
    parts, lift = car_body(4.5, 2.0, 0.62, 1.7, 0.66, 0.55)
    # Open bed: side walls and tailgate.
    for side in (1, -1):
        parts.append(box(PAINT, (-1.2, side * 0.92, lift + 0.62 + 0.18), (1.9, 0.12, 0.36), bevel=0.03, tint=True))
    parts.append(box(PAINT, (-2.12, 0, lift + 0.62 + 0.18), (0.12, 1.9, 0.36), bevel=0.03, tint=True))
    return parts, wheel_sockets(4.5, 2.0)


def taxi():
    parts, lift = car_body(4.2, 1.95, 0.62, 2.2, 0.62, -0.2, paint=YELLOW, tint=False)
    roof = lift + 0.62 + 0.62
    parts.append(box(WHITE, (-0.2, 0, roof + 0.1), (0.6, 0.3, 0.2), bevel=0.04))
    # Chequer stripe along each side.
    for i in range(12):
        for side in (1, -1):
            color = BLACK if i % 2 else WHITE
            parts.append(box(color, (-1.65 + i * 0.3, side * 0.985, lift + 0.42), (0.3, 0.02, 0.12), bevel=0.0))
    return parts, wheel_sockets(4.2, 1.95)


def ice_cream():
    parts, lift = car_body(4.8, 2.1, 0.75, 3.7, 1.1, -0.45, paint=WHITE, tint=False)
    top = lift + 0.75 + 1.1
    parts += [
        box(PINK, (-0.45, 0, lift + 1.2), (3.72, 2.12, 0.18), bevel=0.02),        # pink band
        fb.make_part('cone', (0.93, 0.78, 0.50), loc=(-0.6, 0, top + 0.45), rot=(180, 0, 0), scale=(0.55, 0.55, 0.8), segments=12),
        fb.make_part('sphere', PINK, loc=(-0.6, 0, top + 0.95), scale=(0.62, 0.62, 0.55)),
        fb.make_part('sphere', (0.55, 0.30, 0.15), loc=(-0.6, 0, top + 1.25), scale=(0.3, 0.3, 0.25)),  # chocolate top
        box(GLASS, (-0.8, -1.07, lift + 1.4), (1.6, 0.04, 0.55), bevel=0.02),    # serving hatch
    ]
    return parts, wheel_sockets(4.8, 2.1)


def cruiser():
    """Classic black-and-white with a light bar. The door stripe is tinted with the driver's colour."""
    length, width = 4.4, 2.0
    parts, lift = car_body(length, width, 0.64, 2.3, 0.62, -0.25, paint=BLACK, tint=False, cabin_paint=WHITE)
    body_z = lift + 0.32
    for side in (1, -1):
        parts += [
            box(WHITE, (-0.1, side * (width / 2 + 0.005), body_z + 0.02), (2.2, 0.03, 0.56), bevel=0.02),   # white doors
            box(PAINT, (-0.1, side * (width / 2 + 0.015), body_z + 0.02), (2.2, 0.02, 0.12), bevel=0.0, tint=True),  # badge stripe
            box(GOLD_STAR, (-0.1, side * (width / 2 + 0.03), body_z + 0.2), (0.22, 0.02, 0.22), bevel=0.0, rot=(0, 0, 0)),
        ]
    roof = lift + 0.64 + 0.62
    parts += [
        box(TRIM, (-0.25, 0, roof + 0.05), (0.5, 1.5, 0.1), bevel=0.03),
        box(SIREN_RED, (-0.25, 0.4, roof + 0.16), (0.4, 0.6, 0.16), bevel=0.04),
        box(SIREN_BLUE, (-0.25, -0.4, roof + 0.16), (0.4, 0.6, 0.16), bevel=0.04),
        # Push bar
        box(TRIM, (length / 2 + 0.2, 0, lift + 0.35), (0.08, 1.2, 0.5), bevel=0.02),
        box(TRIM, (length / 2 + 0.12, 0.45, lift + 0.35), (0.2, 0.08, 0.5), bevel=0.02),
        box(TRIM, (length / 2 + 0.12, -0.45, lift + 0.35), (0.2, 0.08, 0.5), bevel=0.02),
    ]
    return parts, wheel_sockets(length, width)


GOLD_STAR = (1.0, 0.78, 0.18)


def wheel():
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


def add_sockets(mesh_obj, sockets):
    objects = [mesh_obj]
    for name, loc in sockets.items():
        empty = fb.link(bpy.data.objects.new(f"SOCKET_{name}", None))
        empty.empty_display_type = 'PLAIN_AXES'
        empty.location = loc
        empty.parent = mesh_obj
        objects.append(empty)
    return objects


def export_static(path, objects):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    for other in bpy.context.view_layer.objects:
        other.select_set(False)
    for obj in objects:
        obj.select_set(True)
    bpy.context.view_layer.objects.active = objects[0]
    bpy.ops.export_scene.fbx(
        filepath=path,
        use_selection=True,
        object_types={'MESH', 'EMPTY'},
        apply_unit_scale=True,
        apply_scale_options='FBX_SCALE_UNITS',
        axis_forward='-Z',
        axis_up='Y',
        mesh_smooth_type='FACE',
        colors_type='LINEAR',
        bake_anim=False,
    )
    print(f"FTO: exported {path}")


def main():
    args = fb.script_args()
    out_dir = os.path.abspath(args.get("out", "Art/Source/Vehicles"))
    preview_dir = args.get("preview")

    for name, build in VEHICLES.items():
        fb.reset_scene()
        parts, sockets = build()
        mesh_obj = fb.build_mesh_object(name, parts)
        objects = add_sockets(mesh_obj, sockets)
        export_static(os.path.join(out_dir, f"{name}.fbx"), objects)

        if preview_dir and name != "SM_Wheel":
            # Show the car with wheels in place for review.
            for socket, loc in sockets.items():
                wheel_parts, _ = wheel()
                w = fb.build_mesh_object(f"preview_{socket}", wheel_parts)
                w.location = loc
            cam = fb.setup_preview((420, 300))
            fb.render_view(os.path.join(os.path.abspath(preview_dir), f"{name}.png"), cam, (7, -7, 4.5), (0, 0, 0.8), 6.5)


main()
