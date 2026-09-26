"""
Shared helpers for FTO's scripted Blender assets.

Everything is built from code so art stays reproducible, reviewable and 100% in-house:
low-poly parts get flat colours baked into a vertex colour layer ("Col"). Vertex alpha = 1
marks areas the game tints at runtime (uniform, car paint); see Tools/Unreal/create_materials.py.

Units: Blender metres, character faces -Y, Z up. Exported FBX lands in Unreal as centimetres.
"""
import math
import os
import sys

import bmesh
import bpy
from mathutils import Euler, Matrix, Vector


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
    scene.unit_settings.scale_length = 1.0
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
    return (Matrix.Translation(Vector(loc))
            @ Euler([math.radians(a) for a in rot], 'XYZ').to_matrix().to_4x4()
            @ Matrix.Diagonal(Vector(scale).to_4d()))


def make_part(kind, color, loc=(0, 0, 0), rot=(0, 0, 0), scale=(1, 1, 1), bone=None,
              tint=False, bevel=0.0, smooth=True, segments=16, rings=10, stripes=None):
    """
    kind: 'sphere' | 'cube' | 'cylinder' | 'cone'. Unit-sized (1 m) before `scale`.
    color: sRGB tuple (0-1). tint=True sets vertex alpha 1 so the game can recolour it.
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
    else:
        raise ValueError(kind)

    bmesh.ops.transform(bm, matrix=_transform(loc, rot, scale), verts=bm.verts)

    if bevel > 0.0:
        bmesh.ops.bevel(bm, geom=list(bm.edges) + list(bm.verts), offset=bevel, segments=2,
                        affect='EDGES', profile=0.5, clamp_overlap=True)

    col_layer = bm.loops.layers.color.new("Col")
    alpha = 1.0 if tint else 0.0
    rgba = (color[0], color[1], color[2], alpha)
    for face in bm.faces:
        face.smooth = smooth
        face_rgba = rgba
        if stripes:
            band = int(math.floor(face.calc_center_median().z / stripes[1]))
            if band % 2:
                face_rgba = (stripes[0][0], stripes[0][1], stripes[0][2], alpha)
        for loop in face.loops:
            loop[col_layer] = face_rgba
    return Part(bm, bone)


def build_mesh_object(name, parts, bone_names=None):
    """Merges parts into one mesh object. Parts with a bone get a rigid weight of 1."""
    bone_index = {b: i for i, b in enumerate(bone_names or [])}
    mesh = bpy.data.meshes.new(name)
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
        eb.head = Vector(head)
        eb.tail = Vector(tail)
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
        pb.location = loc
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
    cam = link(bpy.data.objects.new("PreviewCam", cam_data))
    scene.camera = cam
    return cam


def render_view(path, cam, location, look_at, ortho_scale, frame=0):
    scene = bpy.context.scene
    cam.location = Vector(location)
    direction = Vector(look_at) - Vector(location)
    cam.rotation_euler = direction.to_track_quat('-Z', 'Y').to_euler()
    cam.data.ortho_scale = ortho_scale
    scene.frame_set(frame)
    scene.render.filepath = path
    bpy.ops.render.render(write_still=True)
