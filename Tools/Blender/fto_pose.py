"""
Posing FTO's clips on the UE5 mannequin skeleton (fto_rig) without fighting its bone frames.

The mannequin's bone axes point every which way, so clips aren't keyed as raw bone rotations. A pose is described
by a few controls, the way an animator's control rig would be:

  pelvis      offset (cm) and rotation, spine/head bends spread over the spine and neck bones
  hands       IK targets (world space, or "chest" space that moves with the upper body), elbow pole, hand facing
  feet        IK targets for the ankles (planted feet stay put while the hips move), knee pole, heel/toe roll
  fingers     curl per finger, spread, and FK arms/legs for flailing

Rotations are written in the character's own axes at rest: X is its left, -Y its front, Z up. So pitch (about X)
leans forward when positive, roll (about Y) tips to the character's left, yaw (about Z) turns it to its left.
Each bone's rotation R is given in those axes (its parent carries it), and becomes the bone's own rotation
Rest^-1 * R * Rest when keyed.

Clips are keyframed channels (Track) sampled with smooth interpolation, with per-channel lag for overlap (the head
settles after the chest, fingers after the wrist), then solved and keyed on every frame.
"""
import math

import bpy
from mathutils import Matrix, Quaternion, Vector

import fto_rig

LEFT_RIGHT = (("l", 1.0), ("r", -1.0))
SPINE = ["spine_01", "spine_02", "spine_03", "spine_04", "spine_05"]
SPINE_W = [0.14, 0.18, 0.2, 0.22, 0.26]
NECK = ["neck_01", "neck_02", "head"]
NECK_W = [0.3, 0.3, 0.4]
FINGERS = ["thumb", "index", "middle", "ring", "pinky"]


def V(*a):
    return Vector(a[0]) if len(a) == 1 else Vector(a)


def euler(pitch=0.0, roll=0.0, yaw=0.0):
    """Degrees about X (pitch), Y (roll), Z (yaw), applied pitch, then roll, then yaw."""
    qx = Quaternion((1, 0, 0), math.radians(pitch))
    qy = Quaternion((0, 1, 0), math.radians(roll))
    qz = Quaternion((0, 0, 1), math.radians(yaw))
    return qz @ qy @ qx


def axis_angle(axis, deg):
    return Quaternion(Vector(axis).normalized(), math.radians(deg))


def frame_quat(primary, secondary):
    """Rotation of the frame (primary, secondary orthogonalised, their cross)."""
    a = Vector(primary).normalized()
    b = Vector(secondary)
    b = (b - a * a.dot(b)).normalized()
    c = a.cross(b)
    return Matrix((a, b, c)).transposed().to_quaternion()


def swing_twist(q, axis):
    """Split q into swing * twist about axis; returns the twist angle (radians)."""
    axis = Vector(axis).normalized()
    p = Vector((q.x, q.y, q.z)).dot(axis)
    twist = Quaternion((q.w, axis.x * p, axis.y * p, axis.z * p))
    if twist.magnitude < 1e-9:
        return 0.0
    twist.normalize()
    ang = 2.0 * math.atan2(Vector((twist.x, twist.y, twist.z)).dot(axis), twist.w)
    return ang


class Rig:
    """Rest data of the export armature (read back from Blender so it matches exactly)."""

    def __init__(self, arm_obj):
        self.arm = arm_obj
        bones = arm_obj.data.bones
        self.names = [b.name for b in bones]
        self.parent = {b.name: (b.parent.name if b.parent else None) for b in bones}
        self.rest_q = {b.name: b.matrix_local.to_quaternion() for b in bones}
        self.head = {b.name: b.matrix_local.translation.copy() for b in bones}
        self.order = []
        seen = set()

        def visit(n):
            if n in seen:
                return
            if self.parent[n]:
                visit(self.parent[n])
            seen.add(n)
            self.order.append(n)
        for n in self.names:
            visit(n)
        self.hand = {}
        for side, _sx in LEFT_RIGHT:
            h = self.head[f"hand_{side}"]
            f = (self.head[f"middle_01_{side}"] - h).normalized()
            s = self.head[f"index_01_{side}"] - self.head[f"pinky_01_{side}"]
            s = (s - f * f.dot(s)).normalized()
            n = f.cross(s) * (1.0 if side == "l" else -1.0)
            self.hand[side] = (f, n)
        self.finger_axis = {}
        for side, _sx in LEFT_RIGHT:
            _f, n = self.hand[side]
            for finger in FINGERS:
                for i in (1, 2, 3):
                    b = f"{finger}_0{i}_{side}"
                    nxt = f"{finger}_0{i + 1}_{side}" if i < 3 else None
                    d = (self.head[nxt] - self.head[b]) if nxt else (self.head[b] - self.head[f"{finger}_0{i - 1}_{side}"])
                    d.normalize()
                    if finger == "thumb":
                        # The thumb folds across the palm towards the little finger.
                        across = (self.head[f"pinky_01_{side}"] - self.head[b]).normalized()
                        bend_to = (n * 0.75 + across * 0.65).normalized()
                    else:
                        bend_to = n
                    self.finger_axis[b] = d.cross(bend_to).normalized()


class Pose:
    """Per-bone rotations R (character axes, see module doc) plus the pelvis offset; solved into bone space."""

    def __init__(self, rig):
        self.rig = rig
        self.R = {n: Quaternion() for n in rig.names}
        self.pelvis_offset = Vector()
        self._D = None

    # ---- forward kinematics ----
    def solve_fk(self, upto=None):
        rig = self.rig
        D, P = {}, {}
        for n in rig.order:
            par = rig.parent[n]
            if par is None:
                D[n] = self.R[n].copy()
                P[n] = rig.head[n].copy()
            else:
                D[n] = D[par] @ self.R[n]
                P[n] = P[par] + D[par] @ (rig.head[n] - rig.head[par])
                if n == "pelvis":
                    P[n] = P[n] + self.pelvis_offset
        self.D, self.P = D, P
        return D, P

    def local(self, name):
        rq = self.rig.rest_q[name]
        return rq.inverted() @ self.R[name] @ rq

    # ---- helpers ----
    def spread(self, bones, weights, q_pitch_roll_yaw):
        pitch, roll, yaw = q_pitch_roll_yaw
        for b, w in zip(bones, weights):
            self.R[b] = euler(pitch * w, roll * w, yaw * w) @ self.R[b]

    def two_bone(self, a, b, c, target, pole, end_rot=None):
        """
        IK for the chain a -> b -> c (upper arm, forearm, hand / thigh, calf, foot): c's head reaches target, the
        middle joint bends towards pole. Parents of a must be posed (solve_fk) already. end_rot: the global rotation
        delta wanted for c (None keeps it following b).
        """
        rig = self.rig
        D, P = self.solve_fk()
        pa = rig.parent[a]
        Dp = D[pa]
        S = P[a]
        ra, rb, rc = rig.head[a], rig.head[b], rig.head[c]
        l1, l2 = (rb - ra).length, (rc - rb).length
        T = Vector(target)
        d_vec = T - S
        d = max(abs(l1 - l2) + 0.1, min(d_vec.length, (l1 + l2) * 0.9995))
        u = d_vec.normalized()
        pole = Vector(pole)
        v = pole - u * u.dot(pole)
        if v.length < 1e-6:
            v = Vector((0, -1, 0)) - u * u.dot(Vector((0, -1, 0)))
        v.normalize()
        x = (l1 * l1 - l2 * l2 + d * d) / (2 * d)
        h = math.sqrt(max(l1 * l1 - x * x, 0.0))
        E = S + u * x + v * h
        Tn = S + u * d
        # Rest plane: the rest chain is slightly bent, which defines its bend normal.
        u_r = (rc - ra).normalized()
        v_r = (rb - ra) - u_r * u_r.dot(rb - ra)
        v_r.normalize()
        n_r = u_r.cross(v_r)
        n_p = u.cross(v)
        Da = frame_quat(E - S, n_p) @ frame_quat(rb - ra, n_r).inverted()
        Db = frame_quat(Tn - E, n_p) @ frame_quat(rc - rb, n_r).inverted()
        self.R[a] = Dp.inverted() @ Da
        self.R[b] = Da.inverted() @ Db
        if end_rot is not None:
            self.R[c] = Db.inverted() @ end_rot
        else:
            self.R[c] = Quaternion()
        return Db

    def set_hand(self, side, target, pole, facing=None, extra=None):
        """facing: (finger direction, palm normal) in world space, or None for the relaxed continuation."""
        rig = self.rig
        end = None
        if facing is not None:
            f_r, n_r = rig.hand[side]
            end = frame_quat(facing[0], facing[1]) @ frame_quat(f_r, n_r).inverted()
        Db = self.two_bone(f"upperarm_{side}", f"lowerarm_{side}", f"hand_{side}", target, pole, end)
        if extra is not None:
            self.R[f"hand_{side}"] = self.R[f"hand_{side}"] @ extra
        # Share the hand's twist about the forearm with the forearm twist bones (so the wrist doesn't pinch).
        axis = (rig.head[f"hand_{side}"] - rig.head[f"lowerarm_{side}"]).normalized()
        tw = swing_twist(self.R[f"hand_{side}"], axis)
        self.R[f"lowerarm_twist_01_{side}"] = Quaternion(axis, tw * 0.6)
        self.R[f"lowerarm_twist_02_{side}"] = Quaternion(axis, tw * 0.3)
        return Db

    def set_foot(self, side, target, pole, foot_rot=None):
        self.two_bone(f"thigh_{side}", f"calf_{side}", f"foot_{side}", target, pole,
                      foot_rot if foot_rot is not None else Quaternion())

    def fingers(self, side, curl, spread=0.0):
        """curl: one value or (thumb, index, middle, ring, pinky), 0 open .. 1 fist."""
        if isinstance(curl, (int, float)):
            curl = (curl,) * 5
        joint_deg = {"thumb": (15, 35, 45), "index": (72, 95, 60), "middle": (76, 98, 62), "ring": (78, 98, 60),
                     "pinky": (80, 95, 58)}
        spread_deg = {"thumb": 0, "index": 9, "middle": 2, "ring": -6, "pinky": -13}
        _f, n = self.rig.hand[side]
        for finger, c in zip(FINGERS, curl):
            for i in (1, 2, 3):
                b = f"{finger}_0{i}_{side}"
                q = Quaternion(self.rig.finger_axis[b], math.radians(joint_deg[finger][i - 1] * c))
                if i == 1 and spread and finger != "thumb":
                    q = Quaternion(n, math.radians(spread * spread_deg[finger] * (1 if side == "l" else -1))) @ q
                self.R[b] = q

    def arm_fk(self, side, pitch=0.0, abduct=0.0, elbow=0.0, twist=0.0):
        """
        FK arm from the relaxed A-pose: pitch raises it forward (negative lifts), abduct lifts it out to the side,
        elbow bends (positive flexes), twist rolls the arm about itself.
        """
        rig = self.rig
        sx = 1.0 if side == "l" else -1.0
        ua, la, hd = (rig.head[f"upperarm_{side}"], rig.head[f"lowerarm_{side}"], rig.head[f"hand_{side}"])
        up_dir = (la - ua).normalized()
        q = euler(pitch, -abduct * sx, 0.0) @ Quaternion(up_dir, math.radians(twist * sx))
        self.R[f"upperarm_{side}"] = q
        hinge = (la - ua).cross(hd - la).normalized()
        self.R[f"lowerarm_{side}"] = Quaternion(hinge, math.radians(-elbow))

    def leg_fk(self, side, pitch=0.0, roll=0.0, knee=0.0, ankle=0.0, yaw=0.0):
        """FK leg: pitch swings the thigh (negative = forward), knee bends (positive), ankle points the foot."""
        sx = 1.0 if side == "l" else -1.0
        self.R[f"thigh_{side}"] = euler(pitch, roll * sx, yaw)
        self.R[f"calf_{side}"] = euler(knee, 0, 0)
        self.R[f"foot_{side}"] = euler(ankle, 0, 0)


# --------------------------------------------------------------------------------------
# Keyframed channels
# --------------------------------------------------------------------------------------
def _match(a, b):
    """A number keyed against a tuple (a finger curl for all fingers vs per finger) counts for every element."""
    if isinstance(a, (int, float)) and isinstance(b, (tuple, list)):
        a = (a,) * len(b)
    if isinstance(b, (int, float)) and isinstance(a, (tuple, list)):
        b = (b,) * len(a)
    return a, b


def _lerp(a, b, t):
    a, b = _match(a, b)
    if isinstance(a, (tuple, list)):
        return tuple(_lerp(x, y, t) for x, y in zip(a, b))
    if isinstance(a, Vector):
        return a.lerp(b, t)
    return a + (b - a) * t


def _add(a, b, s=1.0):
    a, b = _match(a, b)
    if isinstance(a, (tuple, list)):
        return tuple(_add(x, y, s) for x, y in zip(a, b))
    return a + b * s


def _sub(a, b):
    return _add(a, b, -1.0)


def _scale(a, s):
    if isinstance(a, (tuple, list)):
        return tuple(_scale(x, s) for x in a)
    return a * s


def ease(t):
    return t * t * (3.0 - 2.0 * t)


class Track:
    """
    Keys (time in frames, value); values are numbers, tuples or Vectors. mode 'smooth' is a Catmull-Rom curve
    through the keys (flowing motion), 'ease' eases in and out of every key (holds and hits), 'linear' is linear.
    loop wraps around the clip length.
    """

    def __init__(self, keys, mode="smooth", loop=False, length=None):
        self.keys = sorted(keys, key=lambda k: k[0])
        self.mode, self.loop, self.length = mode, loop, length

    def __call__(self, t):
        ks = self.keys
        if len(ks) == 1:
            return ks[0][1]
        if self.loop:
            L = self.length
            t = t % L
            ext = [(ks[-1][0] - L, ks[-1][1])] + ks + [(ks[0][0] + L, ks[0][1]), (ks[1][0] + L, ks[1][1])]
            ext = [(ks[-2][0] - L, ks[-2][1])] + ext
        else:
            if t <= ks[0][0]:
                return ks[0][1]
            if t >= ks[-1][0]:
                return ks[-1][1]
            ext = [ks[0]] + ks + [ks[-1]]
        for i in range(1, len(ext) - 2):
            t0, t1 = ext[i][0], ext[i + 1][0]
            if t0 <= t <= t1 and t1 > t0:
                u = (t - t0) / (t1 - t0)
                a, b = ext[i][1], ext[i + 1][1]
                if self.mode == "linear":
                    return _lerp(a, b, u)
                if self.mode == "ease":
                    return _lerp(a, b, ease(u))
                p0, p3 = ext[i - 1][1], ext[i + 2][1]
                tp, tn = ext[i - 1][0], ext[i + 2][0]
                # Catmull-Rom with non-uniform key spacing (tangents scaled to this segment).
                m1 = _scale(_sub(b, p0), (t1 - t0) / max(t1 - tp, 1e-6))
                m2 = _scale(_sub(p3, a), (t1 - t0) / max(tn - t0, 1e-6))
                u2, u3 = u * u, u * u * u
                h00, h10, h01, h11 = 2 * u3 - 3 * u2 + 1, u3 - 2 * u2 + u, -2 * u3 + 3 * u2, u3 - u2
                return _add(_add(_add(_scale(a, h00), m1, h10), b, h01), m2, h11)
        return ks[-1][1]


def wave(t, period, phase=0.0):
    return math.sin(2.0 * math.pi * (t / period + phase))


# --------------------------------------------------------------------------------------
# Keying
# --------------------------------------------------------------------------------------
def key_pose(arm_obj, pose, frame):
    for pb in arm_obj.pose.bones:
        pb.rotation_mode = 'QUATERNION'
        pb.rotation_quaternion = pose.local(pb.name)
        pb.keyframe_insert("rotation_quaternion", frame=frame)
        if pb.name == "pelvis":
            rq = pose.rig.rest_q["pelvis"]
            pb.location = rq.inverted() @ pose.pelvis_offset
        else:
            pb.location = (0, 0, 0)
        pb.keyframe_insert("location", frame=frame)


def bake(arm_obj, rig, name, frames, pose_at):
    """Keys pose_at(frame) -> Pose for frames 0..frames into a new action."""
    arm_obj.animation_data_clear()
    arm_obj.animation_data_create()
    action = bpy.data.actions.new(name)
    arm_obj.animation_data.action = action
    for f in range(frames + 1):
        key_pose(arm_obj, pose_at(f), f)
    scene = bpy.context.scene
    scene.frame_start, scene.frame_end = 0, frames
    action.use_fake_user = True
    return action


def build_rig():
    arm = fto_rig.build_mannequin_armature("Armature")
    return arm, Rig(arm)
