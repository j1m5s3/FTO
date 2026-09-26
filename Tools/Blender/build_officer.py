"""
Builds the FTO officer: a chunky, big-headed cartoon cop with a simple rig and looping clips.

  blender -b --factory-startup -P Tools/Blender/build_officer.py -- --out Art/Source/Characters/Officer [--preview <dir>]

Outputs (in --out):
  SK_Officer.fbx            skeletal mesh + skeleton, rest pose
  A_Officer_<Clip>.fbx      one animation per file (Idle, Walk, Run, Jump, Interact, Cheer)
  Officer.blend             editable source with every clip kept as an action
The uniform (shirt + sleeves) has vertex alpha 1 so each player's badge colour tints it in game.
"""
import math
import os
import sys

sys.path.append(os.path.dirname(os.path.abspath(__file__)))
import fto_blender as fb  # noqa: E402

# ---- palette (sRGB) ----
SHIRT = (0.82, 0.86, 0.95)   # tinted in game
NAVY = (0.07, 0.09, 0.22)
BLACK = (0.03, 0.03, 0.04)
GOLD = (1.0, 0.78, 0.18)
SKIN = (0.96, 0.76, 0.62)
SKIN_DARK = (0.90, 0.62, 0.48)
BROWN = (0.33, 0.18, 0.09)
RADIO = (0.12, 0.12, 0.13)

# name, head, tail, parent  (metres; character faces -Y, left is +X)
BONES = [
    ("root",       (0, 0, 0.0),        (0, 0, 0.25),       None),
    ("pelvis",     (0, 0, 0.72),       (0, 0, 0.86),       "root"),
    ("spine",      (0, 0, 0.86),       (0, 0, 1.30),       "pelvis"),
    ("head",       (0, 0, 1.30),       (0, 0, 1.80),       "spine"),
    ("upperarm_l", (0.33, 0, 1.28),    (0.37, 0, 1.02),    "spine"),
    ("lowerarm_l", (0.37, 0, 1.02),    (0.39, 0, 0.80),    "upperarm_l"),
    ("hand_l",     (0.39, 0, 0.80),    (0.40, 0, 0.68),    "lowerarm_l"),
    ("upperarm_r", (-0.33, 0, 1.28),   (-0.37, 0, 1.02),   "spine"),
    ("lowerarm_r", (-0.37, 0, 1.02),   (-0.39, 0, 0.80),   "upperarm_r"),
    ("hand_r",     (-0.39, 0, 0.80),   (-0.40, 0, 0.68),   "lowerarm_r"),
    ("thigh_l",    (0.13, 0, 0.72),    (0.13, 0, 0.42),    "pelvis"),
    ("calf_l",     (0.13, 0, 0.42),    (0.13, 0, 0.10),    "thigh_l"),
    ("foot_l",     (0.13, 0, 0.10),    (0.13, -0.16, 0.04), "calf_l"),
    ("thigh_r",    (-0.13, 0, 0.72),   (-0.13, 0, 0.42),   "pelvis"),
    ("calf_r",     (-0.13, 0, 0.42),   (-0.13, 0, 0.10),   "thigh_r"),
    ("foot_r",     (-0.13, 0, 0.10),   (-0.13, -0.16, 0.04), "calf_r"),
]
BONE_NAMES = [b[0] for b in BONES]


def officer_parts():
    P = fb.make_part
    parts = [
        # Torso bean, belt, badge, radio
        P('sphere', SHIRT, loc=(0, 0, 1.04), scale=(0.60, 0.50, 0.62), bone="spine", tint=True, segments=20, rings=12),
        P('sphere', NAVY, loc=(0, 0, 0.74), scale=(0.46, 0.36, 0.30), bone="pelvis"),
        P('cylinder', BLACK, loc=(0, 0, 0.80), scale=(0.56, 0.46, 0.07), bone="pelvis", segments=20),
        P('cube', GOLD, loc=(0, -0.235, 0.80), scale=(0.08, 0.02, 0.06), bone="pelvis", smooth=False),
        P('cylinder', GOLD, loc=(0.13, -0.245, 1.16), rot=(90, 0, 0), scale=(0.08, 0.08, 0.02), bone="spine"),
        P('cube', RADIO, loc=(-0.2, -0.19, 1.26), rot=(0, 0, -15), scale=(0.07, 0.04, 0.11), bone="spine", bevel=0.01),

        # Head: face, nose, eyes, the mandatory moustache
        P('sphere', SKIN, loc=(0, 0, 1.55), scale=(0.50, 0.46, 0.48), bone="head", segments=20, rings=12),
        P('sphere', SKIN_DARK, loc=(0, -0.235, 1.53), scale=(0.09, 0.09, 0.09), bone="head"),
        P('sphere', BLACK, loc=(0.085, -0.2, 1.60), scale=(0.055, 0.03, 0.07), bone="head"),
        P('sphere', BLACK, loc=(-0.085, -0.2, 1.60), scale=(0.055, 0.03, 0.07), bone="head"),
        P('cube', BROWN, loc=(0, -0.225, 1.48), scale=(0.17, 0.05, 0.045), bone="head", bevel=0.015),

        # Police cap
        P('cylinder', NAVY, loc=(0, 0, 1.77), scale=(0.50, 0.47, 0.12), bone="head", segments=20),
        P('cylinder', NAVY, loc=(0, 0.01, 1.85), scale=(0.56, 0.52, 0.06), bone="head", segments=20),
        P('cube', BLACK, loc=(0, -0.24, 1.72), rot=(-12, 0, 0), scale=(0.34, 0.16, 0.025), bone="head", bevel=0.01),
        P('cylinder', GOLD, loc=(0, -0.245, 1.79), rot=(90, 0, 0), scale=(0.07, 0.07, 0.02), bone="head"),
    ]

    for side, sx in (("l", 1.0), ("r", -1.0)):
        parts += [
            # Arms: sleeve + shoulder in the uniform colour, bare forearm, mitten hand
            P('sphere', SHIRT, loc=(0.33 * sx, 0, 1.26), scale=(0.18, 0.18, 0.18), bone=f"upperarm_{side}", tint=True),
            P('cylinder', SHIRT, loc=(0.355 * sx, 0, 1.14), scale=(0.15, 0.15, 0.26), bone=f"upperarm_{side}", tint=True),
            P('cylinder', SKIN, loc=(0.38 * sx, 0, 0.91), scale=(0.12, 0.12, 0.24), bone=f"lowerarm_{side}"),
            P('sphere', SKIN, loc=(0.395 * sx, 0, 0.76), scale=(0.14, 0.13, 0.15), bone=f"hand_{side}"),
            # Legs: navy trousers, shiny black shoes
            P('cylinder', NAVY, loc=(0.13 * sx, 0, 0.56), scale=(0.20, 0.20, 0.32), bone=f"thigh_{side}"),
            P('cylinder', NAVY, loc=(0.13 * sx, 0, 0.26), scale=(0.17, 0.17, 0.34), bone=f"calf_{side}"),
            P('cube', BLACK, loc=(0.13 * sx, -0.04, 0.055), scale=(0.17, 0.30, 0.11), bone=f"foot_{side}", bevel=0.035),
        ]
    return parts


# ---- clips: pose_at(t) with t in [0, 1]; angles in degrees ----
# Conventions (checked against Blender's roll-0 bone frames):
#   limbs hanging down: -X swings forward, +X swings back; calf +X bends the knee,
#   lowerarm -X bends the elbow; spine/head +X leans forward; spine Z rolls side to side.
def s(t, k=1.0):
    return math.sin(2.0 * math.pi * k * t)


def c(t, k=1.0):
    return math.cos(2.0 * math.pi * k * t)


def idle(t):
    b = s(t)
    return {
        "pelvis": {"loc": (0, 0.008 * b, 0)},
        "spine": {"rot": (2 + 1.5 * b, 0, 0)},
        "head": {"rot": (-2 * b, 8 * s(t), 0)},
        "upperarm_l": {"rot": (0, 0, -6 + 2 * b)},
        "upperarm_r": {"rot": (0, 0, 6 - 2 * b)},
        "lowerarm_l": {"rot": (-10, 0, 0)},
        "lowerarm_r": {"rot": (-10, 0, 0)},
    }


def locomotion(t, thigh, knee, arm, elbow, lean, bob, waddle):
    p = s(t)
    return {
        "pelvis": {"loc": (0, bob * abs(c(t)), 0)},
        "spine": {"rot": (lean, -6 * p, waddle * p)},
        "head": {"rot": (-lean * 0.6, 0, -waddle * 0.7 * p)},
        "thigh_l": {"rot": (-thigh * p, 0, 0)},
        "thigh_r": {"rot": (thigh * p, 0, 0)},
        "calf_l": {"rot": (6 + knee * max(0.0, c(t)), 0, 0)},
        "calf_r": {"rot": (6 + knee * max(0.0, -c(t)), 0, 0)},
        "foot_l": {"rot": (-8 * max(0.0, c(t)), 0, 0)},
        "foot_r": {"rot": (-8 * max(0.0, -c(t)), 0, 0)},
        "upperarm_l": {"rot": (arm * p, 0, -8)},
        "upperarm_r": {"rot": (-arm * p, 0, 8)},
        "lowerarm_l": {"rot": (elbow, 0, 0)},
        "lowerarm_r": {"rot": (elbow, 0, 0)},
    }


def walk(t):
    return locomotion(t, thigh=28, knee=38, arm=24, elbow=-25, lean=6, bob=0.025, waddle=4)


def run(t):
    return locomotion(t, thigh=50, knee=75, arm=48, elbow=-80, lean=15, bob=0.05, waddle=3)


def jump(t):
    w = s(t)
    return {
        "spine": {"rot": (-5, 0, 0)},
        "head": {"rot": (-8, 0, 0)},
        "thigh_l": {"rot": (-38 + 4 * w, 0, 0)},
        "thigh_r": {"rot": (-30 - 4 * w, 0, 0)},
        "calf_l": {"rot": (62, 0, 0)},
        "calf_r": {"rot": (55, 0, 0)},
        "upperarm_l": {"rot": (-120 + 10 * w, 0, -25)},
        "upperarm_r": {"rot": (-120 - 10 * w, 0, 25)},
        "lowerarm_l": {"rot": (-20, 0, 0)},
        "lowerarm_r": {"rot": (-20, 0, 0)},
    }


def interact(t):
    # Writing a ticket: notepad in the left hand, busy pen in the right.
    return {
        "spine": {"rot": (6, 0, 0)},
        "head": {"rot": (18 + 2 * s(t), -6, 0)},
        "upperarm_l": {"rot": (-45, 0, 10)},
        "lowerarm_l": {"rot": (-70, 0, 0)},
        "hand_l": {"rot": (0, 0, -20)},
        "upperarm_r": {"rot": (-40, 0, -8)},
        "lowerarm_r": {"rot": (-85 + 8 * s(t, 2), 0, 0)},
        "hand_r": {"rot": (0, 15 * s(t, 4), 0)},
    }


def cheer(t):
    hop = abs(s(t))
    wave = s(t, 2)
    return {
        "pelvis": {"loc": (0, 0.12 * hop, 0)},
        "spine": {"rot": (-4, 0, 5 * s(t))},
        "head": {"rot": (-10, 0, 0)},
        "thigh_l": {"rot": (-10 * hop, 0, 0)},
        "thigh_r": {"rot": (-10 * hop, 0, 0)},
        "calf_l": {"rot": (20 * hop, 0, 0)},
        "calf_r": {"rot": (20 * hop, 0, 0)},
        "upperarm_l": {"rot": (-160, 0, -(40 + 15 * wave))},
        "upperarm_r": {"rot": (-160, 0, 40 + 15 * wave)},
        "lowerarm_l": {"rot": (-10 * wave, 0, 0)},
        "lowerarm_r": {"rot": (10 * wave, 0, 0)},
    }


# ---- Phase 2 clips ----
# Pelvis "loc" is in the pelvis bone's space: y is up, z is forward.

def seated_legs(pelvis_drop=0.26, knee=90, feet=0):
    return {
        "pelvis": {"loc": (0, -pelvis_drop, -0.02)},
        "thigh_l": {"rot": (-90, 0, 0)},
        "thigh_r": {"rot": (-90, 0, 0)},
        "calf_l": {"rot": (knee, 0, 0)},
        "calf_r": {"rot": (knee, 0, 0)},
        "foot_l": {"rot": (feet, 0, 0)},
        "foot_r": {"rot": (feet, 0, 0)},
    }


def kneeling_legs():
    return {
        "pelvis": {"loc": (0, -0.32, 0.0)},
        "thigh_l": {"rot": (-10, 0, 0)},
        "thigh_r": {"rot": (-10, 0, 0)},
        "calf_l": {"rot": (100, 0, 0)},
        "calf_r": {"rot": (100, 0, 0)},
        "foot_l": {"rot": (55, 0, 0)},
        "foot_r": {"rot": (55, 0, 0)},
    }


def sit(t):
    b = s(t)
    pose = seated_legs()
    pose.update({
        "spine": {"rot": (-4 + 1.5 * b, 0, 0)},
        "head": {"rot": (-1.5 * b, 6 * s(t), 0)},
        "upperarm_l": {"rot": (-35, 0, 10)},
        "upperarm_r": {"rot": (-35, 0, -10)},
        "lowerarm_l": {"rot": (-45, 0, 0)},
        "lowerarm_r": {"rot": (-45, 0, 0)},
    })
    return pose


def drive(t):
    pose = seated_legs(knee=75)
    steer = 4 * s(t)
    pose.update({
        "spine": {"rot": (-6, 0, 0)},
        "head": {"rot": (0, 5 * s(t), 0)},
        "upperarm_l": {"rot": (-65 + steer, 0, 12)},
        "upperarm_r": {"rot": (-65 - steer, 0, -12)},
        "lowerarm_l": {"rot": (-35, 0, 0)},
        "lowerarm_r": {"rot": (-35, 0, 0)},
    })
    return pose


def talk(t):
    g = s(t, 2)
    return {
        "spine": {"rot": (2, 5 * s(t), 0)},
        "head": {"rot": (5 * s(t, 2), 8 * s(t), 0)},
        "upperarm_r": {"rot": (-45 + 20 * g, 0, 15)},
        "lowerarm_r": {"rot": (-70 + 25 * s(t, 2) * 0.8, 0, 0)},
        "upperarm_l": {"rot": (-15, 0, -10)},
        "lowerarm_l": {"rot": (-30, 0, 0)},
    }


def work(t):
    sweep = 8 * s(t, 2)
    return {
        "spine": {"rot": (12, 0, 0)},
        "head": {"rot": (18, 0, 0)},
        "upperarm_l": {"rot": (-35, 0, 15 + sweep)},
        "upperarm_r": {"rot": (-35, 0, -15 + sweep)},
        "lowerarm_l": {"rot": (-80, 0, 0)},
        "lowerarm_r": {"rot": (-80, 0, 0)},
    }


def hands_up(t):
    shake = 2 * s(t, 3)
    return {
        "spine": {"rot": (-3, 0, shake)},
        "head": {"rot": (-5, 0, 0)},
        "upperarm_l": {"rot": (-165, 0, -15 + shake)},
        "upperarm_r": {"rot": (-165, 0, 15 - shake)},
        "lowerarm_l": {"rot": (-20, 0, 0)},
        "lowerarm_r": {"rot": (-20, 0, 0)},
    }


def kneel(t):
    pose = kneeling_legs()
    pose.update({
        "spine": {"rot": (4 + s(t), 0, 0)},
        "head": {"rot": (8, 0, 0)},
        # Hands laced on the head.
        "upperarm_l": {"rot": (-150, 0, -55)},
        "upperarm_r": {"rot": (-150, 0, 55)},
        "lowerarm_l": {"rot": (-125, 0, 0)},
        "lowerarm_r": {"rot": (-125, 0, 0)},
    })
    return pose


def cuffed(t):
    pose = kneeling_legs()
    pose.update({
        "spine": {"rot": (8, 0, 2 * s(t))},
        "head": {"rot": (15, 6 * s(t), 0)},
        # Wrists together behind the back.
        "upperarm_l": {"rot": (40, 0, 18)},
        "upperarm_r": {"rot": (40, 0, -18)},
        "lowerarm_l": {"rot": (-60, 0, 0)},
        "lowerarm_r": {"rot": (-60, 0, 0)},
    })
    return pose


def cuffing(t):
    fiddle = s(t, 2)
    return {
        "pelvis": {"loc": (0, -0.12, 0)},
        "thigh_l": {"rot": (-35, 0, 0)},
        "thigh_r": {"rot": (-35, 0, 0)},
        "calf_l": {"rot": (55, 0, 0)},
        "calf_r": {"rot": (55, 0, 0)},
        "foot_l": {"rot": (-15, 0, 0)},
        "foot_r": {"rot": (-15, 0, 0)},
        "spine": {"rot": (35, 0, 0)},
        "head": {"rot": (20, 0, 0)},
        "upperarm_l": {"rot": (-55 + 5 * fiddle, 0, 10)},
        "upperarm_r": {"rot": (-55 - 5 * fiddle, 0, -10)},
        "lowerarm_l": {"rot": (-35 + 10 * fiddle, 0, 0)},
        "lowerarm_r": {"rot": (-35 - 10 * fiddle, 0, 0)},
    }


def struggle(t):
    w = s(t, 2)
    return {
        "spine": {"rot": (5, 20 * s(t), 18 * w)},
        "head": {"rot": (0, -10 * s(t), -12 * w)},
        "upperarm_l": {"rot": (-50 + 40 * w, 0, -20)},
        "upperarm_r": {"rot": (-50 - 40 * w, 0, 20)},
        "lowerarm_l": {"rot": (-60, 0, 0)},
        "lowerarm_r": {"rot": (-60, 0, 0)},
        "thigh_l": {"rot": (-15 * s(t), 0, 0)},
        "thigh_r": {"rot": (15 * s(t), 0, 0)},
        "calf_l": {"rot": (15 * max(0.0, s(t)), 0, 0)},
        "calf_r": {"rot": (15 * max(0.0, -s(t)), 0, 0)},
    }


def tackle(t):
    # Flying dive: the whole body tips forward over the feet, arms reaching.
    reach = 1.0  # held pose; the game blends into it as the officer launches
    return {
        "root": {"rot": (75 * reach, 0, 0)},
        "pelvis": {"loc": (0, 0.05, 0)},
        "spine": {"rot": (-10 * reach, 0, 0)},
        "head": {"rot": (-35 * reach, 0, 0)},
        "upperarm_l": {"rot": (-170 * reach, 0, -10)},
        "upperarm_r": {"rot": (-170 * reach, 0, 10)},
        "lowerarm_l": {"rot": (-10, 0, 0)},
        "lowerarm_r": {"rot": (-10, 0, 0)},
        "thigh_l": {"rot": (15, 0, 0)},
        "thigh_r": {"rot": (5, 0, 0)},
        "calf_l": {"rot": (30, 0, 0)},
        "calf_r": {"rot": (15, 0, 0)},
    }


def punch(t):
    # Boxing stance, jab right then left.
    def jab(phase_start):
        u = (t - phase_start) % 1.0
        return max(0.0, math.sin(math.pi * u / 0.25)) if u < 0.25 else 0.0
    right, left = jab(0.0), jab(0.5)
    return {
        "pelvis": {"loc": (0, 0.02 * s(t, 2), 0)},
        "spine": {"rot": (8, 15 * (right - left), 0)},
        "head": {"rot": (5, 0, 0)},
        "upperarm_r": {"rot": (-45 - 50 * right, 0, -15)},
        "lowerarm_r": {"rot": (-120 + 110 * right, 0, 0)},
        "upperarm_l": {"rot": (-45 - 50 * left, 0, 15)},
        "lowerarm_l": {"rot": (-120 + 110 * left, 0, 0)},
        "thigh_l": {"rot": (-15, 0, 0)},
        "thigh_r": {"rot": (10, 0, 0)},
        "calf_l": {"rot": (15, 0, 0)},
        "calf_r": {"rot": (15, 0, 0)},
    }


def cower(t):
    tremble = 2 * s(t, 4)
    return {
        "pelvis": {"loc": (0, -0.28, 0)},
        "thigh_l": {"rot": (-70, 0, 0)},
        "thigh_r": {"rot": (-70, 0, 0)},
        "calf_l": {"rot": (110, 0, 0)},
        "calf_r": {"rot": (110, 0, 0)},
        "spine": {"rot": (45, 0, tremble)},
        "head": {"rot": (25, 0, 0)},
        "upperarm_l": {"rot": (-150, 0, -35)},
        "upperarm_r": {"rot": (-150, 0, 35)},
        "lowerarm_l": {"rot": (-110, 0, 0)},
        "lowerarm_r": {"rot": (-110, 0, 0)},
    }


def aim_pistol(t):
    sway = 1.5 * s(t)
    return {
        "spine": {"rot": (0, 0, 0)},
        "head": {"rot": (0, 0, 0)},
        "upperarm_l": {"rot": (-88 + sway, 0, 28)},
        "upperarm_r": {"rot": (-90 + sway, 0, -22)},
        "lowerarm_l": {"rot": (-12, 0, 0)},
        "lowerarm_r": {"rot": (-5, 0, 0)},
    }


def aim_rifle(t):
    sway = 1.5 * s(t)
    return {
        "spine": {"rot": (0, 12, 0)},
        "head": {"rot": (4, -8, 0)},
        "upperarm_r": {"rot": (-70 + sway, 0, -20)},
        "lowerarm_r": {"rot": (-60, 0, 0)},
        "upperarm_l": {"rot": (-82 + sway, 0, 40)},
        "lowerarm_l": {"rot": (-25, 0, 0)},
    }


def dance(t):
    w = s(t)
    return {
        "pelvis": {"loc": (0, 0.04 * abs(w), 0), "rot": (0, 0, 10 * w)},
        "spine": {"rot": (0, 0, -8 * w)},
        "head": {"rot": (0, 15 * w, 0)},
        "upperarm_l": {"rot": (-120 + 50 * w, 0, -30)},
        "upperarm_r": {"rot": (-120 - 50 * w, 0, 30)},
        "lowerarm_l": {"rot": (-40, 0, 0)},
        "lowerarm_r": {"rot": (-40, 0, 0)},
        "thigh_l": {"rot": (-20 * max(0.0, w), 0, 0)},
        "thigh_r": {"rot": (-20 * max(0.0, -w), 0, 0)},
        "calf_l": {"rot": (40 * max(0.0, w), 0, 0)},
        "calf_r": {"rot": (40 * max(0.0, -w), 0, 0)},
    }


def slump(t):
    b = s(t)
    return {
        "pelvis": {"loc": (0, -0.02, 0)},
        "spine": {"rot": (25 + 2 * b, 0, 0)},
        "head": {"rot": (35, 0, 0)},
        "upperarm_l": {"rot": (-5, 0, -2)},
        "upperarm_r": {"rot": (-5, 0, 2)},
        "lowerarm_l": {"rot": (-5, 0, 0)},
        "lowerarm_r": {"rot": (-5, 0, 0)},
        "calf_l": {"rot": (8, 0, 0)},
        "calf_r": {"rot": (8, 0, 0)},
    }


def dazed(t):
    # Sat on the ground seeing stars.
    return {
        "pelvis": {"loc": (0, -0.55, 0.05)},
        "thigh_l": {"rot": (-85, 0, -8)},
        "thigh_r": {"rot": (-85, 0, 8)},
        "calf_l": {"rot": (10, 0, 0)},
        "calf_r": {"rot": (10, 0, 0)},
        "spine": {"rot": (-12, 0, 0)},
        "head": {"rot": (8 * s(t), 0, 8 * c(t))},
        "upperarm_l": {"rot": (30, 0, -25)},
        "upperarm_r": {"rot": (30, 0, 25)},
        "lowerarm_l": {"rot": (0, 0, 0)},
        "lowerarm_r": {"rot": (0, 0, 0)},
    }


# name, frames at 30 fps, pose function
CLIPS = [
    ("Idle", 60, idle),
    ("Walk", 18, walk),
    ("Run", 12, run),
    ("Jump", 20, jump),
    ("Interact", 36, interact),
    ("Cheer", 30, cheer),
    ("Sit", 60, sit),
    ("Drive", 60, drive),
    ("Talk", 60, talk),
    ("Work", 45, work),
    ("HandsUp", 30, hands_up),
    ("Kneel", 60, kneel),
    ("Cuffed", 45, cuffed),
    ("Cuffing", 30, cuffing),
    ("Struggle", 18, struggle),
    ("Tackle", 15, tackle),
    ("Punch", 24, punch),
    ("Cower", 30, cower),
    ("AimPistol", 60, aim_pistol),
    ("AimRifle", 60, aim_rifle),
    ("Dance", 30, dance),
    ("Slump", 60, slump),
    ("Dazed", 45, dazed),
]


def main():
    args = fb.script_args()
    out_dir = os.path.abspath(args.get("out", "Art/Source/Characters/Officer"))
    preview_dir = args.get("preview")

    fb.reset_scene(fps=30)
    arm = fb.build_armature(BONES)
    body = fb.build_mesh_object("SK_Officer", officer_parts(), BONE_NAMES)
    fb.bind(body, arm)

    fb.export_fbx(os.path.join(out_dir, "SK_Officer.fbx"), [arm, body], with_animation=False)

    actions = []
    for name, frames, pose_at in CLIPS:
        action = fb.bake_clip(arm, f"A_Officer_{name}", frames, pose_at)
        action.use_fake_user = True
        actions.append((name, frames, action))
        fb.export_fbx(os.path.join(out_dir, f"A_Officer_{name}.fbx"), [arm, body], with_animation=True)

    if preview_dir:
        preview_dir = os.path.abspath(preview_dir)
        cam = fb.setup_preview((420, 420))
        arm.animation_data.action = None
        fb.reset_pose(arm)
        fb.render_view(os.path.join(preview_dir, "officer_front.png"), cam, (0, -6, 1.0), (0, 0, 0.95), 2.3)
        fb.render_view(os.path.join(preview_dir, "officer_side.png"), cam, (6, 0, 1.0), (0, 0, 0.95), 2.3)
        only = set(args["clips"].split(",")) if isinstance(args.get("clips"), str) else None
        for name, frames, action in actions:
            if only and name not in only:
                continue
            arm.animation_data.action = action
            for i, frac in enumerate((0.0, 0.25, 0.5, 0.75)):
                frame = int(round(frames * frac))
                fb.render_view(os.path.join(preview_dir, f"clip_{name}_{i}.png"), cam, (6, -3.5, 1.3), (0, 0, 0.95), 2.6, frame)

    blend_path = os.path.join(out_dir, "Officer.blend")
    import bpy
    bpy.ops.wm.save_as_mainfile(filepath=blend_path)
    print(f"FTO: saved {blend_path}")


if __name__ == "__main__":
    main()
