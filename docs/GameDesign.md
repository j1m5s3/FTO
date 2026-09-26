# FTO — Game Design (living document)

> Working title. "FTO" = Field Training Officer.

## Pitch
A 4-player online co-op party game. You and three friends run a city police precinct for one shift. Crimes pop up all over a living, medium-sized city — from someone stealing a garden gnome to a full-blown bank heist to a (cartoonishly incompetent) terrorist plot. Every call you ignore, every traffic violation you let slide, pushes the city's **Chaos Meter** up. Hit 100% and the city descends into slapstick anarchy and your shift is over.

The job is heavy; the game isn't. Think *Overcooked* meets a toy-box *GTA*: bright, chunky, low-poly art, bean-shaped officers, silly suspects, physics comedy.

## Pillars
1. **Party first.** Readable chaos, shouting at your friends, clutch saves. Sessions are short and replayable.
2. **Triage under pressure.** There are always more calls than cops. Deciding what to ignore is the game.
3. **A city that lives.** Pedestrians, traffic, and crimes that happen in front of you whether or not anyone called them in.
4. **Never the same shift twice.** Crimes are procedurally generated from templates, modifiers and escalation chains.
5. **Light-hearted.** No gore. Suspects are "bonked" and "cuffed", not killed. Crime flavour text is jokey.

## Core loop (one shift ≈ 20–30 min)
1. Shift starts at the precinct. Dispatch board shows incoming calls.
2. Players split up: take calls, patrol, run traffic stops.
3. Unattended incidents tick the Chaos Meter up; resolving them pulls it down.
4. Incidents escalate if ignored (a domestic dispute becomes a standoff; a shoplifter becomes a getaway chase).
5. Survive to end of shift → score, grade, silly headline ("LOCAL COPS ONLY MILDLY RESPONSIBLE FOR MAYHEM").

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
- **Arrest & transport**: bring suspects back to the precinct for bonus relief.

## City
Medium-scale procedural grid city (a few districts: downtown, residential, industrial, waterfront), roads, sidewalks, NPC pedestrians and traffic. Built from modular low-poly pieces so it can be generated and later hand-dressed.

## Multiplayer
- Up to 4 players, listen-server (host plays) as the default; dedicated-server-ready code (all gameplay is server-authoritative and replicated).
- Online subsystem: Null for LAN/dev; Steam or EOS for release (both free).

## Art & animation
- Stylized low-poly, bright palette, chunky proportions.
- Only free (CC0) or in-house assets: Kenney, Quaternius, Poly Pizza CC0, plus Blender-scripted in-house models and animations.
- Until art lands, everything is built from engine primitives ("bean cops"), which already fits the tone.

## Tech notes
- UE 5.8, C++ first. Gameplay systems in C++; Blueprints subclass for tuning and art.
- Input is created at runtime in C++ (Enhanced Input) so the project boots with no binary assets.
- Placeholder map: engine `Template_Default`; the city is generated at runtime.

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

Next ideas:
- Set-piece crimes: multi-suspect bank heist with a getaway, "evil masterplan" defusal minigame
- Day/night shifts and weather; night-time lights and headlights
- Steam (or EOS) sessions for invite-based online play
- Radio/voice chat, emotes, officer customisation
- Hand-dressed landmark blocks mixed into the procedural city
