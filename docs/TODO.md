# FTO TODO

What's left, in the order it's being built. Each item is one PR; tick it off when the PR is merged.
Details live in the [Phase 2 plan](Phase2Plan.md) and the [game design](GameDesign.md).

## Phase 2 (remaining)
- [x] **Arrests v2** (plan item 7): two-person synced cuffing (suspect kneels, officer cuffs); suspects resist by
  struggling (mash to subdue) or bolting on foot (sprint and tackle); cuffed suspects walk hands-behind to the cells.
- [x] **Destruction** (plan item 8): shattering windows, bullet holes and facade chips, props that break or topple
  as debris, vehicle damage states (dents, shed parts, smoke, fire), cartoon debris and dust on impacts.
- [x] **Radio** (plan item 9): push-to-talk team voice with a radio filter and squelch; quick callouts with no mic
  ("Need backup!", "Suspect fleeing!", "Officer down!", "10-4") that ping the map.
- [x] **Scoring** (plan item 10): per-officer points (arrests by tier, catches in the act, tickets, busts, revives;
  penalties for collateral and friendly fire), "+250 ARREST!" popups with combos, and an end-of-shift scoreboard
  that counts up with awards and grades while the squad dances (or slumps).
- [x] **Shift extension** (plan item 11): when the 20 minutes are up, the squad votes to extend the shift (chaos carries over) or
  clock off to the scoreboard; the host settles ties and unanswered votes.
- [ ] **Steam invites** (see [the plan](Phase2Plan.md#steam-invites-plan-not-implemented-yet)): OnlineSubsystemSteam
  sessions, invites and "Join game" from the friends list, IP join kept as a fallback. Dev uses `SteamDevAppId=480`.
  - [ ] Owner: register FTO's own Steam App ID (Steamworks, $100) and swap it in for release.
  - [ ] Owner: test on two PCs with two Steam accounts (Steam won't run two logins on one machine).

## Phase 3: James's eight upgrades
Each item is one PR, reviewed before it's merged.
- [x] **Crimes that play out** (upgrade 5): longer response windows; crooks visibly commit the crime (victims,
  brawlers, graffiti, vandals smashing street furniture, shoplifters filling a sack); some run from the police or
  leave with the goods, and a getaway becomes a search for the suspect by description.
- [x] **Sound** (upgrade 4): footsteps by surface, heavier and more realistic synthesised effects, more variety.
- [x] **Tougher cars** (upgrade 3): more health, persistent dents and scrapes where they're hit, BeamNG-style wear.
- [x] **Talking to people** (upgrade 7): conversations for information and chit-chat, stop and search, arrests (with
  a chaos cost for arresting someone who's clean).
- [ ] **Upper floors** (upgrade 6): stairs and lifts in multi-storey buildings.
- [ ] **Building destruction** (upgrade 2): walls broken systematically, cars through walls, buildings levelled.
- [ ] **Characters** (upgrade 1): detailed in-house models on the UE5 mannequin skeleton with Epic's free
  animations, plus weapon-handling fixes.
- [ ] **Hand-to-hand fighting** (upgrade 8): punches, kicks, grabs and throws with weighty, physical reactions.

## Backlog (later ideas)
- [ ] Set-piece crimes: multi-suspect bank heist with a getaway, "evil masterplan" defusal minigame
- [ ] Day/night shifts and weather; night-time lights and headlights
- [ ] Emotes, officer customisation
