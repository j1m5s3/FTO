"""
Builds FTO's citizens and the classic cartoon burglar, all on the officer's skeleton so they
share every animation clip.

  blender -b --factory-startup -P Tools/Blender/build_civilians.py -- --out Art/Source/Characters/Civilians [--preview <dir>]

Outputs SK_Civilian_01..N.fbx and SK_Suspect.fbx. The shirt (torso + sleeves) has vertex alpha 1,
so the game gives every pedestrian a random shirt colour on top of these baked variations.
"""
import os
import sys

sys.path.append(os.path.dirname(os.path.abspath(__file__)))
import fto_blender as fb  # noqa: E402
from build_officer import BONES, BONE_NAMES, BLACK, SKIN  # noqa: E402

SHIRT = (0.85, 0.85, 0.85)   # neutral: tinted in game

SKIN_TONES = [
    SKIN,
    (0.80, 0.56, 0.40),
    (0.58, 0.38, 0.25),
    (0.36, 0.23, 0.15),
    (0.98, 0.84, 0.72),
]
HAIR = {
    "black": (0.05, 0.04, 0.04),
    "brown": (0.30, 0.17, 0.08),
    "blonde": (0.95, 0.80, 0.40),
    "ginger": (0.85, 0.40, 0.12),
    "grey": (0.70, 0.70, 0.72),
    "pink": (1.00, 0.45, 0.75),
}
PANTS = {
    "jeans": (0.20, 0.30, 0.55),
    "khaki": (0.76, 0.66, 0.45),
    "black": (0.08, 0.08, 0.09),
    "grey": (0.45, 0.46, 0.50),
    "red": (0.70, 0.12, 0.12),
    "green": (0.25, 0.45, 0.25),
}
SHOES = {
    "sneaker": (0.95, 0.95, 0.95),
    "brown": (0.35, 0.20, 0.10),
    "black": BLACK,
    "red": (0.85, 0.15, 0.15),
}

# name, skin, hair style, hair colour, pants, shoes, extras, belly (torso width scale)
CIVILIANS = [
    ("01", 0, "short", "brown", "jeans", "sneaker", [], 1.00),
    ("02", 2, "bun", "black", "khaki", "brown", ["glasses"], 0.92),
    ("03", 1, "bald", None, "grey", "black", ["beard"], 1.12),
    ("04", 4, "bob", "blonde", "red", "sneaker", ["bag"], 0.90),
    ("05", 3, "spiky", "black", "black", "red", [], 0.95),
    ("06", 0, "cap", "ginger", "jeans", "sneaker", ["beard"], 1.05),
    ("07", 2, "beanie", "grey", "green", "brown", ["glasses"], 1.18),
    ("08", 1, "bob", "pink", "black", "black", ["bag"], 0.94),
]


def head_parts(skin, hair_style, hair_color, extras):
    P = fb.make_part
    parts = [
        P('sphere', skin, loc=(0, 0, 1.55), scale=(0.50, 0.46, 0.48), bone="head", segments=20, rings=12),
        P('sphere', tuple(c * 0.92 for c in skin), loc=(0, -0.235, 1.53), scale=(0.08, 0.08, 0.08), bone="head"),
        P('sphere', BLACK, loc=(0.085, -0.2, 1.60), scale=(0.05, 0.03, 0.065), bone="head"),
        P('sphere', BLACK, loc=(-0.085, -0.2, 1.60), scale=(0.05, 0.03, 0.065), bone="head"),
        # little smile
        P('cube', (0.45, 0.12, 0.12), loc=(0, -0.222, 1.47), scale=(0.09, 0.03, 0.02), bone="head", bevel=0.008),
    ]
    hair = HAIR.get(hair_color, BLACK)
    if hair_style == "short":
        parts.append(P('sphere', hair, loc=(0, 0.02, 1.62), scale=(0.53, 0.50, 0.40), bone="head"))
    elif hair_style == "bun":
        parts.append(P('sphere', hair, loc=(0, 0.02, 1.62), scale=(0.53, 0.50, 0.40), bone="head"))
        parts.append(P('sphere', hair, loc=(0, 0.12, 1.83), scale=(0.2, 0.2, 0.2), bone="head"))
    elif hair_style == "bob":
        parts.append(P('sphere', hair, loc=(0, 0.04, 1.58), scale=(0.56, 0.54, 0.50), bone="head"))
    elif hair_style == "spiky":
        parts.append(P('sphere', hair, loc=(0, 0.02, 1.64), scale=(0.52, 0.49, 0.34), bone="head"))
        for i, (x, y) in enumerate(((-0.12, 0.0), (0.0, -0.05), (0.12, 0.0), (-0.06, 0.1), (0.06, 0.1))):
            parts.append(P('cone', hair, loc=(x, y, 1.84), rot=(0, (i - 2) * 12, 0), scale=(0.1, 0.1, 0.18), bone="head", segments=8))
    elif hair_style == "cap":
        parts.append(P('sphere', hair, loc=(0, 0.04, 1.58), scale=(0.52, 0.50, 0.42), bone="head"))
        parts.append(P('sphere', (0.15, 0.35, 0.85), loc=(0, 0.0, 1.68), scale=(0.52, 0.50, 0.36), bone="head"))
        parts.append(P('cube', (0.15, 0.35, 0.85), loc=(0, -0.24, 1.70), rot=(-8, 0, 0), scale=(0.3, 0.2, 0.025), bone="head", bevel=0.01))
    elif hair_style == "beanie":
        parts.append(P('sphere', (0.75, 0.2, 0.2), loc=(0, 0.01, 1.66), scale=(0.53, 0.50, 0.40), bone="head"))
        parts.append(P('sphere', (0.95, 0.95, 0.95), loc=(0, 0.01, 1.87), scale=(0.12, 0.12, 0.12), bone="head"))

    if "glasses" in extras:
        for x in (0.085, -0.085):
            parts.append(P('cylinder', BLACK, loc=(x, -0.215, 1.60), rot=(90, 0, 0), scale=(0.12, 0.12, 0.02), bone="head", segments=12))
        parts.append(P('cube', BLACK, loc=(0, -0.225, 1.61), scale=(0.06, 0.01, 0.015), bone="head"))
    if "beard" in extras:
        beard = HAIR.get(hair_color, (0.3, 0.2, 0.1))
        parts.append(P('sphere', beard, loc=(0, -0.13, 1.42), scale=(0.34, 0.26, 0.2), bone="head"))
    return parts


def body_parts(skin, pants, shoes, belly, shirt=SHIRT, tint=True, stripes=None):
    P = fb.make_part
    parts = [
        P('sphere', shirt, loc=(0, 0, 1.04), scale=(0.58 * belly, 0.48 * belly, 0.6), bone="spine", tint=tint, segments=20, rings=14, stripes=stripes),
        P('sphere', pants, loc=(0, 0, 0.74), scale=(0.44, 0.34, 0.30), bone="pelvis"),
    ]
    for side, sx in (("l", 1.0), ("r", -1.0)):
        parts += [
            P('sphere', shirt, loc=(0.32 * sx, 0, 1.26), scale=(0.17, 0.17, 0.17), bone=f"upperarm_{side}", tint=tint, stripes=stripes),
            P('cylinder', shirt, loc=(0.35 * sx, 0, 1.15), scale=(0.14, 0.14, 0.24), bone=f"upperarm_{side}", tint=tint, stripes=stripes),
            P('cylinder', skin, loc=(0.38 * sx, 0, 0.91), scale=(0.115, 0.115, 0.24), bone=f"lowerarm_{side}"),
            P('sphere', skin, loc=(0.395 * sx, 0, 0.76), scale=(0.13, 0.12, 0.14), bone=f"hand_{side}"),
            P('cylinder', pants, loc=(0.13 * sx, 0, 0.56), scale=(0.19, 0.19, 0.32), bone=f"thigh_{side}"),
            P('cylinder', pants, loc=(0.13 * sx, 0, 0.26), scale=(0.16, 0.16, 0.34), bone=f"calf_{side}"),
            P('cube', shoes, loc=(0.13 * sx, -0.04, 0.055), scale=(0.16, 0.28, 0.11), bone=f"foot_{side}", bevel=0.035),
        ]
    return parts


def civilian(spec):
    name, skin_i, hair_style, hair_color, pants, shoes, extras, belly = spec
    skin = SKIN_TONES[skin_i]
    parts = head_parts(skin, hair_style, hair_color, extras) + body_parts(skin, PANTS[pants], SHOES[shoes], belly)
    if "bag" in extras:
        parts.append(fb.make_part('cube', (0.55, 0.30, 0.15), loc=(0.36, 0.1, 1.0), rot=(0, 10, 0), scale=(0.08, 0.22, 0.26), bone="spine", bevel=0.02))
        parts.append(fb.make_part('cube', (0.45, 0.22, 0.10), loc=(0.18, 0.0, 1.2), rot=(0, 45, 0), scale=(0.03, 0.46, 0.03), bone="spine"))
    return f"SK_Civilian_{name}", parts


def suspect():
    """Striped jumper, eye mask, black beanie and a bag of loot. Very subtle."""
    P = fb.make_part
    skin = SKIN_TONES[1]
    parts = head_parts(skin, "bald", None, [])
    parts += [
        P('sphere', BLACK, loc=(0, 0.01, 1.64), scale=(0.53, 0.50, 0.38), bone="head"),              # beanie
        P('cylinder', BLACK, loc=(0, -0.005, 1.60), scale=(0.51, 0.47, 0.07), bone="head", segments=20),  # eye mask
        P('sphere', (0.95, 0.95, 0.95), loc=(0.085, -0.225, 1.60), scale=(0.035, 0.02, 0.035), bone="head"),
        P('sphere', (0.95, 0.95, 0.95), loc=(-0.085, -0.225, 1.60), scale=(0.035, 0.02, 0.035), bone="head"),
    ]
    parts += body_parts(skin, PANTS["black"], SHOES["black"], 1.05, shirt=(0.95, 0.95, 0.95), tint=False, stripes=(BLACK, 0.07))
    # Loot sack slung over the right shoulder.
    parts.append(P('sphere', (0.72, 0.60, 0.40), loc=(-0.32, 0.26, 1.3), scale=(0.34, 0.3, 0.38), bone="upperarm_r"))
    parts.append(P('cylinder', (0.55, 0.45, 0.28), loc=(-0.32, 0.22, 1.52), scale=(0.08, 0.08, 0.1), bone="upperarm_r"))
    return "SK_Suspect", parts


def main():
    args = fb.script_args()
    out_dir = os.path.abspath(args.get("out", "Art/Source/Characters/Civilians"))
    preview_dir = args.get("preview")

    builds = [civilian(spec) for spec in CIVILIANS] + [suspect()]
    for name, parts in builds:
        fb.reset_scene(fps=30)
        arm = fb.build_armature(BONES)
        body = fb.build_mesh_object(name, parts, BONE_NAMES)
        fb.bind(body, arm)
        fb.export_fbx(os.path.join(out_dir, f"{name}.fbx"), [arm, body], with_animation=False)

        if preview_dir:
            cam = fb.setup_preview((300, 360))
            fb.render_view(os.path.join(os.path.abspath(preview_dir), f"{name}.png"), cam, (2.2, -5.5, 1.1), (0, 0, 0.95), 2.2)


main()
