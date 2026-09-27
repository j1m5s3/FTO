"""
Writes the UE5 mannequin's bone names, hierarchy and reference pose to Tools/Blender/data/ue5_mannequin_skeleton.json,
which Tools/Blender/fto_rig.py rebuilds in Blender so FTO's characters can share the mannequin's animations.
Only needed again if Epic changes SK_Mannequin. Needs the mannequin installed (Tools/Unreal/install_epic_content.py):

  UnrealEditor-Cmd.exe FTO.uproject -run=pythonscript -script="<repo>/Tools/Unreal/dump_skeleton.py"

Corrective and helper bones the character meshes never use (twist correctives, muscle helpers) are left out: a mesh
may use a subset of its skeleton's bones as long as the hierarchy agrees.
"""
import json
import os

import unreal

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
OUT = os.path.join(REPO, "Tools", "Blender", "data", "ue5_mannequin_skeleton.json")

mesh = unreal.load_asset("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple")
pose = mesh.skeleton.get_reference_pose()
modifier = unreal.SkeletonModifier()
modifier.set_skeletal_mesh(mesh)

bones = []
for name in unreal.AnimPoseExtensions.get_bone_names(pose):
    name = str(name)
    parent = str(modifier.get_parent_name(name))
    # The helper bones come back without a parent here; so do the gameplay markers. Neither is skinned.
    if name != "root" and parent in ("None", ""):
        continue
    if name in ("interaction", "center_of_mass"):
        continue
    t = unreal.AnimPoseExtensions.get_ref_bone_pose(pose, name, unreal.AnimPoseSpaces.WORLD)
    q = t.rotation
    bones.append({
        "name": name,
        "parent": "" if parent in ("None", "") else parent,
        "loc": [round(v, 4) for v in (t.translation.x, t.translation.y, t.translation.z)],
        "quat": [round(v, 6) for v in (q.x, q.y, q.z, q.w)],
    })

with open(OUT, "w") as f:
    json.dump({"about": "Bone names, hierarchy and reference pose (Unreal mesh space: cm, +Y forward, X/Y/Z/W world "
                        "rotations) of the UE5 mannequin skeleton SK_Mannequin, so in-house characters can share its "
                        "animations. Written by Tools/Unreal/dump_skeleton.py.",
               "bones": bones}, f, indent=0)
unreal.log_warning(f"FTO: wrote {len(bones)} bones to {OUT}")
