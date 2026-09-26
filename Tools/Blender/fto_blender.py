"""
Shared helpers for FTO's scripted Blender assets.

Everything is built from code so art stays reproducible, reviewable and 100% in-house:
low-poly parts get flat colours baked into a vertex colour layer ("Col"). Vertex alpha = 1
marks areas the game tints at runtime (uniform, car paint); see Tools/Unreal/create_materials.py.

Units: scripts author in metres (easy to reason about); the helpers emit centimetres (UNIT = 100) into a
scene whose unit is 1 cm, so the FBX carries real centimetres and Unreal imports at 1:1. (Letting Unreal apply
a x100 import scale instead puts that scale on the root bone, which breaks physics and attachments.)
Characters face -Y, Z up.
"""
import math
import os
import sys

import bmesh
import bpy
from mathutils import Euler, Matrix, Vector

# Metres (what the scripts write) to Blender units (centimetres in an 0.01 m-unit scene).
UNIT = 100.0


# --------------------------------------------------------------------------------------
# Command line
# --------------------------------------------------------------------------------------
def script_args():
    """Arguments after `--` on the Blender command line, as a dict of --key value."""
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    out = {}
    key = None
    for token in argv:
        if token.startswith("--"):
            key = token[2:]
            out[key] = True
        elif key:
            out[key] = token
            key = None
    return out


# --------------------------------------------------------------------------------------
# Scene
# --------------------------------------------------------------------------------------
def reset_scene(fps=30):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    scene.render.fps = fps
    scene.unit_settings.system = 'METRIC'
    scene.unit_settings.scale_length = 0.01  # 1 Blender unit = 1 cm
    return scene


def link(obj):
    bpy.context.scene.collection.objects.link(obj)
    return obj


def set_active(obj):
    for other in bpy.context.view_layer.objects:
        other.select_set(False)
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj


# --------------------------------------------------------------------------------------
# Parts: each part is its own little bmesh (shape + colour + bone weight), merged later
# --------------------------------------------------------------------------------------
def srgb_to_linear(c):
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


class Part:
    """A coloured primitive bound rigidly to one bone (or to nothing for static meshes)."""

    def __init__(self, bm, bone=None):
        self.bm = bm
        self.bone = bone


def _transform(loc=(0, 0, 0), rot=(0, 0, 0), scale=(1, 1, 1)):
    return (Matrix.Translation(Vector(loc) * UNIT)
            @ Euler([math.radians(a) for a in rot], 'XYZ').to_matrix().to_4x4()
            @ Matrix.Diagonal((Vector(scale) * UNIT).to_4d()))


def _create_torus(bm, minor, segments, rings):
    """Ring in the XY plane, outer radius 0.5; `minor` is the tube radius at that size."""
    uv = bm.loops.layers.uv.active
    major = 0.5 - minor
    grid = []
    for i in range(segments):
        u = 2.0 * math.pi * i / segments
        row = []
        for j in range(rings):
            v = 2.0 * math.pi * j / rings
            r = major + minor * math.cos(v)
            row.append(bm.verts.new((r * math.cos(u), r * math.sin(u), minor * math.sin(v))))
        grid.append(row)
    for i in range(segments):
        for j in range(rings):
            i2, j2 = (i + 1) % segments, (j + 1) % rings
            face = bm.faces.new((grid[i][j], grid[i2][j], grid[i2][j2], grid[i][j2]))
            for loop, (a, b) in zip(face.loops, ((i, j), (i + 1, j), (i + 1, j + 1), (i, j + 1))):
                loop[uv].uv = (a / segments, b / rings)


def _finish_part(bm, color, bone, tint, bevel, smooth, material, stripes=None):
    """Bevel, flat vertex colour (alpha = tint mask) and material slot for a part's bmesh."""
    if bevel > 0.0:
        bmesh.ops.bevel(bm, geom=list(bm.edges) + list(bm.verts), offset=bevel * UNIT, segments=2,
                        affect='EDGES', profile=0.5, clamp_overlap=True)

    col_layer = bm.loops.layers.color.new("Col")
    alpha = 1.0 if tint else 0.0
    rgba = (color[0], color[1], color[2], alpha)
    for face in bm.faces:
        face.smooth = smooth
        face.material_index = material
        face_rgba = rgba
        if stripes:
            band = int(math.floor(face.calc_center_median().z / (stripes[1] * UNIT)))
            if band % 2:
                face_rgba = (stripes[0][0], stripes[0][1], stripes[0][2], alpha)
        for loop in face.loops:
            loop[col_layer] = face_rgba
    return Part(bm, bone)


def make_part(kind, color, loc=(0, 0, 0), rot=(0, 0, 0), scale=(1, 1, 1), bone=None,
              tint=False, bevel=0.0, smooth=True, segments=16, rings=10, stripes=None,
              material=0, minor=0.1):
    """
    kind: 'sphere' | 'cube' | 'cylinder' | 'cone' | 'torus'. Unit-sized (1 m) before `scale`.
    color: sRGB tuple (0-1). tint=True sets vertex alpha 1 so the game can recolour it.
    material: index into the object's material slots (see build_mesh_object).
    minor: torus tube radius (the ring's outer radius is 0.5 before scaling).
    """
    bm = bmesh.new()
    bm.loops.layers.uv.new("UVMap")
    if kind == 'sphere':
        bmesh.ops.create_uvsphere(bm, u_segments=segments, v_segments=rings, radius=0.5, calc_uvs=True)
    elif kind == 'cube':
        bmesh.ops.create_cube(bm, size=1.0, calc_uvs=True)
    elif kind == 'cylinder':
        bmesh.ops.create_cone(bm, cap_ends=True, cap_tris=False, segments=segments,
                              radius1=0.5, radius2=0.5, depth=1.0, calc_uvs=True)
    elif kind == 'cone':
        bmesh.ops.create_cone(bm, cap_ends=True, cap_tris=False, segments=segments,
                              radius1=0.5, radius2=0.0, depth=1.0, calc_uvs=True)
    elif kind == 'torus':
        _create_torus(bm, minor, segments, rings)
    else:
        raise ValueError(kind)

    bmesh.ops.transform(bm, matrix=_transform(loc, rot, scale), verts=bm.verts)
    return _finish_part(bm, color, bone, tint, bevel, smooth, material, stripes)


def make_prism(color, profile, y0, y1, tint=False, bevel=0.0, smooth=False, material=0, bone=None):
    """
    Extrudes a convex outline in the XZ plane (list of (x, z) metres) from y0 to y1: sloped hoods,
    raked windscreens, trapezoid windows and other shapes a box can't make.
    """
    bm = bmesh.new()
    uv = bm.loops.layers.uv.new("UVMap")
    near = [bm.verts.new((x * UNIT, y0 * UNIT, z * UNIT)) for x, z in profile]
    far = [bm.verts.new((x * UNIT, y1 * UNIT, z * UNIT)) for x, z in profile]
    bm.faces.new(near)
    bm.faces.new(list(reversed(far)))
    for i in range(len(profile)):
        j = (i + 1) % len(profile)
        bm.faces.new((near[i], near[j], far[j], far[i]))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    for face in bm.faces:  # rough planar UVs; FTO materials only read vertex colour
        for loop in face.loops:
            co = loop.vert.co / (4.0 * UNIT)
            loop[uv].uv = (co.x + co.y, co.z)
    return _finish_part(bm, color, bone, tint, bevel, smooth, material)


def make_text(text, color, loc=(0, 0, 0), rot=(0, 0, 0), size=0.2, depth=0.01, bold=0.03, material=0, tint=False):
    """
    Lettering from Blender's built-in font as a part. Before `rot` it reads along +X with its face
    toward +Z, centred on `loc`; `size` is the letter height in metres.
    """
    curve = bpy.data.curves.new("FTOText", type='FONT')
    curve.body = text
    curve.size = size * UNIT
    curve.extrude = depth * UNIT / 2
    curve.offset = bold * size * UNIT
    curve.align_x = 'CENTER'
    curve.align_y = 'CENTER'
    curve.resolution_u = 3  # low-poly letters
    obj = link(bpy.data.objects.new("FTOText", curve))
    evaluated = obj.evaluated_get(bpy.context.evaluated_depsgraph_get())
    bm = bmesh.new()
    bm.from_mesh(evaluated.to_mesh())
    evaluated.to_mesh_clear()
    bpy.context.scene.collection.objects.unlink(obj)
    bpy.data.objects.remove(obj)
    bpy.data.curves.remove(curve)
    bpy.context.view_layer.update()

    if not bm.loops.layers.uv:
        bm.loops.layers.uv.new("UVMap")
    for layer in list(bm.loops.layers.color):
        bm.loops.layers.color.remove(layer)
    matrix = Matrix.Translation(Vector(loc) * UNIT) @ Euler([math.radians(a) for a in rot], 'XYZ').to_matrix().to_4x4()
    bmesh.ops.transform(bm, matrix=matrix, verts=bm.verts)
    return _finish_part(bm, color, None, tint, 0.0, False, material)


def build_mesh_object(name, parts, bone_names=None, materials=None):
    """
    Merges parts into one mesh object. Parts with a bone get a rigid weight of 1.
    materials: slot names in order (a part's `material` indexes this list); each becomes an FBX
    material, i.e. a named material slot in Unreal.
    """
    bone_index = {b: i for i, b in enumerate(bone_names or [])}
    mesh = bpy.data.meshes.new(name)
    for slot_name in materials or []:
        mat = bpy.data.materials.get(slot_name) or bpy.data.materials.new(slot_name)
        mesh.materials.append(mat)
    merged = bmesh.new()
    scratch = bpy.data.meshes.new(name + "_scratch")

    for part in parts:
        if part.bone is not None:
            deform = part.bm.verts.layers.deform.verify()
            group = bone_index[part.bone]
            for vert in part.bm.verts:
                vert[deform][group] = 1.0
        part.bm.to_mesh(scratch)
        merged.from_mesh(scratch)  # appends
        part.bm.free()

    merged.to_mesh(mesh)
    merged.free()
    bpy.data.meshes.remove(scratch)

    # Make "Col" the active + render colour layer so viewports, previews and FBX all use it.
    colors = mesh.color_attributes
    if "Col" in colors:
        colors.active_color = colors["Col"]
        colors.render_color_index = colors.active_color_index

    obj = link(bpy.data.objects.new(name, mesh))
    for bone in bone_names or []:
        obj.vertex_groups.new(name=bone)
    return obj


# --------------------------------------------------------------------------------------
# Rig + animation
# --------------------------------------------------------------------------------------
def build_armature(bones, name="Armature"):
    """
    bones: list of (name, head, tail, parent). The object is named "Armature" on purpose:
    Unreal's FBX importer then drops the armature node instead of adding an extra root bone.
    """
    data = bpy.data.armatures.new(name + "Data")
    obj = link(bpy.data.objects.new(name, data))
    set_active(obj)
    bpy.ops.object.mode_set(mode='EDIT')
    for bone_name, head, tail, parent in bones:
        eb = data.edit_bones.new(bone_name)
        eb.head = Vector(head) * UNIT
        eb.tail = Vector(tail) * UNIT
        eb.roll = 0.0
        if parent:
            eb.parent = data.edit_bones[parent]
            eb.use_connect = False
    bpy.ops.object.mode_set(mode='OBJECT')
    for pb in obj.pose.bones:
        pb.rotation_mode = 'XYZ'
    return obj


def bind(mesh_obj, arm_obj):
    mesh_obj.parent = arm_obj
    mod = mesh_obj.modifiers.new("Armature", 'ARMATURE')
    mod.object = arm_obj


def reset_pose(arm_obj):
    for pb in arm_obj.pose.bones:
        pb.location = (0, 0, 0)
        pb.rotation_euler = (0, 0, 0)
        pb.scale = (1, 1, 1)


def key_pose(arm_obj, frame, pose):
    """
    pose: {bone: {"rot": (x, y, z) degrees, "loc": (x, y, z) metres in bone space}}.
    Unlisted bones are keyed at rest so every clip is self-contained.
    """
    for pb in arm_obj.pose.bones:
        spec = pose.get(pb.name, {})
        rot = spec.get("rot", (0.0, 0.0, 0.0))
        loc = spec.get("loc", (0.0, 0.0, 0.0))
        pb.rotation_euler = [math.radians(a) for a in rot]
        pb.location = [v * UNIT for v in loc]
        pb.keyframe_insert(data_path="rotation_euler", frame=frame)
        pb.keyframe_insert(data_path="location", frame=frame)


def bake_clip(arm_obj, name, frames, pose_at):
    """Keys pose_at(t) for t in [0, 1] on every frame 0..frames (last == first for loops)."""
    arm_obj.animation_data_clear()
    reset_pose(arm_obj)
    for f in range(frames + 1):
        key_pose(arm_obj, f, pose_at(f / frames))
    action = arm_obj.animation_data.action
    action.name = name
    scene = bpy.context.scene
    scene.frame_start = 0
    scene.frame_end = frames
    return action


# --------------------------------------------------------------------------------------
# Export + preview
# --------------------------------------------------------------------------------------
def export_fbx(path, objects, with_animation):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    for other in bpy.context.view_layer.objects:
        other.select_set(False)
    for obj in objects:
        obj.select_set(True)
    bpy.context.view_layer.objects.active = objects[0]
    bpy.ops.export_scene.fbx(
        filepath=path,
        use_selection=True,
        object_types={'ARMATURE', 'MESH'},
        apply_unit_scale=True,
        apply_scale_options='FBX_SCALE_UNITS',
        axis_forward='-Z',
        axis_up='Y',
        mesh_smooth_type='FACE',
        colors_type='LINEAR',
        add_leaf_bones=False,
        primary_bone_axis='Y',
        secondary_bone_axis='X',
        armature_nodetype='NULL',
        bake_anim=with_animation,
        bake_anim_use_all_actions=False,
        bake_anim_use_nla_strips=False,
        bake_anim_force_startend_keying=True,
        bake_anim_simplify_factor=0.0,
    )
    print(f"FTO: exported {path}")


def setup_preview(resolution=(640, 640)):
    scene = bpy.context.scene
    scene.render.engine = 'BLENDER_WORKBENCH'
    scene.display.shading.light = 'STUDIO'
    scene.display.shading.color_type = 'VERTEX'
    scene.display.shading.show_shadows = True
    scene.display.shading.show_cavity = True
    scene.render.resolution_x, scene.render.resolution_y = resolution
    scene.render.film_transparent = False
    scene.world = bpy.data.worlds.new("PreviewWorld")
    scene.world.color = (0.55, 0.62, 0.72)

    cam_data = bpy.data.cameras.new("PreviewCam")
    cam_data.type = 'ORTHO'
    cam_data.clip_start = 1.0
    cam_data.clip_end = 100000.0
    cam = link(bpy.data.objects.new("PreviewCam", cam_data))
    scene.camera = cam
    return cam


def render_view(path, cam, location, look_at, ortho_scale, frame=0):
    scene = bpy.context.scene
    cam.location = Vector(location) * UNIT
    direction = (Vector(look_at) - Vector(location)) * UNIT
    cam.rotation_euler = direction.to_track_quat('-Z', 'Y').to_euler()
    cam.data.ortho_scale = ortho_scale * UNIT
    scene.frame_set(frame)
    scene.render.filepath = path
    bpy.ops.render.render(write_still=True)
