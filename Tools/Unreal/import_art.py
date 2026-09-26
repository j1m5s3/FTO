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

# (source folder, skeletal mesh, animation clips, destination)
CHARACTERS = [
    ("Characters/Officer", "SK_Officer",
     ["Idle", "Walk", "Run", "Jump", "Interact", "Cheer"],
     "/Game/FTO/Characters/Officer"),
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


def skeletal_options():
    ui = unreal.FbxImportUI()
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


def apply_base_material(mesh):
    base = eal.load_asset(BASE_MATERIAL)
    # Iterating an unreal.Array of structs yields copies, so build a new list.
    slots = []
    for slot in mesh.get_editor_property("materials"):
        slot.set_editor_property("material_interface", base)
        slots.append(slot)
    mesh.set_editor_property("materials", slots)
    eal.save_loaded_asset(mesh)


def import_character(folder, mesh_name, clips, destination):
    source = os.path.join(ART, folder)
    remove_if_wrong_type(f"{destination}/{mesh_name}", "SkeletalMesh")
    run_import(os.path.join(source, mesh_name + ".fbx"), destination, mesh_name, skeletal_options())

    mesh = eal.load_asset(f"{destination}/{mesh_name}")
    if not mesh:
        unreal.log_error(f"FTO: missing {destination}/{mesh_name} after import")
        return
    apply_base_material(mesh)
    skeleton = mesh.get_editor_property("skeleton")

    bounds = mesh.get_bounds()
    unreal.log(f"FTO: {mesh_name} extent {bounds.box_extent} origin {bounds.origin} skeleton {skeleton.get_name()}")

    for clip in clips:
        anim_name = f"A_{mesh_name[3:]}_{clip}"  # SK_Officer -> A_Officer_Walk
        remove_if_wrong_type(f"{destination}/{anim_name}", "AnimSequence")
        run_import(os.path.join(source, anim_name + ".fbx"), destination, anim_name, animation_options(skeleton))
        anim = eal.load_asset(f"{destination}/{anim_name}")
        if anim:
            unreal.log(f"FTO: {anim_name} length {anim.get_play_length():.2f}s")
        else:
            unreal.log_error(f"FTO: animation {anim_name} missing after import")

    # Secondary assets (skeleton, physics asset) aren't saved by the import task itself.
    eal.save_directory(destination, only_if_is_dirty=False, recursive=True)

    # List what actually landed, to catch importer renames.
    for path in eal.list_assets(destination, recursive=False):
        unreal.log(f"FTO: asset {path}")


for args in CHARACTERS:
    import_character(*args)
