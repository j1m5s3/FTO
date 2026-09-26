"""
Creates FTO's master material(s). Run headless:

  UnrealEditor-Cmd.exe FTO.uproject -run=pythonscript -script="<repo>/Tools/Unreal/create_materials.py"

M_FTOBase
  BaseColor = lerp(VertexColor.rgb, VertexColor.rgb * Color, VertexColor.a)
  Emissive  = BaseColor * Emissive
M_FTOGlass   tinted see-through glass (vehicle windows)
MI_FTOGlow   M_FTOBase glowing in its vertex colours (lights, dials, screens)

Engine primitives have no vertex colours (read as white), so they simply take `Color`.
Blender-made assets bake flat colours into vertex colours; alpha = 1 marks "tintable"
regions (uniforms, car paint) that pick up `Color` at runtime.
"""
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
    # The city is instanced meshes and characters are skeletal; without these flags the
    # engine silently falls back to its default grey material in game.
    for usage in ("used_with_instanced_static_meshes", "used_with_skeletal_mesh", "used_with_morph_targets"):
        mat.set_editor_property(usage, True)

    vc = mel.create_material_expression(mat, unreal.MaterialExpressionVertexColor, -900, 0)

    color = mel.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, -900, 250)
    color.set_editor_property("parameter_name", "Color")
    color.set_editor_property("default_value", unreal.LinearColor(1.0, 1.0, 1.0, 1.0))

    tinted = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, -600, 150)
    mel.connect_material_expressions(vc, "", tinted, "A")
    mel.connect_material_expressions(color, "", tinted, "B")

    base = mel.create_material_expression(mat, unreal.MaterialExpressionLinearInterpolate, -350, 0)
    mel.connect_material_expressions(vc, "", base, "A")
    mel.connect_material_expressions(tinted, "", base, "B")
    mel.connect_material_expressions(vc, "A", base, "Alpha")
    mel.connect_material_property(base, "", unreal.MaterialProperty.MP_BASE_COLOR)

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


def build_glow_instance(name, parent, emissive):
    """Constant instance of the base material that glows (lights, dials, screens)."""
    path = f"{PACKAGE_DIR}/{name}"
    if eal.does_asset_exist(path):
        eal.delete_asset(path)

    tools = unreal.AssetToolsHelpers.get_asset_tools()
    mi = tools.create_asset(name, PACKAGE_DIR, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    mel.set_material_instance_parent(mi, parent)
    mel.set_material_instance_scalar_parameter_value(mi, "Emissive", emissive)
    eal.save_loaded_asset(mi)
    unreal.log(f"FTO: created {path}")
    return mi


base_material = build_base_material("M_FTOBase")
build_glass_material("M_FTOGlass")
build_glow_instance("MI_FTOGlow", base_material, 2.5)
