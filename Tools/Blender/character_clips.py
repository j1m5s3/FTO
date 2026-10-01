"""
The clip library for build_character_anims.py: every clip as keyed controls (see build_character_anims.solve).

Coordinates are cm with the character facing -Y (X is its left), root at the origin on the floor. A standing
character's ankles rest 8.2 cm off the floor; hands default to hanging relaxed. Frames are at 30 fps.
Seats: Sit is on a 45 cm chair (hip joints 53 cm up, 4.5 cm behind the root); Drive, Ride, SitCuffed and SitHandsUp
are on a car seat (hip joints 40 cm up, 5 cm behind the root, feet 60 cm ahead); the wheel is centred 40 cm ahead of
the root at 88 cm (chest height), tilted 25 degrees towards the driver, 36 cm across.
"""
import math

from mathutils import Vector

import fto_pose as fp
from fto_pose import Track, wave

SIDES = fp.LEFT_RIGHT
FPS = 30


class Clip:
    """
    frames: length (30 fps); loop: the last frame equals the first. channels: {control: Track or constant}.
    lag: {control: frames} delays a control (overlap, follow-through); the head and fingers lag by default.
    fn(f, c) adds procedural motion. hit: for fighting clips, (frame, bone) of the moment the hit lands.
    """

    def __init__(self, name, frames, loop, channels, lag=None, fn=None, hit=None, travel=None, note=None):
        self.name, self.frames, self.loop = name, frames, loop
        self.channels, self.lag, self.fn, self.hit = channels, lag or {}, fn, hit
        self.travel, self.note = travel, note

    def controls(self, f):
        c = {}
        for k, v in self.channels.items():
            if isinstance(v, Track):
                t = f - self.lag.get(k, default_lag(k))
                if not self.loop:
                    t = max(0.0, t)
                c[k] = v(t)
            elif callable(v):
                c[k] = v(f)
            else:
                c[k] = v
        if self.fn:
            self.fn(f, c)
        return c


def default_lag(k):
    if k == "head":
        return 2.0
    if k.startswith("fingers") or k.startswith("wrist") or k.startswith("spread"):
        return 1.5
    if k.startswith("clav"):
        return 1.0
    return 0.0


def L(n, *keys, mode="smooth"):
    """A looping track over n frames: keys are (frame, value); the loop closes on its own."""
    return Track(list(keys), mode=mode, loop=True, length=n)


def O(*keys, mode="smooth"):
    """A one-shot track (held before the first key and after the last)."""
    return Track(list(keys), mode=mode, loop=False)


def E(*keys):
    return O(*keys, mode="ease")


def add(c, key, delta):
    c[key] = fp._add(c.get(key, (0, 0, 0)), delta)


def breathe(f, c, amount=1.0, period=90):
    b = wave(f, period)
    add(c, "spine", (0.9 * amount * b, 0, 0))
    add(c, "head", (-0.6 * amount * b, 0, 0))
    for side, _sx in SIDES:
        r = c.get(f"clav_{side}", (0, 0))
        c[f"clav_{side}"] = (r[0] + 0.9 * amount * b, r[1])


def mir(v):
    return (-v[0], v[1], v[2])


def sided(fn):
    """Channels for both sides from fn(side, sx) -> dict (keys without the side suffix)."""
    out = {}
    for side, sx in SIDES:
        for k, v in fn(side, sx).items():
            out[f"{k}_{side}"] = v
    return out


def merge(*ds):
    out = {}
    for d in ds:
        out.update(d)
    return out


FEET = {"l": (12.5, 0.5, 8.2), "r": (-12.5, 0.5, 8.2)}
ANKLE = 8.2
FOOT_LEN = 15.5    # ankle to ball, for heel lifts


def heel_lift(base, pitch):
    """Ankle position and toe bend for a foot pitched onto its ball (pitch degrees), the ball staying put."""
    r = math.radians(pitch)
    return (base[0], base[1] - FOOT_LEN * (1 - math.cos(r)) * 0.0 + FOOT_LEN * math.sin(r) * 0.25,
            base[2] + FOOT_LEN * math.sin(r))


def gait(phase, travel, swing, lift, direction, base, roll=True):
    """
    A foot's in-place stepping: during stance (1 - swing of the cycle) it slides back by `travel` cm (the ground
    moving under a character that walks forward); during swing it lifts `lift` cm and swings forward again.
    Returns (ankle position, foot pitch, toe bend).
    """
    d = Vector(direction).normalized()
    phase %= 1.0
    if phase < swing:
        u = phase / swing
        s = -travel / 2 + travel * fp.ease(u)
        z = lift * math.sin(math.pi * u) ** 0.8
        pitch = 22.0 * (1 - u) ** 2 - 12.0 * u ** 3 if roll else 0.0
    else:
        u = (phase - swing) / (1 - swing)
        s = travel / 2 - travel * u
        z = 0.0
        pitch = (-10.0 * max(0.0, 1 - u / 0.15) + 24.0 * max(0.0, (u - 0.75) / 0.25) ** 2) if roll else 0.0
    pos = Vector(base) + d * s + Vector((0, 0, z))
    toe = 0.0
    if pitch > 0:
        pos.z += FOOT_LEN * math.sin(math.radians(pitch)) * (1.0 if phase >= swing else 0.4)
        toe = pitch if phase >= swing else pitch * 0.4
    return pos, pitch, toe


def apply_gait(c, f, n, travel, swing, lift, direction, bases, offsets=(0.0, 0.5), roll=True, yaw=(0.0, 0.0)):
    for (side, _sx), off, y in zip(SIDES, offsets, yaw):
        pos, pitch, toe = gait(f / n + off, travel, swing, lift, direction, bases[side], roll)
        c[f"foot_{side}"] = pos
        c[f"foot_rot_{side}"] = (pitch, 0, y)
        c[f"toe_{side}"] = toe


# ---- hand facings (finger direction, palm normal), world/body/chest axes ----
def palm_down(sx, fwd=(0, -1, -0.2)):
    return (fwd, (0, -0.1, -1))


def palm_in(sx, fingers=(0, 0, -1)):
    return (fingers, (-sx, 0, 0))


def palm_fwd(sx, up=(0, 0.1, 1)):
    return (up, (0, -1, 0))


def fist_fwd(sx):
    """A straight punch: knuckles forward, palm down."""
    return ((0.1 * sx, -1, 0.05), (0, 0, -1))


def fist_guard(sx):
    return ((-0.25 * sx, -0.45, 1.0), (-sx, -0.35, 0))


RELAXED = {"l": (22.0, -1.5, 93.0), "r": (-22.0, -1.5, 93.0)}
RELAXED_FACE = {"l": ((0.05, -0.15, -1.0), (-1.0, 0.1, 0.0)), "r": ((-0.05, -0.15, -1.0), (1.0, 0.1, 0.0))}
POLE_BACK = {"l": (0.35, 1.0, -0.2), "r": (-0.35, 1.0, -0.2)}


# ======================================================================================
# Loops
# ======================================================================================
def idle_base(f, c, amount=1.0, sway=1.2, period=120):
    """A standing weight shift, with the hips over the planted feet and the chest counter-tilting."""
    w = wave(f, period)
    add(c, "pelvis", (sway * w, 0, -1.5 - 0.4 * abs(w)))
    add(c, "hips", (0, -2.5 * w, 1.5 * w))
    add(c, "spine", (0, 2.0 * w, -1.0 * w))
    breathe(f, c, amount)


def seated_car(c, knees=(0, 0), feet_fwd=60.0):
    c.setdefault("pelvis", (0, 7.5, -53.5))
    c.setdefault("hips", (-14, 0, 0))
    for side, sx in SIDES:
        c.setdefault(f"foot_{side}", (sx * (15 + knees[0]), -feet_fwd, 11.0))
        c.setdefault(f"foot_rot_{side}", (18, 0, 0))
        c.setdefault(f"knee_{side}", (0.25 * sx, -0.6, 1.0))


def seated_chair(c):
    c.setdefault("pelvis", (0, 7.0, -40.5))
    c.setdefault("hips", (-6, 0, 0))
    for side, sx in SIDES:
        c.setdefault(f"foot_{side}", (sx * 14.5, -44.0, 8.2))
        c.setdefault(f"knee_{side}", (0.2 * sx, -0.8, 0.6))


def clip_sit():
    n = 150
    ch = {
        "spine": L(n, (0, (6, 0, 0)), (50, (7, 1.5, 4)), (100, (5, -1, -3))),
        "head": L(n, (0, (2, 0, 0)), (35, (0, 2, 18)), (75, (4, 0, -4)), (115, (1, -2, -15))),
        "hand_space_l": "world", "hand_space_r": "world",
        "hand_l": (14.5, -24.0, 64.0), "hand_r": (-14.5, -24.0, 64.0),
        "face_l": ((0.05, -1, -0.3), (0, 0, -1)), "face_r": ((-0.05, -1, -0.3), (0, 0, -1)),
        "elbow_l": (0.8, 1.0, -0.3), "elbow_r": (-0.8, 1.0, -0.3),
        "fingers_l": 0.2,
        "fingers_r": L(n, (0, 0.2), (60, 0.2), (66, (0.2, 0.5, 0.2, 0.2, 0.2)), (72, 0.2), (78, (0.2, 0.5, 0.2, 0.2, 0.2)),
                       (84, 0.2)),
    }

    def fn(f, c):
        seated_chair(c)
        breathe(f, c, 0.8)
    return Clip("Sit", n, True, ch, fn=fn)


WHEEL = Vector((0, -40.0, 88.0))
WHEEL_UP = Vector((0, 0.42, 0.91)).normalized()
WHEEL_R = 18.0


def wheel_grip(side, sx, turn):
    """Where the hand holds the wheel (10 and 2 o'clock, turned by `turn` degrees) and how it faces."""
    a = math.radians(60 * sx + turn)       # measured from the top; the left hand (+X) is at 10 o'clock
    right = Vector((1, 0, 0))
    pos = WHEEL + right * (WHEEL_R * math.sin(a)) + WHEEL_UP * (WHEEL_R * math.cos(a))
    radial = (pos - WHEEL).normalized()
    fingers = (-radial * 0.35 + Vector((0, -0.75, -0.55))).normalized()
    palm = (-radial * 0.6 + Vector((0, -0.75, 0.1))).normalized()
    return pos + radial * 1.5 + Vector((0, 3.0, 2.5)), (tuple(fingers), tuple(palm))


def clip_drive():
    n = 180
    turn = L(n, (0, 0.0), (40, 6.0), (70, -4.0), (110, 10.0), (150, -2.0))
    head = L(n, (0, (2, 0, 0)), (40, (2, 3, 6)), (70, (0, 0, 0)), (95, (0, -2, -35)), (110, (2, 0, -5)),
             (150, (0, 0, 20)), (165, (2, 0, 0)))

    def fn(f, c):
        seated_car(c)
        t = turn(f)
        for side, sx in SIDES:
            pos, face = wheel_grip(side, sx, t)
            c[f"hand_space_{side}"] = "world"
            c[f"hand_{side}"] = pos
            c[f"face_{side}"] = face
            c[f"elbow_{side}"] = (0.8 * sx, 0.3, -1.0)
            c[f"fingers_{side}"] = (0.45, 0.78, 0.8, 0.82, 0.85)
        c["spine"] = (12, -0.25 * t, -0.3 * t)
        c["head"] = head(f - 2)
        c["foot_rot_r"] = (18 + 5 * wave(f, 60), 0, 0)
        breathe(f, c, 0.6)
    return Clip("Drive", n, True, {}, fn=fn)


def clip_ride():
    n = 180
    ch = {
        "spine": L(n, (0, (12, 0, 0)), (90, (13, 1, 3))),
        "head": L(n, (0, (3, 0, 0)), (40, (0, 3, 35)), (95, (2, 0, 30)), (125, (4, 0, -5)), (160, (3, -2, -12))),
        "hand_space_l": "world", "hand_space_r": "world",
        "hand_l": L(n, (0, (9.0, -22.0, 52.0)), (90, (9.5, -23.0, 52.5))),
        "hand_r": L(n, (0, (-9.0, -21.0, 52.0)), (90, (-9.5, -21.0, 52.5))),
        "face_l": ((-0.55, -0.8, -0.2), (0, 0, -1)), "face_r": ((0.55, -0.8, -0.2), (0, 0, -1)),
        "elbow_l": (0.8, 1.0, -0.4), "elbow_r": (-0.8, 1.0, -0.4),
        "fingers_l": 0.35, "fingers_r": 0.3,
    }

    def fn(f, c):
        seated_car(c, knees=(2, 0), feet_fwd=56)
        breathe(f, c, 0.7)
    return Clip("Ride", n, True, ch, fn=fn)


def clip_talk():
    n = 150
    ch = {
        "hand_l": L(n, (0, (20, -22, 110)), (22, (26, -34, 120)), (45, (18, -26, 112)), (80, (22, -30, 116)),
                    (110, (30, -32, 124)), (130, (21, -24, 110))),
        "hand_r": L(n, (0, (-18, -26, 114)), (15, (-28, -36, 126)), (38, (-20, -30, 118)), (62, (-30, -34, 128)),
                    (95, (-19, -27, 112)), (118, (-26, -38, 122))),
        "face_l": L(n, (0, ((0.3, -1, 0.3), (-0.5, -0.2, 0.8))), (22, ((0.6, -0.8, 0.3), (-0.2, -0.3, 1.0))),
                    (80, ((0.3, -1, 0.1), (-0.8, -0.1, 0.5))), (110, ((0.7, -0.7, 0.4), (0, -0.4, 1.0)))),
        "face_r": L(n, (0, ((-0.3, -1, 0.3), (0.5, -0.2, 0.8))), (15, ((-0.6, -0.8, 0.4), (0.2, -0.3, 1.0))),
                    (62, ((-0.5, -0.9, 0.3), (0.4, -0.5, 0.7))), (95, ((-0.3, -1, 0.1), (0.8, -0.1, 0.5)))),
        "elbow_l": (0.7, 0.8, -1.0), "elbow_r": (-0.7, 0.8, -1.0),
        "fingers_l": L(n, (0, 0.25), (22, 0.1), (45, 0.35), (110, 0.1)),
        "fingers_r": L(n, (0, 0.3), (15, 0.08), (40, 0.3), (62, 0.1), (100, 0.35)),
        "spread_l": L(n, (0, 0.2), (22, 0.7), (60, 0.3), (110, 0.8)),
        "spread_r": L(n, (0, 0.3), (15, 0.8), (62, 0.6), (100, 0.2)),
        "head": L(n, (0, (0, 0, 4)), (15, (-4, 2, 8)), (30, (3, 0, 2)), (60, (-3, -3, -6)), (80, (2, 0, -2)),
                  (110, (-5, 2, 6)), (130, (1, 0, 3))),
        "spine": L(n, (0, (2, 0, 0)), (20, (0, 1, 5)), (60, (3, -1, -4)), (110, (0, 1, 6))),
    }
    return Clip("Talk", n, True, ch, fn=lambda f, c: idle_base(f, c, 0.8, 1.0, 150))


def clip_work():
    """Typing at a keyboard / till on a counter about 95 cm high, 35 cm ahead."""
    n = 90

    def fn(f, c):
        idle_base(f, c, 0.6, 0.8, 90)
        c["spine"] = fp._add(c["spine"], (12, 0, 0))
        look = 1.0 if (f % 90) < 60 else 0.0
        c["head"] = fp._add(c.get("head", (0, 0, 0)), (22 - 16 * (1 - look) * math.sin(math.pi * ((f % 90) - 60) / 30)
                                                         if look == 0 else 22, 0, 3 * wave(f, 90)))
        for side, sx in SIDES:
            tap = wave(f, 8, 0.0 if side == "l" else 0.5)
            c[f"hand_space_{side}"] = "world"
            c[f"hand_{side}"] = (sx * 11 + 2.5 * wave(f, 45, 0.25 * sx), -33 + 1.5 * wave(f, 30), 99 + 1.2 * max(0, tap))
            c[f"face_{side}"] = ((-0.15 * sx, -1, -0.35), (0, 0, -1))
            c[f"elbow_{side}"] = (0.7 * sx, 0.8, -0.6)
            curl = 0.35 + 0.25 * max(0.0, tap)
            c[f"fingers_{side}"] = (0.3, curl, curl * 0.9, 0.45, 0.5)
    return Clip("Work", n, True, {}, fn=fn)


def clip_hands_up():
    n = 60
    ch = {
        "hand_l": L(n, (0, (29, -4, 178)), (30, (30, -5, 180))),
        "hand_r": L(n, (0, (-29, -4, 178)), (30, (-30, -5, 179))),
        "face_l": palm_fwd(1, (-0.1, 0.1, 1)), "face_r": palm_fwd(-1, (0.1, 0.1, 1)),
        "elbow_l": (1, 0.1, -0.6), "elbow_r": (-1, 0.1, -0.6),
        "fingers_l": 0.1, "fingers_r": 0.1, "spread_l": 0.8, "spread_r": 0.8,
        "clav_l": (8, 0), "clav_r": (8, 0),
        "head": L(n, (0, (6, 0, 0)), (30, (5, 0, 3))),
        "spine": (-3, 0, 0),
    }

    def fn(f, c):
        idle_base(f, c, 1.3, 0.8, 60)
        for side, sx in SIDES:
            c[f"hand_{side}"] = fp._add(c[f"hand_{side}"], (0.4 * wave(f, 7, 0.3 * sx), 0, 0.3 * wave(f, 5)))
    return Clip("HandsUp", n, True, ch, fn=fn)


def kneel_legs(c, lean=0.0):
    """Kneeling upright: knees on the floor under the hips, shins flat behind, tops of the feet down."""
    c.setdefault("pelvis", (0, 2.5 + lean, -45.5))
    for side, sx in SIDES:
        c.setdefault(f"foot_{side}", (sx * 13.0, 40.0, 6.5))
        c.setdefault(f"foot_rot_{side}", (150, 0, 0))
        c.setdefault(f"knee_{side}", (0.1 * sx, -1.0, -0.2))


def clip_kneel():
    n = 90
    ch = {
        "hand_l": (7, 7, 179), "hand_r": (-7, 7, 179),
        "face_l": ((-0.8, 0.3, 0.2), (0, -0.3, -1)), "face_r": ((0.8, 0.3, 0.2), (0, -0.3, -1)),
        "elbow_l": (1, -0.25, 0.15), "elbow_r": (-1, -0.25, 0.15),
        "fingers_l": 0.45, "fingers_r": 0.45, "clav_l": (6, 0), "clav_r": (6, 0),
        "head": L(n, (0, (14, 0, 0)), (45, (12, 1, 5))),
        "spine": L(n, (0, (4, 0, 0)), (45, (5, 1, 1))),
    }

    def fn(f, c):
        kneel_legs(c)
        breathe(f, c, 1.2)
        add(c, "pelvis", (0.5 * wave(f, 90), 0, 0))
    return Clip("Kneel", n, True, ch, fn=fn)


def cuffed_hands(c, f, n, tug=0.0):
    for side, sx in SIDES:
        c[f"hand_{side}"] = (sx * 3.5, 17.5 + tug, 100.0)
        c[f"face_{side}"] = ((0, 0.35, -1), (-sx, 0.2, 0))
        c[f"elbow_{side}"] = (sx * 0.8, 1.0, 0.2)
        c[f"fingers_{side}"] = 0.35
        c[f"clav_{side}"] = (-3, -6)


def clip_cuffed():
    n = 90
    ch = {"head": L(n, (0, (18, 0, 0)), (40, (16, 2, 8)), (70, (19, -1, -4))),
          "spine": L(n, (0, (10, 0, 0)), (45, (11, 1, 2)))}

    def fn(f, c):
        kneel_legs(c)
        cuffed_hands(c, f, n, 0.8 * wave(f, 45))
        breathe(f, c, 1.3)
    return Clip("Cuffed", n, True, ch, fn=fn)


def clip_hands_behind():
    """Walking cuffed: only the upper body is meant to be used (the game layers it over walking)."""
    n = 60
    ch = {"head": L(n, (0, (8, 0, 0)), (30, (9, 1, 3))), "spine": L(n, (0, (5, 0, 0)), (30, (6, 1, 0)))}

    def fn(f, c):
        idle_base(f, c, 1.0, 0.6, 60)
        cuffed_hands(c, f, n, 0.6 * wave(f, 30))
    return Clip("HandsBehind", n, True, ch, fn=fn)


def crouch_legs(c, depth=48.0, back=6.0, width=15.0):
    c.setdefault("pelvis", (0, back, -depth))
    c.setdefault("hips", (22, 0, 0))
    for side, sx in SIDES:
        c.setdefault(f"foot_{side}", (sx * width, -2.0, 8.2))
        c.setdefault(f"knee_{side}", (0.4 * sx, -1.0, 0.3))


def clip_cower():
    n = 60
    ch = {
        "hand_l": (10, -14, 168), "hand_r": (-10, -12, 170),
        "face_l": ((-0.6, 0.2, 0.8), (-0.2, -0.9, 0.2)), "face_r": ((0.6, 0.2, 0.8), (0.2, -0.9, 0.2)),
        "elbow_l": (0.4, -1, -0.5), "elbow_r": (-0.4, -1, -0.5),
        "fingers_l": 0.5, "fingers_r": 0.5, "clav_l": (10, 10), "clav_r": (10, 10),
        "spine": (42, 0, 0), "head": (28, 0, 0),
    }

    def fn(f, c):
        crouch_legs(c)
        add(c, "spine", (1.5 * wave(f, 20), 1.8 * wave(f, 6), 0.8 * wave(f, 11)))
        add(c, "head", (2.0 * wave(f, 30), 1.2 * wave(f, 7, 0.2), 6.0 * wave(f, 60)))
        breathe(f, c, 2.0, 20)
    return Clip("Cower", n, True, ch, fn=fn)


def clip_dance():
    """A groovy bounce: shoulder shimmy, hip sway and pumping fists (4 beats at 120 bpm)."""
    n = 60

    def fn(f, c):
        beat = f / 15.0
        b = math.sin(math.pi * 2 * beat)
        bounce = abs(math.sin(math.pi * beat))
        side = math.sin(math.pi * beat)          # sways left on one beat, right on the next
        c["pelvis"] = (5.0 * side, 0, -6.0 + 3.5 * (1 - bounce))
        c["hips"] = (0, -7 * side, 8 * side)
        c["spine"] = (6 - 3 * bounce, 9 * side, -10 * side)
        c["head"] = (6 * bounce - 3, -5 * side, 10 * side)
        for s, sx in SIDES:
            lift = math.sin(math.pi * (beat + (0 if s == "l" else 1)))
            c[f"hand_space_{s}"] = "chest"
            c[f"hand_{s}"] = (sx * 22, -26 + 6 * lift, 128 + 16 * max(0.0, lift))
            c[f"elbow_{s}"] = (sx, 0.5, -0.8)
            c[f"face_{s}"] = ((0, -0.3, 1), (-sx, -0.2, 0))
            c[f"fingers_{s}"] = 0.95
            c[f"clav_{s}"] = (6 * max(0.0, lift), 3 * lift)
            c[f"foot_{s}"] = (sx * 15, 0.5 - 3 * max(0.0, lift * sx * side), ANKLE + 3.5 * max(0.0, -side * sx))
            c[f"foot_rot_{s}"] = (14 * max(0.0, -side * sx), 0, sx * 10)
            c[f"toe_{s}"] = 14 * max(0.0, -side * sx)
            c[f"knee_{s}"] = (0.5 * sx, -1, 0)
        _ = b
    return Clip("Dance", n, True, {}, fn=fn, note="120 bpm")


def clip_dance2():
    """Disco: the right arm points up across the body and down to the hip, hips rolling (4 beats at 120 bpm)."""
    n = 60

    def fn(f, c):
        beat = f / 15.0
        up = (int(beat) % 2 == 0)
        u = beat % 1.0
        hit = fp.ease(min(1.0, u / 0.35))
        pose_a = (-28, -18, 185) if up else (-6, -28, 102)
        pose_b = (-6, -28, 102) if up else (-28, -18, 185)
        hand = tuple(pb + (pa - pb) * hit for pa, pb in zip(pose_a, pose_b))
        side = 1 if up else -1
        roll = math.sin(math.pi * beat)
        c["pelvis"] = (3.5 * roll, 0, -5 + 2.0 * abs(math.cos(math.pi * beat)))
        c["hips"] = (0, -6 * roll, 10 * roll)
        c["spine"] = (3, -8 * side * hit, -12 * roll)
        c["head"] = (-4 * side, 4 * side, -8 * side)
        c["hand_r"] = hand
        c["elbow_r"] = (-1, 0.2, -0.3)
        c["face_r"] = ((-0.4, -0.2, 1) if up else (0.2, -0.4, -1), (0.6, -0.6, 0))
        c["fingers_r"] = (0.9, 0.0, 1.0, 1.0, 1.0)
        c["hand_l"] = (22, -8, 104)
        c["face_l"] = ((-0.5, 0.2, -1), (-1, 0, 0.3))
        c["elbow_l"] = (1, 0.2, 0)
        c["fingers_l"] = 0.5
        for s, sx in SIDES:
            c[f"foot_{s}"] = (sx * 16, 0.5, ANKLE)
            c[f"foot_rot_{s}"] = (0, 0, sx * 12)
            c[f"knee_{s}"] = (0.6 * sx, -1, 0)
        lift = "l" if up else "r"
        sx = 1 if lift == "l" else -1
        c[f"foot_{lift}"] = (sx * 16, -2, ANKLE + 5 * math.sin(math.pi * u))
        c[f"foot_rot_{lift}"] = (20 * math.sin(math.pi * u), 0, sx * 12)
        c[f"toe_{lift}"] = 20 * math.sin(math.pi * u)
    return Clip("Dance2", n, True, {}, fn=fn, note="120 bpm")


def clip_slump():
    n = 120
    ch = {
        "spine": L(n, (0, (24, 0, 0)), (60, (27, 1, 0))),
        "head": L(n, (0, (32, 0, 0)), (50, (35, 2, -4)), (90, (30, -1, 3))),
        "clav_l": (-4, 10), "clav_r": (-4, 10),
        "hand_l": (15, -14, 95), "hand_r": (-15, -14, 95),
        "face_l": ((0, -0.3, -1), (-1, 0, 0)), "face_r": ((0, -0.3, -1), (1, 0, 0)),
        "elbow_l": (0.4, 1, 0), "elbow_r": (-0.4, 1, 0),
        "fingers_l": 0.3, "fingers_r": 0.3,
    }

    def fn(f, c):
        idle_base(f, c, 1.8, 0.8, 120)
        # A long sigh halfway: the chest drops and the shoulders sag.
        sigh = max(0.0, math.sin(math.pi * ((f % n) - 55) / 40)) if 55 <= (f % n) <= 95 else 0.0
        add(c, "spine", (4 * sigh, 0, 0))
        add(c, "head", (3 * sigh, 0, 0))
    return Clip("Slump", n, True, ch, fn=fn)


def clip_dazed():
    """Sat on the ground, hands behind propping up, head lolling in circles."""
    n = 90

    def fn(f, c):
        a = 2 * math.pi * f / n
        c["pelvis"] = (2 * math.sin(a), 4.0, -80.0)
        c["hips"] = (-20, 0, 0)
        c["spine"] = (4 + 5 * math.cos(a), 6 * math.sin(a), 3 * math.sin(a))
        c["head"] = (8 + 8 * math.cos(a + 0.8), 10 * math.sin(a + 0.8), 8 * math.sin(a))
        for s, sx in SIDES:
            c[f"foot_{s}"] = (sx * 22, -78, 7.0)
            c[f"foot_rot_{s}"] = (-10, 0, sx * 20)
            c[f"knee_{s}"] = (0.3 * sx, -0.4, 1.0)
            c[f"hand_space_{s}"] = "world"
            c[f"hand_{s}"] = (sx * 26, 26, 8.5)
            c[f"face_{s}"] = ((0.2 * sx, 1, -0.2), (0, 0, -1))
            c[f"elbow_{s}"] = (0.4 * sx, 1, 0.2)
            c[f"fingers_{s}"] = 0.15
    return Clip("Dazed", n, True, {}, fn=fn)


def clip_sit_cuffed():
    n = 120
    ch = {"head": L(n, (0, (14, 0, 0)), (40, (12, 2, 20)), (80, (16, 0, -5))), "spine": (16, 0, 0)}

    def fn(f, c):
        seated_car(c)
        c["hips"] = (-8, 0, 0)
        cuffed_hands(c, f, n, 0.0)
        for side, sx in SIDES:
            c[f"hand_{side}"] = (sx * 4.0, 16.0, 100.0)
        breathe(f, c, 1.0)
    return Clip("SitCuffed", n, True, ch, fn=fn)


def clip_sit_hands_up():
    n = 60

    def fn(f, c):
        seated_car(c)
        c["spine"] = (10, 0, 0)
        c["head"] = (4, 0, 2 * wave(f, 60))
        for side, sx in SIDES:
            c[f"hand_{side}"] = (sx * 27, -8, 176 + 0.5 * wave(f, 30))
            c[f"face_{side}"] = palm_fwd(sx, (-0.1 * sx, 0.1, 1))
            c[f"elbow_{side}"] = (sx, 0.1, -0.6)
            c[f"fingers_{side}"] = 0.12
            c[f"spread_{side}"] = 0.7
        breathe(f, c, 1.3, 60)
    return Clip("SitHandsUp", n, True, {}, fn=fn)


def clip_struggle():
    """Grappling face to face with someone ~55 cm ahead: shoving and wrenching back and forth."""
    n = 48

    def fn(f, c):
        a = 2 * math.pi * f / n
        push = math.sin(a)
        wrench = math.sin(2 * a + 0.6)
        c["pelvis"] = (3 * wrench, -4 * push, -9 + 2 * abs(wrench))
        c["hips"] = (8 + 4 * push, -4 * wrench, 12 * wrench)
        c["spine"] = (14 + 6 * push, 6 * wrench, 16 * wrench)
        c["head"] = (-6 - 4 * push, -4 * wrench, -12 * wrench)
        c["foot_l"] = (13, -16, ANKLE)
        c["foot_r"] = (-15, 20, ANKLE)
        c["foot_rot_r"] = (10 + 8 * max(0, push), 0, -20)
        c["toe_r"] = 10 + 8 * max(0, push)
        c["foot_rot_l"] = (0, 0, 8)
        for s, sx in SIDES:
            c[f"hand_space_{s}"] = "body"
            c[f"hand_{s}"] = (sx * 16 + 3 * wrench, -44 - 5 * push + 3 * sx * wrench, 128 + 4 * sx * wrench)
            c[f"face_{s}"] = ((-0.4 * sx, -0.6, 0.6), (-0.3 * sx, -0.9, -0.1))
            c[f"elbow_{s}"] = (sx, 0.2, -0.8)
            c[f"fingers_{s}"] = 0.75
            c[f"knee_{s}"] = (0.4 * sx, -1, 0)
            c[f"clav_{s}"] = (4 + 3 * sx * wrench, 8)
    return Clip("Struggle", n, True, {}, fn=fn, note="partner ~55 cm ahead, facing")


def clip_idle_bored():
    n = 240
    ch = {
        "head": L(n, (0, (2, 0, 0)), (30, (-8, 2, 25)), (60, (4, 0, 0)), (110, (-4, -3, -35)), (150, (2, 0, -5)),
                  (175, (-16, 0, 0)), (195, (0, 0, 0))),
        "spine": L(n, (0, (2, 0, 0)), (170, (1, 0, 0)), (185, (-4, 0, 0)), (200, (3, 0, 0))),
        "hand_l": L(n, (0, RELAXED["l"]), (60, (21, -3, 93)), (80, (15, -24, 110)), (100, (14, -26, 112)),
                    (120, (21, -2, 93))),
        "face_l": L(n, (0, ((0, -0.15, -1), (-1, 0.1, 0))), (60, ((0, -0.15, -1), (-1, 0.1, 0))),
                    (80, ((-0.8, -0.5, 0.2), (0, 0.2, 1))), (100, ((-0.8, -0.5, 0.2), (0, 0.2, 1))),
                    (120, ((0, -0.15, -1), (-1, 0.1, 0)))),
        "fingers_l": L(n, (0, 0.25), (80, 0.6), (100, 0.6), (120, 0.25)),
        "hand_r": L(n, (0, RELAXED["r"]), (140, (-22, -1, 93)), (160, (-19, 8, 102)), (220, (-19, 8, 102)),
                    (235, RELAXED["r"])),
        "face_r": L(n, (0, ((0, -0.15, -1), (1, 0.1, 0))), (140, ((0, -0.15, -1), (1, 0.1, 0))),
                    (160, ((0.3, 0.3, -1), (0.6, 0.9, 0))), (220, ((0.3, 0.3, -1), (0.6, 0.9, 0))),
                    (235, ((0, -0.15, -1), (1, 0.1, 0)))),
    }

    def fn(f, c):
        idle_base(f, c, 1.1, 1.8, 120)
        # Rock up onto the toes and back once.
        rock = max(0.0, math.sin(math.pi * ((f % n) - 180) / 30)) if 180 <= (f % n) <= 210 else 0.0
        for s, sx in SIDES:
            c[f"foot_{s}"] = (sx * 12.5, 0.5, ANKLE + 6.0 * rock)
            c[f"foot_rot_{s}"] = (24 * rock, 0, 0)
            c[f"toe_{s}"] = 24 * rock
        add(c, "pelvis", (0, -1.5 * rock, 5.0 * rock))
    return Clip("Idle_Bored", n, True, ch, fn=fn)


def clip_phone():
    n = 150
    ch = {
        "hand_r": L(n, (0, (-6, -30, 118)), (75, (-6.5, -30.5, 119))),
        "face_r": ((0.25, -0.9, 0.35), (0.1, 0.35, 1.0)),
        "elbow_r": (-0.6, 0.6, -1),
        "fingers_r": (0.1, 0.55, 0.6, 0.65, 0.7),
        "hand_l": L(n, (0, RELAXED["l"]), (70, (21, -2, 93.5))),
        "fingers_l": 0.3,
        "head": L(n, (0, (26, 0, -4)), (60, (28, 2, -6)), (100, (24, -1, -3))),
        "spine": (5, 0, 0),
    }

    def fn(f, c):
        idle_base(f, c, 0.8, 1.0, 150)
        scroll = max(0.0, wave(f, 25))
        c["fingers_r"] = (0.1 + 0.5 * scroll, 0.55, 0.6, 0.65, 0.7)
    return Clip("Phone", n, True, ch, fn=fn)


def clip_wave():
    n = 36

    def fn(f, c):
        idle_base(f, c, 0.8, 0.8, 72)
        w = wave(f, n)
        c["hand_r"] = (-34 - 5 * w, -6, 172)
        c["face_r"] = ((0.2 * w - 0.1, 0.05, 1), (0.1, -1, 0))
        c["elbow_r"] = (-1, 0.1, -0.6)
        c["wrist_r"] = (0, 0, 0)
        c["fingers_r"] = 0.05
        c["spread_r"] = 0.5
        c["clav_r"] = (10, 0)
        c["spine"] = fp._add(c["spine"], (-2, -4, 0))
        c["head"] = fp._add(c.get("head", (0, 0, 0)), (-3, -2 * w, -4))
    return Clip("Wave", n, True, {}, fn=fn)


def clip_point():
    n = 90
    ch = {
        "hand_r": L(n, (0, (-16, -58, 140)), (45, (-15, -59, 141))),
        "face_r": ((0.05, -1, 0.05), (0.2, 0, -1)),
        "elbow_r": (-1, 0.2, -0.5),
        "fingers_r": (0.7, 0.0, 1.0, 1.0, 1.0),
        "hand_l": (21, -1, 94), "fingers_l": 0.3,
        "clav_r": (4, 8),
        "spine": (2, -2, 8), "head": L(n, (0, (0, 0, 0)), (40, (-2, 0, 4)), (70, (1, 0, -2))),
    }
    return Clip("Point", n, True, ch, fn=lambda f, c: idle_base(f, c, 0.8, 0.8, 90))


def clip_clipboard():
    """Writing a ticket: clipboard held flat-ish in the left hand, pen scribbling in the right."""
    n = 90

    def fn(f, c):
        idle_base(f, c, 0.7, 0.7, 90)
        c["spine"] = fp._add(c["spine"], (6, 0, 4))
        c["head"] = fp._add(c.get("head", (0, 0, 0)), (24, 0, -2))
        c["hand_l"] = (10, -28, 116)
        c["face_l"] = ((-0.9, -0.3, 0.2), (0.1, 0.4, 1.0))
        c["elbow_l"] = (1, 0.4, -1)
        c["fingers_l"] = (0.2, 0.55, 0.6, 0.65, 0.7)
        line = (f % 30) / 30.0
        c["hand_r"] = (-2 + 10 * line + 0.8 * wave(f, 4), -30, 121 - 2.5 * ((f // 30) % 3) + 0.6 * wave(f, 3))
        c["face_r"] = ((0.6, -0.7, -0.4), (0.3, 0.2, -1.0))
        c["elbow_r"] = (-1, 0.5, -0.8)
        c["fingers_r"] = (0.55, 0.45, 0.6, 0.8, 0.85)
    return Clip("Clipboard", n, True, {}, fn=fn)


def clip_search():
    """Patting down someone standing 55 cm ahead (facing away): armpits to hips, then down the legs."""
    n = 150
    y = -46.0
    torso = [(0, 132), (18, 118), (36, 104), (50, 96)]
    legs = [(70, 82), (88, 60), (106, 38), (122, 26)]

    def fn(f, c):
        f = f % n
        down = 0.0
        if 50 <= f < 130:
            down = fp.ease(min(1.0, (f - 50) / 30.0)) if f < 110 else fp.ease(max(0.0, 1.0 - (f - 110) / 30.0))
        elif f >= 130:
            down = max(0.0, 1.0 - (f - 110) / 30.0)
        z = None
        for (f0, z0), (f1, z1) in zip(torso + legs, (torso + legs)[1:]):
            if f0 <= f < f1:
                z = z0 + (z1 - z0) * fp.ease((f - f0) / (f1 - f0))
        if z is None:
            u = (f - 122) / (n - 122)
            z = 26 + (132 - 26) * fp.ease(u)
        pat = abs(math.sin(math.pi * f / 9.0))
        width = 17.0 if z > 90 else 13.0
        c["pelvis"] = (0, 8 * down, -2 - 42 * down)
        c["hips"] = (22 * down, 0, 0)
        c["spine"] = (10 + 38 * down, 0, 0)
        c["head"] = (10 - 10 * down, 0, 0)
        for s, sx in SIDES:
            c[f"foot_{s}"] = (sx * 17, 2, ANKLE)
            c[f"knee_{s}"] = (0.5 * sx, -1, 0.2)
            c[f"hand_space_{s}"] = "world"
            c[f"hand_{s}"] = (sx * (width + 2.5 * pat), y + 4 + 2 * pat, z)
            c[f"face_{s}"] = ((0, -0.3, -1), (-sx, 0, 0))
            c[f"elbow_{s}"] = (sx, 0.4, -0.4)
            c[f"fingers_{s}"] = 0.2 + 0.2 * pat
    return Clip("Search", n, True, {}, fn=fn, note="suspect 55 cm ahead, facing away (SearchedPose)")


def clip_searched_pose():
    """Hands on a wall 68 cm ahead at head height, legs apart, leaning in."""
    n = 90

    def fn(f, c):
        c["pelvis"] = (0.8 * wave(f, 90), 7, -4)
        c["hips"] = (6, 0, 0)
        c["spine"] = (14 + 0.8 * wave(f, 90), 0, 0)
        c["head"] = (10, 2 * wave(f, 45), 18 * wave(f, 90, 0.2))
        for s, sx in SIDES:
            c[f"foot_{s}"] = (sx * 30, 8, ANKLE)
            c[f"foot_rot_{s}"] = (0, 0, sx * 8)
            c[f"knee_{s}"] = (0.4 * sx, -1, 0)
            c[f"hand_space_{s}"] = "world"
            c[f"hand_{s}"] = (sx * 34, -64, 152)
            c[f"face_{s}"] = ((0.1 * sx, 0.05, 1), (0, -1, 0))
            c[f"elbow_{s}"] = (sx, 0, -1)
            c[f"fingers_{s}"] = 0.08
            c[f"spread_{s}"] = 0.6
        breathe(f, c, 1.2, 45)
    return Clip("SearchedPose", n, True, {}, fn=fn, note="wall 68 cm ahead")


def clip_spray():
    """Spraying graffiti on a wall 60 cm ahead: the can sweeps big arcs at chest height."""
    n = 90

    def fn(f, c):
        a = 2 * math.pi * f / n
        idle_base(f, c, 0.8, 1.4, 90)
        x = -8 - 22 * math.sin(a)
        z = 138 + 14 * math.sin(2 * a)
        c["hand_space_r"] = "world"
        c["hand_r"] = (x, -48, z)
        c["face_r"] = ((0.1, -0.2, 1), (0.9, -0.3, 0))
        c["elbow_r"] = (-1, 0.3, -0.6)
        c["fingers_r"] = (0.5, 0.35, 0.85, 0.9, 0.9)
        c["hand_l"] = (20, -4, 100)
        c["face_l"] = ((0, 0.2, -1), (-1, 0.3, 0))
        c["fingers_l"] = 0.4
        c["spine"] = fp._add(c["spine"], (6, 3 * math.sin(a), 10 * math.sin(a)))
        c["head"] = fp._add(c.get("head", (0, 0, 0)), (-3 - 8 * math.sin(2 * a), 0, 12 * math.sin(a - 0.3)))
        c["foot_l"] = (13, -8, ANKLE)
        c["foot_r"] = (-13, 6, ANKLE)
    return Clip("Spray", n, True, {}, fn=fn, note="wall 60 cm ahead")


def clip_smash():
    """Kicking then bashing something knee-high 55 cm ahead (a door, a car, a vending machine)."""
    n = 72
    kick = E((0, (-12.5, 0.5, ANKLE)), (6, (-16, 14, 20)), (13, (-10, -46, 36)), (18, (-12, -30, 28)),
             (26, (-12.5, 0.5, ANKLE)))
    ch = {
        "foot_r": L(n, (0, (-12.5, 0.5, ANKLE)), (7, (-15, 14, 22)), (13, (-9, -46, 36)), (18, (-12, -28, 26)),
                    (26, (-12.5, 0.5, ANKLE)), (72, (-12.5, 0.5, ANKLE)), mode="ease"),
        "foot_rot_r": L(n, (0, (0, 0, 0)), (7, (40, 0, 0)), (13, (-30, 0, 0)), (20, (0, 0, 0))),
        "pelvis": L(n, (0, (0, 0, -3)), (7, (2, 3, -1)), (13, (1, -4, -4)), (22, (0, 0, -4)), (36, (0, 1, -6)),
                    (44, (0, -6, -18)), (52, (0, -2, -10)), (64, (0, 0, -3))),
        "hips": L(n, (0, (0, 0, 0)), (7, (-8, 0, -10)), (13, (-14, 0, 10)), (24, (0, 0, 0)), (40, (-4, 0, 0)),
                  (46, (18, 0, 0)), (60, (4, 0, 0))),
        "spine": L(n, (0, (4, 0, 0)), (7, (-6, 0, -6)), (13, (-10, 0, 8)), (24, (5, 0, 0)), (40, (-14, 0, 0)),
                   (46, (40, 0, 0)), (56, (20, 0, 0)), (66, (4, 0, 0))),
        "head": L(n, (0, (10, 0, 0)), (13, (18, 0, 0)), (40, (-4, 0, 0)), (46, (-10, 0, 0)), (60, (8, 0, 0))),
        "hand_l": L(n, (0, (24, -10, 118)), (13, (34, -12, 128)), (26, (20, -16, 120)), (38, (8, -6, 190)),
                    (46, (6, -54, 110)), (56, (10, -40, 118)), (68, (24, -10, 118))),
        "hand_r": L(n, (0, (-22, -16, 116)), (13, (-30, 10, 118)), (26, (-18, -16, 120)), (38, (-8, -6, 190)),
                    (46, (-6, -54, 110)), (56, (-10, -40, 118)), (68, (-22, -16, 116))),
        "fingers_l": 0.95, "fingers_r": 0.95,
        "face_l": L(n, (0, fist_guard(1)), (38, ((0, 0.3, 1), (-1, 0, 0))), (46, ((0, -0.4, -1), (-1, 0, 0))),
                    (60, fist_guard(1))),
        "face_r": L(n, (0, fist_guard(-1)), (38, ((0, 0.3, 1), (1, 0, 0))), (46, ((0, -0.4, -1), (1, 0, 0))),
                    (60, fist_guard(-1))),
        "elbow_l": (1, 0.2, -0.6), "elbow_r": (-1, 0.2, -0.6),
        "foot_l": (12.5, -4, ANKLE),
    }
    _ = kick
    return Clip("Smash", n, True, ch, lag={"head": 2, "spine": 1})


def clip_grab():
    """Rummaging on shelves at waist height: reach, grab, stuff it in the jacket, glance round."""
    n = 120
    ch = {
        "hand_space_r": "world", "hand_space_l": "world",
        "hand_r": L(n, (0, (-14, -38, 104)), (18, (-30, -44, 100)), (30, (-26, -40, 102)), (42, (-8, -16, 118)),
                    (54, (-6, -14, 116)), (70, (-12, -42, 100)), (90, (4, -46, 104)), (104, (-8, -20, 116))),
        "fingers_r": L(n, (0, 0.25), (18, 0.1), (24, 0.8), (42, 0.8), (50, 0.2), (70, 0.1), (90, 0.8), (104, 0.4)),
        "face_r": ((0.1, -1, -0.4), (0.3, 0, -1)),
        "elbow_r": (-1, 0.4, -0.5),
        "hand_l": L(n, (0, (16, -36, 100)), (30, (24, -44, 98)), (60, (12, -40, 104)), (90, (22, -42, 102))),
        "fingers_l": L(n, (0, 0.3), (30, 0.1), (40, 0.7), (60, 0.3), (90, 0.1), (100, 0.7)),
        "face_l": ((-0.1, -1, -0.4), (-0.3, 0, -1)),
        "elbow_l": (1, 0.4, -0.5),
        "spine": L(n, (0, (22, 0, 0)), (18, (26, -4, -12)), (42, (14, 0, 0)), (70, (24, 0, -6)), (90, (24, 2, 10)),
                   (104, (16, 0, 0))),
        "head": L(n, (0, (14, 0, 0)), (45, (-4, 0, 40)), (55, (-4, 0, -40)), (66, (12, 0, 0)), (104, (6, 0, 20))),
        "pelvis": (0, 4, -6),
    }
    return Clip("Grab", n, True, ch, fn=lambda f, c: breathe(f, c, 1.2, 40), note="shelf at waist height, 40 cm ahead")


def clip_sneak():
    """A crouched tiptoe walk at 1 m/s (in place: the feet slide back under the body at 1 m/s)."""
    n = 36                       # 1.2 s per cycle, two steps
    travel = 100.0 * n / FPS * 0.6    # stance lasts 60% of the cycle

    def fn(f, c):
        ph = f / n
        c["pelvis"] = (2.5 * math.sin(2 * math.pi * ph), 4, -24 + 2.5 * math.cos(4 * math.pi * ph))
        c["hips"] = (14, -3 * math.sin(2 * math.pi * ph), 6 * math.sin(2 * math.pi * ph))
        c["spine"] = (22, 3 * math.sin(2 * math.pi * ph), -8 * math.sin(2 * math.pi * ph))
        c["head"] = (-18, 0, 6 * math.sin(2 * math.pi * ph + 0.5))
        bases = {"l": (12, -2, ANKLE), "r": (-12, -2, ANKLE)}
        apply_gait(c, f, n, travel, 0.4, 13.0, (0, -1, 0), bases)
        for s, sx in SIDES:
            sw = math.sin(2 * math.pi * ph + (0 if s == "l" else math.pi))
            c[f"hand_{s}"] = (sx * 20, -26 - 7 * sw, 128 + 3 * sw)
            c[f"face_{s}"] = ((0.1 * sx, -0.5, -1), (-0.2 * sx, -1, 0.3))
            c[f"elbow_{s}"] = (sx, 0.6, -0.4)
            c[f"fingers_{s}"] = (0.3, 0.25, 0.35, 0.45, 0.55)
            c[f"knee_{s}"] = (0.3 * sx, -1, 0)
    return Clip("Sneak", n, True, {}, fn=fn, travel=(0, -100.0 * n / FPS), note="1 m/s")


# ======================================================================================
# One-shots
# ======================================================================================
def suspect_wrists():
    """Where a kneeling cuffed suspect's wrists are, relative to their root (solved from the Cuffed clip)."""
    return None   # filled in by build_character_anims (it has the rig)


SUSPECT_WRISTS = {}


def clip_cuffing():
    """
    Cuffing a suspect kneeling 76 cm ahead (facing away, Cuffed clip) in 2.6 s: step in and stoop, take the wrists,
    snap the cuffs on, stand back up.
    """
    n = 78
    wl = Vector(SUSPECT_WRISTS.get("l", (4.5, -60.0, 60.0)))
    wr = Vector(SUSPECT_WRISTS.get("r", (-4.5, -60.0, 60.0)))
    mid = (wl + wr) / 2
    ch = {
        "pelvis": E((0, (0, 0, -1.5)), (14, (2, -14, -18)), (58, (2, -14, -20)), (72, (0, -2, -3)), (78, (0, 0, -1.5))),
        "hips": E((0, (0, 0, 0)), (14, (26, 0, 0)), (58, (28, 0, 0)), (74, (0, 0, 0))),
        "spine": E((0, (2, 0, 0)), (16, (34, 0, 0)), (40, (38, 2, 4)), (58, (34, 0, 0)), (76, (2, 0, 0))),
        "head": E((0, (0, 0, 0)), (16, (18, 0, 0)), (60, (18, 0, 0)), (78, (0, 0, 0))),
        "foot_l": E((0, (12.5, 0.5, ANKLE)), (5, (12, -12, 14)), (10, (12, -28, ANKLE)), (62, (12, -28, ANKLE)),
                    (67, (12, -12, 14)), (72, (12.5, 0.5, ANKLE))),
        "foot_rot_l": E((0, (0, 0, 0)), (5, (20, 0, 0)), (10, (0, 0, 0)), (62, (0, 0, 0)), (67, (15, 0, 0)),
                        (72, (0, 0, 0))),
        "foot_r": (-13, 4, ANKLE),
        "hand_space_l": "world", "hand_space_r": "world",
        "hand_l": E((0, (22, -1.5, 93)), (12, (14, -40, 80)), (22, tuple(wl + Vector((1.5, 3, 3)))),
                    (48, tuple(wl + Vector((1.0, 3, 3)))), (62, (16, -32, 92)), (76, (22, -1.5, 93))),
        "hand_r": E((0, (-22, -1.5, 93)), (10, (-20, -30, 96)), (26, tuple(mid + Vector((-6, 5, 9)))),
                    (32, tuple(wr + Vector((-1.5, 3, 3)))), (38, tuple(wr + Vector((-1.0, 2.5, 4)))),
                    (42, tuple(wr + Vector((-1.0, 3, 3)))), (54, tuple(wr + Vector((-1.5, 4, 4)))),
                    (66, (-18, -26, 94)), (78, (-22, -1.5, 93))),
        "face_l": E((0, ((0, -0.15, -1), (-1, 0.1, 0))), (18, ((-0.4, -0.8, -0.5), (0, 0, -1))),
                    (50, ((-0.4, -0.8, -0.5), (0, 0, -1))), (72, ((0, -0.15, -1), (-1, 0.1, 0)))),
        "face_r": E((0, ((0, -0.15, -1), (1, 0.1, 0))), (24, ((0.4, -0.8, -0.4), (0.3, 0, -1))),
                    (38, ((0.2, -0.9, -0.4), (1, 0, 0))), (54, ((0.4, -0.8, -0.4), (0.3, 0, -1))),
                    (76, ((0, -0.15, -1), (1, 0.1, 0)))),
        "fingers_l": E((0, 0.25), (18, 0.1), (24, 0.75), (50, 0.75), (58, 0.2), (78, 0.25)),
        "fingers_r": E((0, 0.25), (10, 0.6), (30, 0.6), (36, 0.85), (44, 0.6), (56, 0.5), (78, 0.25)),
        "elbow_l": (1, 0.6, -0.6), "elbow_r": (-1, 0.6, -0.6),
    }
    return Clip("Cuffing", n, False, ch, lag={"head": 3}, hit=(38, "hand_r"),
                note="suspect kneeling 76 cm ahead, facing away")


def clip_tackle():
    """A flying dive: crouch, launch forward, body flat with the arms reaching (held; the game handles landing)."""
    n = 24
    ch = {
        "pelvis": E((0, (0, 0, -1.5)), (6, (0, 6, -22)), (12, (0, -45, -10)), (18, (0, -70, -18)), (24, (0, -80, -20))),
        "hips": E((0, (0, 0, 0)), (6, (30, 0, 0)), (12, (62, 0, 0)), (18, (80, 0, 0)), (24, (82, 0, 0))),
        "spine": E((0, (2, 0, 0)), (6, (25, 0, 0)), (12, (-8, 0, 0)), (24, (-14, 0, 0))),
        "head": E((0, (0, 0, 0)), (6, (-10, 0, 0)), (14, (-40, 0, 0)), (24, (-44, 0, 0))),
        "legfk_l": E((0, (0, 0, 0, 0)), (6, (-55, 0, 90, -10)), (12, (20, 0, 30, 30)), (24, (15, 0, 20, 35))),
        "legfk_r": E((0, (0, 0, 0, 0)), (6, (-45, 0, 80, -10)), (12, (35, 0, 45, 30)), (24, (28, 0, 30, 35))),
        "armfk_l": E((0, (0, 0, 10, 0)), (6, (30, 10, 60, 0)), (14, (-160, 20, 10, 0)), (24, (-168, 15, 8, 0))),
        "armfk_r": E((0, (0, 0, 10, 0)), (6, (30, 10, 60, 0)), (14, (-160, 20, 10, 0)), (24, (-168, 15, 8, 0))),
        "fingers_l": E((0, 0.3), (12, 0.1), (24, 0.35)), "fingers_r": E((0, 0.3), (12, 0.1), (24, 0.35)),
        "spread_l": 0.4, "spread_r": 0.4,
    }
    return Clip("Tackle", n, False, ch, lag={"head": 2}, travel=(0, -80.0), hit=(18, "hand_l"))


def clip_interact():
    """A generic reach-and-use: press, pick up or open something at waist-to-chest height 45 cm ahead."""
    n = 30
    ch = {
        "hand_r": E((0, RELAXED["r"]), (10, (-12, -46, 112)), (16, (-11, -48, 112)), (30, RELAXED["r"])),
        "face_r": E((0, ((0, -0.15, -1), (1, 0.1, 0))), (10, ((0.1, -1, 0), (0.3, 0, -1))), (22, ((0.1, -1, 0), (0.3, 0, -1))),
                    (30, ((0, -0.15, -1), (1, 0.1, 0)))),
        "fingers_r": E((0, 0.25), (9, 0.1), (13, 0.7), (20, 0.7), (30, 0.25)),
        "elbow_r": (-1, 0.6, -0.4),
        "spine": E((0, (2, 0, 0)), (11, (12, 0, 6)), (20, (10, 0, 4)), (30, (2, 0, 0))),
        "head": E((0, (0, 0, 0)), (10, (14, 0, 0)), (30, (0, 0, 0))),
        "pelvis": E((0, (0, 0, -1.5)), (11, (0, -3, -4)), (30, (0, 0, -1.5))),
    }
    return Clip("Interact", n, False, ch, lag={"head": 2, "fingers_r": 1})


def clip_cheer():
    n = 45
    ch = {
        "pelvis": E((0, (0, 0, -1.5)), (8, (0, 1, -14)), (14, (0, 0, 16)), (22, (0, 0, 20)), (30, (0, 0, -10)),
                    (38, (0, 0, -2)), (45, (0, 0, -1.5))),
        "spine": E((0, (2, 0, 0)), (8, (14, 0, 0)), (16, (-10, 0, 0)), (32, (-6, 0, 0)), (45, (2, 0, 0))),
        "head": E((0, (0, 0, 0)), (8, (10, 0, 0)), (16, (-16, 0, 0)), (34, (-8, 0, 0)), (45, (0, 0, 0))),
        "hand_l": E((0, RELAXED["l"]), (8, (20, -10, 108)), (15, (24, -8, 190)), (34, (26, -6, 186)), (45, RELAXED["l"])),
        "hand_r": E((0, RELAXED["r"]), (8, (-20, -10, 108)), (15, (-24, -8, 190)), (34, (-26, -6, 186)),
                    (45, RELAXED["r"])),
        "face_l": E((0, RELAXED_FACE["l"]), (15, ((0, 0, 1), (-1, -0.3, 0))), (34, ((0, 0, 1), (-1, -0.3, 0))),
                    (45, RELAXED_FACE["l"])),
        "face_r": E((0, RELAXED_FACE["r"]), (15, ((0, 0, 1), (1, -0.3, 0))), (34, ((0, 0, 1), (1, -0.3, 0))),
                    (45, RELAXED_FACE["r"])),
        "fingers_l": E((0, 0.25), (12, 1.0), (38, 1.0), (45, 0.25)),
        "fingers_r": E((0, 0.25), (12, 1.0), (38, 1.0), (45, 0.25)),
        "elbow_l": (1, 0.2, -0.5), "elbow_r": (-1, 0.2, -0.5),
    }

    def fn(f, c):
        air = max(0.0, min(1.0, (f - 12) / 3.0)) * max(0.0, min(1.0, (30 - f) / 3.0))
        h = 20.0 * math.sin(math.pi * min(1.0, max(0.0, (f - 12) / 18.0))) * (air > 0)
        for s, sx in SIDES:
            c[f"foot_{s}"] = (sx * 12.5, 0.5, ANKLE + h)
            c[f"foot_rot_{s}"] = (30 * air, 0, 0)
            c[f"toe_{s}"] = 10 * air
        add(c, "pelvis", (0, 0, h))
    return Clip("Cheer", n, False, ch, fn=fn, lag={"head": 2})


# ======================================================================================
# Fighting
# ======================================================================================
# Orthodox stance: left foot and left side forward, weight in the middle, guard up. Punch reach is measured from the
# root to the fist at contact (the partner's face/chest is about that far ahead).
STANCE_FEET = {"l": (10.0, -17.0, ANKLE), "r": (-15.0, 17.0, ANKLE)}
STANCE_YAW = {"l": -12.0, "r": -40.0}
GUARD = {"l": (9.0, -30.0, 146.0), "r": (-9.0, -20.0, 150.0)}


def stance(c, f=0, bob=1.0, period=30):
    b = math.sin(2 * math.pi * f / period)
    c.setdefault("pelvis", (0, 1.0, -8.0 + 1.6 * bob * abs(b)))
    c.setdefault("hips", (6, 0, -26))
    c.setdefault("spine", (8, 0, -6))
    c.setdefault("head", (4, 0, 30))
    for s, sx in SIDES:
        c.setdefault(f"foot_{s}", STANCE_FEET[s])
        c.setdefault(f"foot_rot_{s}", (0, 0, STANCE_YAW[s]))
        c.setdefault(f"knee_{s}", (0.45 * sx, -1.0, 0.0))
        c.setdefault(f"hand_space_{s}", "body")
        c.setdefault(f"hand_{s}", GUARD[s])
        c.setdefault(f"face_{s}", fist_guard(sx))
        c.setdefault(f"elbow_{s}", (sx * 0.6, 0.3, -1.0))
        c.setdefault(f"fingers_{s}", 0.95)
        c.setdefault(f"clav_{s}", (3, 6))


def fight_clip(name, n, keys, hit=None, fn=None, lag=None, loop=False, travel=None, note=None):
    ch = {}
    for k, ks in keys.items():
        if not isinstance(ks, list):
            ch[k] = ks
        else:
            ch[k] = L(n, *ks, mode="smooth") if loop else O(*ks, mode="smooth")

    def run(f, c):
        if fn:
            fn(f, c)
        stance(c, f)
    return Clip(name, n, loop, ch, fn=run, hit=hit, lag=lag or {"head": 2, "spine": 1}, travel=travel, note=note)


def G(side):
    return GUARD[side]


def clip_fight_idle():
    n = 30

    def fn(f, c):
        b = math.sin(2 * math.pi * f / n)
        c["pelvis"] = (0.6 * b, 1.0, -8.0 + 1.8 * abs(b))
        for s, sx in SIDES:
            gx, gy, gz = GUARD[s]
            lag = math.sin(2 * math.pi * (f - 2) / n)
            c[f"hand_{s}"] = (gx + 0.8 * sx * lag, gy - 1.0 * lag, gz + 1.8 * abs(lag) - 1.0)
        c["spine"] = (8 + 1.5 * abs(b), 0, -6 + 2 * b)
        c["head"] = (4 + abs(b), 0, 30 - 2 * b)
        # Light on the feet: the heels come up a touch on each bounce.
        for s, sx in SIDES:
            lift = 6.0 * (1 - abs(b))
            fx, fy, fz = STANCE_FEET[s]
            c[f"foot_{s}"] = (fx, fy, fz + FOOT_LEN * math.sin(math.radians(lift)))
            c[f"foot_rot_{s}"] = (lift, 0, STANCE_YAW[s])
            c[f"toe_{s}"] = lift
        stance(c, f)
    return Clip("Fight_Idle", n, True, {}, fn=fn)


def clip_block():
    n = 30

    def fn(f, c):
        b = math.sin(2 * math.pi * f / n)
        flinch = max(0.0, math.sin(2 * math.pi * f / n)) ** 4
        c["pelvis"] = (0, 3 + 2 * flinch, -11 - 1.0 * abs(b))
        c["spine"] = (18 + 4 * flinch, 0, -4)
        c["head"] = (16 + 3 * flinch, 0, 26)
        c["hand_l"] = (6, -24 + 2 * flinch, 158)
        c["hand_r"] = (-7, -22 + 2 * flinch, 159)
        c["face_l"] = ((-0.2, -0.2, 1), (-0.3, -1, 0))
        c["face_r"] = ((0.2, -0.2, 1), (0.3, -1, 0))
        c["elbow_l"] = (0.2, -0.6, -1)
        c["elbow_r"] = (-0.2, -0.6, -1)
        c["clav_l"] = (6, 12)
        c["clav_r"] = (6, 12)
        stance(c, f)
    return Clip("Block_Loop", n, True, {}, fn=fn)


def clip_jab():
    n = 20
    keys = {
        "hand_l": [(0, G("l")), (3, (11, -24, 144)), (8, (6, -64, 147)), (10, (6, -63, 147)), (16, (10, -34, 146)),
                   (20, G("l"))],
        "face_l": [(0, fist_guard(1)), (4, fist_guard(1)), (8, fist_fwd(1)), (11, fist_fwd(1)), (18, fist_guard(1))],
        "elbow_l": [(0, (0.6, 0.3, -1)), (8, (1, 0.2, -0.3)), (16, (0.6, 0.3, -1))],
        "hips": [(0, (6, 0, -26)), (3, (6, 0, -22)), (8, (8, 0, -34)), (12, (7, 0, -32)), (20, (6, 0, -26))],
        "spine": [(0, (8, 0, -6)), (3, (7, 0, -2)), (8, (12, -3, -14)), (12, (11, -2, -12)), (20, (8, 0, -6))],
        "pelvis": [(0, (0, 1, -8)), (3, (0, 2, -8.5)), (8, (0, -5, -9)), (13, (0, -3, -8.5)), (20, (0, 1, -8))],
        "clav_l": [(0, (3, 6)), (8, (7, 16)), (14, (3, 6))],
        "foot_l": [(0, STANCE_FEET["l"]), (5, (10, -21, ANKLE + 2)), (8, (10, -23, ANKLE)), (20, STANCE_FEET["l"])],
    }
    return fight_clip("Jab", n, keys, hit=(8, "hand_l"))


def clip_cross():
    n = 22
    keys = {
        "hand_r": [(0, G("r")), (4, (-10, -16, 149)), (9, (-2, -66, 148)), (11, (-2, -65, 148)), (17, (-8, -28, 149)),
                   (22, G("r"))],
        "face_r": [(0, fist_guard(-1)), (4, fist_guard(-1)), (9, fist_fwd(-1)), (12, fist_fwd(-1)), (20, fist_guard(-1))],
        "elbow_r": [(0, (-0.6, 0.3, -1)), (9, (-1, 0.3, -0.2)), (18, (-0.6, 0.3, -1))],
        "hand_l": [(0, G("l")), (6, (12, -18, 148)), (12, (12, -16, 148)), (22, G("l"))],
        "hips": [(0, (6, 0, -26)), (4, (6, 0, -34)), (9, (8, 0, 14)), (13, (8, 0, 10)), (22, (6, 0, -26))],
        "spine": [(0, (8, 0, -6)), (4, (6, 0, -12)), (9, (14, 4, 16)), (13, (13, 3, 12)), (22, (8, 0, -6))],
        "head": [(0, (4, 0, 30)), (9, (6, 0, -4)), (14, (6, 0, 2)), (22, (4, 0, 30))],
        "pelvis": [(0, (0, 1, -8)), (4, (1, 4, -9)), (9, (-1, -6, -10)), (14, (0, -4, -9)), (22, (0, 1, -8))],
        "foot_rot_r": [(0, (0, 0, STANCE_YAW["r"])), (5, (0, 0, STANCE_YAW["r"])), (9, (28, 0, -8)), (14, (22, 0, -10)),
                       (22, (0, 0, STANCE_YAW["r"]))],
        "toe_r": [(0, 0.0), (5, 0.0), (9, 28.0), (14, 22.0), (22, 0.0)],
        "foot_r": [(0, STANCE_FEET["r"]), (5, STANCE_FEET["r"]), (9, (-15, 17, ANKLE + 7)), (14, (-15, 17, ANKLE + 5.5)),
                   (22, STANCE_FEET["r"])],
        "clav_r": [(0, (3, 6)), (9, (8, 18)), (15, (3, 6))],
    }
    return fight_clip("Cross", n, keys, hit=(9, "hand_r"))


def clip_hook():
    n = 24
    keys = {
        "hand_l": [(0, G("l")), (4, (16, -24, 145)), (7, (26, -44, 146)), (10, (4, -54, 148)), (12, (-8, -48, 148)),
                   (18, (6, -32, 146)), (24, G("l"))],
        "face_l": [(0, fist_guard(1)), (5, ((-0.6, -0.8, 0), (0, 0, -1))), (10, ((-1, -0.2, 0), (0, 0, -1))),
                   (13, ((-1, 0.2, 0), (0, 0, -1))), (22, fist_guard(1))],
        "elbow_l": [(0, (0.6, 0.3, -1)), (5, (1, 0, 0.4)), (10, (0.5, 0.2, 1)), (16, (0.6, 0.3, -0.2)),
                    (24, (0.6, 0.3, -1))],
        "hips": [(0, (6, 0, -26)), (4, (6, 0, -8)), (10, (8, 0, -48)), (14, (8, 0, -44)), (24, (6, 0, -26))],
        "spine": [(0, (8, 0, -6)), (4, (6, 2, 6)), (10, (12, -4, -24)), (14, (11, -3, -20)), (24, (8, 0, -6))],
        "head": [(0, (4, 0, 30)), (10, (6, 0, 56)), (16, (6, 0, 50)), (24, (4, 0, 30))],
        "pelvis": [(0, (0, 1, -8)), (4, (2, 1, -9)), (10, (-2, -2, -10)), (16, (-1, -1, -9)), (24, (0, 1, -8))],
        "foot_rot_l": [(0, (0, 0, STANCE_YAW["l"])), (4, (0, 0, STANCE_YAW["l"])), (10, (14, 0, -50)), (16, (10, 0, -45)),
                       (24, (0, 0, STANCE_YAW["l"]))],
        "toe_l": [(0, 0.0), (4, 0.0), (10, 14.0), (16, 10.0), (24, 0.0)],
        "foot_l": [(0, STANCE_FEET["l"]), (4, STANCE_FEET["l"]), (10, (10, -17, ANKLE + 3.5)), (16, (10, -17, ANKLE + 2.5)),
                   (24, STANCE_FEET["l"])],
        "clav_l": [(0, (3, 6)), (8, (10, 16)), (14, (5, 10)), (20, (3, 6))],
    }
    return fight_clip("Hook", n, keys, hit=(10, "hand_l"))


def clip_uppercut():
    n = 26
    keys = {
        "hand_r": [(0, G("r")), (4, (-12, -18, 128)), (7, (-10, -30, 116)), (11, (-3, -42, 160)), (13, (-3, -40, 164)),
                   (19, (-8, -24, 152)), (26, G("r"))],
        "face_r": [(0, fist_guard(-1)), (6, ((0.2, -0.8, 0.4), (0.3, 0.5, 1))), (11, ((0.1, -0.2, 1), (0.1, 1, 0.2))),
                   (14, ((0.1, -0.1, 1), (0.1, 1, 0.2))), (24, fist_guard(-1))],
        "elbow_r": [(0, (-0.6, 0.3, -1)), (7, (-0.8, 0.2, -1)), (11, (-0.3, 0.2, -1)), (26, (-0.6, 0.3, -1))],
        "pelvis": [(0, (0, 1, -8)), (4, (1, 3, -14)), (7, (0, 1, -17)), (11, (-1, -4, -4)), (15, (0, -3, -5)),
                   (26, (0, 1, -8))],
        "hips": [(0, (6, 0, -26)), (5, (10, 0, -36)), (11, (2, 0, 10)), (15, (3, 0, 6)), (26, (6, 0, -26))],
        "spine": [(0, (8, 0, -6)), (5, (16, -6, -10)), (7, (18, -8, -8)), (11, (-4, 6, 16)), (15, (0, 4, 12)),
                  (26, (8, 0, -6))],
        "head": [(0, (4, 0, 30)), (7, (12, 4, 30)), (12, (-2, -4, -4)), (18, (2, 0, 6)), (26, (4, 0, 30))],
        "foot_rot_r": [(0, (0, 0, STANCE_YAW["r"])), (6, (0, 0, STANCE_YAW["r"])), (11, (24, 0, -10)), (16, (18, 0, -12)),
                       (26, (0, 0, STANCE_YAW["r"]))],
        "toe_r": [(0, 0.0), (6, 0.0), (11, 24.0), (16, 18.0), (26, 0.0)],
        "foot_r": [(0, STANCE_FEET["r"]), (6, STANCE_FEET["r"]), (11, (-15, 17, ANKLE + 6)), (16, (-15, 17, ANKLE + 4.5)),
                   (26, STANCE_FEET["r"])],
    }
    return fight_clip("Uppercut", n, keys, hit=(11, "hand_r"))


def clip_kick_front():
    """A push kick with the rear (right) leg to the belly."""
    n = 30
    keys = {
        "foot_r": [(0, STANCE_FEET["r"]), (5, (-12, 0, 42)), (9, (-10, -26, 66)), (12, (-8, -66, 84)), (15, (-8, -62, 82)),
                   (20, (-10, -22, 54)), (25, (-13, 2, 16)), (30, STANCE_FEET["r"])],
        "foot_rot_r": [(0, (0, 0, STANCE_YAW["r"])), (5, (20, 0, -10)), (9, (-20, 0, 0)), (12, (-58, 0, 0)),
                       (16, (-50, 0, 0)), (22, (10, 0, -10)), (30, (0, 0, STANCE_YAW["r"]))],
        "toe_r": [(0, 0.0), (9, 0.0), (12, -20.0), (16, -15.0), (22, 0.0)],
        "knee_r": [(0, (-0.45, -1, 0)), (6, (-0.2, -0.3, 1)), (12, (-0.1, -0.2, 1)), (22, (-0.2, -0.6, 0.6)),
                   (30, (-0.45, -1, 0))],
        "pelvis": [(0, (0, 1, -8)), (5, (0, 4, -6)), (12, (-2, 10, -8)), (16, (-2, 8, -8)), (24, (0, 3, -8)),
                   (30, (0, 1, -8))],
        "hips": [(0, (6, 0, -26)), (5, (-4, 0, -12)), (12, (-22, 0, -4)), (16, (-18, 0, -6)), (24, (0, 0, -20)),
                 (30, (6, 0, -26))],
        "spine": [(0, (8, 0, -6)), (5, (4, 0, -4)), (12, (0, -4, -4)), (16, (2, -3, -4)), (24, (6, 0, -6)),
                  (30, (8, 0, -6))],
        "head": [(0, (4, 0, 30)), (12, (12, 0, 20)), (20, (8, 0, 26)), (30, (4, 0, 30))],
        "hand_r": [(0, G("r")), (10, (-24, 2, 128)), (16, (-24, 0, 128)), (26, G("r"))],
        "hand_l": [(0, G("l")), (12, (14, -26, 150)), (26, G("l"))],
        "foot_l": [(0, STANCE_FEET["l"]), (6, (8, -10, ANKLE)), (30, STANCE_FEET["l"])],
    }
    return fight_clip("Kick_Front", n, keys, hit=(12, "ball_r"))


def clip_kick_side():
    """A side kick with the lead (left) leg: the body turns side-on and drives the heel out ahead."""
    n = 32
    keys = {
        "foot_l": [(0, STANCE_FEET["l"]), (6, (6, -12, 46)), (10, (4, -30, 76)), (13, (2, -76, 92)), (16, (2, -72, 90)),
                   (22, (5, -30, 62)), (27, (9, -18, 18)), (32, STANCE_FEET["l"])],
        "foot_rot_l": [(0, (0, 0, STANCE_YAW["l"])), (6, (10, 0, -60)), (13, (-60, 70, -90)), (17, (-55, 60, -90)),
                       (24, (10, 0, -40)), (32, (0, 0, STANCE_YAW["l"]))],
        "knee_l": [(0, (0.45, -1, 0)), (6, (-0.5, -0.5, 1)), (13, (-1, 0, 0.6)), (24, (0.2, -0.6, 0.6)),
                   (32, (0.45, -1, 0))],
        "foot_rot_r": [(0, (0, 0, STANCE_YAW["r"])), (8, (0, 0, -90)), (22, (0, 0, -90)), (32, (0, 0, STANCE_YAW["r"]))],
        "foot_r": [(0, STANCE_FEET["r"]), (8, (-8, 16, ANKLE)), (24, (-8, 16, ANKLE)), (32, STANCE_FEET["r"])],
        "pelvis": [(0, (0, 1, -8)), (6, (-2, 8, -4)), (13, (-8, 16, -2)), (17, (-7, 14, -2)), (26, (-1, 4, -8)),
                   (32, (0, 1, -8))],
        "hips": [(0, (6, 0, -26)), (6, (0, 0, -70)), (13, (-6, -30, -88)), (17, (-5, -28, -86)), (26, (4, 0, -40)),
                 (32, (6, 0, -26))],
        "spine": [(0, (8, 0, -6)), (6, (6, -4, 10)), (13, (4, -14, 10)), (17, (4, -12, 10)), (26, (8, 0, -4)),
                  (32, (8, 0, -6))],
        "head": [(0, (4, 0, 30)), (8, (4, 12, 62)), (16, (4, 16, 70)), (26, (4, 4, 38)), (32, (4, 0, 30))],
        "hand_l": [(0, G("l")), (10, (8, -26, 140)), (20, (8, -26, 140)), (32, G("l"))],
        "hand_r": [(0, G("r")), (10, (-12, 4, 144)), (20, (-12, 4, 144)), (32, G("r"))],
    }
    return fight_clip("Kick_Side", n, keys, hit=(13, "foot_l"))


def clip_kick_roundhouse():
    """A rear-leg (right) roundhouse to the ribs: pivot on the lead foot, hips whip round, shin swings through."""
    n = 34
    keys = {
        # Chamber the knee out to the side at hip height, then snap the leg out straight and sweep the instep
        # across at rib height (leg almost fully extended at contact), recoil and set the foot back down.
        "foot_r": [(0, STANCE_FEET["r"]), (5, (-22, 6, 34)), (9, (-36, -14, 98)), (12, (-10, -70, 118)),
                   (13, (18, -86, 122)), (16, (34, -70, 112)), (20, (10, -30, 80)), (27, (-12, 6, 22)),
                   (34, STANCE_FEET["r"])],
        "foot_rot_r": [(0, (0, 0, STANCE_YAW["r"])), (5, (20, 0, -30)), (9, (40, -60, 20)), (13, (45, -80, 90)),
                       (16, (40, -70, 110)), (22, (20, 0, 20)), (34, (0, 0, STANCE_YAW["r"]))],
        "knee_r": [(0, (-0.45, -1, 0)), (5, (-0.6, -0.6, 0.4)), (9, (0.1, -1, 0.3)), (13, (0.3, 0.1, 1)),
                   (16, (0.5, 0.3, 1)), (20, (-0.2, -0.8, 0.5)), (34, (-0.45, -1, 0))],
        "foot_rot_l": [(0, (0, 0, STANCE_YAW["l"])), (6, (14, 0, 20)), (13, (16, 0, 70)), (20, (12, 0, 60)),
                       (34, (0, 0, STANCE_YAW["l"]))],
        "toe_l": [(0, 0.0), (6, 14.0), (13, 16.0), (20, 12.0), (34, 0.0)],
        "foot_l": [(0, STANCE_FEET["l"]), (6, (10, -17, ANKLE + 3.5)), (20, (10, -17, ANKLE + 3)), (34, STANCE_FEET["l"])],
        "pelvis": [(0, (0, 1, -8)), (5, (3, 2, -7)), (13, (8, -2, -3)), (17, (8, -4, -4)), (26, (2, 0, -8)),
                   (34, (0, 1, -8))],
        "hips": [(0, (6, 0, -26)), (5, (0, 0, -10)), (9, (-6, 18, 30)), (13, (-10, 40, 70)), (17, (-8, 36, 76)),
                 (24, (0, 10, 20)), (34, (6, 0, -26))],
        # The chest turns with the hips (only a little held back) and leans away from the kick; the head keeps
        # looking at the target.
        "spine": [(0, (8, 0, -6)), (5, (6, 0, -8)), (13, (4, 16, -12)), (17, (4, 14, -14)), (26, (8, 2, -10)),
                  (34, (8, 0, -6))],
        "head": [(0, (4, 0, 30)), (13, (6, -14, -44)), (18, (6, -12, -44)), (27, (4, 0, 16)), (34, (4, 0, 30))],
        "hand_l": [(0, G("l")), (10, (22, -24, 140)), (18, (24, -20, 140)), (30, G("l"))],
        "hand_r": [(0, G("r")), (9, (-10, -24, 150)), (13, (-34, 10, 118)), (18, (-34, 12, 116)), (30, G("r"))],
    }
    return fight_clip("Kick_Roundhouse", n, keys, hit=(13, "foot_r"))


def clip_shove():
    n = 24
    keys = {
        "hand_l": [(0, G("l")), (4, (14, -22, 132)), (9, (14, -58, 132)), (12, (14, -56, 132)), (24, G("l"))],
        "hand_r": [(0, G("r")), (4, (-14, -18, 132)), (9, (-14, -56, 132)), (12, (-14, -54, 132)), (24, G("r"))],
        "face_l": [(0, fist_guard(1)), (5, ((0.1, -0.3, 1), (0, -1, 0.1))), (12, ((0.1, -0.3, 1), (0, -1, 0.1))),
                   (22, fist_guard(1))],
        "face_r": [(0, fist_guard(-1)), (5, ((-0.1, -0.3, 1), (0, -1, 0.1))), (12, ((-0.1, -0.3, 1), (0, -1, 0.1))),
                   (22, fist_guard(-1))],
        "fingers_l": [(0, 0.95), (5, 0.15), (14, 0.15), (22, 0.95)],
        "fingers_r": [(0, 0.95), (5, 0.15), (14, 0.15), (22, 0.95)],
        "spread_l": [(0, 0.0), (6, 0.6), (16, 0.6), (22, 0.0)],
        "spread_r": [(0, 0.0), (6, 0.6), (16, 0.6), (22, 0.0)],
        "elbow_l": [(0, (0.6, 0.3, -1)), (9, (1, 0.3, -0.6)), (24, (0.6, 0.3, -1))],
        "elbow_r": [(0, (-0.6, 0.3, -1)), (9, (-1, 0.3, -0.6)), (24, (-0.6, 0.3, -1))],
        "hips": [(0, (6, 0, -26)), (4, (4, 0, -14)), (9, (12, 0, -6)), (14, (10, 0, -8)), (24, (6, 0, -26))],
        "spine": [(0, (8, 0, -6)), (4, (4, 0, 0)), (9, (18, 0, 6)), (14, (16, 0, 4)), (24, (8, 0, -6))],
        "pelvis": [(0, (0, 1, -8)), (4, (0, 4, -9)), (9, (0, -12, -10)), (14, (0, -10, -9)), (24, (0, 1, -8))],
        "foot_l": [(0, STANCE_FEET["l"]), (4, (10, -20, ANKLE + 4)), (8, (10, -32, ANKLE)), (18, (10, -32, ANKLE)),
                   (22, (10, -24, ANKLE + 3)), (24, STANCE_FEET["l"])],
        "foot_r": [(0, STANCE_FEET["r"]), (10, STANCE_FEET["r"]), (14, (-14, 8, ANKLE + 3)), (18, (-14, 4, ANKLE)),
                   (24, STANCE_FEET["r"])],
    }
    return fight_clip("Shove", n, keys, hit=(9, "hand_r"))


def clip_grab_clinch():
    """Clinch start (named Fight_Grab: Grab is the shoplifter's rummage): both hands shoot out to the partner's collar/neck and pull them in."""
    n = 24
    keys = {
        "hand_l": [(0, G("l")), (6, (14, -40, 146)), (10, (10, -50, 150)), (16, (10, -40, 148)), (24, (10, -38, 146))],
        "hand_r": [(0, G("r")), (6, (-12, -36, 146)), (10, (-10, -50, 150)), (16, (-10, -40, 148)), (24, (-10, -38, 146))],
        "face_l": [(0, fist_guard(1)), (8, ((-0.2, -0.9, 0.3), (-0.3, -0.2, -1))), (24, ((-0.4, -0.8, 0.2), (-0.3, 0.2, -1)))],
        "face_r": [(0, fist_guard(-1)), (8, ((0.2, -0.9, 0.3), (0.3, -0.2, -1))), (24, ((0.4, -0.8, 0.2), (0.3, 0.2, -1)))],
        "fingers_l": [(0, 0.95), (6, 0.1), (10, 0.15), (13, 0.8), (24, 0.8)],
        "fingers_r": [(0, 0.95), (6, 0.1), (10, 0.15), (13, 0.8), (24, 0.8)],
        "elbow_l": [(0, (0.6, 0.3, -1)), (10, (1, 0.2, -0.6)), (24, (1, 0.4, -0.8))],
        "elbow_r": [(0, (-0.6, 0.3, -1)), (10, (-1, 0.2, -0.6)), (24, (-1, 0.4, -0.8))],
        "pelvis": [(0, (0, 1, -8)), (6, (0, -6, -9)), (10, (0, -10, -10)), (16, (0, -6, -9)), (24, (0, -4, -9))],
        "hips": [(0, (6, 0, -26)), (10, (10, 0, -12)), (24, (8, 0, -10))],
        "spine": [(0, (8, 0, -6)), (10, (18, 0, 4)), (16, (8, 0, 2)), (24, (10, 0, 2))],
        "head": [(0, (4, 0, 30)), (10, (0, 0, 14)), (24, (6, 0, 12))],
        "foot_l": [(0, STANCE_FEET["l"]), (5, (10, -22, ANKLE + 4)), (9, (10, -28, ANKLE)), (24, (10, -28, ANKLE))],
    }
    return fight_clip("Fight_Grab", n, keys, hit=(10, "hand_r"), note="ends in the clinch; partner ~50 cm ahead")


def clip_throw():
    """
    Hip throw from the clinch: step in and turn the back into the partner, load them on the hip, heave them over
    and down in front, end looking down at them.
    """
    n = 48
    keys = {
        "foot_r": [(0, STANCE_FEET["r"]), (6, (-6, -8, ANKLE + 6)), (10, (-10, -34, ANKLE)), (40, (-10, -34, ANKLE)),
                   (48, (-12, -30, ANKLE))],
        "foot_rot_r": [(0, (0, 0, STANCE_YAW["r"])), (10, (0, 0, 150)), (40, (0, 0, 150)), (48, (0, 0, 120))],
        "foot_l": [(0, (10, -28, ANKLE)), (12, (10, -28, ANKLE)), (17, (22, -40, ANKLE + 5)), (22, (26, -50, ANKLE)),
                   (48, (26, -50, ANKLE))],
        "foot_rot_l": [(0, (0, 0, STANCE_YAW["l"])), (12, (0, 0, 60)), (22, (0, 0, 150)), (48, (0, 0, 140))],
        "knee_l": (0.6, -0.3, 0), "knee_r": (-0.6, 0.3, 0),
        "pelvis": [(0, (0, -4, -9)), (10, (-2, -24, -14)), (18, (4, -38, -24)), (26, (4, -40, -18)), (36, (6, -40, -14)),
                   (48, (6, -38, -10))],
        "hips": [(0, (8, 0, -10)), (10, (10, 0, 90)), (18, (20, 0, 170)), (26, (30, 10, 180)), (34, (18, 6, 176)),
                 (48, (12, 0, 170))],
        "spine": [(0, (10, 0, 2)), (10, (16, 0, 10)), (18, (30, 0, 6)), (24, (54, 16, -16)), (30, (40, 10, -20)),
                  (48, (20, 0, 0))],
        "head": [(0, (6, 0, 12)), (12, (6, 0, -30)), (22, (20, 0, -30)), (30, (30, 0, -10)), (48, (24, 0, 0))],
        "hand_l": [(0, (10, -38, 146)), (10, (30, -30, 140)), (18, (34, -20, 136)), (26, (30, 10, 104)),
                   (34, (20, 14, 96)), (48, (16, 6, 100))],
        "hand_r": [(0, (-10, -38, 146)), (10, (-4, -30, 140)), (18, (12, -30, 138)), (26, (4, -2, 104)),
                   (34, (-2, 4, 100)), (48, (-10, 0, 102))],
        "face_l": ((0.3, -0.4, -1), (-0.5, -0.8, -0.3)), "face_r": ((0.5, -0.4, -1), (0.6, -0.8, 0)),
        "fingers_l": [(0, 0.8), (30, 0.85), (36, 0.3), (48, 0.3)],
        "fingers_r": [(0, 0.8), (30, 0.85), (36, 0.3), (48, 0.3)],
        "elbow_l": (1, -0.2, -0.3), "elbow_r": (-0.3, -1, -0.3),
    }
    return fight_clip("Throw", n, keys, hit=(24, "pelvis"),
                      note="starts in the clinch (Fight_Grab); ends turned round with the partner thrown down in front")


def react(name, n, lean, shift, arms, heavy=False, steps=None):
    """
    A hit reaction: the body whips away from the hit (lean: (pitch, roll, yaw) at the peak), the pelvis shifts,
    arms fly out (arms: extra body-space hand offsets), then it settles back into the guard.
    """
    pk = 5 if not heavy else 6
    keys = {
        "spine": [(0, (8, 0, -6)), (pk, fp._add((8, 0, -6), lean)), (pk + 8, fp._add((8, 0, -6), fp._scale(lean, 0.35))),
                  (n, (8, 0, -6))],
        "head": [(0, (4, 0, 30)), (pk, fp._add((4, 0, 30), fp._scale(lean, 1.3))),
                 (pk + 9, fp._add((4, 0, 30), fp._scale(lean, 0.3))), (n, (4, 0, 30))],
        "pelvis": [(0, (0, 1, -8)), (pk + 1, fp._add((0, 1, -8), shift)), (pk + 10, fp._add((0, 1, -8), fp._scale(shift, 0.5))),
                   (n, (0, 1, -8))],
        "hand_l": [(0, G("l")), (pk + 1, fp._add(G("l"), arms["l"])), (pk + 9, fp._add(G("l"), fp._scale(arms["l"], 0.3))),
                   (n, G("l"))],
        "hand_r": [(0, G("r")), (pk + 1, fp._add(G("r"), arms["r"])), (pk + 9, fp._add(G("r"), fp._scale(arms["r"], 0.3))),
                   (n, G("r"))],
        "fingers_l": [(0, 0.95), (pk, 0.3), (pk + 10, 0.9), (n, 0.95)],
        "fingers_r": [(0, 0.95), (pk, 0.3), (pk + 10, 0.9), (n, 0.95)],
    }
    if steps:
        keys.update(steps)
    return fight_clip(name, n, keys, lag={"head": 1, "spine": 0, "hand_l": 2, "hand_r": 2})


def clip_hitreacts():
    out = [
        react("HitReact_Light_Front", 22, (-18, 0, 6), (0, 6, 1), {"l": (4, 8, 6), "r": (-4, 8, 8)}),
        react("HitReact_Light_Back", 22, (20, 0, -4), (0, -6, -1), {"l": (6, -6, -4), "r": (-6, -6, -4)}),
        react("HitReact_Light_Left", 22, (2, -18, -22), (-6, 0, 0), {"l": (-4, 4, 4), "r": (-10, 4, -4)}),
        react("HitReact_Light_Right", 22, (2, 18, 22), (6, 0, 0), {"l": (10, 4, -4), "r": (4, 4, 4)}),
    ]
    n = 50
    steps = {
        "foot_l": [(0, STANCE_FEET["l"]), (8, STANCE_FEET["l"]), (13, (12, 4, ANKLE + 8)), (17, (13, 14, ANKLE)),
                   (30, (13, 14, ANKLE)), (34, (12, 34, ANKLE + 6)), (38, (11, 42, ANKLE)), (50, (11, 42, ANKLE))],
        "foot_r": [(0, STANCE_FEET["r"]), (4, STANCE_FEET["r"]), (9, (-17, 34, ANKLE + 8)), (13, (-17, 44, ANKLE)),
                   (22, (-17, 44, ANKLE)), (27, (-16, 64, ANKLE + 6)), (31, (-15, 72, ANKLE)), (50, (-15, 72, ANKLE))],
        "pelvis": [(0, (0, 1, -8)), (5, (0, 14, -6)), (12, (0, 24, -10)), (20, (-2, 30, -12)), (28, (1, 42, -10)),
                   (36, (0, 52, -11)), (44, (0, 56, -9)), (50, (0, 57, -8))],
        "hips": [(0, (6, 0, -26)), (6, (-14, 0, -20)), (16, (-6, 4, -26)), (28, (-4, -3, -24)), (50, (6, 0, -26))],
        "spine": [(0, (8, 0, -6)), (6, (-24, 4, 10)), (14, (-14, -6, 0)), (24, (-6, 4, -4)), (36, (4, -2, -6)),
                  (50, (8, 0, -6))],
        "head": [(0, (4, 0, 30)), (6, (-34, 6, 40)), (14, (-12, -8, 26)), (26, (-2, 4, 30)), (50, (4, 0, 30))],
        "hand_l": [(0, G("l")), (6, (24, -6, 160)), (14, (30, 6, 130)), (26, (20, -14, 138)), (40, G("l"))],
        "hand_r": [(0, G("r")), (6, (-24, 0, 162)), (14, (-32, 8, 128)), (26, (-20, -8, 138)), (40, G("r"))],
        "fingers_l": [(0, 0.95), (6, 0.2), (26, 0.4), (40, 0.95)],
        "fingers_r": [(0, 0.95), (6, 0.2), (26, 0.4), (40, 0.95)],
        "foot_rot_l": [(0, (0, 0, STANCE_YAW["l"])), (13, (10, 0, -16)), (17, (0, 0, -16)), (50, (0, 0, STANCE_YAW["l"]))],
    }
    ch = {k: O(*v) for k, v in steps.items()}

    def run(f, c):
        stance(c, f)
    out.append(Clip("HitReact_Heavy", n, False, ch, fn=run, lag={"head": 1}, travel=(0, 56.0),
                    note="staggers ~56 cm back (the root stays put)"))
    return out


def clip_knockback():
    """Blasted off the feet: thrown back, airborne, lands on the back ~110 cm behind (ends lying face up)."""
    n = 40
    ch = {
        "pelvis": O((0, (0, 1, -8)), (4, (0, 12, -2)), (10, (0, 40, 6)), (18, (0, 80, -30)), (24, (0, 100, -72)),
                    (28, (0, 108, -80)), (32, (0, 110, -82)), (40, (0, 110, -83))),
        "hips": O((0, (6, 0, -26)), (4, (-20, 0, -20)), (10, (-50, 0, -10)), (18, (-80, 0, 0)), (24, (-92, 0, 0)),
                  (28, (-86, 0, 0)), (32, (-90, 0, 0)), (40, (-90, 0, 0))),
        "spine": O((0, (8, 0, -6)), (4, (-20, 0, 0)), (10, (-10, 0, 0)), (18, (6, 0, 0)), (24, (-6, 0, 0)), (28, (10, 0, 0)),
                   (34, (0, 0, 0)), (40, (0, 0, 0))),
        "head": O((0, (4, 0, 30)), (4, (-40, 0, 10)), (12, (10, 0, 0)), (22, (30, 0, 0)), (26, (-10, 0, 0)), (32, (8, 0, 14)),
                  (40, (4, 0, 18))),
        "legfk_l": O((0, (0, 0, 20, 0)), (6, (-30, 0, 30, 20)), (14, (-70, 5, 60, 30)), (22, (-60, 5, 30, 30)),
                     (28, (-20, 8, 10, 20)), (40, (-12, 10, 14, 20))),
        "legfk_r": O((0, (0, 0, 20, 0)), (6, (-10, 0, 20, 20)), (14, (-50, 5, 70, 30)), (22, (-80, 5, 50, 30)),
                     (28, (-30, 8, 25, 20)), (40, (-20, 12, 30, 20))),
        "armfk_l": O((0, (-60, 10, 110, 0)), (5, (-100, 40, 30, 0)), (14, (-140, 60, 20, 0)), (24, (-60, 70, 30, 0)),
                     (30, (-10, 60, 20, 0)), (40, (-20, 50, 30, 0))),
        "armfk_r": O((0, (-60, 10, 110, 0)), (5, (-90, 30, 40, 0)), (14, (-120, 70, 30, 0)), (24, (-40, 80, 30, 0)),
                     (30, (-5, 70, 20, 0)), (40, (-15, 55, 40, 0))),
        "fingers_l": O((0, 0.95), (5, 0.2), (30, 0.2), (40, 0.4)),
        "fingers_r": O((0, 0.95), (5, 0.2), (30, 0.2), (40, 0.4)),
        "spread_l": 0.5, "spread_r": 0.5,
    }
    return Clip("Knockback", n, False, ch, lag={"head": 2, "armfk_l": 1, "armfk_r": 1}, travel=(0, 110.0),
                note="ends lying on the back 110 cm behind the root; GetUp_Back starts there")


def clip_getup_front():
    """From lying face down (head towards -Y, pelvis over the root) up to the guard."""
    n = 54
    ch = {
        "pelvis": O((0, (0, 2, -82)), (10, (0, 0, -78)), (18, (0, 6, -58)), (26, (0, 2, -44)), (34, (0, -4, -38)),
                    (44, (0, 0, -14)), (54, (0, 1, -8))),
        "hips": O((0, (88, 0, 0)), (10, (80, 0, 0)), (18, (60, 0, 0)), (26, (40, 0, 0)), (34, (30, 0, -10)),
                  (44, (14, 0, -20)), (54, (6, 0, -26))),
        "spine": O((0, (0, 0, 0)), (10, (-20, 0, 0)), (18, (0, 0, 0)), (26, (14, 0, 0)), (34, (10, 0, -4)), (44, (8, 0, -6)),
                   (54, (8, 0, -6))),
        "head": O((0, (-50, 0, 20)), (10, (-40, 0, 0)), (20, (-20, 0, 0)), (34, (0, 0, 20)), (54, (4, 0, 30))),
        "hand_space_l": "world", "hand_space_r": "world",
        "hand_l": O((0, (26, -44, 12)), (8, (22, -40, 9)), (26, (22, -40, 9)), (32, (20, -32, 40)), (44, (14, -22, 120)),
                    (54, (9, -29, 138))),
        "hand_r": O((0, (-26, -44, 12)), (8, (-22, -40, 9)), (26, (-22, -40, 9)), (32, (-20, -32, 40)),
                    (44, (-12, -14, 122)), (54, (-9, -19, 142))),
        "face_l": O((0, ((0, -1, 0), (0, 0, -1))), (26, ((0, -1, 0), (0, 0, -1))), (40, fist_guard(1)), (54, fist_guard(1))),
        "face_r": O((0, ((0, -1, 0), (0, 0, -1))), (26, ((0, -1, 0), (0, 0, -1))), (40, fist_guard(-1)),
                    (54, fist_guard(-1))),
        "fingers_l": O((0, 0.1), (30, 0.1), (46, 0.95)), "fingers_r": O((0, 0.1), (30, 0.1), (46, 0.95)),
        "elbow_l": (1, 0.3, 0.2), "elbow_r": (-1, 0.3, 0.2),
        "foot_l": O((0, (13, 60, 4)), (10, (13, 56, 4)), (18, (12, 30, 10)), (26, (11, 4, ANKLE)), (54, STANCE_FEET["l"])),
        "foot_rot_l": O((0, (160, 0, 0)), (10, (130, 0, 0)), (18, (60, 0, 0)), (26, (0, 0, 0)), (54, (0, 0, STANCE_YAW["l"]))),
        "foot_r": O((0, (-13, 60, 4)), (20, (-13, 54, 4)), (28, (-14, 36, 12)), (34, (-15, 16, ANKLE)),
                    (54, STANCE_FEET["r"])),
        "foot_rot_r": O((0, (160, 0, 0)), (20, (140, 0, 0)), (28, (40, 0, -10)), (34, (0, 0, -30)),
                        (54, (0, 0, STANCE_YAW["r"]))),
        "knee_l": O((0, (0.2, 0, -1)), (18, (0.2, -1, -0.3)), (30, (0.4, -1, 0))),
        "knee_r": O((0, (-0.2, 0, -1)), (26, (-0.2, -1, -0.3)), (36, (-0.4, -1, 0))),
    }
    return Clip("GetUp_Front", n, False, ch, lag={"head": 2})


def clip_getup_back():
    """From lying face up (head towards +Y, pelvis over the root): sit up, roll onto a knee, stand into the guard."""
    n = 60
    ch = {
        "pelvis": O((0, (0, 0, -83)), (10, (0, -2, -82)), (20, (0, -4, -78)), (30, (-4, -8, -60)), (38, (-2, -6, -44)),
                    (48, (0, 0, -18)), (60, (0, 1, -8))),
        "hips": O((0, (-90, 0, 0)), (10, (-70, 0, 0)), (20, (-10, 0, 0)), (30, (20, 10, -30)), (38, (24, 0, -20)),
                  (48, (12, 0, -24)), (60, (6, 0, -26))),
        "spine": O((0, (0, 0, 0)), (10, (30, 0, 0)), (20, (30, 0, 10)), (30, (30, 0, 0)), (40, (16, 0, -6)),
                   (60, (8, 0, -6))),
        "head": O((0, (6, 0, 16)), (10, (30, 0, 0)), (20, (10, 0, 10)), (34, (0, 0, 20)), (60, (4, 0, 30))),
        "hand_space_l": "world", "hand_space_r": "world",
        "hand_l": O((0, (40, 20, 10)), (10, (30, 10, 9)), (22, (26, 14, 9)), (30, (14, -20, 40)), (44, (14, -24, 116)),
                    (60, (9, -29, 138))),
        "hand_r": O((0, (-40, 20, 10)), (10, (-30, 12, 9)), (22, (-28, 10, 9)), (28, (-26, 4, 12)), (36, (-20, -10, 60)),
                    (48, (-12, -16, 124)), (60, (-9, -19, 142))),
        "face_l": O((0, ((0.3, 1, 0), (0, 0, -1))), (22, ((0.3, 1, 0), (0, 0, -1))), (40, fist_guard(1)), (60, fist_guard(1))),
        "face_r": O((0, ((-0.3, 1, 0), (0, 0, -1))), (28, ((-0.3, 1, 0), (0, 0, -1))), (44, fist_guard(-1)),
                    (60, fist_guard(-1))),
        "fingers_l": O((0, 0.2), (30, 0.1), (46, 0.95)), "fingers_r": O((0, 0.2), (34, 0.1), (50, 0.95)),
        "elbow_l": (1, 0.3, 0.2), "elbow_r": (-1, 0.3, 0.2),
        "foot_l": O((0, (14, -80, 6)), (12, (14, -70, 6)), (22, (14, -30, ANKLE)), (54, (12, -22, ANKLE)), (60, STANCE_FEET["l"])),
        "foot_rot_l": O((0, (-60, 0, 10)), (12, (-40, 0, 0)), (22, (0, 0, 0)), (60, (0, 0, STANCE_YAW["l"]))),
        "foot_r": O((0, (-14, -80, 6)), (14, (-14, -72, 6)), (24, (-20, -30, 8)), (30, (-16, 12, 6)), (38, (-15, 16, ANKLE)),
                    (60, STANCE_FEET["r"])),
        "foot_rot_r": O((0, (-60, 0, -10)), (14, (-40, 0, 0)), (24, (30, 0, -20)), (30, (150, 0, -20)), (38, (0, 0, -40)),
                        (60, (0, 0, STANCE_YAW["r"]))),
        "knee_l": O((0, (0.2, 0, 1)), (16, (0.3, -0.6, 1)), (30, (0.4, -1, 0.2))),
        "knee_r": O((0, (-0.2, 0, 1)), (16, (-0.3, -0.6, 1)), (26, (-0.2, -1, -0.8)), (40, (-0.4, -1, 0))),
    }
    return Clip("GetUp_Back", n, False, ch, lag={"head": 2})


def clip_taunt():
    """'Come on then': chest out, both hands beckon twice, a little head bob."""
    n = 60
    ch = {
        "hand_l": L(n, (0, (16, -34, 132)), (30, (17, -35, 133))),
        "hand_r": L(n, (0, (-16, -34, 132)), (30, (-17, -35, 133))),
        "face_l": ((-0.3, -0.7, 0.6), (-0.1, 0.6, 0.8)), "face_r": ((0.3, -0.7, 0.6), (0.1, 0.6, 0.8)),
        "elbow_l": (1, 0.3, -0.8), "elbow_r": (-1, 0.3, -0.8),
        "fingers_l": L(n, (0, 0.1), (8, 0.85), (16, 0.1), (24, 0.85), (32, 0.1), (60, 0.1)),
        "fingers_r": L(n, (0, 0.1), (9, 0.85), (17, 0.1), (25, 0.85), (33, 0.1), (60, 0.1)),
        "spine": L(n, (0, (-4, 0, -2)), (12, (-8, 0, 0)), (30, (-2, 0, -2)), (45, (-6, 0, 0))),
        "head": L(n, (0, (-6, 0, 10)), (10, (-14, 0, 10)), (20, (-4, 0, 10)), (40, (-10, 3, 14))),
        "hips": (2, 0, -12),
        "pelvis": L(n, (0, (0, 2, -5)), (15, (0, 1, -3)), (30, (0, 2, -5)), (45, (0, 1, -4))),
        "clav_l": L(n, (0, (2, -4)), (30, (6, -6))), "clav_r": L(n, (0, (2, -4)), (30, (6, -6))),
    }

    def fn(f, c):
        for s, sx in SIDES:
            c.setdefault(f"foot_{s}", STANCE_FEET[s])
            c.setdefault(f"foot_rot_{s}", (0, 0, STANCE_YAW[s]))
            c.setdefault(f"knee_{s}", (0.45 * sx, -1.0, 0.0))
    return Clip("Taunt", n, True, ch, fn=fn)


def clip_fight_step(name, direction, lead):
    """A boxer's shuffle in `direction` at 1 m/s: the leading foot steps, the other follows (in place)."""
    n = 24
    travel = 100.0 * n / FPS

    def fn(f, c):
        ph = f / n
        d = Vector(direction)
        c["pelvis"] = (0.8 * d.x * math.sin(2 * math.pi * ph), 1.0 + 0.8 * d.y * math.sin(2 * math.pi * ph),
                       -8.0 + 1.5 * abs(math.sin(2 * math.pi * ph)))
        offs = {"l": 0.0, "r": 0.5} if lead == "l" else {"l": 0.5, "r": 0.0}
        for s, sx in SIDES:
            pos, pitch, toe = gait(ph + offs[s], travel, 0.3, 5.0, d, STANCE_FEET[s], roll=False)
            c[f"foot_{s}"] = pos
            c[f"foot_rot_{s}"] = (0, 0, STANCE_YAW[s])
            b = math.sin(2 * math.pi * (ph + offs[s]))
            gx, gy, gz = GUARD[s]
            c[f"hand_{s}"] = (gx, gy, gz + 1.2 * b)
        stance(c, f)
    return Clip(name, n, True, {}, fn=fn, travel=tuple(Vector(direction) * travel), note="1 m/s")


def clip_library():
    return [
        clip_sit(), clip_drive(), clip_ride(), clip_talk(), clip_work(), clip_hands_up(), clip_kneel(), clip_cuffed(),
        clip_hands_behind(), clip_cower(), clip_dance(), clip_dance2(), clip_slump(), clip_dazed(), clip_sit_cuffed(),
        clip_sit_hands_up(), clip_struggle(), clip_idle_bored(), clip_phone(), clip_wave(), clip_point(),
        clip_clipboard(), clip_search(), clip_searched_pose(), clip_spray(), clip_smash(), clip_grab(), clip_sneak(),
        clip_cuffing(), clip_tackle(), clip_interact(), clip_cheer(),
        clip_jab(), clip_cross(), clip_hook(), clip_uppercut(), clip_kick_front(), clip_kick_side(),
        clip_kick_roundhouse(), clip_shove(), clip_grab_clinch(), clip_throw(), clip_block(),
        *clip_hitreacts(), clip_knockback(), clip_getup_front(), clip_getup_back(), clip_taunt(), clip_fight_idle(),
        clip_fight_step("Fight_Step_Fwd", (0, -1, 0), "l"), clip_fight_step("Fight_Step_Back", (0, 1, 0), "r"),
        clip_fight_step("Fight_Step_Left", (1, 0, 0), "l"), clip_fight_step("Fight_Step_Right", (-1, 0, 0), "r"),
    ]


FIGHT_CLIPS = {"Jab", "Cross", "Hook", "Uppercut", "Kick_Front", "Kick_Side", "Kick_Roundhouse", "Shove", "Fight_Grab",
               "Throw", "Block_Loop", "HitReact_Light_Front", "HitReact_Light_Back", "HitReact_Light_Left",
               "HitReact_Light_Right", "HitReact_Heavy", "Knockback", "GetUp_Front", "GetUp_Back", "Taunt",
               "Fight_Idle", "Fight_Step_Fwd", "Fight_Step_Back", "Fight_Step_Left", "Fight_Step_Right"}
