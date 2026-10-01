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
# Blender works in metres; Unreal in centimetres.
METRES_TO_CM = 1.0  # FBX from Tools/Blender already carries centimetres

# Everyone is built on Epic's UE5 mannequin skeleton (Tools/Blender/fto_rig.py), so the cast shares Epic's
# animations (Content/Characters/Mannequins, installed by Tools/Unreal/install_epic_content.py), our own clips and its
# ragdoll (PA_Mannequin).
MANNEQUIN_SKELETON = "/Game/Characters/Mannequins/Meshes/SK_Mannequin"
MANNEQUIN_PHYSICS = "/Game/Characters/Mannequins/Rigs/PA_Mannequin"

CHARACTERS = [
    {"folder": "Characters/Officer", "meshes": ["SK_Officer", "SK_Officer_F"],
     "dest": "/Game/FTO/Characters/Officer"},
    {"folder": "Characters/Civilians",
     "meshes": [f"SK_Civilian_{i:02d}" for i in range(1, 9)] + ["SK_Suspect"],
     "dest": "/Game/FTO/Characters/Civilians"},
]
# Our own clips (Tools/Blender/build_character_anims.py): every A_FTO_*.fbx in the folder.
CLIPS = {"folder": "Characters/Anims", "dest": "/Game/FTO/Characters/Anims"}

# Static meshes (vehicles, weapons): folder, names, the sockets the game looks up on them, destination, and the
# FTO_IMPORT key that picks the group alone.
STATICS = [
    {"key": "vehicles", "folder": "Vehicles",
     "meshes": ["SM_Car_Sedan", "SM_Car_Hatchback", "SM_Car_Van", "SM_Car_Pickup", "SM_Car_Taxi",
                "SM_Car_IceCream", "SM_Car_Cruiser", "SM_Wheel",
                # Beaten-up variants (build_vehicles.py --dented), swapped in when a car's badly damaged.
                "SM_Car_Sedan_Dented", "SM_Car_Hatchback_Dented", "SM_Car_Van_Dented", "SM_Car_Pickup_Dented",
                "SM_Car_Taxi_Dented", "SM_Car_IceCream_Dented", "SM_Car_Cruiser_Dented"],
     "sockets": ["Wheel_FL", "Wheel_FR", "Wheel_RL", "Wheel_RR", "Seat_Driver", "Seat_Passenger", "Seat_RearL",
                 "Seat_RearR", "Cam_Driver", "Cam_Passenger", "Lightbar"],
     "dest": "/Game/FTO/Vehicles",
     # Car bodies dent where they're hit (UFTOVehicleDamage), windows and lights along with the paint.
     "base": "/Game/FTO/Materials/M_FTOVehicle",
     "slot_materials": {"glass": "/Game/FTO/Materials/M_FTOVehicleGlass", "glow": "/Game/FTO/Materials/MI_FTOVehicleGlow"},
     "plain": ["SM_Wheel"]},
    {"key": "weapons", "folder": "Weapons",
     "meshes": ["SM_Taser", "SM_Pistol", "SM_Shotgun", "SM_Rifle"],
     "sockets": ["Muzzle"],
     "dest": "/Game/FTO/Weapons"},
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
    ui.set_editor_property("create_physics_asset", False)  # everyone uses Epic's PA_Mannequin
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
    for name in group["meshes"]:
        plain = name in group.get("plain", [])
        base_path = BASE_MATERIAL if plain else group.get("base", BASE_MATERIAL)
        table = SLOT_MATERIALS if plain else group.get("slot_materials", SLOT_MATERIALS)
        base = eal.load_asset(base_path)
        if MESHES and name not in MESHES:
            continue
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
            path = table.get(slot_name.lower(), base_path)
            mesh.set_material(index, eal.load_asset(path) if path != base_path else base)
            slots.append(f"{index}:{slot_name}->{path.rsplit('/', 1)[-1]}")
        eal.save_loaded_asset(mesh)
        bounds = mesh.get_bounds()
        sockets = []
        for socket_name in group["sockets"]:
            # Anything attached here (wheels, people, cameras, muzzle flashes) must attach at 1:1 and square to
            # the mesh. Sockets from FBX empties carry the axis conversion (a 90 degree roll), which lays seated
            # people on their backs and points cameras at the sky.
            socket = mesh.find_socket(socket_name)
            if not socket:
                continue
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
    # (Just the new ones when importing a few by name, so the rest aren't re-saved for nothing.)
    eal.save_directory(destination, only_if_is_dirty=bool(MESHES), recursive=True)


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
    names = sorted(f[:-4] for f in os.listdir(source) if f.lower().endswith(".fbx") and (not MESHES or f[:-4] in MESHES))
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
    eal.save_directory(KIT_DEST, only_if_is_dirty=bool(MESHES), recursive=True)
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


def import_mesh(source, mesh_name, destination, skeleton):
    # (A mesh on another skeleton can't be re-imported in place onto this one.)
    path = f"{destination}/{mesh_name}"
    remove_if_wrong_type(path, "SkeletalMesh")
    old = eal.load_asset(path) if eal.does_asset_exist(path) else None
    if old and old.get_editor_property("skeleton") != skeleton:
        unreal.log_warning(f"FTO: {mesh_name} was on {old.get_editor_property('skeleton').get_name()}: starting it afresh")
        old = None
        eal.delete_asset(path)
    run_import(os.path.join(source, mesh_name + ".fbx"), destination, mesh_name, skeletal_options(skeleton))

    mesh = eal.load_asset(path)
    if not mesh:
        unreal.log_error(f"FTO: missing {path} after import")
        return None
    apply_base_material(mesh)
    # Epic's ragdoll fits (every body is built on the mannequin's own bones).
    mesh.set_editor_property("physics_asset", eal.load_asset(MANNEQUIN_PHYSICS))
    eal.save_loaded_asset(mesh)

    bounds = mesh.get_bounds()
    skeleton_name = mesh.get_editor_property("skeleton").get_name()
    unreal.log(f"FTO: {mesh_name} extent {bounds.box_extent} origin {bounds.origin} skeleton {skeleton_name}")
    return mesh


def import_clips(names=None):
    """Our clips onto the mannequin skeleton: every A_FTO_*.fbx, or just the named ones (Walk, not A_FTO_Walk)."""
    skeleton = eal.load_asset(MANNEQUIN_SKELETON)
    if not skeleton:
        unreal.log_error("FTO: no mannequin skeleton: run Tools/Unreal/install_epic_content.py first")
        return
    source = os.path.join(ART, CLIPS["folder"])
    destination = CLIPS["dest"]
    for filename in sorted(os.listdir(source)):
        if not (filename.startswith("A_FTO_") and filename.endswith(".fbx")):
            continue
        anim_name = filename[:-4]
        if names and anim_name[len("A_FTO_"):] not in names:
            continue
        remove_if_wrong_type(f"{destination}/{anim_name}", "AnimSequence")
        run_import(os.path.join(source, filename), destination, anim_name, animation_options(skeleton))
        anim = eal.load_asset(f"{destination}/{anim_name}")
        if anim:
            unreal.log(f"FTO: {anim_name} length {anim.get_play_length():.2f}s")
            eal.save_loaded_asset(anim)
        else:
            unreal.log_error(f"FTO: animation {anim_name} missing after import")


def import_group(group):
    source = os.path.join(ART, group["folder"])
    destination = group["dest"]
    skeleton = eal.load_asset(MANNEQUIN_SKELETON)
    if not skeleton:
        unreal.log_error("FTO: no mannequin skeleton: run Tools/Unreal/install_epic_content.py first")
        return
    for mesh_name in group["meshes"]:
        import_mesh(source, mesh_name, destination, skeleton)
    eal.save_directory(destination, only_if_is_dirty=False, recursive=True)
    # List what actually landed, to catch importer renames.
    for path in eal.list_assets(destination, recursive=False):
        unreal.log(f"FTO: asset {path}")


# FTO_IMPORT=characters|clips|statics|vehicles|weapons|kit limits the run (default: everything); with
# FTO_IMPORT=clips, FTO_CLIPS=Walk,Jab imports only those clips, and FTO_MESHES=A,B only those static meshes.
ONLY = os.environ.get("FTO_IMPORT", "").lower()
MESHES = set(filter(None, os.environ.get("FTO_MESHES", "").split(",")))

if ONLY in ("", "characters"):
    for character_group in CHARACTERS:
        import_group(character_group)

if ONLY in ("", "characters", "clips"):
    import_clips(set(filter(None, os.environ.get("FTO_CLIPS", "").split(","))))

for static_group in STATICS:
    if ONLY in ("", "statics", static_group["key"]):
        import_statics(static_group)

if ONLY in ("", "kit"):
    import_kit()
