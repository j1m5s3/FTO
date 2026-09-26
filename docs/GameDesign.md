# FTO — Game Design (living document)

> "FTO" = Field Training Officer.

## Decided
- **Title**: FTO.
- **Camera**: third person (over the shoulder when a weapon is up; chase or seat view in a cruiser).
- **Hosting**: listen server, the host plays (the code stays dedicated-server ready).
- **Shift length**: 20 minutes. When the clock runs out the squad gets an end-of-shift prompt to **extend the shift**
  (another round of time with the chaos carried over) or clock off to the scoreboard. Everyone votes; the host's
  choice settles a tie or a vote nobody answers.

## Pitch
A 4-player online co-op party game. You and three friends run a city police precinct for one shift. Crimes pop up all over a living, medium-sized city — from someone stealing a garden gnome to a full-blown bank heist to a (cartoonishly incompetent) terrorist plot. Every call you ignore, every traffic violation you let slide, pushes the city's **Chaos Meter** up. Hit 100% and the city descends into slapstick anarchy and your shift is over.

The job is heavy; the game isn't. Think *Overcooked* meets a toy-box *GTA*: bright, chunky, low-poly art, bean-shaped officers, silly suspects, physics comedy.

## Pillars
1. **Party first.** Readable chaos, shouting at your friends, clutch saves. Sessions are short and replayable.
2. **Triage under pressure.** There are always more calls than cops. Deciding what to ignore is the game.
3. **A city that lives.** Pedestrians, traffic, and crimes that happen in front of you whether or not anyone called them in.
4. **Never the same shift twice.** Crimes are procedurally generated from templates, modifiers and escalation chains.
5. **Light-hearted.** No gore. Suspects are "bonked" and "cuffed", not killed. Crime flavour text is jokey.

## Core loop (one shift = 20 min, extendable)
1. Shift starts at the precinct. Dispatch board shows incoming calls.
2. Players split up: take calls, patrol, run traffic stops.
3. Unattended incidents tick the Chaos Meter up; resolving them pulls it down.
4. Incidents escalate if ignored (a domestic dispute becomes a standoff; a shoplifter becomes a getaway chase).
5. Survive to end of shift → vote to extend the shift or clock off.
6. Clock off → score, grade, silly headline ("LOCAL COPS ONLY MILDLY RESPONSIBLE FOR MAYHEM").

## Chaos Meter
- Range 0–100, replicated from the server (`AFTOGameState`).
- Each active incident adds `ChaosPerSecond` while unresolved (more if it has escalated).
- Resolving an incident subtracts `ChaosRelief`. Witnessed-in-progress catches give a bonus.
- Passive decay when the city is calm; the director spawns crimes faster as chaos rises (feedback loop players must break).
- 100 = shift failed.

## Incidents (procedural)
Every incident is built at runtime from a **template** + **modifiers**:

| Tier | Examples | Officers needed | Notes |
|---|---|---|---|
| Petty | Shoplifting, jaywalking, graffiti, noise complaint, parking | 1 | Frequent, low chaos |
| Minor | Traffic stop, domestic dispute, bar fight, vandalism | 1–2 | The daily grind |
| Major | Armed robbery, car chase, burglary ring | 2–3 | Can escalate |
| Critical | Bank heist, hostage situation, "terrorist" plot | 4 | Rare, whole-team events |

Modifiers (random): *in progress* (witnessable), *armed*, *fleeing*, *repeat offender*, *drunk*, *absurd* (e.g. suspect is dressed as a hot dog). Escalation chains turn ignored incidents into higher-tier ones.

**Reporting:** incidents are either *called in* (appear on the dispatch board after a delay) or only *witnessable* (a player must see them happen on patrol). Witnessing is rewarded.

## Player activities
- **Respond to calls** from the dispatch board.
- **Patrol** on foot or by cruiser; witness crimes in progress.
- **Traffic stops**: pull over NPC cars, quick mini-interaction (ticket / warning / arrest).
- **Small calls**: cat in tree, lost tourist, noise complaint — cheap relief, fun filler.
- **Arrest & transport**: walk (or drive) suspects back to the precinct and into the holding cells to book them
  for bonus relief.
- **Community policing**: chat to shopkeepers, bartenders, customers and residents (tip-offs about crimes nobody has
  called in), and question suspicious characters, who sometimes crack and confess.

## City
Medium-scale procedural grid city (a few districts: downtown, residential, industrial, waterfront), roads, sidewalks, NPC pedestrians and traffic. Built from modular low-poly pieces so it can be generated and later hand-dressed.

## Multiplayer
- Up to 4 players, listen server (the host plays); dedicated-server-ready code (all gameplay is server-authoritative and replicated).
- Online subsystem: Null for LAN/dev; Steam or EOS for release (both free).

## Art & animation
- Stylized low-poly, bright palette, chunky proportions.
- Only free (CC0) or in-house assets: Kenney, Quaternius, Poly Pizza CC0, plus Blender-scripted in-house models and animations.
- Everything so far is in-house and Blender-scripted: characters, vehicles, and a modular building kit (bevelled walls,
  shopfronts, awnings, signs, rooftop clutter, furniture, street dressing) that the city is assembled from at runtime.

## Tech notes
- UE 5.8, C++ first. Gameplay systems in C++; Blueprints subclass for tuning and art.
- Input is created at runtime in C++ (Enhanced Input) so the project boots with no binary assets.
- Placeholder map: engine `Template_Default`; the city is generated at runtime from the building kit, every piece
  instanced (one component per piece, painted per instance) and Nanite where opaque.

## Roadmap
Done:
- Project scaffold, design doc, smoke-test harness
- Chaos meter, procedural crime director (20 templates, modifiers, escalation chains)
- Procedural toy-box city (downtown, suburbs, industrial edge, parks, precinct, bank)
- HUD: chaos meter, dispatch board, markers, report card, driving and escort panels
- Living city: animated citizens (tips, chats, whistle freezes) and traffic
- Traffic stops, siren pull-overs, car chases that end BUSTED or escaped
- Drivable cruisers (arcade handling, driver-authoritative netcode)
- Arrests: cuff, escort or drive, book at the precinct
- Lobby, host/join menu, new shift
- In-house art: officer, citizens, suspect, vehicles (Blender scripts), master material
- In-house audio: synthesised siren, whistle, radio, chimes, fanfare, sad trombone...
- Phase 2: animation set v2 and ragdolls (everyone can be knocked over, seeing stars, and get back up)
- Phase 2: vehicles v2. Real cabins behind see-through glass, with a citizen at the wheel of every car (and
  sometimes passengers), a fully kitted cruiser interior (MDT laptop, radio, radar, shotgun rack, cage, lightbar
  switches), officers sat visibly at the wheel, a seat-view camera, riding shotgun, and suspects sulking in the back.
- Phase 2: buildings v2. The city is built from an in-house modular kit and every ground floor is enterable and
  furnished: shops, diners, bars, offices, homes, warehouses, the bank (with its vault) and the precinct (front desk,
  briefing room, armory, holding cells). Streets get crossings, traffic lights, lamps, hydrants, benches, bins, bus
  stops, planters and trees.
- Phase 2: interior life and indoor crime. Every building has its people: the clerk, the cooks and waiters, the
  bartender, tellers, office workers at their desks, a forklift driver, the desk sergeant and quartermaster,
  customers on stools and in booths, families at home, and now and then a crook lying low who might confess when
  questioned. Rooms fill as officers approach and empty behind them. Crimes happen inside (hold-ups at the counter,
  bar brawls, domestic rows, burglaries, the heist in the vault) with everyone reacting, officers can witness them
  through shop windows, and suspects are walked into the holding cells to be booked.

- Phase 2: ragdolls in the world. Cruisers send anyone they hit at speed flying (citizens cost chaos and go on the
  report card), getaway cars plough through crowds, and officers have a flying tackle. Knocked-down characters ease
  out of the ragdoll into sitting up dazed with stars circling, then get back to their feet.

- Phase 2: the armory and real ballistics. Officers carry up to three guns (a taser to start; pistol, shotgun and
  rifle signed out of the precinct armory racks) with magazines, spare ammo and reloads. Every round is a real
  projectile with muzzle velocity, gravity and drag, flown on every machine: it drops over distance, punches through
  shop glass, ricochets off shallow hits and knocks whoever it lands on into a ragdoll. Armed perps (hold-ups,
  stand-offs, the heist) draw on officers who come close; floor one and the call is handled on the spot. Hitting a
  citizen costs chaos, and an officer hit by gunfire stays down until a partner helps them up.

Next: see [Phase 2 plan](Phase2Plan.md) (arrests v2, destruction, radio, scoring, shift extension, Steam invites),
tracked in [TODO](TODO.md).

Later ideas:
- Set-piece crimes: multi-suspect bank heist with a getaway, "evil masterplan" defusal minigame
- Day/night shifts and weather; night-time lights and headlights
- Emotes, officer customisation
