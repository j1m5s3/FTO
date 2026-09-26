"""
Creates FTO's master material(s). Run headless:

  UnrealEditor-Cmd.exe FTO.uproject -run=pythonscript -script="<repo>/Tools/Unreal/create_materials.py"

M_FTOBase
  BaseColor = lerp(VertexColor.rgb, VertexColor.rgb * Color, VertexColor.a)
  Emissive  = Color * Emissive

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

    glow = scalar_param(mat, "Emissive", 0.0, -900, 600)
    emissive = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, -600, 650)
    mel.connect_material_expressions(color, "", emissive, "A")
    mel.connect_material_expressions(glow, "", emissive, "B")
    mel.connect_material_property(emissive, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

    mel.recompile_material(mat)
    eal.save_loaded_asset(mat)
    unreal.log(f"FTO: created {path}")
    return mat


build_base_material("M_FTOBase")
