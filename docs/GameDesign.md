# FTO — Game Design (living document)

> "FTO" = Field Training Officer.

## Decided
- **Title**: FTO.
- **Camera**: third person (over the shoulder when a weapon is up; chase or seat view in a cruiser).
- **Hosting**: listen server, the host plays (the code stays dedicated-server ready).
- **Shift length**: 10 minutes (overtime adds 5). When the clock runs out the squad gets an end-of-shift prompt to **extend the shift**
  (another round of time with the chaos carried over) or clock off to the scoreboard. Everyone votes; the host's
  choice settles a tie or a vote nobody answers.

## Pitch
A 4-player online co-op party game. You and three friends run a city police precinct for one shift. Crimes pop up all over a living, medium-sized city — from someone stealing a garden gnome to a full-blown bank heist to a (cartoonishly incompetent) terrorist plot. Every call you ignore, every traffic violation you let slide, pushes the city's **Chaos Meter** up. Hit 100% and the city descends into slapstick anarchy and your shift is over.

The job is heavy; the game isn't. Think *Overcooked* meets a toy-box *GTA*: bright, chunky low-poly cities with detailed, life-like (but still playful) people, silly suspects, physics comedy.

## Pillars
1. **Party first.** Readable chaos, shouting at your friends, clutch saves. Sessions are short and replayable.
2. **Triage under pressure.** There are always more calls than cops. Deciding what to ignore is the game.
3. **A city that lives.** Pedestrians, traffic, and crimes that happen in front of you whether or not anyone called them in.
4. **Never the same shift twice.** Crimes are procedurally generated from templates, modifiers and escalation chains.
5. **Light-hearted.** No gore. Suspects are "bonked" and "cuffed", not killed. Crime flavour text is jokey.

## Core loop (one shift = 10 min, extendable)
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

## The shape of a shift
- **Set piece**: 45% of the way into the shift, one whole-squad event, in turn shift by shift: a bank heist that
  turns into a getaway chase if it isn't stopped within 75 s; a bomb to defuse by cutting the right three wires from
  riddles on its label (150 s fuse, wrong wires cost 25 s; it goes off if the clock runs out); a city-wide pursuit of
  a very tough car.
- **Rush hour**: the last two minutes, crimes come about three times as often and up to three more at once.

## Comedy
- A mutator every shift (rolled from the seed): low gravity, bouncy cars, hot dog suits for every crook, big heads.
- Absurd crimes are common (mimes, pigeons, yodellers, gnome smugglers), and the "Costumed"/"Bizarre" twists turn up
  on half of all calls.
- The Daily Siren: every Major or Critical incident, handled or not, gets a silly headline on the front page.

## Juice
- Screen shake for explosions, crashes, collapses and punches; slow motion for tackles, cars through walls and blasts.
- The dispatcher: a synthesised radio voice (in-house formant synthesis, subtitled) that roasts the squad.
- The shift highlight: a photo of the best bust, pinned under the scoreboard.

## Teamwork
- Jobs built for two: shoot out a getaway's tyres from the passenger seat while your partner drives; guard a burgled
  building's door while your partner searches it. Both score TEAMWORK!.
- The squad combo: the whole squad's good work in quick succession builds one streak that multiplies everyone's
  points (to x1.5, the steps smaller in a bigger squad), more when officers take turns; a penalty knocks it back.

## Pacing
- A new crime every 12 s or so in a calm city, down to every 4.5 s as chaos climbs (solo slower: x1.5; two officers
  x1.1). The first one comes 2 s after the briefing. Up to 4 + 2 per officer at once.
- Three in four new crimes land a short run or drive from an officer (25-60 m, just out of sight, else up to 120 m),
  the squad taking turns (skipping anyone who's down), so there's always something close; the rest land anywhere.
- Grades are per officer and per ten minutes on the clock (overtime doesn't make an S easier).

## Incidents (procedural)
Every incident is built at runtime from a **template** + **modifiers**:

| Tier | Examples | Officers needed | Notes |
|---|---|---|---|
| Petty | Shoplifting, jaywalking, graffiti, noise complaint, parking | 1 | Frequent, low chaos |
| Minor | Traffic stop, domestic dispute, bar fight, vandalism | 1–2 | The daily grind |
| Major | Armed robbery, car chase, burglary ring | 2–3 | Can escalate |
| Critical | Bank heist, hostage situation, "terrorist" plot | 4 | Rare, whole-team events |

Modifiers (random): *in progress* (witnessable), *armed*, *fleeing*, *repeat offender*, *drunk*, *absurd* (e.g. suspect is dressed as a hot dog). Escalation chains turn ignored incidents into higher-tier ones.

**Twists:** the everyday crimes each play differently, so it's never just "walk up and press E":
- Pickpocket: hidden in a crowd of look-alikes; picked out by the description (talk, search, arrest).
- Burglary: the burglar hides in the building, often upstairs; the squad searches room by room.
- Drunk and Disorderly: a conversation; the friendly answer talks them round, the wrong ones start a fight.
- Bar fight / street brawl: takes two officers to pull apart (or one handy with their fists).

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
- Stylized low-poly city in a bright palette; the people are detailed and life-like (Epic's mannequin proportions and animations, our own models and clips), with a sense of fun.
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

- Phase 2: arrests v2. Every arrest is a two-person animation: the suspect drops to their knees, hands on head, the
  officer steps in behind them and cuffs them, and the cuffed suspect gets up and walks to the cells with their hands
  behind their back. Talk a crook down (stand at the scene) and they give up; try to arrest them sooner and they
  might come quietly, fight back (a struggle: mash E to wrestle them down, partners can pile in, and lose and you're
  shoved over) or bolt on foot (sprint after them and tackle them, or whistle to stop them for a moment). Floored
  suspects (shot, tased, tackled or run over) kneel for the cuffs; car chases end with the driver climbing out and
  giving up beside the car. A suspect who outruns everyone, or is left kneeling alone, gets away.

- Hand-to-hand fighting (upgrade 8). Officers and the people they're up against fight with their fists: punches
  that string into combos, kicks, and grabbing someone and throwing them over. It's built to feel weighty and loose:
  every blow lands at the clip's moment of contact, the one who takes it reels the way it came from with their upper
  body knocked loose under physics, and a big one staggers them back. Blows add up (grogginess wears off if you stop),
  and enough of them puts someone on the floor in a ragdoll, which for a suspect means they're caught. Suspects who'd
  rather fight than come quietly (brawlers, drunks, muggers) square up, close in and swing back; bar fights and street
  brawls are a proper scrap before the police arrive. Officers roughing up citizens costs chaos.

- Phase 2: the radio. Push-to-talk squad voice through a walkie-talkie filter with squelch, and a callout wheel
  (need backup, suspect fleeing, officer down, 10-4) that needs no mic and drops pings on everyone's HUD. Going down
  calls "officer down" automatically.

- Phase 2: scoring. Every officer scores for arrests (by tier), busts, calls handled, assists, catching crimes in the
  act, bookings, tickets and revives, and loses points for hurting citizens or shooting partners. Quick work builds a
  combo (up to x3) that any penalty breaks, and "+250 ARREST! x1.5" pops up where it happened. At the end of the
  shift the squad lines up outside the precinct, dancing (or slumping), while a scoreboard counts up, ranks them,
  hands out awards (Top Cop, Traffic Warden, Bull in a China Shop...) and grades the shift S to F.

- Shift extension. When the clock runs out the city holds still while the squad votes: five minutes of
  overtime (chaos and all), or clock off to the scoreboard. Most votes win; the host breaks ties and speaks for anyone
  who stays quiet.

- Phase 2: destruction. Shop windows shatter into showers of glass, every round leaves a pock mark (cars carry
  theirs around), and street furniture gives way: bins, news boxes, mailboxes and parking meters go flying, benches,
  planters, fences and bus stops smash to chunks, hydrants burst into fountains, and lamp posts, traffic lights and
  trees topple when a cruiser hits them fast enough. Cars take knocks from crashes and gunfire: panels fly off, the
  body crumples (a beaten-up model of every car), smoke pours from the bonnet, then it catches fire and scorches, and
  finally it's a write-off (a wrecked getaway car ends the chase; the motor pool fetches wrecked cruisers). The debris
  is Chaos physics, made on each machine; what's broken is decided by the server and the same for everyone. The
  police breaking things costs chaos and goes on the report card.

- Building destruction (in the spirit of The Finals). Every building but the precinct is a structure of wall panels,
  storey on storey, and every panel can be knocked down. A car hitting a wall cracks it (cracks spread and the paint
  goes dusty the more it takes); hit one head on at about 65 km/h or more and the car goes straight through, leaving a
  car-wide hole and a battered car. Rounds chip away at a wall (a rifle quicker than a pistol), and a burning car
  burns down until it blows, cracking the walls round about, blowing out windows and street furniture, throwing
  people off their feet and setting other cars going. It's systematic: a panel holds up the one above it and props up
  its neighbours a panel or two either side, so knock out a wide enough stretch and what's above drops off; lose too
  much of a storey (about 40%, or nearly all of one side) and everything from there up comes down in a cloud of
  dust, leaving a heap of rubble on whatever's left. A building that's come down has nobody in it and no more crime.
  Knocking walls down costs chaos, and bringing a building down costs a lot.

Next: see [Phase 2 plan](Phase2Plan.md) (Steam invites),
tracked in [TODO](TODO.md).

Later ideas:
- Day/night shifts and weather; night-time lights and headlights
- Emotes, officer customisation
