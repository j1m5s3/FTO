# Development

## Requirements
- Unreal Engine 5.8 (Epic Games Launcher)
- Visual Studio 2022 or 2026 with the "Game development with C++" workload
- Blender 5.x (only for regenerating art)
- Git LFS (`git lfs install`) — all `.uasset`/`.umap`/`.fbx`/`.blend` files are LFS-tracked

## Build
```bash
"C:/Program Files/Epic Games/UE_5.8/Engine/Build/BatchFiles/Build.bat" FTOEditor Win64 Development -Project="$PWD/FTO.uproject" -WaitMutex
```
Or right-click `FTO.uproject` → *Generate Visual Studio project files* and build from the IDE.

## Play
- Open `FTO.uproject` and press Play, or run standalone:
  `UnrealEditor.exe FTO.uproject -game -windowed -ResX=1600 -ResY=900`
- Replay a specific city/crime roll with `?Seed=1234` on the map URL.
- Local multiplayer test: host with `UnrealEditor.exe FTO.uproject /Engine/Maps/Templates/Template_Default?listen -game`,
  join with `UnrealEditor.exe FTO.uproject 127.0.0.1 -game`.

### Host console commands (`~`)
| Command | Effect |
|---|---|
| `FTOSpawnCrime BankHeist` | Spawn any crime template by id (see `FTOCrimeCatalog.cpp`) |
| `FTOAddChaos 20` | Add (or with a negative number, remove) chaos |
| `FTOSkipBriefing` | Start the shift immediately |

## Smoke test
`UnrealEditor.exe FTO.uproject -game -windowed -ResX=1600 -ResY=900 -FTOSmokeTest -FTOSmokeTestQuit`
tours the city and writes screenshots to `Saved/Screenshots/SmokeTest/`. Handy after any gameplay or art change.

## Art pipeline
- **Characters**: `Tools/Blender/build_officer.py` models, rigs and animates the officer entirely from code and exports FBX
  to `Art/Source/Characters/Officer` (plus an editable `Officer.blend`):
  `blender -b --factory-startup -P Tools/Blender/build_officer.py -- --out Art/Source/Characters/Officer --preview <dir>`
  (`--preview` renders turnaround and clip frames; `Tools/Blender/contact_sheet.py` tiles them into one image.)
- `Tools/Blender/build_civilians.py` makes eight citizen variants and the striped-jumper suspect on the **same skeleton**,
  so every character shares the officer's clips. Shirts are tinted per pedestrian at runtime.
- **Import**: `Tools/Unreal/import_art.py` brings the FBX into `/Game/FTO/...` (metres to centimetres, vertex colours,
  master material) and can be re-run after any Blender change:
  `UnrealEditor-Cmd.exe FTO.uproject -run=pythonscript -script="<repo>/Tools/Unreal/import_art.py"`
- **Animation** needs no Animation Blueprint: `UFTOCharacterAnimInstance` samples the clips in C++ and blends
  idle/walk/run by speed, with jump, interact (tickets, scenes) and cheer layered on top.
- **Materials**: `Tools/Unreal/create_materials.py` builds `Content/FTO/Materials/M_FTOBase` (vertex colour × `Color` tint, `Emissive`).
  Run: `UnrealEditor-Cmd.exe FTO.uproject -run=pythonscript -script="<repo>/Tools/Unreal/create_materials.py"`
- Everything uses that one material. Engine primitives have no vertex colour, so they just take `Color`.
  Blender assets bake flat colours into vertex colours; vertex alpha = 1 marks tintable areas (uniforms, car paint).
- Only free (CC0) or in-house assets. Record any third-party asset and its licence in `docs/Credits.md`.
