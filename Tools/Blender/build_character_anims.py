"""
FTO's own animation clips on the UE5 mannequin skeleton: everything Epic's engine animations don't cover (sitting,
driving, arrests, crimes in progress, hand-to-hand fighting...).

  blender -b --factory-startup -P Tools/Blender/build_character_anims.py -- --out Art/Source/Characters/Anims
      [--preview <dir>] [--clips Jab,Cross] [--mesh Art/Source/Characters/Officer/Officers.blend]

Outputs (in --out):
  A_FTO_<Clip>.fbx     one clip per file, armature only, 30 fps, root at the origin (in place)
  Anims.blend          every clip as an action on the armature
  fight_timing.json    per fighting clip: when the hit lands, its reach and which bone lands it
With --preview, <dir>/<Clip>.png is a strip of key frames posed on SK_Officer (taken from --mesh).

Clips are written with fto_pose's controls (IK hands and feet, spine/head bends, finger curls), keyframed with
easing and overlap. Conventions: the character faces -Y, X is its left; cm; frames at 30 fps. Two-person clips
assume the partner's spot given in each clip's comment (the game lines them up, see AFTOCharacter::BeginSyncedAction).
"""
import json
import math
import os
import sys

import bpy  # noqa: I001 (first: outside Blender, the bpy module brings mathutils)
from mathutils import Quaternion, Vector

sys.path.append(os.path.dirname(os.path.abspath(__file__)))
import character_clips as cc  # noqa: E402
import fto_blender as fb  # noqa: E402
import fto_pose as fp  # noqa: E402
from fto_pose import V, euler  # noqa: E402

FPS = 30


# --------------------------------------------------------------------------------------
# Solving controls into a pose
# --------------------------------------------------------------------------------------
REST = {}          # filled from the rig in main(): rest heads
SIDES = fp.LEFT_RIGHT


def ankle_rest(side):
    return REST[f"foot_{side}"].copy()


def solve(rig, c):
    """
    c: controls (all optional)
      pelvis      Vector offset (cm)           hips       (pitch, roll, yaw) of the pelvis
      spine       (pitch, roll, yaw) over the spine        head (pitch, roll, yaw) over the neck and head
      clav_<s>    (raise, forward) degrees
      hand_<s>    target for the wrist; hand_space_<s> 'chest' (default, moves with the upper body) | 'world' |
                  'pelvis' | 'body' (world directions, moved with the chest)
      elbow_<s>   pole direction (same space);  face_<s> (finger direction, palm normal) or None (relaxed)
      wrist_<s>   extra (pitch, roll, yaw) on the hand;  fingers_<s> curl (0..1 or 5-tuple);  spread_<s>
      armfk_<s>   (pitch, abduct, elbow, twist) FK arm instead of IK
      foot_<s>    ankle target (world, default: planted at rest); foot_rot_<s> (pitch, roll, yaw); toe_<s> degrees
      knee_<s>    pole direction;  legfk_<s> (pitch, roll, knee, ankle) FK leg instead of IK
    """
    p = fp.Pose(rig)
    p.pelvis_offset = V(c.get("pelvis", (0, 0, 0)))
    p.R["pelvis"] = euler(*c.get("hips", (0, 0, 0)))
    p.spread(fp.SPINE, fp.SPINE_W, c.get("spine", (0, 0, 0)))
    p.spread(fp.NECK, fp.NECK_W, c.get("head", (0, 0, 0)))
    for side, sx in SIDES:
        raise_, fwd = c.get(f"clav_{side}", (0, 0))
        p.R[f"clavicle_{side}"] = euler(0, -raise_ * sx, -fwd * sx)
    D, P = p.solve_fk()

    # Legs first (they don't depend on the arms).
    for side, sx in SIDES:
        if f"legfk_{side}" in c:
            p.leg_fk(side, *c[f"legfk_{side}"])
            continue
        target = V(c.get(f"foot_{side}", ankle_rest(side)))
        frot = euler(*c.get(f"foot_rot_{side}", (0, 0, 0)))
        knee = V(c.get(f"knee_{side}", (0.12 * sx, -1.0, 0.0)))
        p.set_foot(side, target, knee, frot)
        toe = c.get(f"toe_{side}", 0.0)
        if toe:
            p.R[f"ball_{side}"] = euler(-toe, 0, 0)

    D, P = p.solve_fk()
    for side, sx in SIDES:
        if f"armfk_{side}" in c:
            p.arm_fk(side, *c[f"armfk_{side}"])
        else:
            space = c.get(f"hand_space_{side}", "chest")
            if space == "world":
                to_w = (lambda v: V(v)), (lambda d: V(d))
            elif space == "body":
                # World directions, carried along with the chest's movement (not its rotation): fighting guards.
                shift = P["spine_05"] - REST["spine_05"]
                to_w = (lambda v, s=shift: V(v) + s), (lambda d: V(d))
            else:
                bone = "spine_05" if space == "chest" else "pelvis"
                Db, Pb, rb = D[bone], P[bone], REST[bone]
                to_w = (lambda v, Db=Db, Pb=Pb, rb=rb: Pb + Db @ (V(v) - rb)), (lambda d, Db=Db: Db @ V(d))
            target = to_w[0](c.get(f"hand_{side}", RELAXED_HAND[side]))
            pole = to_w[1](c.get(f"elbow_{side}", (0.35 * sx, 1.0, -0.2)))
            face = c.get(f"face_{side}", "relaxed")
            if face == "relaxed":
                face = ((0.05 * sx, -0.15, -1.0), (-sx, 0.1, 0.0))
            if face is not None:
                face = (to_w[1](face[0]), to_w[1](face[1]))
            extra = euler(*c[f"wrist_{side}"]) if f"wrist_{side}" in c else None
            p.set_hand(side, target, pole, face, extra)
        p.fingers(side, c.get(f"fingers_{side}", 0.25), c.get(f"spread_{side}", 0.0))
    return p


RELAXED_HAND = cc.RELAXED


# --------------------------------------------------------------------------------------
# Main
# --------------------------------------------------------------------------------------
def load_preview_mesh(path, arm):
    """Appends SK_Officer from the characters' .blend and binds it to our armature for preview renders."""
    with bpy.data.libraries.load(path, link=False) as (src, dst):
        dst.objects = [n for n in src.objects if n == "SK_Officer"]
    body = dst.objects[0]
    fb.link(body)
    body.parent = arm
    body.matrix_parent_inverse.identity()
    for mod in body.modifiers:
        if mod.type == 'ARMATURE':
            mod.object = arm
    return body


def main():
    args = fb.script_args()
    out_dir = os.path.abspath(args.get("out", "Art/Source/Characters/Anims"))
    preview_dir = args.get("preview")
    mesh_path = os.path.abspath(args.get("mesh", "Art/Source/Characters/Officer/Officers.blend"))
    only = set(args["clips"].split(",")) if isinstance(args.get("clips"), str) else None

    fb.reset_scene(fps=FPS)
    arm, rig = fp.build_rig()
    REST.update(rig.head)

    # Cuffing reaches for the wrists of a suspect kneeling in the Cuffed pose 76 cm ahead.
    cuffed = [c for c in cc.clip_library() if c.name == "Cuffed"][0]
    kneel = solve(rig, cuffed.controls(0))
    _D, P = kneel.solve_fk()
    for side, _sx in SIDES:
        cc.SUSPECT_WRISTS[side] = tuple(P[f"hand_{side}"] + Vector((0, -76.0, 0)))

    clips = [c for c in cc.clip_library() if not only or c.name in only]
    timing = {}
    actions = []
    for clip in clips:
        poses = {}

        def pose_at(f, clip=clip, poses=poses):
            p = solve(rig, clip.controls(f))
            poses[f] = p
            return p
        action = fp.bake(arm, rig, f"A_FTO_{clip.name}", clip.frames, pose_at)
        actions.append((clip, action))
        fb.export_fbx(os.path.join(out_dir, f"A_FTO_{clip.name}.fbx"), [arm], with_animation=True)
        if clip.name in cc.FIGHT_CLIPS or clip.hit:
            timing[clip.name] = clip_timing(clip, poses)

    path = os.path.join(out_dir, "fight_timing.json")
    existing = {}
    if only and os.path.exists(path):
        with open(path) as f:
            existing = json.load(f)
    existing.update(timing)
    with open(path, "w") as f:
        json.dump(existing, f, indent=2, sort_keys=True)
        f.write("\n")

    if preview_dir:
        render_previews(arm, actions, os.path.abspath(preview_dir), mesh_path)

    if not only:
        arm.animation_data.action = None
        bpy.ops.wm.save_as_mainfile(filepath=os.path.join(out_dir, "Anims.blend"), copy=True)


def clip_timing(clip, poses):
    """What the game needs to line up a fighting clip: when and where the hit lands, how far the body travels."""
    out = {"length_s": round(clip.frames / FPS, 3), "frames": clip.frames, "loop": clip.loop}
    if clip.hit:
        frame, bone = clip.hit
        _D, P = poses[frame].solve_fk()
        p = P[bone]
        out.update({
            "contact_frame": frame,
            "contact_time_s": round(frame / FPS, 3),
            "contact_fraction": round(frame / clip.frames, 2),
            "bone": bone,
            "reach_cm": round(math.hypot(p.x, p.y), 1),
            "contact_point_cm": [round(p.x, 1), round(p.y, 1), round(p.z, 1)],
        })
    if clip.travel:
        out["travel_cm"] = [round(v, 1) for v in clip.travel]
    if clip.note:
        out["note"] = clip.note
    return out


def render_previews(arm, actions, preview_dir, mesh_path):
    import build_characters as bc
    os.makedirs(preview_dir, exist_ok=True)
    body = load_preview_mesh(mesh_path, arm)
    cam = fb.setup_preview((200, 250))
    scene = bpy.context.scene
    scene.display.shading.show_cavity = False
    floor = bpy.data.meshes.new("Floor")
    floor.from_pydata([(-300, -300, 0), (300, -300, 0), (300, 300, 0), (-300, 300, 0)], [], [(0, 1, 2, 3)])
    fb.link(bpy.data.objects.new("Floor", floor))
    for clip, action in actions:
        arm.animation_data.action = action
        n = 6 if clip.frames > 20 else 4
        frames = sorted({int(round(clip.frames * i / n)) for i in range(n)})
        if clip.hit:
            frames = sorted(set(frames) | {clip.hit[0]})
        paths = []
        for i, fr in enumerate(frames):
            path = os.path.join(preview_dir, f"_{clip.name}_{i}.png")
            scene.frame_set(fr)
            px, py = arm.pose.bones["pelvis"].head.x / 100.0, arm.pose.bones["pelvis"].head.y / 100.0
            fb.render_view(path, cam, (px + 3.6, py - 4.2, 1.6), (px, py - 0.2, 0.8), 2.6, fr)
            paths.append(path)
        bc.contact_row(paths, os.path.join(preview_dir, f"{clip.name}.png"))


if __name__ == "__main__":
    main()
