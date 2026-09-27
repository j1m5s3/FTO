"""
The UE5 mannequin skeleton (SK_Mannequin) rebuilt in Blender, so FTO's in-house characters share the Epic
animations that ship with the engine (Content/Characters/Mannequins, see Tools/Unreal/install_epic_content.py).

A mesh can only play another skeleton's animations if its bones carry the same names, hierarchy and bone frames
(an animation stores each bone's local rotation, so a bone that points a different way plays it wrong). The
reference pose comes from Tools/Blender/data/ue5_mannequin_skeleton.json (dumped from the engine by
Tools/Unreal/dump_skeleton.py), and build_mannequin_armature() reproduces each bone's frame exactly.

Those frames don't point along the limbs, so they're no good for automatic skinning. build_weight_armature()
builds a second armature with the same bone names but anatomical bones (each one runs to its child), which
Blender's automatic weights understand; skin with that, then bind the mesh to the export armature (vertex groups
go by name).

Units: centimetres, the same as fto_blender (a 0.01 m-unit scene). Characters face -Y in Blender (+Y in Unreal).
"""
import json
import os

import bpy
from mathutils import Matrix, Quaternion, Vector

import fto_blender as fb

DATA = os.path.join(os.path.dirname(os.path.abspath(__file__)), "data", "ue5_mannequin_skeleton.json")

# Unreal is left-handed: its mesh space is Blender's with Y mirrored.
_MIRROR_Y = Matrix.Diagonal((1.0, -1.0, 1.0, 1.0))


def load_bones():
    """[(name, parent, head (Vector, cm, Blender space), frame (3x3 Matrix, Blender space))] parents first."""
    with open(DATA) as f:
        data = json.load(f)
    out = []
    for b in data["bones"]:
        x, y, z = b["loc"]
        qx, qy, qz, qw = b["quat"]
        # A rotation mirrored through the XZ plane: its axis is a pseudovector, so X and Z flip.
        frame = Quaternion((qw, -qx, qy, -qz)).to_matrix()
        out.append((b["name"], b["parent"], Vector((x, -y, z)), frame))
    return out


BONES = load_bones()
BONE_NAMES = [b[0] for b in BONES]
PARENT = {b[0]: b[1] for b in BONES}
HEAD = {b[0]: b[2] for b in BONES}

# Bones that only exist for IK or gameplay (hand/foot IK targets): kept so the skeleton matches, never skinned.
NON_DEFORM = {n for n in BONE_NAMES if n.startswith("ik_") or n == "root"}


def children(name):
    return [b for b in BONE_NAMES if PARENT[b] == name]


def build_mannequin_armature(name="Armature", frame_fix=None):
    """
    The export armature: every bone with the mannequin's exact frame. With fto_blender.export_fbx's bone axes
    (primary Y, secondary X) a Blender bone's matrix goes out as the FBX node's rotation unchanged, so the frame is
    set as it is (checked in Unreal: every bone within 0.2 degrees of SK_Mannequin). frame_fix (3x3) is for testing
    other mappings.
    """
    data = bpy.data.armatures.new(name + "Data")
    obj = fb.link(bpy.data.objects.new(name, data))
    fb.set_active(obj)
    bpy.ops.object.mode_set(mode='EDIT')
    for bone_name, parent, head, frame in BONES:
        eb = data.edit_bones.new(bone_name)
        kids = children(bone_name)
        length = max(2.0, min(12.0, (HEAD[kids[0]] - head).length)) if kids else 4.0
        eb.head = head
        eb.tail = head + Vector((0.0, length, 0.0))
        rot = frame @ frame_fix if frame_fix is not None else frame
        m = rot.to_4x4()
        m.translation = head
        eb.matrix = m
        eb.length = length
        eb.use_deform = bone_name not in NON_DEFORM
        if parent:
            eb.parent = data.edit_bones[parent]
            eb.use_connect = False
    bpy.ops.object.mode_set(mode='OBJECT')
    for pb in obj.pose.bones:
        pb.rotation_mode = 'QUATERNION'
    return obj


def build_weight_armature(name="WeightRig"):
    """Same names, anatomical bones (head to the first child, end bones continue their parent): skin with this."""
    data = bpy.data.armatures.new(name + "Data")
    obj = fb.link(bpy.data.objects.new(name, data))
    fb.set_active(obj)
    bpy.ops.object.mode_set(mode='EDIT')
    for bone_name, parent, head, _frame in BONES:
        if bone_name in NON_DEFORM:
            continue
        eb = data.edit_bones.new(bone_name)
        eb.head = head
        # Twist bones sit along their limb: end them just short of the next twist/joint.
        kids = [k for k in children(bone_name) if k not in NON_DEFORM and "twist" not in k]
        if bone_name == "hand_l" or bone_name == "hand_r":
            kids = [k for k in kids if k.startswith("middle")]
        if kids:
            tail = sum((HEAD[k] for k in kids), Vector()) / len(kids)
        elif parent and parent in HEAD:
            tail = head + (head - HEAD[parent]).normalized() * max(3.0, (head - HEAD[parent]).length * 0.6)
        else:
            tail = head + Vector((0.0, 0.0, 10.0))
        if bone_name == "head":
            tail = head + Vector((0.0, 0.0, 22.0))
        if bone_name.startswith("ball_"):
            tail = head + Vector((0.0, -8.0, 0.0))
        if (tail - head).length < 1.0:
            tail = head + Vector((0.0, 0.0, 2.0))
        eb.tail = tail
        eb.use_deform = True
    for bone_name, parent, _head, _frame in BONES:
        if bone_name in NON_DEFORM or not parent or parent in NON_DEFORM:
            continue
        data.edit_bones[bone_name].parent = data.edit_bones[parent]
    bpy.ops.object.mode_set(mode='OBJECT')
    return obj


def skin_automatic(mesh_obj, weight_rig):
    """Blender's heat-map weights from the anatomical rig (groups named like the mannequin bones)."""
    fb.set_active(weight_rig)
    mesh_obj.select_set(True)
    weight_rig.select_set(True)
    bpy.context.view_layer.objects.active = weight_rig
    bpy.ops.object.parent_set(type='ARMATURE_AUTO')
    mesh_obj.parent = None
    for mod in list(mesh_obj.modifiers):
        if mod.type == 'ARMATURE':
            mesh_obj.modifiers.remove(mod)


def bind_to(mesh_obj, arm_obj):
    """Parent the (already weighted) mesh to the export armature, keeping its vertex groups."""
    for name in BONE_NAMES:
        if name not in mesh_obj.vertex_groups:
            mesh_obj.vertex_groups.new(name=name)
    fb.bind(mesh_obj, arm_obj)
