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

## Playing with friends
1. Launch the game. It opens on the menu (Esc toggles it at any time).
2. The host clicks **Host an online game**. The menu then shows the host's IP.
3. Friends type that IP into **Join** (port 7777 is added automatically).
4. Everyone gathers in the precinct lobby, and the host clicks **Start shift**. After the report card, **New shift** rolls a fresh city and brings everyone along.

On the same network this just works. Over the internet, either forward UDP port 7777 on the host's router or use a
free virtual LAN like Tailscale or ZeroTier and join with that IP. (Steam invites need a Steam app ID and are on the roadmap.)

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
| `FTOEndShift 1` | End the shift (1 = survived, 0 = overrun) |
| `FTODrive` | Jump into the nearest free cruiser |
| `FTORide` | Ride shotgun in the nearest cruiser that has a driver |
| `FTOCallout Backup` | Make a radio callout (`Backup`, `Fleeing`, `OfficerDown`, `Copy`); works for any player |

Launch flags: `-FTOQuickStart` skips the lobby and starts the shift immediately.

## Controls
- **On foot**: WASD / left stick to move, Shift to sprint, Space to jump. Q blows the police whistle (citizens
  freeze, nearby crimes get called in). E interacts: tickets, chats (with anyone, indoors or out), questioning a
  suspicious character, taking a cruiser, or riding shotgun in one someone's already driving. F (B on a gamepad) is
  a flying tackle that bowls over whoever's in front (citizens cost a little chaos). Cuffed suspects follow in your
  footsteps: walk them into the precinct's holding cells to book them.
- **Arrests**: E on a suspect arrests them. One who's given up (talked down by standing at the scene, run to ground,
  or put on the floor) kneels and you step in behind them to cuff them. One who hasn't may come quietly, or fight
  back (mash E to wrestle them down before the meter drains; partners can pile in with E; lose and you're shoved
  over) or bolt on foot (sprint after them and tackle with F, or run them over; the whistle stops them for a moment).
  A suspect left kneeling with nobody about, or who outruns everyone, gets away. Car chases end with the driver
  climbing out and kneeling beside their car.
- **Weapons** (on foot): every officer carries a taser; the precinct armory racks hand out a pistol, a shotgun and a
  rifle (three slots; E at a rack takes one, swaps it for the one in hand when you're full, or restocks its ammo).
  1, 2, 3 or the mouse wheel pick a weapon (the same number again puts it away), right mouse raises the last one
  used (left trigger on a gamepad), left mouse fires (right trigger), R reloads (right bumper). A perp put on the
  floor is subdued (cuff them with E); hitting a citizen costs chaos. Armed perps shoot back: an officer who's hit
  goes down until a partner helps them up (E), or comes round after a while.
- **Driving a cruiser**: W/S to drive and brake, A/D to steer, Space for the handbrake (drift). Q switches the lights
  and siren (offending cars ahead pull over), H honks. C (right stick click) swaps the chase camera for the view from
  the seat, and the mouse / right stick glances around (it eases back to the road). E gets out. Anyone you hit at
  speed goes flying (and hitting citizens costs chaos).
- **Riding shotgun**: the mouse / right stick looks around, Q works the lights and siren, C swaps cameras, E gets out.
- **Radio** (anywhere, on foot or in a car): hold V (d-pad down) to talk to the squad; teammates hear you through a
  walkie-talkie filter with a squelch at each end, and see who's on air. Hold T (d-pad up) for the callout wheel:
  point with the mouse / right stick and let go, or press 1-4: *Need backup!*, *Suspect fleeing!* (pings the nearest
  getaway), *Officer down!* (pings the nearest downed partner) and *10-4*. Pings show on everyone's HUD for 20 s.
  Going down calls *Officer down!* for you. Voice needs a microphone and uses the engine's VOIP (push-to-talk only).

## Sharing a build
`powershell -ExecutionPolicy Bypass -File Tools/Build/package.ps1` builds, cooks and packages a Windows
Development build into `Build/Package/Windows` (about 900 MB). Zip that folder and send it: friends just run
`FTO.exe`, with no Unreal install needed. Add `-Config Shipping` for a lean release build (no console or dev tools).
The smoke test also runs on the packaged game: `FTO.exe -windowed -FTOSmokeTest -FTOSmokeTestQuit`.

## Smoke test
`UnrealEditor.exe FTO.uproject -game -windowed -ResX=1600 -ResY=900 -FTOSmokeTest -FTOSmokeTestQuit`
tours the city and writes screenshots to `Saved/Screenshots/SmokeTest/`. Handy after any gameplay or art change.
Along the way it books a suspect into the cells, looks inside every kind of building (with their people), stages a
hold-up and a bar brawl, questions a crook, walks through front doors, checks that shop windows let sight through,
drives into three citizens, tackles one, signs a shotgun out of the armory, trades fire with an armed robber (then
cuffs them where they fell), has a downed officer helped up (the radio calls it in), makes the arrests that don't go
quietly: a brawler wrestled down, a vandal who wins the struggle and runs (and is tackled), and a getaway driver who
gives up beside their car, then opens the callout wheel and keys the radio; the log (`SMOKE:` lines) reports each check.
For a two-player check, run a listen-server host and a client (see *Play*) both with `-FTOSmokeTest -FTOSmokeTag=host`
(or `client`) and `-FTOSmokeRideAlong`: the host parks in a cruiser, and the client rides shotgun, looks around, gets out
and arrests a shoplifter the host puts beside them. Then the client calls for backup and stays on air, and the host
checks it heard the call and has the client's voice going through the radio filter (the client checks the same for the
host).

## Art pipeline
- **Characters**: `Tools/Blender/build_officer.py` models, rigs and animates the officer entirely from code and exports FBX
  to `Art/Source/Characters/Officer` (plus an editable `Officer.blend`):
  `blender -b --factory-startup -P Tools/Blender/build_officer.py -- --out Art/Source/Characters/Officer --preview <dir>`
  (`--preview` renders turnaround and clip frames; `Tools/Blender/contact_sheet.py` tiles them into one image.
  `--clips HandsBehind,Cuffing` re-exports just those clips, e.g. after adding or tweaking one.)
- `Tools/Blender/build_civilians.py` makes eight citizen variants and the striped-jumper suspect on the **same skeleton**,
  so every character shares the officer's clips. Shirts are tinted per pedestrian at runtime.
- `Tools/Blender/build_vehicles.py` builds the cars (sedan, hatchback, van, pickup, taxi, ice cream truck, cruiser) and
  a shared wheel as static meshes facing +X. They're real shells: doors, floor, dashboard, seats and a steering wheel
  behind see-through glass, plus the cruiser's police kit (MDT laptop, radio, radar, shotgun rack, cage, lightbar
  switches, and donuts). Three material slots: `Body` (vertex colour, alpha 1 = paint the game tints), `Glass` and
  `Glow` (lights, dials, screens). `SOCKET_*` empties become sockets: `Wheel_*`, `Seat_*` (where a seated character's
  root goes), `Cam_*` (seat-view camera) and the cruiser's `Lightbar`. Seats are sized from the officer's car-seat
  pose (`build_officer.car_legs`); `--preview <dir>` also renders roofless cutaways with posed occupants and the
  driver's-eye view to check the fit.
- `Tools/Blender/build_weapons.py` builds the taser, pistol, shotgun and rifle into `Art/Source/Weapons`: barrel along
  +X with the grip at the origin (the game puts the grip in the hand and turns the barrel along the aim) and a
  `SOCKET_Muzzle` where rounds leave. `--preview <dir>` renders each one.
- `Tools/Blender/build_kit.py` builds the city's building kit into `Art/Source/Kit`: wall panels on a 2 m grid (plain,
  window, door, shopfront, loading door; ground floor 4 m, upper floors 3.2 m), corners, parapets, cornices, awnings,
  shop signs, rooftop units, gable roofs, porches and fences, furniture for every interior (shop, diner, bar, office,
  home, warehouse, bank vault, precinct armory and cells), street dressing and trees. Every piece faces +X; collision is
  `UCX_` boxes, so doorways stay open. Translucent panes are separate `*_Glass` pieces so the walls can be Nanite, and
  `kit_manifest.json` tells the importer which pieces have glass. The panes carry the window's collision (the wall
  pieces only frame the opening), and the game lets sight lines through them, so officers can see into shops.
  Seats are low (`SEAT`, 31 cm) to suit the cast's short legs: sitters' feet reach the floor and their hands the
  tables. `--preview <dir>` renders every piece.
  `Source/FTO/City/FTOCityKit.h` mirrors the grid, and the `FTOCity*.cpp` files assemble buildings, rooms and streets.
  Furnishing a room also records its *spots*: where staff work, where customers and residents sit or stand (with
  what they do there), and where a crime inside would play out. `AFTOInteriorLife` fills rooms from those spots as
  officers come near and empties them once they've gone.
- **Import**: `Tools/Unreal/import_art.py` brings the FBX into `/Game/FTO/...` (vertex colours, materials by slot name,
  sockets squared up to scale 1 and no rotation, Nanite for opaque kit pieces) and can be re-run after any Blender change:
  (`FTO_IMPORT=characters`, `statics`, `vehicles`, `weapons` or `kit` limits a run to one group, and
  `FTO_IMPORT=clips FTO_CLIPS=HandsBehind` imports just the named animation clips)
  `UnrealEditor-Cmd.exe FTO.uproject -run=pythonscript -script="<repo>/Tools/Unreal/import_art.py"`
- **Audio**: `Tools/Unreal/make_audio.py` synthesises every sound effect from code (siren, whistle, horn, engine,
  radio squelch, chimes, alarm, fanfare, sad trombone, the knockdown bonk, gunshots, handcuffs, a scuffle) into
  `Art/Source/Audio` and imports them to `/Game/FTO/Audio` (`FTO_SOUNDS=SW_Bonk` rebuilds just the ones named).
- **Animation** needs no Animation Blueprint: `UFTOCharacterAnimInstance` samples the clips in C++ and blends
  idle/walk/run by speed, with full-body actions (tickets, cuffing, driving, riding along...) crossfading straight
  into one another, and an upper-body layer on top (aiming, or hands cuffed behind the back). Actors animating several
  people (a car's driver and passengers) pick each one's action per mesh. Two-person moves (cuffing, a struggle) lock
  the officer onto a spot beside the suspect (`AFTOCharacter::BeginSyncedAction`) so the two clips line up.
- **Materials**: `Tools/Unreal/create_materials.py` builds `Content/FTO/Materials`: `M_FTOBase` (vertex colour ×
  `Color` tint, glowing in its own colour by `Emissive`), `M_FTOGlass` (tinted see-through glass), `MI_FTOGlow`
  (the base material, glowing), `MI_FTOCity` (tinted per instance from custom data, for the instanced city) and
  `MI_FTOCityInterior` (the same, a little self-lit for rooms).
  Run: `UnrealEditor-Cmd.exe FTO.uproject -run=pythonscript -script="<repo>/Tools/Unreal/create_materials.py"`
- Nearly everything uses `M_FTOBase`. Engine primitives have no vertex colour, so they just take `Color`.
  Blender assets bake flat colours into vertex colours; vertex alpha = 1 marks tintable areas (uniforms, car paint).
- Only free (CC0) or in-house assets. Record any third-party asset and its licence in `docs/Credits.md`.
