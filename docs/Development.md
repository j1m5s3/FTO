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
| `FTOShiftTimeLeft 0` | Set the shift clock (0 runs it out and starts the overtime vote) |
| `FTOVote Overtime` | Vote at the end of the shift (`Overtime` or `ClockOff`); works for any player |
| `FTOMutator LowGravity` | Change today's mutator (`LowGravity`, `BouncyCars`, `HotDogs`, `BigHeads`, `None`) |
| `FTOSetPiece Bomb` | Start a set piece now (`Heist`, `Bomb`, `Pursuit`; no argument for this shift's own) |

Launch flags: `-FTOQuickStart` skips the lobby and starts the shift immediately; `-FTOMutator=HotDogs` picks the
shift's mutator (`None` for none).

## Controls
- **On foot**: WASD / left stick to move, Shift to sprint, Space to jump. Q blows the police whistle (citizens
  freeze, nearby crimes get called in). E interacts: tickets, talking to people, taking a cruiser, or riding shotgun
  in one someone's already driving. F (B on a gamepad) is
  a flying tackle that bowls over whoever's in front (citizens cost a little chaos). Cuffed suspects follow in your
  footsteps: walk them into the precinct's holding cells to book them.
- **Hand to hand**: with no weapon raised, LMB (right trigger) punches (jab, cross, hook, uppercut, strung together
  if you keep them coming), G (right bumper) kicks (a roundhouse on the end of a run of punches), and F right up close
  to someone (not running) grabs hold of them and throws them over. Every blow rocks whoever takes it, their upper
  body knocked loose under physics for a moment, and leaves them groggier; enough of them, or a big one when they're
  already groggy, or a throw, and they go down. A suspect put down is caught (cuff them with E). A suspect you lay a
  hand on may fight back, run or give in, and brawlers may choose to fight rather than come quietly: they square up,
  close in and swing, and can put you down too. Roughing up citizens costs chaos. RMB raises a weapon; with one up,
  LMB fires.
- **Upstairs**: every building with floors above the ground has a way up. Towers, the bank and the precinct have a
  lift: steel doors on the street (by the corner, under a floor sign); E brings up the buttons (up a floor, down a
  floor, the top, the street) and everyone standing at the doors rides along. Every floor has a stop inside, with
  offices on the lower floors and flats higher up. Two-storey houses have stairs up the back wall to a doorway into
  the upstairs room.
- **Talking to people**: E on anyone (in the street, indoors, a victim at a scene) stops them for a word, and 1-4 (the
  d-pad) pick what to say: *Seen anything unusual?* (a tip-off about trouble nobody's reported, a sighting of a
  suspect you're searching for, which moves the search, or a victim's statement), *How's your day?* (chit-chat), *I'm
  going to search you* (hands up for a pat-down) then *You're under arrest*, and *That's all* (E, or walking away,
  also ends it). About one in eight people is carrying something they shouldn't; arresting them for it makes them a
  suspect caught red-handed (they may still come quietly, fight or run). A search that finds nothing costs the city a
  little goodwill (chaos), and arresting someone who's clean is a wrongful arrest: much more chaos and a score penalty.
  A suspect lying low talks like anyone else (if nervously; they may bolt), and a search turns up the goods. A crook
  lying low indoors may confess when asked what they've seen.
- **Arrests**: E on a suspect arrests them. One who's given up (talked down by standing at the scene, run to ground,
  or put on the floor) kneels and you step in behind them to cuff them. One who hasn't may come quietly, wrestle
  (mash E to wrestle them down before the meter drains; partners can pile in with E; lose and you're shoved over),
  put their fists up for a fight (see *Hand to hand*) or bolt on foot (sprint after them and tackle with F, or run them over; the whistle stops them for a moment).
  A suspect left kneeling with nobody about, or who outruns everyone, gets away into the crowd. Car chases end with the driver
  climbing out and kneeling beside their car.
- **Crimes play out**: crooks get on with it (a tagger sprays the wall a letter at a time, a vandal goes from bin to
  bench kicking them over, a shoplifter works the shelves filling a sack, a mugger has a victim with their hands up,
  brawlers trade blows) and some finish before you arrive and walk off with the goods. Others run when they see the
  police. A suspect who gets clean away lies low in the crowd: the call becomes a **search** (the board shows what
  they look like and how long's left, the marker sits where they were last seen and moves when a citizen phones in a
  sighting). Suspects in street clothes look like anyone else, so find whoever matches and talk to them (E); crooks
  ditch the striped jumper. Take a victim's statement (E) for the description and which way they ran. Lying low, a
  suspect may bolt if an officer gets close.
- **Every crime its twist**: standing at the scene talks most crooks down, but not all of them.
  - *Pickpockets* work a crowd: the board has their description from the start, and the crowd is full of people who
    look a bit like them (the same outfit, or the same colour top). Talk to people (E), search the one who matches and
    arrest them; searching or arresting the wrong one costs goodwill as usual.
  - *Burglars* hide somewhere in the building, upstairs if it has an upstairs (take the lift, or a house's outside
    stairs). Get a look at them and they give up, have a go, or run. Leave it too long and they slip out with the goods.
  - *Drunks* (Drunk and Disorderly) are talked round: E to talk, then pick the friendly answer three times (the options
    are shuffled). Two answers that wind them up and they swing for you. Talked round, they wave you off (a taxi home); option 4 just ends the chat. Wound up, they fight, and put on the floor they can be cuffed.
  - *Brawls* (bar fights, street brawls) take two officers to pull apart; on your own, put them down with your fists.
- **Weapons** (on foot): every officer carries a taser; the precinct armory racks hand out a pistol, a shotgun and a
  rifle (three slots; E at a rack takes one, swaps it for the one in hand when you're full, or restocks its ammo).
  1, 2, 3 or the mouse wheel pick a weapon (the same number again puts it away), right mouse raises the last one
  used (left trigger on a gamepad), left mouse fires (right trigger), R reloads (right bumper). A perp put on the
  floor is subdued (cuff them with E); hitting a citizen costs chaos. Armed perps shoot back: an officer who's hit
  goes down until a partner helps them up (E), or comes round after a while.
- **Driving a cruiser**: W/S to drive and brake, A/D to steer, Space for the handbrake (drift). Q switches the lights
  and siren (offending cars ahead pull over), H honks. C (right stick click) swaps the chase camera for the view from
  the seat, and the mouse / right stick glances around (it eases back to the road). E gets out. Anyone you hit at
  speed goes flying (and hitting citizens costs chaos), and so does street furniture: bins and hydrants at a jog,
  lamp posts and trees only flat out. Every knock dents the car right where it landed, as deep as it was hard, with
  the paint scraped to bare metal round it (another knock there goes deeper), and grinding along a wall scrapes the
  paint. Cruisers take a beating before they smoke, catch fire and are written off (the motor pool fetches a
  write-off back to the lot, good as new, once it's been left empty for a while).
- **Riding shotgun**: the mouse / right stick looks around, Q works the lights and siren, C swaps cameras, E gets out.
  The window's open: 1-3 pick a weapon (the first LMB brings out the last one used) and LMB fires out of it. Two rounds
  in a getaway car's tyres stop it dead (the driver gives up), and that's teamwork for the gunner and the driver.
- **Teamwork**: some jobs want two. One drives while the other shoots out a getaway's tyres; one guards a burgled
  building's front door while the other searches it (a burglar who bolts or tries to slip out runs straight into the
  guard). With a partner in on it (a player at the wheel; someone else on the shift), both score TEAMWORK!. The whole squad shares a streak too: every bit of good work
  within 20 s of the last adds to it, up to x1.5 on everyone's points (0.1 a step solo, less in a bigger squad; another 0.1 if officers are taking
  turns: TAG TEAM!), shown under the chaos meter; a penalty knocks it back three steps.
- **Radio** (anywhere, on foot or in a car): hold V (d-pad down) to talk to the squad; teammates hear you through a
  walkie-talkie filter with a squelch at each end, and see who's on air. Hold T (d-pad up) for the callout wheel:
  point with the mouse / right stick and let go, or press 1-4: *Need backup!*, *Suspect fleeing!* (pings the nearest
  getaway), *Officer down!* (pings the nearest downed partner) and *10-4*. Pings show on everyone's HUD for 20 s.
  Going down calls *Officer down!* for you. Voice needs a microphone and uses the engine's VOIP (push-to-talk only).
- **The shape of a shift**: a little under halfway through, the shift's set piece starts (a banner, a siren, every
  officer told). They take turns, shift by shift: a *bank heist* (an armed crew in the vault; leave them 75 s and
  they make off in a car, a tough one, and it's a chase), an *Evil Masterplan* (a bomb ticking downtown: E for the
  wires, and the label says which to cut next, "the colour of a rubber duck"; a wrong wire takes 25 s off the clock,
  three right ones defuse it, and if it goes off it takes the windows, walls and everyone near with it) and a
  *city-wide pursuit* (the city's most wanted in a car three times tougher than most). The last two minutes on the
  clock are *rush hour*: crimes come about three times as fast and there can be three more at once.
- **Comedy**: every shift rolls a mutator, announced at roll call and shown under the clock: *low gravity* (floaty
  jumps, slow-falling ragdolls and debris), *bouncy cars* (every car on hydraulics; cruisers bounce off walls with a
  boing), *hot dog day* (every crook, and a pickpocket's whole crowd, in a hot dog suit) or *big heads*. Absurd calls
  turn up as often as anything else (a mime in an invisible box, unlicensed pigeon feeding, competitive yodelling,
  garden gnome smuggling), and half of all calls come with a twist on the board. Every big incident, handled or not,
  makes the front page of *The Daily Siren*, which slides in for everyone with a silly headline; the shift's last
  front page heads the scoreboard.
- **End of shift**: when the 10-minute clock runs out the city holds still and everyone votes: Y (left bumper) for 5
  minutes of overtime (chaos carries over; the clock reads OVERTIME), N (view button) to clock off. Most votes win;
  the host's vote breaks a tie and decides for anyone who says nothing within 20 s. Clocking off (or the city falling)
  lines the squad up outside the precinct for the scoreboard.

## Sharing a build
`powershell -ExecutionPolicy Bypass -File Tools/Build/package.ps1` builds, cooks and packages a Windows
Development build into `Build/Package/Windows` (about 900 MB). Zip that folder and send it: friends just run
`FTO.exe`, with no Unreal install needed. Add `-Config Shipping` for a lean release build (no console or dev tools).
The smoke test also runs on the packaged game: `FTO.exe -windowed -FTOSmokeTest -FTOSmokeTestQuit`.

## Smoke test
`UnrealEditor.exe FTO.uproject -game -windowed -ResX=1600 -ResY=900 -FTOSmokeTest -FTOSmokeTestQuit`
tours the city and writes screenshots to `Saved/Screenshots/SmokeTest/`. Handy after any gameplay or art change.
Along the way it books a suspect into the cells (and checks they scored), checks the shift is ten minutes and that
the director puts new crimes a short run from the officer, runs the shift clock out and votes for
overtime, runs it out again and clocks off to the scoreboard with the squad lined up dancing outside the precinct,
looks inside every kind of building (with their people), stages a
hold-up and a bar brawl, questions a crook, walks through front doors, checks that shop windows let sight through,
drives into three citizens, tackles one, signs a shotgun out of the armory, trades fire with an armed robber (then
cuffs them where they fell), has a downed officer helped up (the radio calls it in), makes the arrests that don't go
quietly: a brawler wrestled down, a vandal who wins the struggle and runs (and is tackled), and a getaway driver who
gives up beside their car, watches a mugger walk off with the goods and tracks them down in the crowd (a word, a search, the cuffs), stops
and searches citizens (arresting one caught carrying and, wrongly, one who was clean), rides a lift to the top of a
tower and climbs a house's outside stairs, starts the shift's set piece on schedule (the heist, for a first shift), defuses a bomb (after a wrong wire) and
lets another go off, watches the heist crew make off in a getaway car and starts a city-wide pursuit, runs the clock
into rush hour, picks a pickpocket out of a crowd of look-alikes (searching a bystander first),
finds a burglar hiding upstairs, talks a drunk round (after one answer that winds them up), checks one officer can't
break up a bar fight, shoots out a getaway car's tyres, tries every mutator (the officer floating up a jump in low gravity, cars on their
hydraulics, a mime in a hot dog suit, a big head), makes the front page, catches a burglar at the door they're guarding, builds the
squad's streak and breaks it, films a tagger
and a vandal at work, then opens the callout wheel and keys the radio, then shoots out a shop window and a
hydrant, knocks a lamp post flat with a cruiser, crashes the cruiser until it's a burning wreck, and writes off a
citizen's car; the log (`SMOKE:` lines) reports each check.
For a two-player check, run a listen-server host and a client (see *Play*) both with `-FTOSmokeTest -FTOSmokeTag=host`
(or `client`) and `-FTOSmokeRideAlong`: the host parks in a cruiser, and the client rides shotgun, looks around, fires out of the window, gets out
and arrests a shoplifter the host puts beside them. Then the client calls for backup and stays on air, and the host
checks it heard the call and has the client's voice going through the radio filter (the client checks the same for the
host). Finally the client checks it sees everything the host broke broken too, and the shift's set piece, the front pages and the mutator.

## Art pipeline
- **Epic's mannequin content first**: the cast is built on Epic's UE5 mannequin skeleton and plays Epic's engine
  animations (walking, jogging, jumping, holding a pistol or rifle) and ragdoll (`PA_Mannequin`). That content ships with
  every engine install but isn't ours to publish, so it's git-ignored: after cloning, run
  `python Tools/Unreal/install_epic_content.py` once (it copies it to `Content/Characters/Mannequins`).
- **Characters** (on the UE5 mannequin skeleton, so they also play Epic's engine animations):
  `Tools/Blender/build_characters.py` builds `SK_Officer`, `SK_Officer_F` (in `Art/Source/Characters/Officer`,
  plus `Officers.blend`), `SK_Civilian_01..08` and `SK_Suspect` (in `Art/Source/Characters/Civilians`, plus
  `Civilians.blend`):
  `blender -b --factory-startup -P Tools/Blender/build_characters.py -- --out Art/Source/Characters --preview Art/Previews/Characters`
  (`--only SK_Officer,SK_Suspect` rebuilds just those.) Each body is one smooth, watertight surface: skin, clothes,
  hair and hats are signed distance fields (`Tools/Blender/fto_sdf.py`, numpy only) stacked as layers and meshed
  with surface nets, then decimated to about 10k triangles, so clothes can't clip through a bent joint and hems are
  small steps. Faces take the colour of the layer they lie on and the mesh is cut exactly where layers meet (crisp
  hems, hairlines, stripes). Hands are separate meshes with jointed fingers; eyes, brows, mouth, badges, pouches and
  buttons are props placed on the surface. Weights come from bone proximity per body region (an arm never pulls the
  ribs, one leg never the other), smoothed over the surface; the head is rigid above the jaw; props take the
  weights under them; at most four influences. `Col` vertex alpha 1 = tinted in game (the uniform shirt, civilians'
  tops). About 12-15k triangles each. Casting (build, skin tone, hair, outfit) is the `CAST` table at the top.
- **Clips**: `Tools/Blender/build_character_anims.py` exports our own clips on that skeleton as
  `Art/Source/Characters/Anims/A_FTO_<Clip>.fbx` (armature only, 30 fps, root left at the origin), all of them in
  `Anims.blend`, and `fight_timing.json` (per fighting clip: contact frame/time, reach in cm from the root, the bone
  that lands it, the contact point, and `travel_cm` for clips whose body moves away from the root, e.g. the heavy
  stagger and knockback):
  `blender -b --factory-startup -P Tools/Blender/build_character_anims.py -- --out Art/Source/Characters/Anims --preview Art/Previews/Characters/Clips`
  (`--clips Jab,Cross` for just some; the preview poses `SK_Officer` from `--mesh`, default
  `Art/Source/Characters/Officer/Officers.blend`, so build the characters first). Clips are written in
  `Tools/Blender/character_clips.py` as keyed controls solved by `Tools/Blender/fto_pose.py`: IK hands and feet
  (planted feet stay put while the hips shift), spine and neck bends spread over the chain, finger curls, and lag
  on the head, fingers and wrists for overlap. Loops: `Sit`, `Drive`, `Ride`, `Talk`, `Work`, `HandsUp`, `Kneel`,
  `Cuffed`, `HandsBehind`, `Cower`, `Dance`, `Dance2`, `Slump`, `Dazed`, `SitCuffed`, `SitHandsUp`, `Struggle`,
  `Idle_Bored`, `Phone`, `Wave`, `Point`, `Clipboard`, `Search`, `SearchedPose`, `Spray`, `Smash`, `Grab`
  (rummaging), `Sneak` (1 m/s). One-shots: `Cuffing` (2.6 s), `Tackle`, `Interact`, `Cheer`. Fighting: `Jab`,
  `Cross`, `Hook`, `Uppercut`, `Kick_Front`, `Kick_Side`, `Kick_Roundhouse`, `Shove`, `Fight_Grab` (clinch start;
  `Grab` was taken by the rummage), `Throw`, `Block_Loop`, `HitReact_Light_Front/Back/Left/Right`, `HitReact_Heavy`,
  `Knockback`, `GetUp_Front`, `GetUp_Back`, `Taunt`, `Fight_Idle`, `Fight_Step_Fwd/Back/Left/Right` (1 m/s).
  Spots the clips assume: `Sit` is a 45 cm chair (hip joints 53 cm up, 4.5 cm behind the root, feet 44 cm ahead);
  the car clips (`Drive`, `Ride`, `SitCuffed`, `SitHandsUp`) sit with the hip joints 40 cm up and 5 cm behind the
  root, feet about 60 cm ahead, and `Drive`'s wheel is centred 40 cm ahead of the root at 88 cm, tilted 25 degrees
  towards the driver, 36 cm across. `Cuffing` has the suspect kneeling (`Cuffed`) 76 cm ahead facing away;
  `Search` has them 55 cm ahead facing away (`SearchedPose`, hands on a wall 68 cm ahead of them); `Struggle` and
  the clinch are face to face about 50-55 cm apart. `Knockback` ends lying face up 110 cm behind the root, where
  `GetUp_Back` starts (it and `GetUp_Front` start lying with the pelvis over the root). Walk/run/jump/aiming come
  from Epic's mannequin animations.
- `Tools/Unreal/import_art.py` imports the cast onto Epic's skeleton (`/Game/Characters/Mannequins/Meshes/SK_Mannequin`)
  with `PA_Mannequin` as their ragdoll, and our clips to `/Game/FTO/Characters/Anims` (`FTO_IMPORT=clips FTO_CLIPS=Jab`
  for just some). Shirts are tinted per pedestrian at runtime. (`build_officer.py` and `build_civilians.py`, the old
  cartoon cast on its own skeleton, are kept only for the seated figures in `build_vehicles.py`'s previews.)
- `Tools/Blender/build_vehicles.py` builds the cars (sedan, hatchback, van, pickup, taxi, ice cream truck, cruiser) and
  a shared wheel as static meshes facing +X. They're real shells: doors, floor, dashboard, seats and a steering wheel
  behind see-through glass, plus the cruiser's police kit (MDT laptop, radio, radar, shotgun rack, cage, lightbar
  switches, and donuts). Three material slots: `Body` (vertex colour, alpha 1 = paint the game tints), `Glass` and
  `Glow` (lights, dials, screens). `SOCKET_*` empties become sockets: `Wheel_*`, `Seat_*` (where a seated character's
  root goes), `Cam_*` (seat-view camera) and the cruiser's `Lightbar`. Seats are sized from the officer's car-seat
  pose (`build_officer.car_legs`); `--preview <dir>` also renders roofless cutaways with posed occupants and the
  driver's-eye view to check the fit. Every body is diced so no edge is longer than 14 cm (`dice()`): flat panels
  need vertices in the middle for the game to push its dents into. `--dented` also exports each body's beaten-up
  twin (`*_Dented`: crumpled nose and tail, a tented bonnet, knocked-in doors; same slots and sockets) that the game
  swaps in for a write-off (`--dented_only` just those, `--only SM_Car_Taxi,...` just some cars). The importer gives
  car bodies the dentable materials (`M_FTOVehicle`, `MI_FTOVehicleGlow`, `M_FTOVehicleGlass`).
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
  `FTOCityUpperFloors.cpp` adds the upper storeys (slabs, lights, furniture), plans each lift (`AFTOLift`, one actor per
  stop, spawned on the server with the crime spawn points) and builds houses' outside stairs.
  Furnishing a room also records its *spots*: where staff work, where customers and residents sit or stand (with
  what they do there), and where a crime inside would play out. `AFTOInteriorLife` fills rooms from those spots as
  officers come near and empties them once they've gone.
- **Import**: `Tools/Unreal/import_art.py` brings the FBX into `/Game/FTO/...` (vertex colours, materials by slot name,
  sockets squared up to scale 1 and no rotation, Nanite for opaque kit pieces) and can be re-run after any Blender change:
  (`FTO_IMPORT=characters`, `statics`, `vehicles`, `weapons` or `kit` limits a run to one group,
  `FTO_IMPORT=clips FTO_CLIPS=HandsBehind` imports just the named animation clips, and `FTO_MESHES=SM_Car_Van_Dented`
  just the named vehicle or weapon meshes)
  `UnrealEditor-Cmd.exe FTO.uproject -run=pythonscript -script="<repo>/Tools/Unreal/import_art.py"`
- **Audio**: every sound effect is synthesised from code, no samples. `Tools/Audio/fto_synth.py` is plain Python
  (numpy + scipy, no Unreal; `python -m pip install numpy scipy`) and renders 44.1 kHz 16-bit mono WAVs:
  `python Tools/Audio/fto_synth.py --out Art/Source/Audio [--only SW_A,SW_B] [--list]` (the whole set takes seconds).
  Each sound is seeded from its name, so `--only` gives the same file as a full rebuild. The rendered WAVs are
  committed in `Art/Source/Audio` (Git LFS). `Tools/Unreal/make_audio.py` imports them to `/Game/FTO/Audio`, flags
  the loops and creates `SA_FTOWorld` (`FTO_SOUNDS=SW_Bonk` imports just the ones named, `FTO_RESYNTH=1` re-renders
  first, using `python` on PATH if the editor's Python lacks numpy).
  - Recipes are layered: a transient, a body and a tail, with modal (resonant partial) banks for metal, glass and
    wood, shaped noise, tanh saturation for weight and short room or outdoor slap-back tails. Levels differ on
    purpose: gunshots and wall breaks peak near full scale, footsteps around a third of that.
  - Loops (`SW_SirenLoop`, `SW_EngineLoop`, `SW_TireSkidLoop`, `SW_ElevatorLoop`, `SW_CityAmbienceLoop`,
    `SW_GushLoop`, `SW_FireLoop`) are seamless: built from whole periods and filtered circularly, or crossfaded at
    the wrap. The engine is a cross-plane V8 idle meant to be pitched 0.7x-2.2x.
  - Variants: the original names stay, and frequently repeated sounds also come numbered (`_01`...), to pick at
    random: `SW_ShotPistol/Rifle/Shotgun_01..04`, `SW_Crash_01..04`, `SW_Glass_01..03`, `SW_Clang_01..03`,
    `SW_Ricochet_01..03`.
  - Fighting: `SW_Punch_01..04`, `SW_Kick_01..03`, `SW_BodyFall_01..03`, `SW_Whoosh_01..03` (swings). No voices.
  - Footsteps: `SW_Step_<Surface>_01..06` and `SW_StepRun_<Surface>_01..04` for Concrete, Wood, Tile, Carpet, Metal
    and Grass, plus `SW_Land_Concrete_01..02` (jump landing) and `SW_Scuff_01..03` (shoe scrape).
  - Vehicles and destruction: `SW_CarImpactLight_01..03`, `SW_CarImpactHeavy_01..03`, `SW_MetalCreak_01..02`,
    `SW_Rubble_01..04`, `SW_WallBreak_01..03`, `SW_Collapse_01` (about 10 s), `SW_Explosion_01..03` (a car going up).
  - Buildings and city: `SW_DoorOpen`, `SW_DoorClose`, `SW_ElevatorDing`, plus the loops above.
  - `--list` prints every name with its loop flag.
  - In the game (`Source/FTO/Audio`): `FTOAudio::Vary` swaps any sound with numbered takes for a random one (every
    world sound goes through it, so gunshots, crashes and breaking glass never repeat exactly), `FTOAudio::Pick`
    picks from a family by name. `UFTOFootsteps` (on officers and everyone in the crowd) plays a step per stride on
    whatever's underfoot: rooms by what the building is (homes and bars wood, offices carpet, warehouses concrete,
    the rest tile), green ground grass, cars metal, everything else concrete; running steps are heavier and a jump
    lands with a thud. The crowd's steps are only heard close up. Cruisers squeal their tyres in a slide, crashes are
    a light or heavy impact (heavy ones with the metal groaning after), knockdowns land with a body fall, and the
    city hums under everything.
- **Animation** needs no Animation Blueprint: `UFTOCharacterAnimInstance` samples the clips in C++ and blends
  idle/walk/run by speed (Epic's `MM_Idle`, `MF_Unarmed_Walk_Fwd` and `MF_Unarmed_Jog_Fwd`, held in place, at 300 and
  600 cm/s), the take-off then a fall loop in the air, with full-body actions (our `A_FTO_<Action>` clips: tickets,
  cuffing, driving, riding along, spraying graffiti...) crossfading straight into one another, and an upper-body layer
  on top: Epic's pistol or rifle aiming pose (the chest, arms and fingers, and the hand IK bones), or hands cuffed
  behind the back. Looking up and down leans the spine, neck and head about the body's side-to-side axis, so the arms
  follow the aim. Guns go in the right hand where Epic's aiming poses put one (`FTOWeapons::HoldInHand`), so the hands
  are always on the gun. Actors animating several
  people (a car's driver and passengers) pick each one's action per mesh. Two-person moves (cuffing, a struggle) lock
  the officer onto a spot beside the suspect (`AFTOCharacter::BeginSyncedAction`) so the two clips line up.
- **Hand to hand** (`Source/FTO/Combat/FTOFighting`): one move table (from `fight_timing.json`: each clip's length,
  moment of contact and reach, plus how groggy and how far each blow sends them). `FTOFighting::Swing` plays a move
  on the fighter's `UFTOKnockdownComponent` (`PlayMove`: a short replicated full-body override every machine shows over
  whatever they're doing, the anim instance picking it up, with a serial so the same punch twice restarts) and lands it
  at contact on whoever's then in reach in front. `TakeHit` rocks them (`TakeBlow`: a reaction by the direction it came
  from, and on every machine the upper body below `spine_02` goes physics-simulated with a decaying blend weight under
  the blow's impulse, so it lolls and swings back) and adds to their grogginess; at 100 (or a heavy blow past 55, or a
  throw) `FTOImpact::Strike` floors them (a suspect floored by the police is caught; citizens cost chaos). Officers
  punch with Fire when unarmed (`ServerFight` strings the combo), kick with G, and grab-then-throw with F up close;
  suspects can answer an arrest, or a blow, by fighting (`AFTOPerp::BeginFighting`/`TickFighting`), and brawlers in bar
  fights and street brawls trade staged blows (rock, never floor) until the police arrive.
- **Destruction** (`Source/FTO/Physics`): `AFTODestruction` keeps the list of broken pieces of the city, a replicated
  fast array of instanced-component name plus instance index (every machine builds the city identically, so those
  agree everywhere). Each machine tucks the broken instance away and has `UFTODebris` put on the show: Chaos rigid-body
  chunks, glass shards and knocked-off props, fading bullet-hole decals, and hydrant fountains, all cosmetic and made
  per machine. `UFTOVehicleDamage` gives cars health (server-side) and the look to match everywhere: panels flying off,
  dents, smoke, fire and scorching. Dents are a short replicated list (up to 12: a centre on the paint, found by
  tracing the body's own triangles; a push; a radius; how much paint's scraped off) that the body's materials turn
  into moved vertices, bent normals and bare metal, so every machine sees the same crumples; a knock near an old dent
  deepens it. What breaks what (speeds, rounds, chaos) is the table in
  `FTODestruction.cpp`.
  Buildings are structures: while it builds the city, `AFTOCityGenerator` records every instance placed between
  `BeginStructure` and `EndStructure` (each building but the precinct) as an `FFTOStructurePiece` (component, instance,
  role: wall, glass, trim, floor, inside or foundation; and for the facade its face, column and storey), into
  `GetStructures()`, identically on every machine. `AFTODestruction` sorts those into cells (a panel on a face on a
  storey, glass and trim going with their panel). Walls wear down (`DamageWall`: rounds, `DamageAt`: crashes and
  blasts) through a replicated fast array of worn panels (cracks from `M_FTOCrackDecal` and darkening on every
  machine); at `WallStrength` a cell crumbles (its pieces join the broken list). `Settle` then works up the storeys:
  a cell stands if the one below it does, or if one that does is within two panels round the ring; anything else
  drops off whole (thrown by `UFTODebris`), and a storey with too little left brings everything above down as one
  replicated `FFTOCollapse`, which every machine plays out itself (instances tucked away at once, copies in
  collision-free proxy components falling into a dust cloud, then a deterministic heap of rubble with collision).
  A car going through a wall at `BreakThroughSpeed` or more breaks every panel its width covers (`WallsInTheWay`);
  burning cars burn down and call `Blast`. The crime director and `AFTOInteriorLife` skip buildings that are down.
- **Materials**: `Tools/Unreal/create_materials.py` builds `Content/FTO/Materials`: `M_FTOBase` (vertex colour ×
  `Color` tint, glowing in its own colour by `Emissive`, charred towards black by `Scorch`), `M_FTOGlass` (tinted
  see-through glass), `MI_FTOGlow` (the base material, glowing), `MI_FTOCity` (tinted per instance from custom data,
  for the instanced city), `MI_FTOCityInterior` (the same, a little self-lit for rooms) and `M_FTODecal` (a bullet
  hole: a deferred decal that fades out), and the dentable car materials `M_FTOVehicle` (M_FTOBase plus up to 12
  `DentN`/`PushN` dents moving the vertices, bending the normals and scraping the paint), `MI_FTOVehicleGlow` and
  `M_FTOVehicleGlass`, and `M_FTOCrackDecal` (cracks in a knocked wall, worked out in the shader).
  `FTO_MATERIALS=M_FTODecal` builds just the named ones.
  Run: `UnrealEditor-Cmd.exe FTO.uproject -run=pythonscript -script="<repo>/Tools/Unreal/create_materials.py"`
- Nearly everything uses `M_FTOBase`. Engine primitives have no vertex colour, so they just take `Color`.
  Blender assets bake flat colours into vertex colours; vertex alpha = 1 marks tintable areas (uniforms, car paint).
- Only free (CC0) or in-house assets. Record any third-party asset and its licence in `docs/Credits.md`.
