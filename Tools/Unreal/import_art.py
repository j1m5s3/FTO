"""
Imports FTO's in-house art (FBX made by Tools/Blender/*) into /Game/FTO. Run headless:

  UnrealEditor-Cmd.exe FTO.uproject -run=pythonscript -script="<repo>/Tools/Unreal/import_art.py"

Re-running replaces the assets in place, so iterate in Blender, re-export, re-import.
"""
import os

import unreal

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
ART = os.path.join(REPO, "Art", "Source")
BASE_MATERIAL = "/Game/FTO/Materials/M_FTOBase"
# Blender works in metres; Unreal in centimetres.
METRES_TO_CM = 100.0

OFFICER_SKELETON = "/Game/FTO/Characters/Officer/SK_Officer_Skeleton"

# Everyone shares the officer's skeleton, so the officer's clips animate every character.
CHARACTERS = [
    {"folder": "Characters/Officer", "meshes": ["SK_Officer"],
     "clips": ["Idle", "Walk", "Run", "Jump", "Interact", "Cheer"],
     "dest": "/Game/FTO/Characters/Officer"},
    {"folder": "Characters/Civilians",
     "meshes": [f"SK_Civilian_{i:02d}" for i in range(1, 9)] + ["SK_Suspect"],
     "clips": [], "dest": "/Game/FTO/Characters/Civilians", "skeleton": OFFICER_SKELETON},
]

# Static meshes (vehicles, props): folder, names, destination.
STATICS = [
    {"folder": "Vehicles",
     "meshes": ["SM_Car_Sedan", "SM_Car_Hatchback", "SM_Car_Van", "SM_Car_Pickup", "SM_Car_Taxi",
                "SM_Car_IceCream", "SM_Car_Cruiser", "SM_Wheel"],
     "dest": "/Game/FTO/Vehicles"},
]

eal = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()

# The classic FBX importer honours FbxImportUI (skeleton reuse, vertex colours) predictably.
unreal.SystemLibrary.execute_console_command(None, "Interchange.FeatureFlags.Import.FBX False")


def remove_if_wrong_type(path, expected_class):
    """A previous bad import may have left an asset of the wrong type at this path."""
    if not eal.does_asset_exist(path):
        return
    data = eal.find_asset_data(path)
    if data.asset_class_path.asset_name != expected_class:
        unreal.log_warning(f"FTO: removing {path} (was {data.asset_class_path.asset_name}, want {expected_class})")
        eal.delete_asset(path)


def run_import(filename, destination, name, options):
    task = unreal.AssetImportTask()
    task.filename = filename
    task.destination_path = destination
    task.destination_name = name
    task.replace_existing = True
    task.replace_existing_settings = True
    task.automated = True
    task.save = True
    task.options = options
    tools.import_asset_tasks([task])
    paths = list(task.imported_object_paths)
    if not paths:
        unreal.log_error(f"FTO: import produced nothing for {filename}")
    return paths


def skeletal_options(skeleton=None):
    ui = unreal.FbxImportUI()
    if skeleton:
        ui.set_editor_property("skeleton", skeleton)
    ui.set_editor_property("automated_import_should_detect_type", False)
    ui.set_editor_property("import_mesh", True)
    ui.set_editor_property("import_as_skeletal", True)
    ui.set_editor_property("import_animations", False)
    ui.set_editor_property("import_materials", False)
    ui.set_editor_property("import_textures", False)
    ui.set_editor_property("create_physics_asset", True)
    ui.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_SKELETAL_MESH)
    data = ui.get_editor_property("skeletal_mesh_import_data")
    data.set_editor_property("import_uniform_scale", METRES_TO_CM)
    data.set_editor_property("vertex_color_import_option", unreal.VertexColorImportOption.REPLACE)
    data.set_editor_property("import_morph_targets", False)
    data.set_editor_property("convert_scene", True)
    return ui


def animation_options(skeleton):
    ui = unreal.FbxImportUI()
    ui.set_editor_property("automated_import_should_detect_type", False)
    ui.set_editor_property("import_mesh", False)
    ui.set_editor_property("import_as_skeletal", True)
    ui.set_editor_property("import_animations", True)
    ui.set_editor_property("import_materials", False)
    ui.set_editor_property("import_textures", False)
    ui.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_ANIMATION)
    ui.set_editor_property("skeleton", skeleton)
    data = ui.get_editor_property("anim_sequence_import_data")
    data.set_editor_property("import_uniform_scale", METRES_TO_CM)
    data.set_editor_property("import_bone_tracks", True)
    data.set_editor_property("remove_redundant_keys", False)
    data.set_editor_property("animation_length", unreal.FBXAnimationLengthImportType.FBXALIT_EXPORTED_TIME)
    data.set_editor_property("convert_scene", True)
    return ui


def static_options():
    ui = unreal.FbxImportUI()
    ui.set_editor_property("automated_import_should_detect_type", False)
    ui.set_editor_property("import_mesh", True)
    ui.set_editor_property("import_as_skeletal", False)
    ui.set_editor_property("import_animations", False)
    ui.set_editor_property("import_materials", False)
    ui.set_editor_property("import_textures", False)
    ui.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_STATIC_MESH)
    data = ui.get_editor_property("static_mesh_import_data")
    data.set_editor_property("import_uniform_scale", METRES_TO_CM)
    data.set_editor_property("vertex_color_import_option", unreal.VertexColorImportOption.REPLACE)
    data.set_editor_property("combine_meshes", True)
    data.set_editor_property("auto_generate_collision", False)
    data.set_editor_property("build_nanite", False)
    data.set_editor_property("convert_scene", True)
    return ui


def import_statics(group):
    source = os.path.join(ART, group["folder"])
    destination = group["dest"]
    base = eal.load_asset(BASE_MATERIAL)
    for name in group["meshes"]:
        remove_if_wrong_type(f"{destination}/{name}", "StaticMesh")
        run_import(os.path.join(source, name + ".fbx"), destination, name, static_options())
        mesh = eal.load_asset(f"{destination}/{name}")
        if not mesh:
            unreal.log_error(f"FTO: missing {destination}/{name} after import")
            continue
        for index in range(len(mesh.get_editor_property("static_materials"))):
            mesh.set_material(index, base)
        eal.save_loaded_asset(mesh)
        bounds = mesh.get_bounds()
        sockets = []
        for socket_name in ("Wheel_FL", "Wheel_FR", "Wheel_RL", "Wheel_RR"):
            socket = mesh.find_socket(socket_name)
            if socket:
                # The import scale leaks into socket scale; wheels must attach at 1:1.
                scale = socket.get_editor_property("relative_scale")
                if abs(scale.x - 1.0) > 1e-3 or abs(scale.y - 1.0) > 1e-3 or abs(scale.z - 1.0) > 1e-3:
                    unreal.log(f"FTO: {name}.{socket_name} scale {scale.x:.1f} reset to 1")
                    socket.set_editor_property("relative_scale", unreal.Vector(1.0, 1.0, 1.0))
                loc = socket.get_editor_property("relative_location")
                sockets.append(f"{socket_name}=({loc.x:.0f},{loc.y:.0f},{loc.z:.0f})")
        unreal.log(f"FTO: {name} extent {bounds.box_extent} sockets {sockets}")
        eal.save_loaded_asset(mesh)
    eal.save_directory(destination, only_if_is_dirty=False, recursive=True)


def apply_base_material(mesh):
    base = eal.load_asset(BASE_MATERIAL)
    # Iterating an unreal.Array of structs yields copies, so build a new list.
    slots = []
    for slot in mesh.get_editor_property("materials"):
        slot.set_editor_property("material_interface", base)
        slots.append(slot)
    mesh.set_editor_property("materials", slots)
    eal.save_loaded_asset(mesh)


def import_mesh(source, mesh_name, destination, skeleton=None):
    remove_if_wrong_type(f"{destination}/{mesh_name}", "SkeletalMesh")
    run_import(os.path.join(source, mesh_name + ".fbx"), destination, mesh_name, skeletal_options(skeleton))

    mesh = eal.load_asset(f"{destination}/{mesh_name}")
    if not mesh:
        unreal.log_error(f"FTO: missing {destination}/{mesh_name} after import")
        return None
    apply_base_material(mesh)

    bounds = mesh.get_bounds()
    skeleton_name = mesh.get_editor_property("skeleton").get_name()
    unreal.log(f"FTO: {mesh_name} extent {bounds.box_extent} origin {bounds.origin} skeleton {skeleton_name}")
    return mesh


def import_clips(source, mesh_name, clips, destination, skeleton):
    for clip in clips:
        anim_name = f"A_{mesh_name[3:]}_{clip}"  # SK_Officer -> A_Officer_Walk
        remove_if_wrong_type(f"{destination}/{anim_name}", "AnimSequence")
        run_import(os.path.join(source, anim_name + ".fbx"), destination, anim_name, animation_options(skeleton))
        anim = eal.load_asset(f"{destination}/{anim_name}")
        if anim:
            unreal.log(f"FTO: {anim_name} length {anim.get_play_length():.2f}s")
        else:
            unreal.log_error(f"FTO: animation {anim_name} missing after import")


def import_group(group):
    source = os.path.join(ART, group["folder"])
    destination = group["dest"]
    shared = eal.load_asset(group["skeleton"]) if group.get("skeleton") else None

    for mesh_name in group["meshes"]:
        mesh = import_mesh(source, mesh_name, destination, shared)
        if mesh and group["clips"]:
            import_clips(source, mesh_name, group["clips"], destination, mesh.get_editor_property("skeleton"))

    # Secondary assets (skeleton, physics asset) aren't saved by the import task itself.
    eal.save_directory(destination, only_if_is_dirty=False, recursive=True)

    # List what actually landed, to catch importer renames.
    for path in eal.list_assets(destination, recursive=False):
        unreal.log(f"FTO: asset {path}")


# FTO_IMPORT=characters|statics limits the run (default: everything).
ONLY = os.environ.get("FTO_IMPORT", "").lower()

if ONLY in ("", "characters"):
    for character_group in CHARACTERS:
        import_group(character_group)

if ONLY in ("", "statics"):
    for static_group in STATICS:
        import_statics(static_group)
