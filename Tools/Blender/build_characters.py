"""
Builds FTO's cast on the UE5 mannequin skeleton (Tools/Blender/fto_rig.py), so every character plays Epic's engine
animations and our own clips (build_character_anims.py):

  blender -b --factory-startup -P Tools/Blender/build_characters.py -- --out Art/Source/Characters [--preview <dir>] [--only SK_Officer,SK_Suspect]

Outputs:
  <out>/Officer/SK_Officer.fbx, SK_Officer_F.fbx, Officers.blend
  <out>/Civilians/SK_Civilian_01..08.fbx, SK_Suspect.fbx, Civilians.blend
  <preview>/<name>.png   front / side / back turnaround of each character (with --preview)

How a character is made:
  * The body is one smooth, watertight surface: skin, clothes, hair and hats are signed distance fields (fto_sdf)
    stacked as layers, and the mesh is the surface of their union. Clothes sit a centimetre over the skin and end
    in a small hem step, so nothing can clip through when a joint bends. Each face takes the colour of the layer it
    lies on, and the mesh is cut exactly along where one layer meets the next, so hems and hairlines are crisp.
  * Hands are separate little meshes (fingers are too close together for the body's voxel size), weighted finger by
    finger; eyes, brows, mouth, badges, pouches and other small props are coloured parts placed on the surface.
  * Skinning: Blender's automatic weights from fto_rig's anatomical twin rig, then clean-up (left bones stay on the
    left, the head is rigid above the jaw, at most four influences). Props take the weights of the body under them.
  * Vertex colours in "Col", alpha 1 = tinted in game (uniform shirt, civilians' tops). Characters face -Y.
"""
import math
import os
import sys

import bpy  # noqa: I001 (first: outside Blender, the bpy module brings bmesh and mathutils)
import bmesh
import numpy as np
from mathutils import Matrix, Vector
from mathutils.bvhtree import BVHTree

sys.path.append(os.path.dirname(os.path.abspath(__file__)))
import fto_blender as fb  # noqa: E402
import fto_rig  # noqa: E402
import fto_sdf as S  # noqa: E402

VOXEL = 0.75          # cm; the body's meshing resolution before decimation
BODY_TRIS = 8400      # about the body's triangle count (hands and props come on top)
MAX_EDGE = 5.0        # cm: no longer edges on the body, so it bends smoothly
MAX_EDGE_HEAD = 3.0
HAND_TRIS = 760

J = {name: np.array(tuple(v)) for name, v in fto_rig.HEAD.items()}


def mirror(p):
    return np.array((-p[0], p[1], p[2]))


def side_pt(p, sx):
    return np.array((p[0] * sx, p[1], p[2]))


def unit(v):
    v = np.asarray(v, dtype=float)
    return v / np.linalg.norm(v)


# --------------------------------------------------------------------------------------
# Palette (sRGB)
# --------------------------------------------------------------------------------------
SKIN_TONES = [
    (0.96, 0.78, 0.64),
    (0.86, 0.63, 0.47),
    (0.66, 0.45, 0.30),
    (0.45, 0.29, 0.19),
    (0.99, 0.86, 0.76),
    (0.78, 0.55, 0.38),
]
HAIR = {
    "black": (0.05, 0.04, 0.04),
    "dark": (0.16, 0.10, 0.06),
    "brown": (0.34, 0.20, 0.10),
    "blonde": (0.93, 0.76, 0.40),
    "ginger": (0.78, 0.36, 0.12),
    "grey": (0.72, 0.72, 0.74),
    "pink": (0.98, 0.45, 0.72),
}
WHITE = (0.95, 0.95, 0.94)
OFFWHITE = (0.90, 0.89, 0.86)
BLACK = (0.03, 0.03, 0.04)
NEAR_BLACK = (0.07, 0.07, 0.08)
NAVY = (0.07, 0.09, 0.22)
TROUSER_NAVY = (0.11, 0.14, 0.29)
GOLD = (1.0, 0.78, 0.2)
SILVER = (0.75, 0.77, 0.80)
DENIM = (0.20, 0.30, 0.52)
DENIM_DARK = (0.12, 0.17, 0.32)
KHAKI = (0.74, 0.64, 0.44)
GREY = (0.45, 0.46, 0.50)
CHARCOAL = (0.20, 0.21, 0.24)
BROWN = (0.36, 0.21, 0.11)
TAN = (0.66, 0.46, 0.28)
RED = (0.72, 0.12, 0.12)
LIP = (0.62, 0.30, 0.28)
MOUTH = (0.30, 0.08, 0.08)
PUPIL = (0.08, 0.06, 0.05)
UNIFORM = (0.82, 0.86, 0.95)   # the officer's shirt, tinted by each player's colour
TOP = (0.85, 0.85, 0.85)       # civilians' tops, tinted per pedestrian


# --------------------------------------------------------------------------------------
# Anatomy: skin as groups of distance fields (cm, Blender space, facing -Y)
# --------------------------------------------------------------------------------------
DEFAULT_BUILD = dict(sh=1.0, chest=1.0, belly=0.0, hip=1.0, bust=0.0, arm=1.0, leg=1.0, neck=1.0, glute=1.0,
                     jaw=1.0, female=False)


class Anatomy:
    """The skin's parts, cached per batch of points so clothes can be built as offsets of the same shapes."""

    def __init__(self, build):
        b = dict(DEFAULT_BUILD, **build)
        self.b = b
        sh, belly, hip = b["sh"], b["belly"], b["hip"]
        fem = b["female"]
        C = S.cached

        pelvis = S.ellipsoid((0, -0.5, 95.0), (15.2 * hip, 10.8 + 0.8 * belly, 11.5))
        belly_e = S.ellipsoid((0, -2.5 - 3.8 * belly, 106.5 - 1.5 * belly),
                              ((13.2 if fem else 14.9) + 3.8 * belly, 9.6 + 4.5 * belly, 11.0 + 2.0 * belly))
        waist = S.ellipsoid((0, -1.0, 116.0), (14.6 * sh + 3.0 * belly, 9.6 + 2.0 * belly, 10.0))
        chest = S.ellipsoid((0, -1.2, 129.0), (15.6 * sh * b["chest"] + 1.2 * belly, 10.1 * b["chest"] + 1.0 * belly,
                                               14.0))
        yoke = S.capsule((-14.2 * sh, 0.8, 140.0), (14.2 * sh, 0.8, 140.0), 6.2 if not fem else 5.6)
        traps = [S.ellipsoid((sx * 7.0 * sh, 2.5, 146.0), (7.5 * sh, 5.2, 4.2 if not fem else 3.4)) for sx in (1, -1)]
        back = S.ellipsoid((0, 3.8, 125.0), (14.4 * sh, 7.8, 13.5))
        glutes = [S.ellipsoid((sx * 6.8, 4.0, 89.5), (8.0 * b["glute"] * hip, 7.0 * b["glute"], 9.0)) for sx in (1, -1)]
        parts = [pelvis, belly_e, waist, chest, yoke, back] + glutes + traps
        fronts = []
        if fem or b["bust"] > 0:
            bust = b["bust"] or 1.0
            fronts = [S.ellipsoid((sx * 7.6, -8.2, 127.5), (6.4, 4.8 * bust, 6.0)) for sx in (1, -1)]
        else:
            fronts = [S.ellipsoid((sx * 6.2, -6.4, 133.0), (6.4 * b["chest"], 2.5 * b["chest"], 4.6)) for sx in (1, -1)]
        self.torso = C(S.smooth_union(3.0, S.smooth_union(5.0, *parts), *fronts))
        self.hips = C(S.smooth_union(5.0, pelvis, *glutes))

        nr = 6.6 * b["neck"] if not fem else 6.0 * b["neck"]
        self.neck = C(S.round_cone((0, 1.9, 145.0), (0, 0.8, 163.0), nr, nr * 0.9))

        jaw = b["jaw"]
        cranium = S.ellipsoid((0, 0.9, 172.6), (10.4, 11.6, 11.6))
        face = S.ellipsoid((0, -4.0, 166.2), (9.0 * jaw, 8.0, 8.8))
        chin = S.ellipsoid((0, -8.0, 159.6), (4.2 * jaw, 3.2, 2.8))
        jaws = [S.ellipsoid((sx * 5.4 * jaw, -3.0, 161.8), (3.4, 4.8, 3.6)) for sx in (1, -1)]
        cheeks = [S.ellipsoid((sx * 4.9, -7.2, 165.6), (3.8, 3.2, 3.1)) for sx in (1, -1)]
        nose = S.smooth_union(0.8, S.round_cone((0, -11.6, 169.8), (0, -13.0, 167.2), 0.9, 1.3),
                              S.ellipsoid((0, -13.4, 166.9), (1.75, 1.35, 1.35)))
        ears = [S.ellipsoid((sx * 10.3, 1.4, 169.0), (1.7, 3.0, 3.9)) for sx in (1, -1)]
        # Head shapes are written at life size and drawn a touch bigger (head_scale about the chin), the
        # stylised look; hair, hats and face props go through the same mapping (headspace, hp).
        self.hs = b.get("head_scale", 1.08)
        self.pivot = np.array((0.0, -1.0, 163.0))
        self.drop = np.array((0.0, 0.0, -1.8))   # sits a little lower on the neck: a shorter, chunkier neck
        self.cranium0 = C(cranium)
        self.head0 = C(S.smooth_union(1.1, S.smooth_union(2.8, cranium, face, chin, *jaws, *cheeks), nose, *ears))
        self.jaw0 = C(S.smooth_union(2.5, face, chin, *jaws))
        self.cranium = C(self.headspace(self.cranium0))
        self.head = C(self.headspace(self.head0))

        self.upperarm, self.forearm, self.thigh, self.shin, self.foot = {}, {}, {}, {}, {}
        for side, sx in (("l", 1), ("r", -1)):
            shoulder, elbow, wrist = J[f"upperarm_{side}"], J[f"lowerarm_{side}"], J[f"hand_{side}"]
            a = b["arm"]
            delt = S.ellipsoid(side_pt((20.0, 1.6, 142.4), sx), (6.2 * a, 6.4 * a, 6.6 * a))
            upper = S.round_cone(shoulder + (elbow - shoulder) * 0.05, elbow, 5.4 * a, 4.4 * a)
            bicep = S.ellipsoid(shoulder + (elbow - shoulder) * 0.5 + np.array((0, -1.2, 0)), (4.4 * a, 4.6 * a, 6.5 * a))
            self.upperarm[side] = C(S.smooth_union(2.5, delt, upper, bicep))
            fore = S.round_cone(elbow, wrist - unit(wrist - elbow) * 0.5, 4.3 * a, 3.0 * a)
            fore_m = S.ellipsoid(elbow + (wrist - elbow) * 0.3, (4.3 * a, 4.5 * a, 6.2 * a))
            self.forearm[side] = C(S.smooth_union(2.0, fore, fore_m))

            hip_j, knee, ankle = J[f"thigh_{side}"], J[f"calf_{side}"], J[f"foot_{side}"]
            l = b["leg"]
            thigh = S.round_cone(hip_j + np.array((0, 0.8, 1.5)), knee, 8.6 * l * hip, 5.5 * l)
            quad = S.ellipsoid(hip_j + (knee - hip_j) * 0.45 + np.array((0, -1.6, 0)), (6.8 * l, 6.6 * l, 16.0))
            self.thigh[side] = C(S.smooth_union(3.0, thigh, quad))
            shin = S.round_cone(knee, ankle + np.array((0, -0.3, 1.0)), 5.3 * l, 3.5)
            calf_m = S.ellipsoid(side_pt((12.9, 2.2, 37.0), sx), (5.1 * l, 5.4 * l, 9.5))
            kneecap = S.ellipsoid(knee + np.array((0, -3.8, 0.5)), (3.2, 2.0, 3.0))
            self.shin[side] = C(S.smooth_union(2.0, shin, calf_m, kneecap))
            heel = S.ellipsoid(side_pt((14.2, 2.0, 5.0), sx), (4.0, 4.8, 5.0))
            mid = S.ellipsoid(side_pt((14.8, -5.0, 3.9), sx), (4.4, 7.0, 3.8))
            toes = S.ellipsoid(side_pt((15.5, -13.2, 3.0), sx), (4.8, 5.6, 3.0))
            self.foot[side] = C(S.intersect(S.smooth_union(2.0, heel, mid, toes), S.halfspace((0, 0, 1.2), (0, 0, -1))))

        # Straight trouser legs (filled out from the hip to the ankle), and the core a top's hem has to cover:
        # the torso plus the tops of the legs, which stick out past the hips.
        self.straight = {}
        for side in ("l", "r"):
            hip_j, ankle = J[f"thigh_{side}"], J[f"foot_{side}"]
            self.straight[side] = S.round_cone(hip_j + np.array((0, 0.6, 0)), ankle + np.array((0, -0.5, 1.0)),
                                               8.4 * b["leg"] * hip, 5.2 * b["leg"])
        upper = S.intersect(S.union(*self.thigh.values(), *self.straight.values()), above(76.0))
        self.core = C(S.smooth_union(3.0, self.torso, self.hips, upper))
        self.arms = C(S.union(*self.upperarm.values(), *self.forearm.values()))
        self.legs = C(S.union(*self.thigh.values(), *self.shin.values()))

    def headspace(self, f):
        hs, pivot, drop = self.hs, self.pivot, self.drop
        return lambda P: hs * f(pivot + (P - pivot - drop) / hs)

    def hp(self, x, y, z):
        """A point written in life-size head coordinates, where it ends up on the drawn head."""
        return tuple(self.pivot + self.drop + (np.array((x, y, z)) - self.pivot) * self.hs)

    def skin(self):
        up = S.union(*self.upperarm.values())
        return S.smooth_union(3.2, self.torso, self.neck, self.head,
                              S.smooth_union(2.0, up, *self.forearm.values()),
                              S.smooth_union(2.5, *self.thigh.values(), *self.shin.values(), *self.foot.values()))

    def arm_to(self, side, frac):
        """Point frac of the way down the upper arm (0) to the elbow (1) and beyond onto the forearm (1..2)."""
        s, e, w = J[f"upperarm_{side}"], J[f"lowerarm_{side}"], J[f"hand_{side}"]
        return s + (e - s) * frac if frac <= 1 else e + (w - e) * (frac - 1)

    def limb_cut(self, side, frac, keep_upper=True):
        """A plane across an arm at frac (see arm_to); keeps the shoulder side (or the hand side)."""
        s, e, w = J[f"upperarm_{side}"], J[f"lowerarm_{side}"], J[f"hand_{side}"]
        n = unit(e - s) if frac <= 1 else unit(w - e)
        return S.halfspace(self.arm_to(side, frac), n if keep_upper else -n)

    def leg_cut(self, z, keep_above=True):
        return S.halfspace((0, 0, z), (0, 0, -1) if keep_above else (0, 0, 1))


def zband(lo, hi):
    return S.intersect(S.halfspace((0, 0, lo), (0, 0, -1)), S.halfspace((0, 0, hi), (0, 0, 1)))


def above(z):
    return S.halfspace((0, 0, z), (0, 0, -1))


def below(z):
    return S.halfspace((0, 0, z), (0, 0, 1))


# --------------------------------------------------------------------------------------
# Clothing and hair: each returns layers (name, sdf, colour, tint, bias)
# --------------------------------------------------------------------------------------
class Layer:
    def __init__(self, name, sdf, color, tint=False, bias=0.0, weight=None):
        self.name, self.sdf, self.color, self.tint, self.bias = name, sdf, color, tint, bias
        self.weight = weight   # optional weight tweak for the verts on this layer (see skin_body)


def sleeve(an, side, frac, t, cuff=0.0):
    """Clothing over one arm down to frac (see Anatomy.arm_to), t cm thick, with an optional thicker cuff."""
    arm = S.union(an.upperarm[side], an.forearm[side]) if frac > 1 else an.upperarm[side]
    shell = S.intersect(S.offset(arm, t), an.limb_cut(side, frac))
    if cuff > 0:
        band = S.intersect(S.offset(arm, t + cuff), an.limb_cut(side, frac), an.limb_cut(side, frac - 0.12, False))
        shell = S.union(shell, band)
    return shell


def top_shell(an, t, bottom, sleeves, neck_top=None, extra=()):
    """A shirt/jumper: the torso t cm thick down to z=bottom, with sleeves to `sleeves` (0 = none)."""
    parts = [S.intersect(S.offset(an.core, t), above(bottom))]
    if sleeves > 0:
        parts += [sleeve(an, side, sleeves, t) for side in ("l", "r")]
    shell = S.smooth_union(3.0, *parts, *extra)
    if neck_top is not None:
        collar = S.intersect(S.offset(an.neck, t + 0.3), below(neck_top))
        shell = S.union(shell, collar)
    return shell


def neck_opening(shell, depth_z, width=5.5, back=146.5):
    """Scoops a round/V neckline out of a top: everything in front above depth_z and inside the neck ring."""
    scoop = S.ellipsoid((0, -8.0, 150.0), (width, 9.0, 150.0 - depth_z))
    ring = S.ellipsoid((0, 1.5, back + 4.0), (6.8, 6.8, 4.5))
    return S.subtract(shell, S.union(scoop, ring))


def trousers(an, t, hem_z, top_z=102.0, shorts=False, flare=1.0):
    """Trousers (or shorts) with straight legs: the legs t cm thick, filled out towards a hem flare x the ankle."""
    legs = [an.thigh[s] for s in ("l", "r")] + ([] if shorts else [an.shin[s] for s in ("l", "r")])
    straight = []
    for side in ("l", "r"):
        if shorts:
            hip_j, knee = J[f"thigh_{side}"], J[f"calf_{side}"]
            straight.append(S.round_cone(hip_j + np.array((0, 0.6, 0)), knee, 8.4 * an.b["leg"] * an.b["hip"],
                                         6.4 * an.b["leg"]))
        else:
            straight.append(an.straight[side])
    shell = S.smooth_union(3.0, S.intersect(S.offset(an.hips, t), below(top_z)),
                           S.intersect(S.offset(an.torso, t), below(top_z)),
                           S.smooth_union(4.0, *[S.offset(l, t) for l in legs], *straight))
    shell = S.intersect(shell, below(top_z), above(hem_z))
    hem = S.intersect(S.offset(S.union(*legs), t + 0.35), above(hem_z), below(hem_z + 2.2))
    return S.union(shell, hem)


def shoes(an, t, top_z, sole_color=None, toe_cap=0.0):
    shell = S.intersect(S.offset(S.smooth_union(1.5, an.foot["l"], an.foot["r"],
                                                an.shin["l"], an.shin["r"]), t), below(top_z))
    layers = [shell]
    if toe_cap:
        layers.append(S.intersect(S.offset(S.union(an.foot["l"], an.foot["r"]), t + toe_cap), below(6.0)))
    return S.union(*layers)


def sole(an, t):
    return S.intersect(S.offset(S.union(an.foot["l"], an.foot["r"]), t), below(2.4))


def belt(an, t, lo=96.5, hi=101.5):
    return S.intersect(S.offset(an.core, t), zband(lo, hi))


def hair_layers(an, style, color):
    """Hair and hats that are part of the head's surface."""
    out = [Layer(l.name, an.headspace(l.sdf), l.color, l.tint, l.bias) for l in _hair_layers(an, style, color)]
    return out


def _hair_layers(an, style, color):
    cr = an.cranium0
    nape = above(162.5)
    sides = S.halfspace((0, -4.5, 167.5), unit((0, -1.0, -0.35)))    # in front of the ears only above z~168
    hairline = S.halfspace((0, -8.0, 176.0), unit((0, -1.0, -0.9)))   # forehead
    base = S.intersect(S.offset(cr, 1.1), nape, sides, hairline)
    out = []
    if style == "none":
        return out
    if style == "short":
        out.append(Layer("hair", base, color))
    elif style == "buzz":
        out.append(Layer("hair", S.intersect(S.offset(cr, 0.45), nape, sides, hairline), color))
    elif style == "under_cap":
        out.append(Layer("hair", S.intersect(S.offset(cr, 0.9), nape, sides, below(177.5)), color))
    elif style == "under_cap_bun":
        bun = S.ellipsoid((0, 10.6, 168.5), (4.2, 3.6, 3.9))
        out.append(Layer("hair", S.union(S.intersect(S.offset(cr, 0.9), nape, sides, below(177.5)), bun), color))
    elif style == "bun":
        bun = S.smooth_union(1.0, S.ellipsoid((0, 9.0, 181.0), (4.6, 4.4, 4.2)))
        out.append(Layer("hair", S.smooth_union(1.2, S.intersect(S.offset(cr, 1.0), nape, sides, hairline), bun), color))
    elif style == "bob":
        shell = S.ellipsoid((0, 1.6, 170.0), (11.8, 12.8, 13.6))
        fringe = S.intersect(S.halfspace((0, -6.5, 0), (0, 1, 0)), below(173.5))
        face_cut = S.ellipsoid((0, -9.0, 164.0), (8.0, 8.0, 9.5))
        bob = S.subtract(S.intersect(shell, above(158.5)), S.union(fringe, face_cut))
        out.append(Layer("hair", S.smooth_union(0.8, bob, base), color))
    elif style == "long":
        shell = S.ellipsoid((0, 2.2, 168.0), (11.6, 12.4, 16.0))
        back = S.intersect(S.ellipsoid((0, 6.5, 156.0), (10.0, 5.6, 14.0)), S.halfspace((0, 3.0, 0), (0, -1, 0)))
        fringe = S.intersect(S.halfspace((0, -6.0, 0), (0, 1, 0)), below(174.5))
        face_cut = S.ellipsoid((0, -9.0, 164.0), (8.2, 9.0, 11.0))
        long = S.subtract(S.intersect(S.smooth_union(3.0, shell, back), above(146.0)), S.union(fringe, face_cut))
        out.append(Layer("hair", S.smooth_union(0.8, long, base), color))
    elif style == "ponytail":
        tail = S.smooth_union(1.2, S.ellipsoid((0, 11.0, 174.0), (3.0, 2.6, 3.0)),
                              S.round_cone((0, 12.6, 172.0), (0, 14.8, 156.0), 3.0, 1.6))
        out.append(Layer("hair", S.smooth_union(1.0, base, tail), color))
    elif style == "mohawk":
        crest = S.intersect(S.ellipsoid((0, 1.0, 177.5), (2.4, 12.8, 9.0)), above(170.0))
        sides_ = S.intersect(S.offset(cr, 0.35), nape, sides, hairline)
        out.append(Layer("hair", S.smooth_union(0.8, crest, sides_), color))
    elif style == "curly":
        blobs = [S.sphere((10.5 * math.sin(a) * math.cos(e), 1.8 + 10.5 * math.cos(a) * math.cos(e),
                           175.0 + 9.5 * math.sin(e)), 4.6)
                 for a in np.linspace(0, 2 * math.pi, 9, endpoint=False) for e in (0.0, 0.7)]
        blobs.append(S.sphere((0, 1.8, 185.0), 5.4))
        curly = S.intersect(S.smooth_union(1.4, S.offset(cr, 1.6), *blobs), nape, sides,
                            S.halfspace((0, -8.0, 176.5), unit((0, -1.0, -0.8))))
        out.append(Layer("hair", curly, color))
    elif style == "beanie":
        beanie = S.intersect(S.offset(cr, 1.5), above(172.0))
        cuff = S.intersect(S.offset(cr, 2.3), zband(171.2, 175.2))
        out.append(Layer("hat", S.union(beanie, cuff), color))
    elif style == "baseball":
        crown = S.intersect(S.offset(cr, 1.3), above(174.0))
        peak = S.intersect(S.ellipsoid((0, -11.0, 175.0), (8.6, 7.4, 0.9)), S.halfspace((0, -6, 0), (0, 1, 0)))
        out.append(Layer("hat", S.union(crown, peak), color))
        out.append(Layer("hair", S.intersect(S.offset(cr, 0.8), nape, sides, below(175.5)), HAIR["brown"]))
    return out


def police_cap(an):
    return [Layer(l.name, an.headspace(l.sdf), l.color, l.tint, l.bias) for l in _police_cap()]


def _police_cap():
    tilt = unit((0, 0.18, 1.0))
    band = S.cylinder((0, 0.6, 177.4), tilt, 10.9, 2.3, rounding=0.8)
    top = S.intersect(S.ellipsoid((0, -0.2, 181.4), (12.4, 13.2, 3.6)), above(179.0))
    crown = S.smooth_union(1.5, S.cylinder((0, 0.6, 179.6), tilt, 10.6, 1.5, rounding=0.6), top)
    ax = np.array([[1, 0, 0], [0, math.cos(0.3), -math.sin(0.3)], [0, math.sin(0.3), math.cos(0.3)]]).T
    visor = S.intersect(S.box((0, -10.4, 175.2), (8.4, 5.0, 0.55), rounding=0.5, axes=ax),
                        S.ellipsoid((0, -7.0, 175.2), (10.0, 9.6, 6.0)))
    return [Layer("cap_crown", crown, NAVY), Layer("cap_band", band, NEAR_BLACK, bias=0.05),
            Layer("cap_visor", visor, BLACK, bias=0.1)]


def moustache():
    m = S.smooth_union(1.0, S.ellipsoid((2.2, -12.9, 165.3), (2.7, 1.4, 1.1)),
                       S.ellipsoid((-2.2, -12.9, 165.3), (2.7, 1.4, 1.1)),
                       S.ellipsoid((4.2, -12.0, 164.4), (1.2, 1.2, 1.2)),
                       S.ellipsoid((-4.2, -12.0, 164.4), (1.2, 1.2, 1.2)))
    return m


def beard(an):
    b = S.intersect(S.offset(an.jaw0, 1.0), below(165.8), S.halfspace((0, -2.0, 0), (0, 1, 0)))
    return S.union(b, moustache())


# --------------------------------------------------------------------------------------
# Outfits
# --------------------------------------------------------------------------------------
def outfit_officer(an, fem):
    shirt = top_shell(an, 0.9, 96.0, 0.58)
    collar = S.intersect(S.offset(an.neck, 1.5), zband(143.0, 151.0))
    vcut = S.intersect(S.halfspace((0, -3.0, 0), (0, 1, 0)),
                       S.halfspace((0, 0, 151.5), unit((0, 0.45, -1.0))),
                       S.halfspace((0, 0, 0), unit((1.0, 0, -0.3))), S.halfspace((0, 0, 0), unit((-1.0, 0, -0.3))))
    shirt = S.union(shirt, S.subtract(collar, S.intersect(vcut, above(140.0))))
    return [
        Layer("shirt", shirt, UNIFORM, tint=True),
        Layer("belt", belt(an, 2.1), NEAR_BLACK, bias=0.05),
        Layer("trousers", trousers(an, 1.0, 9.5), TROUSER_NAVY),
        Layer("boots", shoes(an, 1.1, 13.0, toe_cap=0.25), BLACK),
        Layer("sole", sole(an, 1.5), NEAR_BLACK, bias=0.05),
    ]


def outfit_hoodie(an, pants=DENIM, shoe=WHITE):
    body = top_shell(an, 1.3, 88.0, 1.95)
    hem = S.intersect(S.offset(an.core, 1.9), zband(88.0, 92.0))
    cuffs = [S.intersect(S.offset(an.forearm[s], 1.8), an.limb_cut(s, 1.95), an.limb_cut(s, 1.82, False))
             for s in ("l", "r")]
    hood = S.smooth_union(2.0, S.ellipsoid((0, 6.5, 150.0), (10.5, 6.0, 5.2)),
                          S.intersect(S.offset(an.neck, 2.4), below(152.0)))
    hood = S.subtract(hood, S.ellipsoid((0, -7.0, 152.0), (6.0, 7.0, 6.5)))
    pocket = S.intersect(S.offset(an.torso, 2.0), zband(94.0, 108.0), S.halfspace((0, -4.0, 0), (0, 1, 0)),
                         S.box((0, -10, 101), (9.0, 12, 7.5), rounding=2.0))
    return [
        Layer("top", S.union(body, hem, *cuffs, hood, pocket), TOP, tint=True),
        Layer("trousers", trousers(an, 1.0, 8.0), pants),
        Layer("shoes", shoes(an, 1.2, 8.5), shoe),
        Layer("sole", sole(an, 1.6), OFFWHITE, bias=0.05),
    ]


def skirt_tube(top, hem, r_top, r_hem):
    """A skirt as an elliptic tube from top (x, y, z) to hem: radii (rx, ry) change linearly, so it follows the
    hips and legs instead of flaring round like a cone."""
    top, hem, r_top, r_hem = (np.asarray(v, dtype=float) for v in (top, hem, r_top, r_hem))

    def f(P):
        t = np.clip((top[2] - P[:, 2]) / (top[2] - hem[2]), 0.0, 1.0)[:, None]
        c = top + (hem - top) * t
        r = r_top + (r_hem - r_top) * t
        q = (P[:, :2] - c[:, :2]) / r
        k = np.sqrt((q * q).sum(axis=1))
        return (k - 1.0) * r.min(axis=1)
    return S.intersect(f, zband(hem[2], top[2]))


def outfit_dress(an, shoe=RED):
    bodice = top_shell(an, 0.8, 99.0, 0.3)
    bodice = neck_opening(bodice, 140.5, width=6.5)
    skirt = skirt_tube((0, -1.2, 101.0), (0, -2.5, 58.0), (17.2, 12.8), (20.6, 12.5))
    skirt = S.smooth_union(2.0, S.intersect(S.offset(an.core, 1.0), zband(64.0, 102.0)), skirt)
    return [
        Layer("top", S.union(bodice, skirt), TOP, tint=True, weight="skirt"),
        Layer("shoes", shoes(an, 0.7, 6.0), shoe),
        Layer("sole", sole(an, 1.0), NEAR_BLACK, bias=0.05),
    ]


def outfit_overalls(an):
    tee = top_shell(an, 0.8, 96.0, 0.42)
    tee = neck_opening(tee, 145.5, width=5.0)
    bib = S.intersect(S.offset(an.torso, 1.6), zband(95.0, 127.0), S.halfspace((0, -3.0, 0), (0, 1, 0)),
                      S.box((0, -10, 111), (9.5, 12, 16.5), rounding=1.5))
    straps = [S.intersect(S.offset(an.torso, 1.5),
                          S.capsule((sx * 8.5, -12.0, 126.0), (sx * 9.5, 12.0, 118.0), 12.0),
                          S.box((sx * 9.0, 0, 134), (1.9, 20, 16)), above(115.0)) for sx in (1, -1)]
    back = S.intersect(S.offset(an.torso, 1.6), zband(95.0, 113.0), S.halfspace((0, 3.0, 0), (0, -1, 0)))
    return [
        Layer("top", tee, TOP, tint=True),
        Layer("overalls", S.union(trousers(an, 1.4, 11.0), bib, back, *straps), DENIM, bias=0.02),
        Layer("boots", shoes(an, 1.3, 14.0, toe_cap=0.3), TAN),
        Layer("sole", sole(an, 1.8), BROWN, bias=0.05),
    ]


def outfit_suit(an):
    jacket = top_shell(an, 1.3, 84.0, 1.93)
    jacket = S.union(jacket, S.intersect(S.offset(an.core, 1.3), zband(84.0, 100.0)))
    lapel_v = S.intersect(S.halfspace((0, -4.0, 0), (0, 1, 0)), S.halfspace((0, 0, 151.0), unit((0, 0.2, -1.0))),
                          S.halfspace((0, 0, 118.0), unit((0.0, 0, 1.0))),
                          S.halfspace((0, 0, 118.0), unit((1.0, 0, -0.3))),
                          S.halfspace((0, 0, 118.0), unit((-1.0, 0, -0.3))))
    jacket = S.subtract(jacket, lapel_v)
    shirt = S.union(S.intersect(S.offset(an.torso, 0.7), above(98.0)),
                    S.intersect(S.offset(an.neck, 1.2), zband(140.0, 150.5)))
    shirt = S.subtract(shirt, S.intersect(S.halfspace((0, -3.0, 0), (0, 1, 0)), above(149.0),
                                          S.halfspace((0, 0, 0), unit((1.0, 0, -0.1))),
                                          S.halfspace((0, 0, 0), unit((-1.0, 0, -0.1)))))
    tie = S.intersect(S.offset(an.torso, 1.1), S.box((0, -10, 132), (1.8, 12, 16), rounding=0.5))
    knot = S.ellipsoid((0, -9.3, 146.5), (1.6, 1.5, 1.6))
    cuffs = [S.intersect(S.offset(an.forearm[s], 0.9), an.limb_cut(s, 1.97), an.limb_cut(s, 1.9, False))
             for s in ("l", "r")]
    return [
        Layer("top", jacket, TOP, tint=True, bias=0.02),
        Layer("shirt", S.union(shirt, *cuffs), WHITE),
        Layer("tie", S.union(tie, knot), RED, bias=0.05),
        Layer("trousers", trousers(an, 1.0, 8.5), CHARCOAL),
        Layer("shoes", shoes(an, 1.0, 8.0), BROWN),
        Layer("sole", sole(an, 1.4), NEAR_BLACK, bias=0.05),
    ]


def outfit_tank(an):
    tank = top_shell(an, 0.8, 94.0, 0.0)
    holes = [S.ellipsoid(side_pt((19.5, 1.5, 141.0), sx), (7.5, 9.5, 10.0)) for sx in (1, -1)]
    tank = neck_opening(S.subtract(tank, S.union(*holes)), 136.0, width=6.0)
    straps_keep = S.intersect(S.offset(an.torso, 0.8), S.union(*[S.box(side_pt((9.5, 0, 142), sx), (2.0, 15, 6))
                                                                  for sx in (1, -1)]), above(134.0))
    return [
        Layer("top", S.union(tank, straps_keep), TOP, tint=True),
        Layer("shorts", trousers(an, 1.2, 60.0, shorts=True), (0.20, 0.36, 0.22)),
        Layer("shoes", shoes(an, 1.2, 9.0), RED),
        Layer("sole", sole(an, 1.6), WHITE, bias=0.05),
    ]


def outfit_cardigan(an):
    blouse = neck_opening(top_shell(an, 0.7, 99.0, 0.3), 144.0, width=4.8)
    cardi = top_shell(an, 1.4, 92.0, 1.92)
    opening = S.intersect(S.halfspace((0, -4.0, 0), (0, 1, 0)), S.halfspace((0, 0, 0), unit((1.0, 0, 0.12))),
                          S.halfspace((0, 0, 0), unit((-1.0, 0, 0.12))))
    cardi = S.subtract(cardi, S.intersect(opening, S.box((0, -10, 128), (4.0, 12, 30))))
    cardi = S.subtract(cardi, S.ellipsoid((0, -6.0, 150.0), (7.5, 9.0, 8.5)))
    skirt = S.intersect(S.round_cone((0, 0.0, 101.0), (0, -0.5, 62.0), 16.5, 19.5), zband(60.0, 102.0))
    skirt = S.smooth_union(2.0, S.intersect(S.offset(an.hips, 1.0), below(102.0)), skirt)
    return [
        Layer("blouse", blouse, WHITE),
        Layer("top", cardi, TOP, tint=True, bias=0.02),
        Layer("skirt", skirt, (0.55, 0.14, 0.18), weight="skirt"),
        Layer("shoes", shoes(an, 0.8, 6.5), NEAR_BLACK),
        Layer("sole", sole(an, 1.1), NEAR_BLACK, bias=0.05),
    ]


def outfit_jacket(an):
    tee = neck_opening(top_shell(an, 0.7, 95.0, 0.3), 145.0)
    jacket = top_shell(an, 1.5, 93.0, 1.94)
    jacket = S.union(jacket, S.intersect(S.offset(an.core, 2.1), zband(93.0, 97.0)),
                     S.intersect(S.offset(an.neck, 2.4), zband(142.0, 151.0)))
    opening = S.intersect(S.halfspace((0, -4.0, 0), (0, 1, 0)), S.box((0, -10, 128), (3.2, 12, 40)))
    jacket = S.subtract(jacket, S.union(opening, S.ellipsoid((0, -8.5, 150.0), (5.5, 7.0, 9.0))), k=0.8)
    return [
        Layer("tee", tee, (0.12, 0.12, 0.14)),
        Layer("top", jacket, TOP, tint=True, bias=0.02),
        Layer("trousers", trousers(an, 0.9, 12.0), NEAR_BLACK),
        Layer("boots", shoes(an, 1.3, 16.0, toe_cap=0.3), (0.22, 0.14, 0.09)),
        Layer("sole", sole(an, 1.8), NEAR_BLACK, bias=0.05),
    ]


def outfit_apron(an):
    tee = neck_opening(top_shell(an, 0.8, 95.0, 0.42), 145.0, width=5.2)
    apron = S.intersect(S.offset(S.smooth_union(3.0, an.torso, an.hips, an.thigh["l"], an.thigh["r"]), 2.1),
                        S.halfspace((0, -2.0, 0), (0, 1, 0)), zband(66.0, 131.0),
                        S.union(S.box((0, -10, 99), (15.5, 12, 33)), S.box((0, -10, 124), (9.0, 12, 8))))
    strings = S.intersect(S.offset(an.torso, 2.2), zband(103.0, 105.2))
    neck_strap = S.intersect(S.offset(S.union(an.torso, an.neck), 1.8),
                             S.capsule((0, -2.0, 147.0), (0, 8.0, 142.0), 5.0), above(129.0))
    return [
        Layer("top", tee, TOP, tint=True),
        Layer("apron", S.union(apron, strings), (0.93, 0.93, 0.90), bias=0.03, weight="apron"),
        Layer("apron_strap", neck_strap, (0.93, 0.93, 0.90), bias=0.0),
        Layer("trousers", trousers(an, 1.0, 9.0), (0.14, 0.15, 0.20)),
        Layer("shoes", shoes(an, 1.1, 8.5), NEAR_BLACK),
        Layer("sole", sole(an, 1.5), NEAR_BLACK, bias=0.05),
    ]


def outfit_suspect(an):
    jumper = top_shell(an, 1.4, 90.0, 1.95)
    jumper = S.union(jumper, S.intersect(S.offset(an.core, 1.9), zband(90.0, 94.0)),
                     S.intersect(S.offset(an.neck, 1.8), zband(142.0, 150.0)))
    band = 7.0

    def stripes(sign):
        return lambda P: jumper(P) + sign * 0.03 * np.sin(np.pi * P[:, 2] / band)
    return [
        Layer("jumper_black", stripes(1.0), NEAR_BLACK),
        Layer("jumper_white", stripes(-1.0), OFFWHITE),
        Layer("trousers", trousers(an, 1.0, 9.0), NEAR_BLACK),
        Layer("shoes", shoes(an, 1.2, 8.5), (0.25, 0.25, 0.27)),
        Layer("sole", sole(an, 1.6), WHITE, bias=0.05),
    ]


def eye_mask(an):
    band = S.intersect(S.offset(an.head0, 0.45), zband(168.0, 172.8))
    return Layer("mask", an.headspace(band), BLACK, bias=0.02)


# --------------------------------------------------------------------------------------
# The cast
# --------------------------------------------------------------------------------------
# build tweaks: see DEFAULT_BUILD. hair: (style, colour). face: extras on the head.
CAST = [
    dict(name="SK_Officer", folder="Officer", build=dict(sh=1.04, chest=1.04, belly=0.25), skin=0,
         hair=("under_cap", "brown"), face=["moustache"], outfit="officer"),
    dict(name="SK_Officer_F", folder="Officer", build=dict(female=True, sh=0.92, hip=1.06, chest=0.95, arm=0.9,
                                                           neck=0.95, jaw=0.9), skin=2,
         hair=("under_cap_bun", "dark"), face=["lashes"], outfit="officer"),
    dict(name="SK_Civilian_01", folder="Civilians", build=dict(), skin=0, hair=("short", "brown"),
         face=[], outfit="hoodie"),
    dict(name="SK_Civilian_02", folder="Civilians", build=dict(female=True, sh=0.9, chest=0.92, arm=0.86, leg=0.94,
                                                               neck=0.92, jaw=0.9), skin=3,
         hair=("bun", "black"), face=["glasses", "lashes"], outfit="dress"),
    dict(name="SK_Civilian_03", folder="Civilians", build=dict(sh=1.08, chest=1.08, belly=1.0, arm=1.1, leg=1.08,
                                                               neck=1.1, jaw=1.08), skin=1,
         hair=("none", "brown"), face=["beard"], outfit="overalls"),
    dict(name="SK_Civilian_04", folder="Civilians", build=dict(female=True, sh=0.9, hip=1.08, arm=0.9, jaw=0.92,
                                                               neck=0.92), skin=4,
         hair=("bob", "blonde"), face=["lashes", "bag"], outfit="cardigan"),
    dict(name="SK_Civilian_05", folder="Civilians", build=dict(sh=1.1, chest=1.1, arm=1.12, leg=1.02), skin=3,
         hair=("mohawk", "black"), face=[], outfit="tank"),
    dict(name="SK_Civilian_06", folder="Civilians", build=dict(belly=0.45, sh=1.02), skin=4,
         hair=("short", "ginger"), face=["beard"], outfit="suit"),
    dict(name="SK_Civilian_07", folder="Civilians", build=dict(female=True, sh=0.98, belly=0.8, hip=1.14, arm=1.06,
                                                               leg=1.08, glute=1.1, jaw=1.0), skin=5,
         hair=("curly", "grey"), face=["glasses", "lashes"], outfit="apron"),
    dict(name="SK_Civilian_08", folder="Civilians", build=dict(female=True, sh=0.92, arm=0.9, leg=0.96, jaw=0.9,
                                                               neck=0.92), skin=1,
         hair=("ponytail", "pink"), face=["lashes"], outfit="jacket"),
    dict(name="SK_Suspect", folder="Civilians", build=dict(sh=1.02, belly=0.2), skin=1,
         hair=("beanie", "black"), face=["mask", "stubble"], outfit="suspect"),
]

OUTFITS = {
    "officer": lambda an, spec: outfit_officer(an, spec["build"].get("female", False)),
    "hoodie": lambda an, spec: outfit_hoodie(an),
    "dress": lambda an, spec: outfit_dress(an),
    "overalls": lambda an, spec: outfit_overalls(an),
    "cardigan": lambda an, spec: outfit_cardigan(an),
    "tank": lambda an, spec: outfit_tank(an),
    "suit": lambda an, spec: outfit_suit(an),
    "apron": lambda an, spec: outfit_apron(an),
    "jacket": lambda an, spec: outfit_jacket(an),
    "suspect": lambda an, spec: outfit_suspect(an),
}


def character_layers(spec):
    an = Anatomy(spec["build"])
    skin = SKIN_TONES[spec["skin"]]
    style, hair_name = spec["hair"]
    hair = HAIR[hair_name]
    layers = [Layer("skin", an.skin(), skin)]
    layers += OUTFITS[spec["outfit"]](an, spec)
    layers += hair_layers(an, style, hair)
    if spec["outfit"] == "officer":
        layers += police_cap(an)
    if "moustache" in spec["face"]:
        layers.append(Layer("moustache", an.headspace(moustache()), hair, bias=0.02))
    if "beard" in spec["face"]:
        layers.append(Layer("beard", an.headspace(beard(an)), hair, bias=0.02))
    if "mask" in spec["face"]:
        layers.append(eye_mask(an))
    return an, layers


# --------------------------------------------------------------------------------------
# Meshing the body
# --------------------------------------------------------------------------------------
def mesh_from_arrays(name, verts, faces):
    mesh = bpy.data.meshes.new(name)
    mesh.from_pydata([tuple(v) for v in verts], [], [tuple(int(i) for i in f) for f in faces])
    mesh.validate()
    obj = fb.link(bpy.data.objects.new(name, mesh))
    return obj


def apply_modifier(obj, mod):
    fb.set_active(obj)
    bpy.ops.object.modifier_apply(modifier=mod.name)


def decimate(obj, target_tris, weights=None):
    tris = sum(len(p.vertices) - 2 for p in obj.data.polygons)
    if tris <= target_tris:
        return
    mod = obj.modifiers.new("Decimate", 'DECIMATE')
    mod.decimate_type = 'COLLAPSE'
    mod.ratio = target_tris / tris
    mod.use_symmetry = True
    mod.symmetry_axis = 'X'
    mod.use_collapse_triangulate = True
    if weights:
        # Lower weights decimate less.
        mod.vertex_group = weights
        mod.vertex_group_factor = 3.0
    apply_modifier(obj, mod)


def body_mesh(name, layers):
    solid = S.intersect(S.union(*[l.sdf for l in layers]), above(0.0))
    verts, quads = S.surface_nets(solid, (-58.0, -32.0, -1.5), (58.0, 32.0, 192.0), VOXEL)
    obj = mesh_from_arrays(name, verts, quads)
    # Decimation keeps detail where the shape needs it (face, hands of the hems) but leaves long slivers on flat
    # areas, which bend badly: decimate further than needed, then split long edges back onto the surface.
    head = obj.vertex_groups.new(name="_detail")
    zs = np.array([v.co.z for v in obj.data.vertices])
    head.add([int(i) for i in np.nonzero(zs <= 156.0)[0]], 1.0, 'REPLACE')
    head.add([int(i) for i in np.nonzero(zs > 156.0)[0]], 0.3, 'REPLACE')   # the face keeps more detail
    # So do hems and openings: where two layers meet, the surface steps and coarse triangles turn it ragged.
    P = np.array([v.co for v in obj.data.vertices])
    vals = np.sort(np.stack([l.sdf(P) - l.bias for l in layers], axis=1), axis=1)
    near = np.nonzero((vals[:, 1] - vals[:, 0] < 0.9) & (zs <= 156.0))[0]
    head.add([int(i) for i in near], 0.45, 'REPLACE')
    decimate(obj, int(BODY_TRIS * 0.30), weights="_detail")
    obj.vertex_groups.remove(obj.vertex_groups["_detail"])
    even_out(obj, solid, lambda p: MAX_EDGE_HEAD if p.z > 157.0 else MAX_EDGE)
    cut_along_labels(obj, layers, solid)
    return obj, solid


def even_out(obj, solid, max_edge):
    bm = bmesh.new()
    bm.from_mesh(obj.data)
    for _ in range(5):
        long = [e for e in bm.edges if e.calc_length() > max_edge((e.verts[0].co + e.verts[1].co) / 2)]
        if not long:
            break
        res = bmesh.ops.subdivide_edges(bm, edges=long, cuts=1, use_grid_fill=False)
        bmesh.ops.triangulate(bm, faces=bm.faces[:])
        new = [v for v in res["geom_inner"] if isinstance(v, bmesh.types.BMVert)]
        moved = S.project_to_surface(solid, np.array([v.co for v in new]), max_step=1.0)
        for v, p in zip(new, moved):
            v.co = Vector(p)
    bmesh.ops.beautify_fill(bm, faces=bm.faces[:], edges=bm.edges[:])
    # Decimation leaves vertices a little off the surface: put them back on it (colours depend on it).
    bm.verts.ensure_lookup_table()
    moved = S.project_to_surface(solid, np.array([v.co for v in bm.verts]), max_step=0.6)
    for v, p in zip(bm.verts, moved):
        v.co = Vector(p)
    bm.to_mesh(obj.data)
    bm.free()


def cut_along_labels(obj, layers, solid):
    """
    Splits every edge whose ends lie on different layers where the two layers meet, and joins the splits across
    each face, so colour boundaries (hems, hairlines, stripes) are clean lines instead of a zigzag of triangles.
    Then colours each face by its layer.
    """
    fields = [(l.sdf, l.bias) for l in layers]
    bm = bmesh.new()
    bm.from_mesh(obj.data)
    bm.verts.ensure_lookup_table()
    P = np.array([v.co for v in bm.verts])
    vals = np.stack([f(P) - b for f, b in fields], axis=1)
    lab = np.argmin(vals, axis=1)

    vlab = bm.verts.layers.int.new("fto_vlab")   # the layer each original vertex lies on; -1 for cut vertices
    for v in bm.verts:
        v[vlab] = int(lab[v.index])

    # Where each edge crosses from one layer to the other: bisect the difference of the two fields along the edge
    # (it is far from linear over a 5 cm edge, e.g. a stripe's sine).
    cross = []
    for e in bm.edges:
        a, b = e.verts
        la, lb = lab[a.index], lab[b.index]
        if la != lb:
            cross.append((e, a, b, la, lb))
    new_verts = []
    if cross:
        A = np.array([a.co for _e, a, _b, _la, _lb in cross])
        B = np.array([b.co for _e, _a, b, _la, _lb in cross])
        LA = np.array([c[3] for c in cross])
        LB = np.array([c[4] for c in cross])

        def diff(t):
            Q = A + (B - A) * t[:, None]
            out = np.empty(len(Q))
            for la, lb in set(zip(LA.tolist(), LB.tolist())):
                idx = np.nonzero((LA == la) & (LB == lb))[0]
                (fa, ba), (fb_, bb) = fields[la], fields[lb]
                out[idx] = (fa(Q[idx]) - ba) - (fb_(Q[idx]) - bb)
            return out        # < 0 where la still wins
        lo, hi = np.zeros(len(cross)), np.ones(len(cross))
        for _ in range(10):
            mid = 0.5 * (lo + hi)
            below = diff(mid) < 0.0
            lo = np.where(below, mid, lo)
            hi = np.where(below, hi, mid)
        T = np.clip(0.5 * (lo + hi), 0.03, 0.97)
        for (e, a, _b, _la, _lb), t in zip(cross, T):
            _e, v = bmesh.utils.edge_split(e, a, float(t))
            v[vlab] = -1
            new_verts.append(v)
        NP = np.array([v.co for v in new_verts])
        NP = S.project_to_surface(solid, NP, max_step=0.6)
        for v, p in zip(new_verts, NP):
            v.co = Vector(p)
    marked = set(new_verts)
    for f in list(bm.faces):
        if not f.is_valid:
            continue
        vs = [v for v in f.verts if v in marked]
        if len(vs) == 2:
            try:
                bmesh.ops.connect_verts(bm, verts=vs)
            except Exception:
                pass
        elif len(vs) == 3:
            c = f.calc_center_median()
            res = bmesh.ops.poke(bm, faces=[f])
            centre = res["verts"][0]
            centre.co = c
            centre[vlab] = -1
    bmesh.ops.triangulate(bm, faces=bm.faces[:])

    # Colour each face by the layer of its original corners: after the cut, every piece has corners on one layer
    # only (sampling the field at face centres instead flips thin pieces along the boundary and leaves teeth).
    # Pieces with only cut corners (the centre of a three-layer corner) take the layer under their centre.
    C = S.project_to_surface(solid, np.array([f.calc_center_median() for f in bm.faces]), max_step=0.6)
    flab = S.label_vertices(fields, C)
    for i, f in enumerate(bm.faces):
        own = [v[vlab] for v in f.verts if v[vlab] >= 0]
        if own:
            flab[i] = max(set(own), key=own.count)
    col = bm.loops.layers.color.new("Col")
    for f, li in zip(bm.faces, flab):
        layer = layers[li]
        rgba = (*layer.color, 1.0 if layer.tint else 0.0)
        f.smooth = True
        for loop in f.loops:
            loop[col] = rgba
    bm.verts.layers.int.remove(vlab)
    face_layer = bm.faces.layers.int.new("fto_layer")
    for f, li in zip(bm.faces, flab):
        f[face_layer] = int(li)
    bm.to_mesh(obj.data)
    bm.free()
    obj.data.color_attributes.active_color = obj.data.color_attributes["Col"]


# --------------------------------------------------------------------------------------
# Hands
# --------------------------------------------------------------------------------------
FINGERS = ("index", "middle", "ring", "pinky")


def hand_frame(side):
    h = J[f"hand_{side}"]
    f = unit(J[f"middle_01_{side}"] - h)
    s = J[f"index_01_{side}"] - J[f"pinky_01_{side}"]
    s = unit(s - f * (s @ f))
    n = np.cross(f, s) * (1.0 if side == "l" else -1.0)   # palm normal (points out of the palm)
    return h, f, s, n


def finger_chain(side, finger):
    """Joint positions from the knuckle to the fingertip."""
    if finger == "thumb":
        pts = [J[f"thumb_0{i}_{side}"] for i in (1, 2, 3)]
        tip = pts[-1] + unit(pts[-1] - pts[-2]) * 2.6
    else:
        pts = [J[f"{finger}_0{i}_{side}"] for i in (1, 2, 3)]
        tip = pts[-1] + unit(pts[-1] - pts[-2]) * {"index": 2.4, "middle": 2.6, "ring": 2.5, "pinky": 2.1}[finger]
    return pts + [tip]


FINGER_R = {"thumb": 1.25, "index": 1.02, "middle": 1.06, "ring": 1.0, "pinky": 0.88}


def hand_sdf(side):
    h, f, s, n = hand_frame(side)
    axes = np.stack([f, s, n], axis=1)
    palm = S.box(h + f * 4.9 - n * 0.2, (4.6, 4.2, 1.35), rounding=1.2, axes=axes)
    heel = S.ellipsoid(h + f * 2.0 - n * 0.1, (3.2, 3.6, 1.8))
    wrist = S.capsule(h - f * 2.6, h + f * 1.2, 2.75)
    thumb_base = J[f"thumb_01_{side}"]
    thenar = S.ellipsoid(thumb_base + (J[f"thumb_02_{side}"] - thumb_base) * 0.3 + n * 0.6, (2.2, 2.2, 2.2))
    fingers = []
    for finger in FINGERS + ("thumb",):
        pts = finger_chain(side, finger)
        r = FINGER_R[finger]
        segs = []
        if finger != "thumb":
            segs.append(S.round_cone(pts[0] - unit(pts[1] - pts[0]) * 1.6, pts[0], r * 1.05, r * 1.02))
        for i in range(len(pts) - 1):
            segs.append(S.round_cone(pts[i], pts[i + 1], r * (1.0 - 0.07 * i), r * (0.93 - 0.07 * i)))
        fingers.append(S.union(*segs))
    base = S.smooth_union(1.4, palm, heel, wrist, thenar)
    return S.union(S.smooth_union(0.8, base, fingers[-1]), *[S.smooth_union(0.5, base, fg) for fg in fingers[:-1]])


def hand_bones(side):
    """(bone, a, b) segments for weighting the hand."""
    h, f, _s, _n = hand_frame(side)
    segs = [(f"hand_{side}", h - f * 3.0, h + f * 2.5)]
    for finger in FINGERS:
        pts = finger_chain(side, finger)
        meta = J[f"{finger}_metacarpal_{side}"]
        segs.append((f"{finger}_metacarpal_{side}", meta, pts[0]))
        for i in range(3):
            segs.append((f"{finger}_0{i + 1}_{side}", pts[i], pts[i + 1]))
    pts = finger_chain(side, "thumb")
    for i in range(3):
        segs.append((f"thumb_0{i + 1}_{side}", pts[i], pts[i + 1]))
    return segs


def seg_dist(P, a, b):
    ab = b - a
    t = np.clip(((P - a) @ ab) / (ab @ ab), 0.0, 1.0)
    return np.linalg.norm(P - (a + np.outer(t, ab)), axis=1), t


def hand_mesh(side, skin):
    sdf = hand_sdf(side)
    h, _f, _s, _n = hand_frame(side)
    lo, hi = h - 16.0, h + 16.0
    verts, quads = S.surface_nets(sdf, lo, hi, 0.34)
    obj = mesh_from_arrays(f"Hand_{side}", verts, quads)
    decimate_plain(obj, HAND_TRIS)
    colorize(obj, skin)
    return obj


def decimate_plain(obj, target):
    tris = sum(len(p.vertices) - 2 for p in obj.data.polygons)
    if tris > target:
        mod = obj.modifiers.new("Decimate", 'DECIMATE')
        mod.decimate_type = 'COLLAPSE'
        mod.ratio = target / tris
        mod.use_collapse_triangulate = True
        apply_modifier(obj, mod)


def colorize(obj, color, tint=False):
    me = obj.data
    if "Col" not in me.color_attributes:
        me.color_attributes.new("Col", 'BYTE_COLOR', 'CORNER')
    attr = me.color_attributes["Col"]
    rgba = (*[fb.srgb_to_linear(c) for c in color], 1.0 if tint else 0.0)
    for d in attr.data:
        d.color = rgba
    me.color_attributes.active_color = attr
    for p in me.polygons:
        p.use_smooth = True


def weight_hand(obj, side, body_sampler):
    P = np.array([v.co for v in obj.data.vertices])
    segs = hand_bones(side)
    D = np.stack([seg_dist(P, a, b)[0] for _n, a, b in segs], axis=1)
    names = [n for n, _a, _b in segs]
    chain_of = []
    for n in names:
        if n.startswith("thumb"):
            chain_of.append("thumb")
        elif n.startswith("hand"):
            chain_of.append("hand")
        else:
            chain_of.append(n.split("_")[0])
    chain_of = np.array(chain_of)
    nearest = np.argmin(D, axis=1)
    h, f, _s, _n = hand_frame(side)
    along = (P - h) @ f
    for vi, p in enumerate(P):
        chain = chain_of[nearest[vi]]
        ok = (chain_of == chain) | (chain_of == "hand")
        d = np.where(ok, D[vi], np.inf)
        w = 1.0 / np.maximum(d, 0.15) ** 6
        w[~ok] = 0.0
        top = np.argsort(-w)[:3]
        ws = {names[i]: w[i] for i in top if w[i] > 0}
        tot = sum(ws.values())
        ws = {k: v / tot for k, v in ws.items()}
        blend = float(np.clip((along[vi] + 1.5) / 3.0, 0.0, 1.0))
        if blend < 1.0:
            body = body_sampler(Vector(p))
            mixed = {k: v * blend for k, v in ws.items()}
            for k, v in body.items():
                mixed[k] = mixed.get(k, 0.0) + v * (1.0 - blend)
            ws = mixed
        set_weights(obj, vi, ws)


def set_weights(obj, vi, ws):
    for name, w in ws.items():
        if w <= 1e-4:
            continue
        g = obj.vertex_groups.get(name) or obj.vertex_groups.new(name=name)
        g.add([vi], float(w), 'REPLACE')


# --------------------------------------------------------------------------------------
# Skinning
# --------------------------------------------------------------------------------------
FINGER_BONES = {n for n in fto_rig.BONE_NAMES if any(n.startswith(p) for p in FINGERS + ("thumb",))}


def read_weights(obj):
    names = {g.index: g.name for g in obj.vertex_groups}
    out = []
    for v in obj.data.vertices:
        out.append({names[g.group]: g.weight for g in v.groups if g.weight > 0})
    return out


def write_weights(obj, weights):
    for g in list(obj.vertex_groups):
        obj.vertex_groups.remove(g)
    groups = {}
    for vi, ws in enumerate(weights):
        for name, w in ws.items():
            if name not in groups:
                groups[name] = obj.vertex_groups.new(name=name)
            groups[name].add([vi], float(w), 'REPLACE')


def clean(ws, limit=4):
    ws = {k: v for k, v in ws.items() if v > 0.002}
    top = sorted(ws.items(), key=lambda kv: -kv[1])[:limit]
    tot = sum(w for _k, w in top) or 1.0
    return {k: w / tot for k, w in top}


def smoothstep(a, b, x):
    t = min(1.0, max(0.0, (x - a) / (b - a)))
    return t * t * (3 - 2 * t)


REGIONS = {
    # region: bones that may move it (the anatomy group a vertex lies closest to decides its region)
    "head": ["head", "neck_02"],
    "neck": ["neck_01", "neck_02", "head", "spine_05", "clavicle_l", "clavicle_r"],
    "torso": ["pelvis", "spine_01", "spine_02", "spine_03", "spine_04", "spine_05", "clavicle_l", "clavicle_r",
              "neck_01", "thigh_l", "thigh_r"],
}
for _s in ("l", "r"):
    REGIONS[f"upperarm_{_s}"] = [f"upperarm_{_s}", f"upperarm_twist_01_{_s}", f"upperarm_twist_02_{_s}",
                                 f"clavicle_{_s}", f"lowerarm_{_s}", "spine_05"]
    REGIONS[f"forearm_{_s}"] = [f"lowerarm_{_s}", f"lowerarm_twist_01_{_s}", f"lowerarm_twist_02_{_s}",
                                f"upperarm_{_s}", f"upperarm_twist_02_{_s}", f"hand_{_s}"]
    REGIONS[f"thigh_{_s}"] = [f"thigh_{_s}", f"thigh_twist_01_{_s}", f"thigh_twist_02_{_s}", "pelvis", f"calf_{_s}"]
    REGIONS[f"shin_{_s}"] = [f"calf_{_s}", f"calf_twist_01_{_s}", f"calf_twist_02_{_s}", f"thigh_{_s}",
                             f"thigh_twist_02_{_s}", f"foot_{_s}"]
    REGIONS[f"foot_{_s}"] = [f"foot_{_s}", f"ball_{_s}", f"calf_{_s}", f"calf_twist_01_{_s}"]


def region_fields(an):
    out = {"head": an.head, "neck": an.neck, "torso": an.core}
    for s in ("l", "r"):
        out[f"upperarm_{s}"] = an.upperarm[s]
        out[f"forearm_{s}"] = an.forearm[s]
        out[f"thigh_{s}"] = an.thigh[s]
        out[f"shin_{s}"] = an.shin[s]
        out[f"foot_{s}"] = an.foot[s]
    return out


def auto_weights(body, an, weight_rig, smooth_iters=10):
    """
    Skin weights from bone proximity, restricted per body region (so an arm never pulls the ribs and a leg never
    the other leg), then smoothed over the surface so regions blend. (Blender's heat weights fail on these meshes
    for some bones, silently, so they're not used.)
    """
    bones = [b for b in weight_rig.data.bones if b.use_deform and b.name not in FINGER_BONES]
    names = [b.name for b in bones]
    col = {n: i for i, n in enumerate(names)}
    A = np.array([tuple(b.head_local) for b in bones])
    Bt = np.array([tuple(b.tail_local) for b in bones])
    P = np.array([tuple(v.co) for v in body.data.vertices])
    D = np.empty((len(P), len(bones)))
    for j in range(len(bones)):
        D[:, j] = seg_dist(P, A[j], Bt[j])[0]
    fields = region_fields(an)
    rnames = list(fields)
    R = np.argmin(np.stack([fields[r](P) for r in rnames], axis=1), axis=1)
    allowed = np.zeros((len(rnames), len(bones)), dtype=bool)
    for ri, r in enumerate(rnames):
        for n in REGIONS[r]:
            allowed[ri, col[n]] = True
    W = 1.0 / np.maximum(D, 0.5) ** 5
    W[~allowed[R]] = 0.0
    # Keep the three strongest, normalise.
    order = np.argsort(-W, axis=1)
    keep = np.zeros_like(W, dtype=bool)
    np.put_along_axis(keep, order[:, :3], True, axis=1)
    W[~keep] = 0.0
    W /= np.maximum(W.sum(axis=1, keepdims=True), 1e-12)
    # Smooth over the mesh.
    E = np.array([tuple(e.vertices) for e in body.data.edges])
    deg = np.bincount(E.ravel(), minlength=len(P)).astype(float)
    for _ in range(smooth_iters):
        acc = np.zeros_like(W)
        np.add.at(acc, E[:, 0], W[E[:, 1]])
        np.add.at(acc, E[:, 1], W[E[:, 0]])
        W = 0.5 * W + 0.5 * acc / np.maximum(deg, 1)[:, None]
    return [{names[j]: float(W[i, j]) for j in np.nonzero(W[i] > 0.003)[0]} for i in range(len(P))]


def skin_body(body, layers, weight_rig, an):
    W = auto_weights(body, an, weight_rig)
    face_layer = body.data.attributes["fto_layer"].data
    vert_layers = [set() for _ in body.data.vertices]
    for poly in body.data.polygons:
        li = face_layer[poly.index].value
        for vi in poly.vertices:
            vert_layers[vi].add(li)
    out = []
    for v, ws in zip(body.data.vertices, W):
        p = v.co
        ws = {k: w for k, w in ws.items() if k not in FINGER_BONES}
        # Left bones stay on the left and right on the right (automatic weights bleed across the crotch and neck).
        for k in list(ws):
            if k.endswith("_l"):
                ws[k] *= smoothstep(-2.5, 0.5, p.x)
            elif k.endswith("_r"):
                ws[k] *= smoothstep(-2.5, 0.5, -p.x)
        # The head is rigid above the jaw line; the neck blends below it.
        head_zone = max(smoothstep(160.0, 163.5, p.z), smoothstep(-4.0, -6.5, p.y) * smoothstep(155.5, 158.5, p.z))
        if head_zone > 0:
            ws = {k: w * (1 - head_zone) for k, w in ws.items()}
            ws["head"] = ws.get("head", 0.0) + head_zone
        tweaks = {layers[li].weight for li in vert_layers[v.index]} - {None}
        if "skirt" in tweaks and p.z < 95:
            # Skirts hang from the hips: part pelvis, so they don't split between the legs.
            share = 0.55 * smoothstep(95.0, 70.0, p.z) + 0.25
            ws = {k: w * (1 - share) for k, w in ws.items()}
            ws["pelvis"] = ws.get("pelvis", 0.0) + share
        if "apron" in tweaks and p.z < 96:
            share = 0.5
            ws = {k: w * (1 - share) for k, w in ws.items()}
            ws["pelvis"] = ws.get("pelvis", 0.0) + share
        if not ws:
            ws = {"pelvis": 1.0}
        out.append(clean(ws))
    write_weights(body, out)


class BodySampler:
    """Weights of the body surface nearest to a point (barycentric over the nearest triangle)."""

    def __init__(self, body):
        me = body.data
        self.verts = [v.co.copy() for v in me.vertices]
        self.tris = [tuple(p.vertices) for p in me.polygons]
        self.bvh = BVHTree.FromPolygons(self.verts, self.tris)
        self.W = read_weights(body)

    def __call__(self, p):
        loc, _n, idx, _d = self.bvh.find_nearest(p)
        tri = self.tris[idx]
        a, b, c = (self.verts[i] for i in tri)
        from mathutils.geometry import barycentric_transform
        bc = barycentric_transform(loc, a, b, c, Vector((1, 0, 0)), Vector((0, 1, 0)), Vector((0, 0, 1)))
        out = {}
        for w, vi in zip(bc, tri):
            for k, x in self.W[vi].items():
                out[k] = out.get(k, 0.0) + max(0.0, w) * x
        return clean(out)

    def ray(self, origin, direction):
        """Where a ray first hits the body (or, if it misses, the surface nearest its origin) and the normal there."""
        loc, normal, _i, _d = self.bvh.ray_cast(Vector(origin), Vector(direction).normalized())
        if loc is None:
            loc, normal, _i, _d = self.bvh.find_nearest(Vector(origin))
        return loc, normal


# --------------------------------------------------------------------------------------
# Props on the surface (face, badges, pouches...)
# --------------------------------------------------------------------------------------
class Props:
    """Small coloured parts, positioned in cm; each gets rigid weights from the body under its centre (or the
    named bone)."""

    def __init__(self, sampler):
        self.sampler = sampler
        self.items = []   # (bmesh, weights-or-None, anchor)

    def add(self, kind, color, M, dims, tint=False, bone=None, anchor=None, **kw):
        kw.setdefault("segments", 10)
        kw.setdefault("rings", 6)
        part = fb.make_part(kind, color, scale=tuple(d / fb.UNIT for d in dims), tint=tint, **kw)
        bmesh.ops.transform(part.bm, matrix=M, verts=part.bm.verts)
        a = anchor if anchor is not None else M.translation.copy()
        self.items.append((part.bm, {bone: 1.0} if bone else None, a))

    def add_bm(self, bm, bone=None, anchor=None):
        a = anchor if anchor is not None else sum((v.co for v in bm.verts), Vector()) / max(1, len(bm.verts))
        self.items.append((bm, {bone: 1.0} if bone else None, a))

    def build(self, name):
        mesh = bpy.data.meshes.new(name)
        merged = bmesh.new()
        scratch = bpy.data.meshes.new(name + "_scratch")
        weights = []
        for bm, ws, anchor in self.items:
            ws = ws or self.sampler(anchor)
            bm.to_mesh(scratch)
            merged.from_mesh(scratch)
            weights += [ws] * len(bm.verts)
            bm.free()
        merged.to_mesh(mesh)
        merged.free()
        bpy.data.meshes.remove(scratch)
        obj = fb.link(bpy.data.objects.new(name, mesh))
        if "Col" in mesh.color_attributes:
            mesh.color_attributes.active_color = mesh.color_attributes["Col"]
        write_weights(obj, weights)
        return obj


def frame_at(loc, normal, up=(0, 0, 1)):
    """Matrix placing a part's local -Y along normal (its front faces out of the surface), +Z towards up."""
    n = Vector(normal).normalized()
    y = -n
    z = Vector(up)
    z = (z - y * z.dot(y)).normalized()
    x = y.cross(z)
    M = Matrix((x, y, z)).transposed().to_4x4()
    M.translation = Vector(loc)
    return M


def rot_x(deg):
    return Matrix.Rotation(math.radians(deg), 4, 'X')


def rot_y(deg):
    return Matrix.Rotation(math.radians(deg), 4, 'Y')


def rot_z(deg):
    return Matrix.Rotation(math.radians(deg), 4, 'Z')


def face_props(props, sampler, spec, an):
    """Eyes, brows, mouth (the nose and ears are part of the head)."""
    hair = HAIR[spec["hair"][1]]
    brow_col = tuple(c * 0.8 for c in hair) if spec["hair"][1] not in ("grey", "pink", "blonde") else \
        tuple(c * 0.7 for c in hair)
    lashes = "lashes" in spec["face"]
    for sx in (1, -1):
        loc, n = sampler.ray(an.hp(sx * 3.95, -40.0, 170.0), (0, 1, 0))
        M = frame_at(loc, n)
        props.add('sphere', WHITE, M @ Matrix.Translation((0, 0.45, 0)), (3.7, 2.1, 4.3), bone="head",
                  segments=12, rings=7)
        pupil = M @ Matrix.Translation((0, -0.25, -0.1))
        props.add('sphere', PUPIL, pupil, (2.5, 1.0, 2.9), bone="head", segments=10, rings=5)
        props.add('sphere', WHITE, M @ Matrix.Translation((0.35, -0.62, 0.55)), (0.5, 0.3, 0.5),
                  bone="head", segments=6, rings=4)
        if lashes:
            props.add('cube', PUPIL, M @ Matrix.Translation((sx * 1.0, -0.35, 1.55)) @ rot_y(sx * -18),
                      (2.8, 0.5, 0.45), bone="head")
        bl, bn = sampler.ray(an.hp(sx * 4.1, -40.0, 173.6), (0, 1, 0))
        B = frame_at(bl, bn) @ Matrix.Translation((0, -0.15, 0)) @ rot_y(sx * 8)
        props.add('cube', brow_col, B, (3.4, 0.8, 0.75), bone="head")
    # Mouth: a small smile of three overlapping blobs curving up at the corners.
    for x, dz, sz in ((0.0, 0.0, 1.0), (1.45, 0.35, 0.8), (-1.45, 0.35, 0.8)):
        ml, mn = sampler.ray(an.hp(x, -40.0, 164.3 + dz), (0, 1, 0))
        M = frame_at(ml, mn) @ Matrix.Translation((0, 0.15, 0))
        props.add('sphere', MOUTH, M, (2.0 * sz, 0.7, 0.75 * sz), bone="head", segments=8, rings=4)
    if "glasses" in spec["face"]:
        for sx in (1, -1):
            loc, n = sampler.ray(an.hp(sx * 3.95, -40.0, 170.2), (0, 1, 0))
            M = frame_at(loc + Vector((0, -1.3, 0)), (0, -1, 0))
            props.add('torus', NEAR_BLACK, M @ rot_x(90), (5.0, 5.0, 5.0), bone="head", minor=0.07, segments=16, rings=5)
            arm_from = Vector(an.hp(sx * 6.2, 0, 170.6)) + Vector((0, loc.y - 1.0 - an.hp(0, 0, 0)[1], 0))
            arm_to = Vector(an.hp(sx * 10.2, 1.5, 170.0))
            mid = (arm_from + arm_to) / 2
            d = arm_to - arm_from
            A = Matrix.Translation(mid) @ Vector((0, 1, 0)).rotation_difference(d).to_matrix().to_4x4()
            props.add('cube', NEAR_BLACK, A, (0.4, d.length, 0.4), bone="head")
        bl, _n = sampler.ray(an.hp(0, -40.0, 170.8), (0, 1, 0))
        props.add('cube', NEAR_BLACK, Matrix.Translation(bl + Vector((0, -1.1, 0))), (2.0, 0.4, 0.4), bone="head")
    if "stubble" in spec["face"]:
        pass


def officer_props(props, sampler, fem, an):
    # Badge (left chest) and name bar (right chest).
    bl, bn = sampler.ray((8.6 if not fem else 9.2, -40.0, 134.0 if not fem else 136.0), (0, 1, 0))
    M = frame_at(bl, bn)
    shield = [(-2.0, 2.4), (2.0, 2.4), (2.2, 0.4), (0.0, -2.7), (-2.2, 0.4)]
    part = fb.make_prism(GOLD, [(x / 100, z / 100) for x, z in shield], 0.0, 0.006)
    bmesh.ops.transform(part.bm, matrix=M @ Matrix.Translation((0, -0.35, 0)), verts=part.bm.verts)
    props.add_bm(part.bm, anchor=bl)
    props.add('cylinder', (0.85, 0.62, 0.12), M @ Matrix.Translation((0, -0.8, 0.1)) @ rot_x(90), (2.0, 2.0, 0.3),
              anchor=bl, segments=10)
    nl, nn = sampler.ray((-8.6, -40.0, 133.0 if not fem else 135.5), (0, 1, 0))
    props.add('cube', GOLD, frame_at(nl, nn) @ Matrix.Translation((0, -0.2, 0)), (5.2, 0.45, 1.15), anchor=nl)
    # Pocket flaps with a button (shirt colour, tinted with it).
    for sx in (1, -1):
        if fem:
            break
        pl, pn = sampler.ray((sx * 8.4, -40.0, 128.5), (0, 1, 0))
        P = frame_at(pl, pn)
        props.add('cube', UNIFORM, P @ Matrix.Translation((0, -0.1, 0)), (7.0, 0.7, 2.4), tint=True, anchor=pl)
        props.add('sphere', NAVY, P @ Matrix.Translation((0, -0.6, -0.5)), (0.8, 0.5, 0.8), anchor=pl, segments=8,
                  rings=4)
    # Placket buttons.
    for z in (108, 115, 122, 129, 136):
        l, n = sampler.ray((0.0, -40.0, z), (0, 1, 0))
        props.add('sphere', NAVY, frame_at(l, n), (0.8, 0.5, 0.8), anchor=l, segments=8, rings=4)
    # Shoulder patches and epaulettes.
    for side, sx in (("l", 1), ("r", -1)):
        s, e = J[f"upperarm_{side}"], J[f"lowerarm_{side}"]
        mid = s + (e - s) * 0.3
        out = unit(np.cross(e - s, (0, 1, 0))) * sx
        out = out if out[0] * sx > 0 else -out
        l, n = sampler.ray(tuple(mid + out * 30), tuple(-out))
        P = frame_at(l, n, up=tuple(unit(s - e)))
        props.add('sphere', NAVY, P, (4.6, 0.8, 5.4), anchor=l, segments=14, rings=6)
        props.add('torus', GOLD, P @ Matrix.Translation((0, -0.25, 0)) @ rot_x(90) @ Matrix.Diagonal((1, 1.17, 1, 1)),
                  (4.7, 4.7, 4.7), anchor=l, minor=0.06, segments=16, rings=4)
        props.add('sphere', GOLD, P @ Matrix.Translation((0, -0.45, 0.4)), (1.8, 0.3, 1.8), anchor=l, segments=8,
                  rings=4)
        top_l, top_n = sampler.ray((sx * 12.5, 1.0, 158.0), (0, 0, -1))
        E = frame_at(top_l, top_n, up=(sx, 0, 0))
        props.add('cube', UNIFORM, E @ Matrix.Translation((0, -0.1, 0)), (3.2, 0.6, 11.0), tint=True, anchor=top_l)
        props.add('sphere', GOLD, E @ Matrix.Translation((0, -0.5, 4.2)), (0.9, 0.5, 0.9), anchor=top_l, segments=8,
                  rings=4)
    # Belt kit: buckle, holster with the pistol grip (right hip), radio (left front), cuff pouch (back).
    bl, bn = sampler.ray((0, -40.0, 99.0), (0, 1, 0))
    props.add('cube', SILVER, frame_at(bl, bn), (5.0, 0.8, 3.8), anchor=bl)
    hl, hn = sampler.ray((-40.0, -1.5, 99.0), (1, 0, 0))
    H = frame_at(hl, hn) @ Matrix.Translation((0, -2.4, -5.0)) @ rot_x(-6)
    props.add('cube', NEAR_BLACK, H, (4.6, 4.4, 14.0), anchor=hl, bevel=0.008)
    props.add('cube', (0.15, 0.15, 0.17), H @ Matrix.Translation((0, 0.4, 8.4)) @ rot_x(-15), (3.0, 5.6, 4.2), anchor=hl,
              bevel=0.006)
    rl, rn = sampler.ray((12.5, -40.0, 99.0), (0, 1, 0))
    R = frame_at(rl, rn) @ Matrix.Translation((0, -1.8, 0.5))
    props.add('cube', (0.10, 0.10, 0.11), R, (5.0, 3.2, 9.0), anchor=rl, bevel=0.006)
    props.add('cylinder', NEAR_BLACK, R @ Matrix.Translation((1.4, 0, 7.0)), (0.8, 0.8, 5.5), anchor=rl, segments=8)
    props.add('cube', (0.9, 0.2, 0.15), R @ Matrix.Translation((-1.2, -1.65, 3.0)), (1.2, 0.3, 0.8), anchor=rl)
    for x in (-7.0, 7.5):
        cl, cn = sampler.ray((x, 40.0, 99.0), (0, -1, 0))
        props.add('cube', NEAR_BLACK, frame_at(cl, cn, up=(0, 0, 1)) @ Matrix.Translation((0, -1.6, -0.5)),
                  (6.4 if x < 0 else 5.0, 3.4, 5.4), anchor=cl, bevel=0.008)
    # Lapel mic on the left shoulder and a cap badge.
    ml, mn = sampler.ray((12.0, -40.0, 139.0), (0, 1, 0.2))
    props.add('cube', NEAR_BLACK, frame_at(ml, mn) @ Matrix.Translation((0, -1.0, 0)), (2.6, 2.0, 4.2), anchor=ml)
    cl, cn = sampler.ray(an.hp(0, -40.0, 179.8), (0, 1, 0))
    C = frame_at(cl, cn)
    star = [(1.9 * math.sin(a) * (1.0 if i % 2 == 0 else 0.45), 1.9 * math.cos(a) * (1.0 if i % 2 == 0 else 0.45))
            for i, a in enumerate(np.linspace(0, 2 * math.pi, 10, endpoint=False))]
    for i in range(5):
        tri = [star[(2 * i - 1) % 10], star[2 * i], star[(2 * i + 1) % 10], (0.0, 0.0)]
        part = fb.make_prism(GOLD, [(x / 100, z / 100) for x, z in tri], 0.0, 0.005)
        bmesh.ops.transform(part.bm, matrix=C @ Matrix.Translation((0, -0.3, 0)), verts=part.bm.verts)
        props.add_bm(part.bm, bone="head")


def civilian_props(props, sampler, spec):
    outfit = spec["outfit"]
    if outfit == "hoodie":
        for sx in (1, -1):
            l, n = sampler.ray((sx * 3.2, -40.0, 142.0), (0, 1, 0))
            props.add('cylinder', OFFWHITE, frame_at(l, n) @ Matrix.Translation((0, -0.6, -5.0)), (0.6, 0.6, 10.0),
                      anchor=l, segments=6)
    if outfit == "suit":
        for z in (104, 112):
            l, n = sampler.ray((0.0, -40.0, z), (0, 1, 0))
            props.add('sphere', NEAR_BLACK, frame_at(l, n), (1.0, 0.5, 1.0), anchor=l, segments=8, rings=4)
        l, n = sampler.ray((9.5, -40.0, 131.0), (0, 1, 0))
        props.add('cube', WHITE, frame_at(l, n) @ Matrix.Translation((0, -0.2, 0.6)) @ rot_y(-10), (3.0, 0.4, 1.2),
                  anchor=l)
    if outfit == "cardigan":
        for z in (108, 115, 122):
            for sx in (1, -1):
                l, n = sampler.ray((sx * 4.9, -40.0, z), (0, 1, 0))
                props.add('sphere', (0.92, 0.85, 0.65), frame_at(l, n), (1.0, 0.55, 1.0), anchor=l, segments=8, rings=4)
    if outfit == "jacket":
        for sx in (1, -1):
            l, n = sampler.ray((sx * 3.3, -40.0, 118.0), (0, 1, 0))
            props.add('cube', SILVER, frame_at(l, n) @ Matrix.Translation((0, -0.2, 0)), (0.5, 0.5, 44.0), anchor=l)
    if outfit == "overalls":
        for sx in (1, -1):
            l, n = sampler.ray((sx * 8.5, -40.0, 125.5), (0, 1, 0))
            props.add('cube', SILVER, frame_at(l, n) @ Matrix.Translation((0, -0.3, 0)), (2.4, 0.6, 1.8), anchor=l)
        l, n = sampler.ray((0, -40.0, 115.0), (0, 1, 0))
        props.add('cube', DENIM_DARK, frame_at(l, n) @ Matrix.Translation((0, -0.2, 0)), (8.0, 0.4, 7.0), anchor=l)
    if outfit == "apron":
        l, n = sampler.ray((0, -40.0, 97.0), (0, 1, 0))
        props.add('cube', (0.85, 0.85, 0.82), frame_at(l, n) @ Matrix.Translation((0, -0.2, 0)), (14.0, 0.5, 6.5),
                  anchor=l)
    if "bag" in spec["face"]:
        # Handbag on the left shoulder: strap over the shoulder, bag at the hip.
        top_l, _n = sampler.ray((13.0, 0.0, 158.0), (0, 0, -1))
        bag_c = Vector((19.0, 1.0, 103.0))
        props.add('cube', (0.55, 0.28, 0.14), Matrix.Translation(bag_c) @ rot_y(8), (6.0, 16.0, 13.0),
                  anchor=Vector((17.0, 0.0, 108.0)), bevel=0.012)
        for a, b in ((top_l + Vector((0, -2.5, 0.5)), bag_c + Vector((-1.0, -6.0, 6.0))),
                     (top_l + Vector((0, 3.0, 0.5)), bag_c + Vector((-1.0, 6.0, 6.0)))):
            d = b - a
            M = Matrix.Translation((a + b) / 2) @ Vector((0, 0, 1)).rotation_difference(d).to_matrix().to_4x4()
            props.add('cube', (0.45, 0.22, 0.10), M, (1.4, 0.5, d.length), anchor=Vector((15.0, 0.0, 130.0)))


# --------------------------------------------------------------------------------------
# Assemble
# --------------------------------------------------------------------------------------
def build_character(spec, weight_rig, arm):
    name = spec["name"]
    an, layers = character_layers(spec)
    body, solid = body_mesh(name, layers)
    skin_body(body, layers, weight_rig, an)
    sampler = BodySampler(body)

    hands = []
    for side in ("l", "r"):
        h = hand_mesh(side, SKIN_TONES[spec["skin"]])
        weight_hand(h, side, sampler)
        hands.append(h)

    props = Props(sampler)
    face_props(props, sampler, spec, an)
    if spec["outfit"] == "officer":
        officer_props(props, sampler, spec["build"].get("female", False), an)
    civilian_props(props, sampler, spec)
    extras = props.build(name + "_Props")

    count = lambda o: sum(len(p.vertices) - 2 for p in o.data.polygons)  # noqa: E731
    body_tris, hand_tris, prop_tris = count(body), sum(count(h) for h in hands), count(extras)
    for obj in hands + [extras]:
        obj.data.color_attributes.active_color = obj.data.color_attributes["Col"]
    fb.set_active(body)
    for obj in hands + [extras]:
        obj.select_set(True)
    bpy.ops.object.join()
    if "fto_layer" in body.data.attributes:
        body.data.attributes.remove(body.data.attributes["fto_layer"])
    colors = body.data.color_attributes
    colors.active_color = colors["Col"]
    colors.render_color_index = colors.active_color_index
    for g in list(body.vertex_groups):
        if g.name not in fto_rig.BONE_NAMES:
            body.vertex_groups.remove(g)
    fto_rig.bind_to(body, arm)
    tris = sum(len(p.vertices) - 2 for p in body.data.polygons)
    print(f"FTO: {name}: {tris} triangles ({body_tris} body, {hand_tris} hands, {prop_tris} props)")
    return body


def export_character(body, arm, path):
    # The armature must be called "Armature" so Unreal drops the node instead of adding a root bone.
    holder = bpy.data.objects.get("Armature")
    if holder and holder != arm:
        holder.name = "Armature_hold"
    arm.name = "Armature"
    fb.export_fbx(path, [arm, body], with_animation=False)


def render_turnaround(body, arm, preview_dir, name):
    cam = bpy.context.scene.camera or fb.setup_preview((240, 330))
    hidden = [o for o in bpy.context.scene.objects if o.type == 'MESH' and o != body]
    for o in hidden:
        o.hide_render = True
    x0 = body.matrix_world.translation.x / fb.UNIT
    target = (x0, 0, 0.93)
    paths = []
    for view, loc in (("front", (x0, -6, 1.05)), ("side", (x0 + 6, 0, 1.05)), ("back", (x0, 6, 1.05)),
                      ("face", (x0 + 0.9, -3.0, 1.7))):
        path = os.path.join(preview_dir, f"_{name}_{view}.png")
        if view == "face":
            fb.render_view(path, cam, loc, (x0, 0, 1.68), 0.42)
        else:
            fb.render_view(path, cam, loc, target, 2.05)
        paths.append(path)
    for o in hidden:
        o.hide_render = False
    return paths


def main():
    args = fb.script_args()
    out_root = os.path.abspath(args.get("out", "Art/Source/Characters"))
    preview_dir = args.get("preview")
    only = set(args["only"].split(",")) if isinstance(args.get("only"), str) else None

    fb.reset_scene(fps=30)
    weight_rig = fto_rig.build_weight_armature()
    by_folder = {}
    for i, spec in enumerate(CAST):
        if only and spec["name"] not in only:
            continue
        holder = bpy.data.objects.get("Armature")
        if holder:
            holder.name = f"Armature_{holder.children[0].name if holder.children else 'x'}"
        arm = fto_rig.build_mannequin_armature("Armature")
        body = build_character(spec, weight_rig, arm)
        folder = os.path.join(out_root, spec["folder"])
        export_character(body, arm, os.path.join(folder, spec["name"] + ".fbx"))
        by_folder.setdefault(spec["folder"], []).append((spec, body, arm))

    if preview_dir:
        preview_dir = os.path.abspath(preview_dir)
        os.makedirs(preview_dir, exist_ok=True)
        fb.setup_preview((240, 330))
        bpy.context.scene.display.shading.show_cavity = False
        for folder, items in by_folder.items():
            for spec, body, arm in items:
                paths = render_turnaround(body, arm, preview_dir, spec["name"])
                contact_row(paths, os.path.join(preview_dir, f"{spec['name']}.png"))

    # One editable .blend per folder, the characters side by side (armatures keep their per-character names).
    for folder, items in by_folder.items():
        for i, (spec, body, arm) in enumerate(items):
            arm.name = f"Armature_{spec['name']}"
            arm.location.x = i * 80.0
    weight_rig.hide_set(True)
    for folder, items in by_folder.items():
        keep = {o for _s, b, a in items for o in (b, a)} | {weight_rig}
        for o in bpy.context.scene.objects:
            o.hide_viewport = o not in keep
        blend = os.path.join(out_root, folder, "Officers.blend" if folder == "Officer" else "Civilians.blend")
        bpy.ops.wm.save_as_mainfile(filepath=blend, copy=True)
        print(f"FTO: saved {blend}")


def contact_row(paths, out_path, remove=True):
    images = [bpy.data.images.load(p) for p in paths]
    h = max(img.size[1] for img in images)
    w = sum(img.size[0] for img in images)
    sheet = np.ones((h, w, 4), dtype=np.float32)
    x = 0
    for img in images:
        iw, ih = img.size
        px = np.array(img.pixels[:], dtype=np.float32).reshape(ih, iw, 4)
        sheet[h - ih:h, x:x + iw] = px
        x += iw
    out = bpy.data.images.new("row", w, h, alpha=False)
    out.pixels[:] = sheet.ravel()
    out.filepath_raw = out_path
    out.file_format = 'PNG'
    out.save()
    for img in images:
        bpy.data.images.remove(img)
    bpy.data.images.remove(out)
    if remove:
        for p in paths:
            os.remove(p)


if __name__ == "__main__":
    main()
