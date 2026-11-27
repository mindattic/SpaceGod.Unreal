---
layer: bible
status: living
---
# SpaceGod — Project Bible

> Single source of truth for what SpaceGod IS, is NOT, and the rules that keep it coherent.
> README says how to build and run it; this says how to think about it. The finish line lives in
> `docs/RELEASE.md`.

## 1. The one sentence {#US-§1}
SpaceGod is a co-op space game in Unreal Engine 5.8 (C++ only) where the player is an unseen god over a simulated
galaxy who can drop into any ship, craft, station or suit and fly it, with one button switching between true scale
and a playable game scale.

## 2. The product promise {#US-§2}
- **Make flying fun** (owner's north star, US-T170): taking over any vehicle and cruising or fighting in it is a joy,
  from a twitchy light fighter to a lumbering freighter to a capital ship that rolls damaged facings away.
- **Real scale and game scale** (`ScaleModel`, G or backtick): every feature works and reads correctly in both.
- **Sol is real**: planets, moons and asteroids sit where JPL orbital elements put them for the game's date
  (the game starts 3026-01-01, docs/HISTORY.md). Systems outside Sol are procedural.
- **Possession**: from the god view the player takes a seat (helm, launched craft, turret, station, EVA worker,
  satellite) and sees what that pilot sees (docs/Possession.md); letting go returns the god's view.
- **Co-op**: one listen-server host owns the clock; everyone has full control, including time.

## 3. What it is NOT {#US-§3}
- Not a GLMZ game: SpaceGod is its own universe (US-T859).
- Not a walkable-interior game: ship and station interiors stay menu/minigame based.
- Not procedural-art-first: the purchased prebuilt ships and stations (Ultimate Spaceships Creator, Space Station
  Creator, Star Sparrow) are used as-is before anything is assembled or generated (docs/ThirdPartyArt.md).
- Not dependent on sibling repos at build or run time (the Media.Capture plugin checkout is the one documented
  exception for recording). SpaceGod.Studio reads this repo; this repo never reads it.
- Not a home for the archived copies: Unity.SpaceGod, SpaceGod.Unity.CSharp, SpaceGod.Unreal.CSharp,
  SpaceGod.Unreal.Cpp and Unreal.SpaceGod are history only (docs/RACE.md, docs/MIGRATION.md).

## 4. Architecture canon {#US-§4}
```
 Cpp/Source/SpaceGodCore/   pure C++20 simulation (no Unreal types), file-for-file port of Unity.SpaceGod Core/
          |   stepped each frame by
 Cpp/Source/SpaceGod/       Runtime: UnitySpaceApp (+ partials), Camera, Input, Views, UI/Hud, Coop, Audio, Radio
          |   draws with
 Shared/Plugins/SpaceGodRender/   ported shaders, materials, imported resources
 Bench/Cpp + tools/bench.ps1      headless Core over Bench/scenarios.json, digests vs Bench/expected-digests.json
```
### 4.1 Modules
- `SpaceGodCore`: Assembly, Combat, Economy, Embodiment, Law, Lore, Persistence, Sensors, Session, Settings, Systems.
- `SpaceGod`: Camera, Input, Views, Rendering, UI, Coop, Audio, Radio, Voice, Streaming, Smoke (FeatureAudit), Tour.
- `tools/`: build.ps1, play.ps1 (solo, host, join, dogfight, audit, smoke...), package.ps1, bench.ps1, import scripts.
### 4.2 Domain nouns
Polity, system, body, settlement, ship (`ShipClass`, `FlightFamily`), fleet, battle, carrier flight deck and launch
tube, fighter wing, pilot (named, with a career), mission, embodiable seat, ScaleModel blend.
### 4.3 Key verbs
- `World::Update` steps the simulation; `War::Resolve` finishes battles and books the player's contribution.
- `TakeSeat` / Fly this drops the player into a fight (US-T806); `UpdatePossession` plays the seat-change show.
- Missions are offered from what the A-life is doing (`World::UpdateAdventures`, US-T812) and judged by the fight.

## 5. The Laws {#US-§5}
Inherits `MindAttic.HouseRules.md` by reference.
1. **Both scales.** Every feature works in realistic and gameplay scale. {#US-LAW-1}
2. **Bench stays green.** `tools/bench.ps1` matches `Bench/expected-digests.json`; player-only changes are gated so
   AI-only runs are unchanged, and any intended digest change is re-recorded in its own commit. {#US-LAW-2}
3. **Roles never collide.** A feature states what it does for each embodied role (docs/ROLES.md); one owner per
   key per frame (`InputState::Arbitrate`). {#US-LAW-3}
4. **Prebuilt art first.** USC ships and SSC stations as-is; procedural only as a fallback. Third-party assets are
   never committed (docs/ThirdPartyArt.md). {#US-LAW-4}
5. **Self-contained.** A fresh clone builds with UE 5.8 and VS C++ tools only. {#US-LAW-5}
6. **Tunables are settings.** Rates and feel numbers live in settings, not constants. {#US-LAW-6}
7. **Shared checkout care.** Never commit hunks that leave another agent's file mis-merged; keep uncommitted edits
   compiling; one editor build at a time. {#US-LAW-7}

## 6. Verified state {#US-§6}
- Deploys with `run.ps1 -Redeploy` to `R:\MindAttic\Apps\SpaceGod.Unreal\{Debug,Release}`; the DebugGame package is
  there, Shipping is pending US-T889.
- Implemented on main (see git log): god view with no-collision camera, possession show and ghost transitions,
  Fly this into battles (US-T806), carrier decks and launch tubes, dogfight AI, hit/kill feedback and combat radar,
  pilot career and missions (US-T812), tactical write-back (US-T809), joystick and pad curves, Kardashev tiers,
  canon history and manual.
- 884 tickets closed as of 2026-11-27.

## 7. Active frontier {#US-§7}
`docs/RELEASE.md`: the one milestone, a playable 1.0 slice of the Fly-this combat loop in both scales.

## 8. Quality bar {#US-§8}
A feature is done when it builds, the Bench digests match (or are deliberately re-recorded), it works in both
scales and for every role it touches, its docs page says how it behaves, and it can be seen in a packaged run.

## 9. Glossary {#US-§9}
- **God view / command view**: the free camera when nothing is held.
- **Seat**: anything the player can embody. **Fly this**: the button that seats the player in a fight.
- **Gameplay / realistic scale**: the two ends of the `ScaleModel` blend.
- **Bench**: the headless digest check of the simulation Core.
- **USC / SSC**: Ultimate Spaceships Creator / Space Station Creator, the purchased Ebal Studios packs.
