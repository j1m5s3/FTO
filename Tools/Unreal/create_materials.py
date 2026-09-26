"""
Creates FTO's master material(s). Run headless:

  UnrealEditor-Cmd.exe FTO.uproject -run=pythonscript -script="<repo>/Tools/Unreal/create_materials.py"

M_FTOBase
  BaseColor = lerp(VertexColor.rgb, VertexColor.rgb * Color, VertexColor.a)
  Emissive  = BaseColor * Emissive
  Scorch    = 0-1 chars it all towards soot black (burnt-out cars)
  UseInstanceColor = 1 takes Color from per-instance custom data 0-2 instead (instanced city meshes)
M_FTOGlass   tinted see-through glass (vehicle windows, shopfronts)
MI_FTOGlow   M_FTOBase glowing in its vertex colours (lights, dials, screens)
MI_FTOCity   M_FTOBase tinted per instance (the building kit)
MI_FTOCityInterior  the same, a little self-lit so rooms read clearly from the street
M_FTODecal   deferred decal: a bullet hole (dark pit, chipped rim) that fades out over its lifetime

Engine primitives have no vertex colours (read as white), so they simply take `Color`.
Blender-made assets bake flat colours into vertex colours; alpha = 1 marks "tintable"
regions (uniforms, car paint) that pick up `Color` at runtime.
"""
import os

import unreal

PACKAGE_DIR = "/Game/FTO/Materials"
mel = unreal.MaterialEditingLibrary
eal = unreal.EditorAssetLibrary


def scalar_param(mat, name, value, x, y):
    node = mel.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, x, y)
    node.set_editor_property("parameter_name", name)
    node.set_editor_property("default_value", value)
    return node


def build_base_material(name, two_sided=False):
    path = f"{PACKAGE_DIR}/{name}"
    if eal.does_asset_exist(path):
        eal.delete_asset(path)

    tools = unreal.AssetToolsHelpers.get_asset_tools()
    mat = tools.create_asset(name, PACKAGE_DIR, unreal.Material, unreal.MaterialFactoryNew())
    mat.set_editor_property("two_sided", two_sided)
    # The city is instanced (and Nanite) meshes and characters are skeletal; without these flags
    # the engine silently falls back to its default grey material in game.
    for usage in ("used_with_instanced_static_meshes", "used_with_skeletal_mesh", "used_with_morph_targets",
                  "used_with_nanite"):
        mat.set_editor_property(usage, True)

    vc = mel.create_material_expression(mat, unreal.MaterialExpressionVertexColor, -900, 0)

    color = mel.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, -1200, 200)
    color.set_editor_property("parameter_name", "Color")
    color.set_editor_property("default_value", unreal.LinearColor(1.0, 1.0, 1.0, 1.0))

    # City instances carry their own tint (per-instance custom data 0-2), so one instanced mesh can
    # paint every building a different colour. UseInstanceColor picks between the two.
    instance_color = mel.create_material_expression(mat, unreal.MaterialExpressionPerInstanceCustomData3Vector, -1200, 350)
    instance_color.set_editor_property("data_index", 0)
    instance_color.set_editor_property("const_default_value", unreal.LinearColor(1.0, 1.0, 1.0, 1.0))
    use_instance = scalar_param(mat, "UseInstanceColor", 0.0, -1200, 500)
    pick = mel.create_material_expression(mat, unreal.MaterialExpressionLinearInterpolate, -900, 300)
    mel.connect_material_expressions(color, "", pick, "A")
    mel.connect_material_expressions(instance_color, "", pick, "B")
    mel.connect_material_expressions(use_instance, "", pick, "Alpha")

    tinted = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, -600, 150)
    mel.connect_material_expressions(vc, "", tinted, "A")
    mel.connect_material_expressions(pick, "", tinted, "B")

    base = mel.create_material_expression(mat, unreal.MaterialExpressionLinearInterpolate, -350, 0)
    mel.connect_material_expressions(vc, "", base, "A")
    mel.connect_material_expressions(tinted, "", base, "B")
    mel.connect_material_expressions(vc, "A", base, "Alpha")

    # Scorch 0-1 chars the whole surface towards soot black (a burnt-out car), paint, trim and all.
    scorch = scalar_param(mat, "Scorch", 0.0, -350, 250)
    soot = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, -200, 100)
    soot.set_editor_property("const_b", 0.08)
    mel.connect_material_expressions(base, "", soot, "A")
    burnt = mel.create_material_expression(mat, unreal.MaterialExpressionLinearInterpolate, -100, 0)
    mel.connect_material_expressions(base, "", burnt, "A")
    mel.connect_material_expressions(soot, "", burnt, "B")
    mel.connect_material_expressions(scorch, "", burnt, "Alpha")
    mel.connect_material_property(burnt, "", unreal.MaterialProperty.MP_BASE_COLOR)

    rough = scalar_param(mat, "Roughness", 0.75, -600, 400)
    mel.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)

    spec = scalar_param(mat, "Specular", 0.35, -600, 500)
    mel.connect_material_property(spec, "", unreal.MaterialProperty.MP_SPECULAR)

    # Glow in the surface's own colour, so one glowing instance lights headlights white,
    # taillights red and screens blue straight from the vertex colours.
    glow = scalar_param(mat, "Emissive", 0.0, -900, 600)
    emissive = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, -350, 650)
    mel.connect_material_expressions(base, "", emissive, "A")
    mel.connect_material_expressions(glow, "", emissive, "B")
    mel.connect_material_property(emissive, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

    mel.recompile_material(mat)
    eal.save_loaded_asset(mat)
    unreal.log(f"FTO: created {path}")
    return mat


def build_glass_material(name):
    """Tinted see-through glass for car windows and shop fronts (lit translucency so it catches light)."""
    path = f"{PACKAGE_DIR}/{name}"
    if eal.does_asset_exist(path):
        eal.delete_asset(path)

    tools = unreal.AssetToolsHelpers.get_asset_tools()
    mat = tools.create_asset(name, PACKAGE_DIR, unreal.Material, unreal.MaterialFactoryNew())
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    mat.set_editor_property("translucency_lighting_mode", unreal.TranslucencyLightingMode.TLM_SURFACE)
    mat.set_editor_property("two_sided", True)
    for usage in ("used_with_instanced_static_meshes", "used_with_skeletal_mesh"):
        mat.set_editor_property(usage, True)

    color = mel.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, -600, 0)
    color.set_editor_property("parameter_name", "Color")
    color.set_editor_property("default_value", unreal.LinearColor(0.18, 0.32, 0.45, 1.0))
    mel.connect_material_property(color, "", unreal.MaterialProperty.MP_BASE_COLOR)

    opacity = scalar_param(mat, "Opacity", 0.32, -600, 200)
    mel.connect_material_property(opacity, "", unreal.MaterialProperty.MP_OPACITY)
    rough = scalar_param(mat, "Roughness", 0.05, -600, 300)
    mel.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    spec = scalar_param(mat, "Specular", 0.9, -600, 400)
    mel.connect_material_property(spec, "", unreal.MaterialProperty.MP_SPECULAR)

    mel.recompile_material(mat)
    eal.save_loaded_asset(mat)
    unreal.log(f"FTO: created {path}")
    return mat


def build_decal_material(name):
    """
    Deferred decal for bullet holes and scuffs: a dark pit with a chipped, paler rim, soft at the edge, fading out
    over the decal's lifetime (UDecalComponent::SetFadeOut). Projected along the decal's X; UV 0-1 across it.
    """
    path = f"{PACKAGE_DIR}/{name}"
    if eal.does_asset_exist(path):
        eal.delete_asset(path)

    tools = unreal.AssetToolsHelpers.get_asset_tools()
    mat = tools.create_asset(name, PACKAGE_DIR, unreal.Material, unreal.MaterialFactoryNew())
    mat.set_editor_property("material_domain", unreal.MaterialDomain.MD_DEFERRED_DECAL)
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)

    # Distance from the middle of the decal (0 at the centre, 1 at the edge of the square).
    uv = mel.create_material_expression(mat, unreal.MaterialExpressionTextureCoordinate, -1400, 0)
    middle = mel.create_material_expression(mat, unreal.MaterialExpressionConstant2Vector, -1400, 150)
    middle.set_editor_property("r", 0.5)
    middle.set_editor_property("g", 0.5)
    dist = mel.create_material_expression(mat, unreal.MaterialExpressionDistance, -1200, 50)
    mel.connect_material_expressions(uv, "", dist, "A")
    mel.connect_material_expressions(middle, "", dist, "B")
    two = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, -1050, 50)
    two.set_editor_property("const_b", 2.0)
    mel.connect_material_expressions(dist, "", two, "A")

    # The pit (dark, the inner 40%) inside a paler chipped rim, which fades to nothing at the edge.
    pit_mask = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, -900, -100)
    pit_mask.set_editor_property("const_b", 2.5)
    mel.connect_material_expressions(two, "", pit_mask, "A")
    pit = mel.create_material_expression(mat, unreal.MaterialExpressionSaturate, -780, -100)
    mel.connect_material_expressions(pit_mask, "", pit, "")
    dark = mel.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, -900, -300)
    dark.set_editor_property("parameter_name", "Color")
    dark.set_editor_property("default_value", unreal.LinearColor(0.02, 0.02, 0.02, 1.0))
    rim = mel.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, -900, -450)
    rim.set_editor_property("parameter_name", "Rim")
    rim.set_editor_property("default_value", unreal.LinearColor(0.55, 0.52, 0.48, 1.0))
    color = mel.create_material_expression(mat, unreal.MaterialExpressionLinearInterpolate, -600, -300)
    mel.connect_material_expressions(dark, "", color, "A")
    mel.connect_material_expressions(rim, "", color, "B")
    mel.connect_material_expressions(pit, "", color, "Alpha")
    mel.connect_material_property(color, "", unreal.MaterialProperty.MP_BASE_COLOR)

    edge = mel.create_material_expression(mat, unreal.MaterialExpressionOneMinus, -900, 100)
    mel.connect_material_expressions(two, "", edge, "")
    soft = mel.create_material_expression(mat, unreal.MaterialExpressionSaturate, -780, 100)
    mel.connect_material_expressions(edge, "", soft, "")
    sharpen = mel.create_material_expression(mat, unreal.MaterialExpressionPower, -650, 100)
    sharpen.set_editor_property("const_exponent", 0.35)
    mel.connect_material_expressions(soft, "", sharpen, "Base")
    lifetime = mel.create_material_expression(mat, unreal.MaterialExpressionDecalLifetimeOpacity, -650, 250)
    opacity = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, -450, 150)
    mel.connect_material_expressions(sharpen, "", opacity, "A")
    mel.connect_material_expressions(lifetime, "", opacity, "B")
    mel.connect_material_property(opacity, "", unreal.MaterialProperty.MP_OPACITY)
    rough = scalar_param(mat, "Roughness", 0.9, -450, 350)
    mel.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)

    mel.recompile_material(mat)
    eal.save_loaded_asset(mat)
    unreal.log(f"FTO: created {path}")
    return mat


def build_instance(name, parent, scalars):
    """Constant instance of the base material with some scalar parameters set."""
    path = f"{PACKAGE_DIR}/{name}"
    if eal.does_asset_exist(path):
        eal.delete_asset(path)

    tools = unreal.AssetToolsHelpers.get_asset_tools()
    mi = tools.create_asset(name, PACKAGE_DIR, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    mel.set_material_instance_parent(mi, parent)
    for param, value in scalars.items():
        mel.set_material_instance_scalar_parameter_value(mi, param, value)
    eal.save_loaded_asset(mi)
    unreal.log(f"FTO: created {path}")
    return mi


# FTO_MATERIALS=M_FTODecal (comma-separated) builds just those; the rest are left alone (rebuilding the base
# material churns every asset that uses it).
ONLY = set(filter(None, os.environ.get("FTO_MATERIALS", "").split(",")))


def wanted(name):
    return not ONLY or name in ONLY


if wanted("M_FTOBase") or wanted("MI_FTOGlow") or wanted("MI_FTOCity") or wanted("MI_FTOCityInterior"):
    base_material = build_base_material("M_FTOBase") if wanted("M_FTOBase") else eal.load_asset(f"{PACKAGE_DIR}/M_FTOBase")
    if wanted("MI_FTOGlow"):
        build_instance("MI_FTOGlow", base_material, {"Emissive": 2.5})                  # lights, dials, screens
    if wanted("MI_FTOCity"):
        build_instance("MI_FTOCity", base_material, {"UseInstanceColor": 1.0})          # the building kit, tinted per instance
    if wanted("MI_FTOCityInterior"):
        build_instance("MI_FTOCityInterior", base_material, {"UseInstanceColor": 1.0, "Emissive": 0.12})  # rooms: a little self-lit
if wanted("M_FTOGlass"):
    build_glass_material("M_FTOGlass")
if wanted("M_FTODecal"):
    build_decal_material("M_FTODecal")                                              # bullet holes and scuffs
