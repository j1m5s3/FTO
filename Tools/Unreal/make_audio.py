"""
Imports FTO's sound effects into the editor. Run headless:

  UnrealEditor-Cmd.exe FTO.uproject -run=pythonscript -script="<repo>/Tools/Unreal/make_audio.py"

The sounds themselves are synthesised by Tools/Audio/fto_synth.py (plain Python + numpy/scipy, no Unreal) into
Art/Source/Audio, where the rendered WAVs are committed. This script imports them to /Game/FTO/Audio as SoundWaves
(loops flagged from fto_synth.SOUNDS) and creates SA_FTOWorld, the shared 3D attenuation for world sounds.
Set FTO_SOUNDS=SW_Bonk,SW_Chime (comma-separated) to import just those, and FTO_RESYNTH=1 to re-render the WAVs
first (runs fto_synth with the editor's Python if it has numpy/scipy, otherwise with `python` on PATH).
"""
import os
import subprocess
import sys

import unreal

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
SYNTH_DIR = os.path.join(REPO, "Tools", "Audio")
OUT = os.path.join(REPO, "Art", "Source", "Audio")
DEST = "/Game/FTO/Audio"

sys.path.insert(0, SYNTH_DIR)
import fto_synth  # noqa: E402  (the registry imports without numpy; only rendering needs it)


def resynth(names):
    """Render WAVs with fto_synth, in-process when the editor's Python has numpy/scipy, else via system Python."""
    only = ",".join(names)
    if fto_synth.np is not None:
        fto_synth.main(["--out", OUT, "--jobs", "1"] + (["--only", only] if only else []))
        return
    cmd = [os.environ.get("FTO_PYTHON", "python"), os.path.join(SYNTH_DIR, "fto_synth.py"), "--out", OUT]
    if only:
        cmd += ["--only", only]
    unreal.log(f"FTO: rendering sounds with {' '.join(cmd)}")
    subprocess.run(cmd, check=True)


# ------------------------------------------------------------------------------------------
# Import
# ------------------------------------------------------------------------------------------
eal = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()


def import_wav(path, name, looping):
    task = unreal.AssetImportTask()
    task.filename = path
    task.destination_path = DEST
    task.destination_name = name
    task.replace_existing = True
    task.automated = True
    task.save = True
    tools.import_asset_tasks([task])
    sound = eal.load_asset(f"{DEST}/{name}")
    if not sound:
        unreal.log_error(f"FTO: failed to import {name}")
        return
    sound.set_editor_property("looping", looping)
    eal.save_loaded_asset(sound)
    unreal.log(f"FTO: {name} {sound.get_editor_property('duration'):.2f}s{' (loop)' if looping else ''}")


def make_attenuation():
    path = f"{DEST}/SA_FTOWorld"
    att = eal.load_asset(path) if eal.does_asset_exist(path) else tools.create_asset(
        "SA_FTOWorld", DEST, unreal.SoundAttenuation, unreal.SoundAttenuationFactory())
    settings = att.get_editor_property("attenuation")
    settings.set_editor_property("attenuate", True)
    settings.set_editor_property("spatialize", True)
    settings.set_editor_property("attenuation_shape_extents", unreal.Vector(600.0, 0.0, 0.0))
    settings.set_editor_property("falloff_distance", 4500.0)
    att.set_editor_property("attenuation", settings)
    eal.save_loaded_asset(att)
    unreal.log("FTO: SA_FTOWorld ready")


ONLY = [name for name in os.environ.get("FTO_SOUNDS", "").split(",") if name]
NAMES = [name for name in fto_synth.SOUNDS if not ONLY or name in ONLY]
missing = [name for name in NAMES if not os.path.exists(os.path.join(OUT, name + ".wav"))]
if os.environ.get("FTO_RESYNTH") == "1":
    resynth(ONLY)
elif missing:
    resynth(missing)
for sound_name in NAMES:
    import_wav(os.path.join(OUT, sound_name + ".wav"), sound_name, fto_synth.SOUNDS[sound_name][1])
if not ONLY:
    make_attenuation()
