# FTO Phase 2 plan

Phase 1 delivered a playable co-op loop. Phase 2 makes the city deep, physical and loud, while keeping the
light-hearted tone: nobody dies. People get **knocked out** (cartoon stars, ragdoll, get back up or get cuffed),
and there's no gore. Everything stays in-house: art from Blender scripts, audio synthesised, VFX built in engine.

## Guiding choices
- **Lean on UE5**: Chaos physics (ragdolls, physics props, debris, vehicle impacts), Lumen (sunlight bouncing into
  interiors, emissive ceiling lights lighting rooms without hundreds of light components), Nanite for the dense
  building kit, Enhanced Input, VOIP over the online subsystem, and Niagara/Chaos Destruction where they can be
  authored from code.
- **Server-authoritative gameplay, client-side cosmetics.** Ragdolls, debris and tracers simulate locally on each
  machine; hits, damage, downs and arrests are decided by the server.
- **Everything activates near players.** ~450 buildings with interiors and ~900 interior NPCs can't all be
  simulated at once, so interiors populate within a radius of any officer and go dormant behind them.

## Features and delivery order
Each line is one PR. Done so far: 1 to 6.

1. **Animation set v2 + ragdoll foundation.** New clips: sit, drive, talk, work (cashier/typing), hands up,
   kneel, cuffed, cuffing, struggle, tackle, punch, cower, aim pistol, aim rifle, dance, slump. Upper-body layering
   (aim while walking) with aim pitch. Physics assets for every character. `Knockdown()` ragdolls anyone and gets
   them back up (replicated state, local simulation).
2. **Vehicles v2.** Real cabins: seats, steering wheel and dash behind see-through glass (new translucent glass
   material). An NPC driver visibly sits in every traffic car, sometimes with passengers. The squad car interior
   gets a laptop/MDT, radio, radar, shotgun rack and cage partition, and lightbar controls. Officers sit visibly
   when driving, with a chase/interior camera toggle, and a second officer can ride shotgun.
3. **Buildings v2: the city opens up.** A modular in-house building kit (beveled walls, recessed windows, awnings,
   parapets, rooftop units, signs) replaces the blocks, and every building's ground floor is enterable. Interiors by
   type: shop, bar, diner, bank (with vault), office, home, warehouse, and the precinct (front desk, briefing room,
   **armory**, holding cells). Street dressing: hydrants, benches, bins, traffic lights, bus stops, crosswalks.
4. **Interior life and indoor crime.** Every building has people: owners and workers (clerk at the counter,
   bartender, tellers, office staff), customers, residents, and criminals. Crime spawn points move indoors
   (robbery at the counter, bar brawl, domestic dispute in the living room, bank heist in the vault). Suspects are
   booked in the precinct holding cells.
5. **Ragdolls in the world.** Cars bowl people over (chaos penalty for civilians), tackles knock suspects down,
   and knocked-out characters ragdoll then recover.
6. **Armory, guns and real ballistics.** Pick up to three guns at the precinct armory (pistol, shotgun, rifle,
   plus a taser as a non-lethal sidearm). Over-the-shoulder aiming. Every shot is a simulated projectile with
   muzzle velocity, gravity drop, air drag and travel time, glass/thin-material penetration and ricochets, resolved
   by the server with client-side tracers. Hits knock people out (suspects become arrestable; civilians cost chaos).
   Armed suspects in big crimes shoot back; downed officers can be revived by teammates.
7. **Arrests v2.** Cuffing is a two-person synced animation (suspect kneels, officer cuffs). Suspects can
   **resist**: struggle (mash to subdue, with an animation) or bolt on foot (sprint + tackle, with an animation).
8. **Destruction.** Windows shatter, bullets leave holes and chip facades, and street props break or topple as
   physics debris. Vehicles dent (damage-state meshes), shed bumpers, doors and hoods, smoke, and catch fire at high
   damage. Impacts spawn chunky cartoon debris and dust. Big hits from speeding cars show real damage.
9. **Radio.** Push-to-talk team voice chat with a radio filter and squelch. Plus quick radio callouts with no mic
   needed ("Need backup!", "Suspect fleeing!", "Officer down!", "10-4") that ping the map.
10. **Scoring.** Per-officer points for everything (arrests by tier, catches in the act, tickets, busts, revives;
    penalties for collateral and friendly fire), animated "+250 ARREST!" popups with combo multipliers, and an
    end-of-shift scoreboard that counts up with awards and grades while the squad dances (or slumps on a loss).

## Steam invites: plan (not implemented yet)
Goal: friends join from the Steam friends list or an invite, and nobody types an IP.

- **Plugins**: enable `OnlineSubsystemSteam` and `SteamSockets` (UE ships the Steamworks SDK).
- **Config** (`DefaultEngine.ini`): `DefaultPlatformService=Steam`, `bEnabled=true`, `SteamDevAppId=480` for
  development (Valve's shared test app), then FTO's own App ID from Steamworks ($100 fee) for release;
  `bInitServerOnClient=true`; net driver `SteamSocketsNetDriver` with IP fallback; `bHasVoiceEnabled=true` so the
  radio uses Steam voice.
- **Sessions** (new `UFTOSessionSubsystem`, a `UGameInstanceSubsystem`): `CreateSession` with `bUsesPresence`,
  `bAllowJoinViaPresence`, `bAllowInvites`, max 4 public connections, when the host clicks *Host*; `JoinSession`
  plus `ClientTravel` to the resolved connect string; handle `OnSessionUserInviteAccepted` (Steam overlay invites
  and "Join game" from the friends list); destroy the session on leave/new shift.
- **Menu**: *Host* creates the session and shows "Invite friends (Shift+Tab)"; *Join* lists friends' games; the IP
  box stays as a LAN/dev fallback when Steam isn't running.
- **Packaging**: ship `steam_appid.txt` for dev builds; Steam must be running on each machine.
- **Testing**: two PCs with different Steam accounts (Steam won't run two logins on one machine), or the IP
  fallback for single-machine tests.
