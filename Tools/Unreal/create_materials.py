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
M_FTOCrackDecal  deferred decal: cracks spreading through a knocked wall (jagged spokes from a crushed middle, and
             a web of smaller cracks round it), worked out in the shader, so there's no texture to it
M_FTOVehicle M_FTOBase for car bodies, plus dents: up to DENTS dents (DentN = local centre xyz + radius w,
             PushN = the push into the body xyz + scrape w) move the vertices (World Position Offset), bend the
             normals to match and scrape the paint back to bare metal. Set by UFTOVehicleDamage per car.
MI_FTOVehicleGlow  M_FTOVehicle glowing (the lights, so they dent with the body)
M_FTOVehicleGlass  M_FTOGlass that dents along with the body

Engine primitives have no vertex colours (read as white), so they simply take `Color`.
Blender-made assets bake flat colours into vertex colours; alpha = 1 marks "tintable"
regions (uniforms, car paint) that pick up `Color` at runtime.
"""
import os

import unreal

PACKAGE_DIR = "/Game/FTO/Materials"
mel = unreal.MaterialEditingLibrary
eal = unreal.EditorAssetLibrary


# How many dents a car's material can carry (UFTOVehicleDamage::MaxDents must match).
DENTS = 12

# Each dent is a smooth hollow pressed in along its push: a vertex is pushed by Push * g, where g falls off with the
# distance from the line the push travels along (exp(-3 d^2 / r^2)) and with how far in front of or behind the
# dent's centre it is, so only the panel that was hit moves, not the far side of the car. The centre is on the paint
# (UFTOVehicleDamage finds it). The normal tilts by the slope of that; the scrape mask uses the same shape.
DENT_SHAPE = """float r = max(D.w, 1.0); float3 v = Pos - D.xyz; float3 k = normalize(P.xyz + float3(0, 0, 1e-4)); float a = dot(v, k); float3 q = v - a * k; float g = exp(-3.0 * dot(q, q) / (r * r)) * saturate(1.0 - abs(a) / 45.0);"""
DENT_HLSL_OFFSET = """
float3 o = 0;
#define FTO_DENT(D, P) { %s o += P.xyz * g; }
%%s
#undef FTO_DENT
return o;
""" % DENT_SHAPE
DENT_HLSL_NORMAL = """
float3 n = normalize(N);
float3 t = 0;
#define FTO_DENT(D, P) { %s float3 dg = g * (-6.0 / (r * r)) * q; t -= dot(P.xyz, n) * (dg - dot(dg, n) * n); }
%%s
#undef FTO_DENT
return normalize(n + t);
""" % DENT_SHAPE
DENT_HLSL_SCRAPE = """
float s = 0;
#define FTO_DENT(D, P) { %s s += P.w * g; }
%%s
#undef FTO_DENT
return saturate(s);
""" % DENT_SHAPE


def dent_nodes(mat, x, y):
    """The dent maths: (world offset, world normal, scrape mask) nodes, fed by the DentN/PushN parameters."""
    calls = "\n".join(f"FTO_DENT(D{i}, P{i})" for i in range(DENTS))
    # The vertex in the mesh's own space: its world position (before any offset, this one included) taken back into
    # the component's frame.
    world_pos = mel.create_material_expression(mat, unreal.MaterialExpressionWorldPosition, x - 1100, y)
    world_pos.set_editor_property("world_position_shader_offset", unreal.WorldPositionIncludedOffsets.WPT_EXCLUDE_ALL_SHADER_OFFSETS)
    local_pos = mel.create_material_expression(mat, unreal.MaterialExpressionTransformPosition, x - 900, y)
    local_pos.set_editor_property("transform_source_type", unreal.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_WORLD)
    local_pos.set_editor_property("transform_type", unreal.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_LOCAL)
    mel.connect_material_expressions(world_pos, "", local_pos, "")
    normal_ws = mel.create_material_expression(mat, unreal.MaterialExpressionVertexNormalWS, x - 1100, y + 150)
    normal_ls = mel.create_material_expression(mat, unreal.MaterialExpressionTransform, x - 900, y + 150)
    normal_ls.set_editor_property("transform_source_type", unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_WORLD)
    normal_ls.set_editor_property("transform_type", unreal.MaterialVectorCoordTransform.TRANSFORM_LOCAL)
    mel.connect_material_expressions(normal_ws, "", normal_ls, "")

    params = []
    for i in range(DENTS):
        d = mel.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, x - 900, y + 300 + i * 120)
        d.set_editor_property("parameter_name", f"Dent{i}")
        d.set_editor_property("default_value", unreal.LinearColor(0.0, 0.0, -100000.0, 1.0))
        pnode = mel.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, x - 700, y + 300 + i * 120)
        pnode.set_editor_property("parameter_name", f"Push{i}")
        pnode.set_editor_property("default_value", unreal.LinearColor(0.0, 0.0, 0.0, 0.0))
        params.append((d, pnode))

    def named_input(name):
        pin = unreal.CustomInput()
        pin.set_editor_property("input_name", name)
        return pin

    def custom(code, out_type, with_normal, cx, cy):
        node = mel.create_material_expression(mat, unreal.MaterialExpressionCustom, cx, cy)
        node.set_editor_property("code", code % calls)
        node.set_editor_property("output_type", out_type)
        inputs = [named_input("Pos")]
        if with_normal:
            inputs.append(named_input("N"))
        for i in range(DENTS):
            inputs.append(named_input(f"D{i}"))
            inputs.append(named_input(f"P{i}"))
        node.set_editor_property("inputs", inputs)
        mel.connect_material_expressions(local_pos, "", node, "Pos")
        if with_normal:
            mel.connect_material_expressions(normal_ls, "", node, "N")
        for i, (d, pnode) in enumerate(params):
            mel.connect_material_expressions(d, "RGBA", node, f"D{i}")
            mel.connect_material_expressions(pnode, "RGBA", node, f"P{i}")
        return node

    offset_ls = custom(DENT_HLSL_OFFSET, unreal.CustomMaterialOutputType.CMOT_FLOAT3, False, x - 400, y)
    offset_ws = mel.create_material_expression(mat, unreal.MaterialExpressionTransform, x - 200, y)
    offset_ws.set_editor_property("transform_source_type", unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_LOCAL)
    offset_ws.set_editor_property("transform_type", unreal.MaterialVectorCoordTransform.TRANSFORM_WORLD)
    mel.connect_material_expressions(offset_ls, "", offset_ws, "")

    normal_new = custom(DENT_HLSL_NORMAL, unreal.CustomMaterialOutputType.CMOT_FLOAT3, True, x - 400, y + 200)
    normal_out = mel.create_material_expression(mat, unreal.MaterialExpressionTransform, x - 200, y + 200)
    normal_out.set_editor_property("transform_source_type", unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_LOCAL)
    normal_out.set_editor_property("transform_type", unreal.MaterialVectorCoordTransform.TRANSFORM_WORLD)
    mel.connect_material_expressions(normal_new, "", normal_out, "")

    scrape = custom(DENT_HLSL_SCRAPE, unreal.CustomMaterialOutputType.CMOT_FLOAT1, False, x - 400, y + 400)
    return offset_ws, normal_out, scrape


def scalar_param(mat, name, value, x, y):
    node = mel.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, x, y)
    node.set_editor_property("parameter_name", name)
    node.set_editor_property("default_value", value)
    return node


def build_base_material(name, two_sided=False, dents=False):
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
    if dents:
        # Dents: the body pushed in, the light catching the creases, and scraped paint showing bare metal.
        offset, normal, scrape = dent_nodes(mat, -400, 900)
        mat.set_editor_property("tangent_space_normal", False)
        mel.connect_material_property(offset, "", unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
        mel.connect_material_property(normal, "", unreal.MaterialProperty.MP_NORMAL)
        metal = mel.create_material_expression(mat, unreal.MaterialExpressionConstant3Vector, -200, 300)
        metal.set_editor_property("constant", unreal.LinearColor(0.3, 0.31, 0.33, 1.0))
        scraped = mel.create_material_expression(mat, unreal.MaterialExpressionLinearInterpolate, 0, 100)
        mel.connect_material_expressions(burnt, "", scraped, "A")
        mel.connect_material_expressions(metal, "", scraped, "B")
        mel.connect_material_expressions(scrape, "", scraped, "Alpha")
        burnt = scraped
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


def build_glass_material(name, dents=False):
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
    if dents:
        # A car's windows move with its dents (the scrape mask means nothing on glass).
        offset, _normal, _scrape = dent_nodes(mat, -400, 700)
        mel.connect_material_property(offset, "", unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)

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


# Cracks in a wall, worked out from the decal's UV (0-1 across it): a crushed patch in the middle, a web of short
# cracks round that (the edges of a Voronoi pattern), and seven jagged spokes running out, thinning as they go.
CRACK_HLSL = """
float2 p = (UV - 0.5) * 2.0;
float r = length(p);
float ang = atan2(p.y, p.x);
float spokes = 0.0;
for (int k = 0; k < 7; k++)
{
    float a0 = frac(sin(k * 12.9898 + 4.1) * 43758.5453) * 6.2831853;
    float len = 0.55 + 0.45 * frac(sin(k * 78.233 + 1.7) * 12345.678);
    float wob = (0.18 * sin(r * 23.0 + k * 3.1) + 0.08 * sin(r * 57.0 + k)) * r;
    float da = abs(fmod(ang - a0 - wob + 21.9911486, 6.2831853) - 3.14159265);
    float d = da * r;
    float w = 0.02 * saturate(1.0 - r / len) + 0.003;
    spokes = max(spokes, (1.0 - smoothstep(w * 0.5, w, d)) * step(r, len));
}
float2 g = p * 6.0;
float2 i = floor(g);
float2 f = frac(g);
float f1 = 8.0;
float f2 = 8.0;
for (int y = -1; y <= 1; y++)
{
    for (int x = -1; x <= 1; x++)
    {
        float2 o = float2(x, y);
        float2 h = frac(sin(float2(dot(i + o, float2(127.1, 311.7)), dot(i + o, float2(269.5, 183.3)))) * 43758.5453);
        float dd = length(o + h - f);
        if (dd < f1) { f2 = f1; f1 = dd; } else if (dd < f2) { f2 = dd; }
    }
}
float web = (1.0 - smoothstep(0.03, 0.08, f2 - f1)) * (1.0 - smoothstep(0.15, 0.5, r));
float crush = 1.0 - smoothstep(0.06, 0.16, r);
return saturate(max(max(spokes, web), crush * 0.8));
"""


def build_crack_decal_material(name):
    """Deferred decal: cracks spreading through a wall where it took a knock (CRACK_HLSL). They don't fade."""
    path = f"{PACKAGE_DIR}/{name}"
    if eal.does_asset_exist(path):
        eal.delete_asset(path)

    tools = unreal.AssetToolsHelpers.get_asset_tools()
    mat = tools.create_asset(name, PACKAGE_DIR, unreal.Material, unreal.MaterialFactoryNew())
    mat.set_editor_property("material_domain", unreal.MaterialDomain.MD_DEFERRED_DECAL)
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)

    uv = mel.create_material_expression(mat, unreal.MaterialExpressionTextureCoordinate, -1000, 0)
    cracks = mel.create_material_expression(mat, unreal.MaterialExpressionCustom, -800, 0)
    cracks.set_editor_property("code", CRACK_HLSL)
    cracks.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT1)
    pin = unreal.CustomInput()
    pin.set_editor_property("input_name", "UV")
    cracks.set_editor_property("inputs", [pin])
    mel.connect_material_expressions(uv, "", cracks, "UV")

    dark = mel.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, -600, -200)
    dark.set_editor_property("parameter_name", "Color")
    dark.set_editor_property("default_value", unreal.LinearColor(0.035, 0.032, 0.03, 1.0))
    mel.connect_material_property(dark, "", unreal.MaterialProperty.MP_BASE_COLOR)
    strength = scalar_param(mat, "Strength", 0.92, -600, 150)
    opacity = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, -400, 50)
    mel.connect_material_expressions(cracks, "", opacity, "A")
    mel.connect_material_expressions(strength, "", opacity, "B")
    mel.connect_material_property(opacity, "", unreal.MaterialProperty.MP_OPACITY)
    rough = scalar_param(mat, "Roughness", 0.95, -400, 300)
    mel.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)

    mel.recompile_material(mat)
    eal.save_loaded_asset(mat)
    return mat


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
if wanted("M_FTOVehicle") or wanted("MI_FTOVehicleGlow"):
    vehicle_material = build_base_material("M_FTOVehicle", dents=True) if wanted("M_FTOVehicle") else eal.load_asset(f"{PACKAGE_DIR}/M_FTOVehicle")
    if wanted("MI_FTOVehicleGlow"):
        build_instance("MI_FTOVehicleGlow", vehicle_material, {"Emissive": 2.5})         # a car's lights
if wanted("M_FTOVehicleGlass"):
    build_glass_material("M_FTOVehicleGlass", dents=True)
if wanted("M_FTODecal"):
    build_decal_material("M_FTODecal")                                              # bullet holes and scuffs
if wanted("M_FTOCrackDecal"):
    build_crack_decal_material("M_FTOCrackDecal")                                   # cracks in knocked walls
