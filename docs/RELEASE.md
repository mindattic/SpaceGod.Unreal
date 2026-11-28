---
layer: release
status: living
---
# SpaceGod.Unreal — Release charter

> The one finish line this repo is working toward. Every open ticket in this repo sits under one of the epics
> below, and every epic is part of the milestone. Work that does not fit is out of scope until this milestone is
> reached (owner, 2026-11-27, MAT-T319).

## 1. What this is
SpaceGod: a co-op space game in Unreal Engine 5.8 (C++) where an unseen god over a simulated galaxy drops into any
ship or craft and flies it, in true scale or game scale. SpaceGod.Unreal is the one true SpaceGod; every other copy
is an archive.

## 2. The milestone: SpaceGod 1.0, the Fly-this slice {#milestone}
- **Milestone:** MS-14, "SpaceGod 1.0: playable Fly-this combat loop"
- **Published means:** an internal game build at `R:\MindAttic\Apps\SpaceGod.Unreal\Release\SpaceGod.exe`
  (Shipping), started from the `SpaceGod` Desktop shortcut through `run.ps1`.
- **The natural stopping point:** the owner can sit down and play one complete core loop without help: Quick Start
  in Sol, find a fight or accept a mission from the god view, Fly this into a fighter (launching from a carrier
  where one is present), fight with clear hit and kill feedback, see the fight resolve and the debrief and career
  update, and return to the god view; the same loop works with the scale toggle on either side. It stops here
  because this is the first point where SpaceGod is a game rather than a growing simulation, and every system it
  needs is already on main.

## 3. Done when (acceptance checks)
1. build — `tools/build.ps1` builds the editor target clean on main, and `tools/bench.ps1` matches
   `Bench/expected-digests.json`.
2. exists — `R:\MindAttic\Apps\SpaceGod.Unreal\Release\SpaceGod.exe` (Shipping) exists and launches.
3. signoff — the owner plays the loop end to end (Quick Start, Fly this or a mission, fight, debrief, back to the
   god view) in both gameplay and realistic scale.

## 4. Epics, in priority order
| Priority | Epic (ticket) | What it delivers | Features |
|---|---|---|---|
| 1100 | US-T964 Light Society foundation | The agent-society foundation of "Modeling Earth-Scale Human-Like Societies with One Billion Agents" (agents with profile/internal/external state, CSR social graph, batched event queue, six simulation operations, mixture-of-models router over an in-engine teacher, distilled surrogates and lookup tables), self-contained, at game scale, fed by and feeding the live game | (filed from the design pass) |
| 1000 | US-T942 Live Sol sandbox: inhabit anything | Sol runs live; fly as god; inhabit any ship, station or mech, ride along with its job or take over; hunt pirates, turn on your own clan, rob ships on the lanes; then the full suite and a lossless 1080p60 tour film | US-T947 Inhabit anything (1090), US-T948 Ride along or take over (1080), US-T949 Hunt pirates (1070), US-T950 Pirate life (1060), US-T951 Proof: sg.Inhabit (1050), US-T943 Final gate: tests then tour film (1010) |
| 900 | US-T891 Deploy and launch | Shipping and DebugGame packages deploy from the run menu, launch, and hold frame rate in a carrier fight | US-T895 Run menu packages (990: US-T873, US-T889, US-T874), US-T896 Frame budget in a fight (980: US-T897, US-T898) |
| 800 | US-T892 The Fly-this loop | Quick Start to a fight or mission, fly it, see it resolve and the debrief, back to the god view | US-T899 Into the fight (890: US-T900, US-T901), US-T902 Mission fight resolves (880: US-T903, US-T904), US-T905 Debrief and back (870: US-T906) |
| 700 | US-T893 Both scales | Every loop step works in gameplay and realistic scale, including toggling mid-fight | US-T907 Loop audit in both scales (790: US-T908), US-T909 Scale toggle mid-fight (780: US-T910, US-T911) |
| 600 | US-T894 Prebuilt art in the loop | Loop ships and stations drawn from USC, Star Sparrow and SSC prebuilts | US-T912 Prebuilt hulls (690: US-T913), US-T914 Prebuilt stations (680: US-T915) |

## 5. Out of scope (not before this milestone)
A new request that lands here is filed `proposed`, never approved automatically.
- Civilization dynamics (decline, famine, epidemics, secession, schisms, fiscal and succession crises, dark ages;
  the former U.SPC-T396..T409, T437..T439, T607..T642) — a galaxy-scale simulation layer, not part of the loop.
- Open-ended galaxy systems: new polity, economy, Kardashev, lore, Prose and encyclopedia features beyond what the
  loop already shows.
- New embodiable roles (mech, marine, EVA, satellite, station) beyond keeping the existing ones working; the 1.0 loop
  is the fighter (and the carrier it launches from).
- Procedural or Lego-assembled ship and station art where a purchased USC, SSC or Star Sparrow prebuilt fits.
- Co-op features beyond what already works; walkable interiors; rewind branching; LLM stories.
- Soak, bughunt and "continuous" audit loops.

## 6. Current verified state
- On main: god view and possession (docs/GodView.md, docs/Possession.md), Fly this into a battle (US-T806), carrier
  flight decks, launch tubes and the tube launch view (US-T779, US-T786, US-T843), dogfight AI, hit and kill feedback
  (US-T751, US-T866, US-T867, US-T880), combat radar (US-T853), pilot career and missions with briefings and
  debriefings (US-T812), tactical write-back (US-T809), joystick and pad curves (US-T826, US-T827), USC, SSC and Star
  Sparrow imports (docs/ThirdPartyArt.md), the scale toggle (G / backtick, docs/Controls.md).
- Deploy: `run.ps1 -Redeploy` packages DebugGame to `R:\MindAttic\Apps\SpaceGod.Unreal\Debug` (done); the Shipping
  package is US-T889 (in progress).
- 884 tickets closed by 2026-11-27.

## 7. Decision record
- 2026-11-27 — the owner chose SpaceGod.Unreal as the one SpaceGod; the milestone is a playable slice of one core
  loop, both scales; civ dynamics and open-ended galaxy systems out of scope; prebuilt art first.
- 2026-11-27 — charter drafted from README, docs, git log and tickets; Legion vote: Approve, 9 of 9 voters (plurality quorum reached, consensus 1.0); no dissent.
- 2026-11-28 — the owner asked directly for the live-Sol sandbox (US-T942): inhabiting stations and mechs and pirate
  play, previously out of scope below, are now in, ahead of the other epics; it ends with the full test suite and a
  lossless 1920x1080 60 fps film of the tour.
- 2026-11-28 — the owner set a goal (US-T964): build the Light Society paper's foundation into the simulation at game
  scale. Agent-society dynamics, which the out-of-scope list below excludes as galaxy-scale civ dynamics, are in for that
  foundation; the named crises (famine, secession and the rest) stay out.
- Triage: SpaceGod.Unreal 4 open (US-T873, US-T874, US-T889 kept; US-T890 is the triage job itself) plus 25 new tickets (4 epics incl. US-T891, 9 features, 12 tasks). Archive copies: Unity.SpaceGod 55 open, SpaceGod.Unity.CSharp 1, SpaceGod.Unreal.CSharp 1, SpaceGod.Unreal.Cpp 0, Unreal.SpaceGod 0; all 57 closed wont-do (civ dynamics out of scope, or archive-repo upkeep), none re-filed because none is core to the loop.
