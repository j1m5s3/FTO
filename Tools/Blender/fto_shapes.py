"""
Shape helpers for FTO's static props (vehicles, the building kit, furniture, street dressing), in metres.

Everything is built from coloured parts (see fto_blender.make_part) into one mesh with up to three material
slots: "Body" (vertex colours; vertex alpha 1 marks areas the game tints), "Glass" (translucent) and "Glow"
(emissive). Simple collision comes from UCX_ boxes exported alongside the mesh.
"""
import math
import os

import bmesh
import bpy
from mathutils import Euler, Vector

import fto_blender as fb

# Material slots
BODY, GLASS, GLOW = 0, 1, 2
MATERIALS = ["Body", "Glass", "Glow"]


# --------------------------------------------------------------------------------------
# Parts
# --------------------------------------------------------------------------------------
def box(color, center, size, bevel=0.03, tint=False, rot=(0, 0, 0), material=BODY):
    return fb.make_part('cube', color, loc=center, rot=rot, scale=size, bevel=bevel, tint=tint, smooth=False,
                        material=material)


def cyl(color, center, radius, length, axis='z', rot=None, segments=12, material=BODY, tint=False):
    rot = rot if rot is not None else {'x': (0, 90, 0), 'y': (90, 0, 0), 'z': (0, 0, 0)}[axis]
    return fb.make_part('cylinder', color, loc=center, rot=rot, scale=(radius * 2, radius * 2, length),
                        segments=segments, material=material, tint=tint)


def cone(color, center, radius, length, rot=(0, 0, 0), segments=12, material=BODY, tint=False):
    return fb.make_part('cone', color, loc=center, rot=rot, scale=(radius * 2, radius * 2, length),
                        segments=segments, material=material, tint=tint)


def ball(color, center, size, material=BODY, tint=False, segments=12, rings=8):
    scale = size if isinstance(size, (tuple, list)) else (size, size, size)
    return fb.make_part('sphere', color, loc=center, scale=scale, material=material, segments=segments, rings=rings,
                        tint=tint)


def ring(color, center, outer_radius, tube_radius, rot=(0, 0, 0), segments=24, rings=8, material=BODY):
    """A torus lying in the XY plane (before `rot`)."""
    return fb.make_part('torus', color, loc=center, rot=rot, scale=(outer_radius * 2,) * 3,
                        minor=tube_radius / (outer_radius * 2), segments=segments, rings=rings, material=material)


def beam(color, p0, p1, width, thick, bevel=0.02, tint=False, material=BODY):
    """A box running from p0 to p1: `width` across (kept horizontal), `thick` through."""
    a, b = Vector(p0), Vector(p1)
    d = b - a
    pitch = -math.degrees(math.atan2(d.z, math.hypot(d.x, d.y)))
    yaw = math.degrees(math.atan2(d.y, d.x))
    return fb.make_part('cube', color, loc=(a + b) / 2, rot=(0, pitch, yaw), scale=(d.length, width, thick),
                        bevel=bevel, tint=tint, smooth=False, material=material)


def prism(color, profile, y0, y1, bevel=0.0, tint=False, material=BODY):
    """Convex outline in the XZ plane, extruded along Y."""
    return fb.make_prism(color, profile, min(y0, y1), max(y0, y1), tint=tint, bevel=bevel, material=material)


def star(color, cx, cz, y0, y1, r_out=0.12, r_in=0.05):
    """Five-pointed star in the XZ plane (a pentagon plus five points), extruded y0..y1."""
    def point(r, degrees):
        a = math.radians(degrees)
        return (cx + r * math.cos(a), cz + r * math.sin(a))
    parts = [prism(color, [point(r_in, 126 + 72 * k) for k in range(5)], y0, y1)]
    for k in range(5):
        tip = 90 + 72 * k
        parts.append(prism(color, [point(r_in, tip - 36), point(r_out, tip), point(r_in, tip + 36)], y0, y1))
    return parts


class Frame:
    """A local frame (centre + XYZ Euler degrees) for parts that sit on something tilted or turned."""

    def __init__(self, center, rot):
        self.center = Vector(center)
        self.rot = tuple(rot)
        self.matrix = Euler([math.radians(a) for a in rot], 'XYZ').to_matrix()

    def at(self, offset):
        return self.center + self.matrix @ Vector(offset)


# Rotations that stand lettering up on a face so it reads left-to-right to someone looking at that face.
FACING = {'+y': (90, 0, 180), '-y': (90, 0, 0), '+x': (90, 0, 90), '-x': (90, 0, -90)}


def lettering(text, color, center, facing, size, material=BODY, tint=False):
    return fb.make_text(text, color, loc=center, rot=FACING[facing], size=size, depth=0.01, material=material, tint=tint)


def both_flanks(text, color, x, z, half_width, size):
    """The same lettering on the left and right flanks."""
    return [lettering(text, color, (x, half_width, z), '+y', size), lettering(text, color, (x, -half_width, z), '-y', size)]


# --------------------------------------------------------------------------------------
# Collision, sockets, export
# --------------------------------------------------------------------------------------
def collision_boxes(mesh_name, boxes):
    """
    Simple collision for a static mesh: one UCX_<mesh>_NN box per (center, size) in metres. Unreal's FBX
    importer turns these into the mesh's collision (so a door frame can leave its doorway open).
    """
    objects = []
    for i, (center, size) in enumerate(boxes):
        bm = bmesh.new()
        bmesh.ops.create_cube(bm, size=1.0)
        bmesh.ops.transform(bm, verts=bm.verts, matrix=fb._transform(center, (0, 0, 0), size))
        mesh = bpy.data.meshes.new(f"UCX_{mesh_name}_{i:02d}")
        bm.to_mesh(mesh)
        bm.free()
        objects.append(fb.link(bpy.data.objects.new(f"UCX_{mesh_name}_{i:02d}", mesh)))
    return objects


def add_sockets(mesh_obj, sockets):
    objects = [mesh_obj]
    for name, loc in sockets.items():
        empty = fb.link(bpy.data.objects.new(f"SOCKET_{name}", None))
        empty.empty_display_type = 'PLAIN_AXES'
        empty.location = [v * fb.UNIT for v in loc]
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
