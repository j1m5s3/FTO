"""
Imports FTO's in-house art (FBX made by Tools/Blender/*) into /Game/FTO. Run headless:

  UnrealEditor-Cmd.exe FTO.uproject -run=pythonscript -script="<repo>/Tools/Unreal/import_art.py"

Re-running replaces the assets in place, so iterate in Blender, re-export, re-import.
"""
import json
import os

import unreal

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
ART = os.path.join(REPO, "Art", "Source")
BASE_MATERIAL = "/Game/FTO/Materials/M_FTOBase"
# Material slot name (from the Blender material) -> material; anything else gets BASE_MATERIAL.
SLOT_MATERIALS = {
    "glass": "/Game/FTO/Materials/M_FTOGlass",
    "glow": "/Game/FTO/Materials/MI_FTOGlow",
}
# The building kit (Tools/Blender/build_kit.py): every FBX in Art/Source/Kit. Instanced by the city
# generator, so the paint comes from per-instance data (MI_FTOCity) rather than a material per colour.
KIT_SOURCE = "Kit"
KIT_DEST = "/Game/FTO/Kit"
KIT_SLOT_MATERIALS = {
    "body": "/Game/FTO/Materials/MI_FTOCity",
    "glass": "/Game/FTO/Materials/M_FTOGlass",
    "glow": "/Game/FTO/Materials/MI_FTOGlow",
}
# Sockets the game looks up on vehicle meshes (Tools/Blender/build_vehicles.py).
VEHICLE_SOCKETS = ["Wheel_FL", "Wheel_FR", "Wheel_RL", "Wheel_RR", "Seat_Driver", "Seat_Passenger", "Seat_RearL",
                   "Seat_RearR", "Cam_Driver", "Cam_Passenger", "Lightbar"]
# Blender works in metres; Unreal in centimetres.
METRES_TO_CM = 1.0  # FBX from Tools/Blender already carries centimetres

OFFICER_SKELETON = "/Game/FTO/Characters/Officer/SK_Officer_Skeleton"

# Everyone shares the officer's skeleton, so the officer's clips animate every character.
CHARACTERS = [
    {"folder": "Characters/Officer", "meshes": ["SK_Officer"],
     "clips": ["Idle", "Walk", "Run", "Jump", "Interact", "Cheer",
               "Sit", "Drive", "Talk", "Work", "HandsUp", "Kneel", "Cuffed", "Cuffing", "Struggle", "Tackle",
               "Punch", "Cower", "AimPistol", "AimRifle", "Dance", "Slump", "Dazed",
               "Ride", "SitCuffed", "SitHandsUp"],
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
    ui.set_editor_property("create_physics_asset", False)  # built properly by ensure_physics_assets()
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


def static_options(nanite=False):
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
    data.set_editor_property("build_nanite", nanite)
    data.set_editor_property("convert_scene", True)
    return ui


def import_statics(group):
    source = os.path.join(ART, group["folder"])
    destination = group["dest"]
    base = eal.load_asset(BASE_MATERIAL)
    for name in group["meshes"]:
        # Start fresh: a reimport keeps the old material slots, which then pile up in front of the new ones.
        if eal.does_asset_exist(f"{destination}/{name}"):
            eal.delete_asset(f"{destination}/{name}")
        run_import(os.path.join(source, name + ".fbx"), destination, name, static_options())
        mesh = eal.load_asset(f"{destination}/{name}")
        if not mesh:
            unreal.log_error(f"FTO: missing {destination}/{name} after import")
            continue
        slots = []
        for index, slot in enumerate(mesh.get_editor_property("static_materials")):
            slot_name = str(slot.get_editor_property("material_slot_name"))
            path = SLOT_MATERIALS.get(slot_name.lower(), BASE_MATERIAL)
            mesh.set_material(index, eal.load_asset(path) if path != BASE_MATERIAL else base)
            slots.append(f"{index}:{slot_name}->{path.rsplit('/', 1)[-1]}")
        eal.save_loaded_asset(mesh)
        bounds = mesh.get_bounds()
        sockets = []
        for socket_name in VEHICLE_SOCKETS:
            socket = mesh.find_socket(socket_name)
            if socket:
                # Anything attached here (wheels, people, cameras) must attach at 1:1 and square to
                # the vehicle. Sockets from FBX empties carry the axis conversion (a 90 degree roll),
                # which lays seated people on their backs and points cameras at the sky.
                scale = socket.get_editor_property("relative_scale")
                if abs(scale.x - 1.0) > 1e-3 or abs(scale.y - 1.0) > 1e-3 or abs(scale.z - 1.0) > 1e-3:
                    unreal.log(f"FTO: {name}.{socket_name} scale {scale.x:.1f} reset to 1")
                    socket.set_editor_property("relative_scale", unreal.Vector(1.0, 1.0, 1.0))
                rot = socket.get_editor_property("relative_rotation")
                if abs(rot.roll) > 1e-3 or abs(rot.pitch) > 1e-3 or abs(rot.yaw) > 1e-3:
                    unreal.log(f"FTO: {name}.{socket_name} rotation ({rot.roll:.0f},{rot.pitch:.0f},{rot.yaw:.0f}) reset to 0")
                    socket.set_editor_property("relative_rotation", unreal.Rotator(0.0, 0.0, 0.0))
                loc = socket.get_editor_property("relative_location")
                sockets.append(f"{socket_name}=({loc.x:.0f},{loc.y:.0f},{loc.z:.0f})")
        unreal.log(f"FTO: {name} extent {bounds.box_extent} slots {slots} sockets {sockets}")
        eal.save_loaded_asset(mesh)
    eal.save_directory(destination, only_if_is_dirty=False, recursive=True)


def import_kit():
    """
    Building kit: collision comes from the UCX boxes in each FBX. Opaque pieces are Nanite (the city places
    tens of thousands of them); anything with a translucent Glass section stays a regular mesh.
    """
    source = os.path.join(ART, KIT_SOURCE)
    if not os.path.isdir(source):
        unreal.log_warning(f"FTO: no kit at {source}")
        return
    materials = {slot: eal.load_asset(path) for slot, path in KIT_SLOT_MATERIALS.items()}
    with open(os.path.join(source, "kit_manifest.json")) as f:
        manifest = json.load(f)  # written by build_kit.py: the material slots each piece uses
    names = sorted(f[:-4] for f in os.listdir(source) if f.lower().endswith(".fbx"))
    nanite_count = 0
    for name in names:
        path = f"{KIT_DEST}/{name}"
        if eal.does_asset_exist(path):
            eal.delete_asset(path)
        nanite = "Glass" not in manifest.get(name, {}).get("slots", ["Glass"])
        run_import(os.path.join(source, name + ".fbx"), KIT_DEST, name, static_options(nanite=nanite))
        mesh = eal.load_asset(path)
        if not mesh:
            unreal.log_error(f"FTO: missing {path} after import")
            continue

        for index, slot in enumerate(mesh.get_editor_property("static_materials")):
            slot_name = str(slot.get_editor_property("material_slot_name")).lower()
            mesh.set_material(index, materials.get(slot_name) or materials["body"])

        nanite_count += int(nanite)
        body_setup = mesh.get_editor_property("body_setup")
        geom = body_setup.get_editor_property("agg_geom") if body_setup else None
        boxes = len(geom.get_editor_property("box_elems")) if geom else 0
        convex = len(geom.get_editor_property("convex_elems")) if geom else 0
        unreal.log(f"FTO: kit {name} nanite={nanite} collision={boxes} boxes + {convex} hulls")
        eal.save_loaded_asset(mesh)
    eal.save_directory(KIT_DEST, only_if_is_dirty=False, recursive=True)
    unreal.log(f"FTO: imported {len(names)} kit pieces ({nanite_count} Nanite)")


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


def ensure_physics_assets():
    """Ragdolls need a capsule per limb; the importer's auto physics asset merges our small bones away."""
    for group in CHARACTERS:
        for mesh_name in group["meshes"]:
            mesh = eal.load_asset(f"{group['dest']}/{mesh_name}")
            if not mesh:
                continue
            bodies = unreal.FTOEditorLibrary.rebuild_physics_asset(mesh, 2.0)
            physics = mesh.get_editor_property("physics_asset")
            if physics:
                eal.save_loaded_asset(physics)
            eal.save_loaded_asset(mesh)
            unreal.log(f"FTO: {mesh_name} physics asset has {bodies} bodies")


# FTO_IMPORT=characters|statics|kit limits the run (default: everything).
ONLY = os.environ.get("FTO_IMPORT", "").lower()

if ONLY in ("", "characters"):
    for character_group in CHARACTERS:
        import_group(character_group)
    ensure_physics_assets()

if ONLY in ("", "statics"):
    for static_group in STATICS:
        import_statics(static_group)

if ONLY in ("", "kit"):
    import_kit()
