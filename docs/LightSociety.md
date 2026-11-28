# Light Society and the Populace

Light Society is a game-agnostic agent-society library (`Cpp/Source/SpaceGodCore/LightSociety/`, namespace
`MindAttic::Society`). The Populace is SpaceGod's adapter over it: the people of the galaxy hold opinions on five
topics, events move them, and their mood moves the worlds back. Design notes live in
docs/research/LightSociety-design.md; the paper summary is docs/research/LightSociety-paper.md.

Where to see it: the galaxy map's OPINION tab (section "The Opinion map mode"). Where to tune it: the `light.*`
knobs (section "Knobs").

## The paper and the code

The paper (arXiv 2506.12078v2) models a society as M = <D, T, S_A, S_E, V, Q, F>. `LightSociety/Engine.h` is that
model, with F = { f_I, f_P, f_pi, f_A, f_E, f_U } plus a readout.

| Paper | Code                                                                                                                                            |
|---|-----------------------------------------------------------------------------------------------------------------------------------------------------|
| D, the seed dataset | `Populace::Build` (Populace.cpp): citizens, houses, businesses, ties and Civics become profiles and edges                         |
| T, the timeline | `Engine::StepTo`, whole days; `Config::slotsPerDay` = 10 slots a day                                                                  |
| S_A, agent state | `AgentStore` columns: `Profile` (static), stance, confidence, memory ring, trust, express (internal), group and graph row (external) |
| S_E, environment | `Engine::Drift(topic)`, one level per topic in [-1, 1]                                                                               |
| V, events | `EnvEvent` (time, priority, kind, topic, push, magnitude, scope, target, source)                                                            |
| Q, the queue | `EventQueue`, a min-heap over (time, priority, seq); events with the same (time, priority) pop as one batch grouped by kind              |
| f_I, initialization | `Engine::Init(config, profiles, edges, topics)`; the Populace builds its inputs in `Populace::Build`                              |
| f_P, perception | `Engine::Perceive`: who hears each event (by `Scope`: All, Group, Agent, Neighbours)                                                  |
| f_pi, policy | `IPolicy::Decide`; the Populace uses `Router`: the rule `Teacher`, then a distilled `Surrogate` for `Op::Shift`                          |
| f_A, agent evolution | `Engine::EvolveAgents`: memories fade, confidence relaxes, agents age; runs each whole day                                       |
| f_E, environment evolution | `Engine::EvolveEnvironment`: each topic's drift is `0.98 * drift + 0.05 * (2u - 1)`, clamped to [-1, 1]                    |
| f_U, update | `Engine::Apply`: applies the policy's `StanceChange`s to the store                                                                        |
| Readout | `Readout`, per group and topic: mean, supporter / neutral / skeptic shares, confidence, variance, change rate                                 |
| Mixture-of-models engine | `Router` with policy `AllTeacher`, `AllSurrogate` or `Weighted(w)`; an exact `LruCache`; optional `Lookup` tier              |
| Knowledge distillation | `Distill`: macro-F1 shortlist, then smallest change-rate gap; `Surrogate` is softmax regression or an MLP                      |
| LLM teacher | Replaced by `Teacher`, an in-engine rule policy (no network at run time)                                                                  |

A supporter has stance above 0.2 (`SupporterStance`), a skeptic below -0.2, everyone else is neutral. The library
also carries the paper's scenarios (`Scenarios/`: diffusion, continuous opinion, trust / dictator / ultimatum games)
for `sg.LightSociety scenario` and the selftest; the game's own society does not run them.

## The five topics

A topic is a stance in -1..1 per world (`Topic` in Populace.h). A topic's mood is the mean over a world's agents; its
share is the fraction of supporters. Both read 0 until the society has produced its first readout.

| Topic | Positive end (+) | Negative end (-)                         |
|---|---|-------------------------------------------------------------|
| `PolitySupport` | for the flag that rules them | against their flag |
| `PirateFear` | afraid of pirates | not afraid                       |
| `GodFaith` | faith in the SpaceGod | doubt                          |
| `TradeTrust` | trust in trade and traders | suspicion               |
| `WarHawk` | for war | for peace                                     |

All five start at prior mean 0, spread 0.25, stubbornness 0.3. `WarHawk` is read and drawn but nothing publishes
events on it yet.

## What the Populace builds

`Populace::Build` runs on the first `Civilization::Update` after the galaxy has a Society and at least one world (so
older saves build on their first day). The society's day 0 is that sim day (`OriginJd`).

- Anchor agents: every living citizen of every house that has not scattered, in a standing world. Profile from house
  wealth class (0..4 by credits: 1e3, 1e4, 1e5, 1e6), age, traits and the world's Learning.
- Synthetic agents: each world is topped up to `light.agentsPerSettlement`, shaped by the world's Civics (Liberty,
  Hawk, Learning, FaithShare, Weariness). About half work for one of the world's businesses.
- Edges: family cliques (weight 255), business cliques (160), the citizens' own ties weighted by strength, a
  Barabasi-Albert web within each world (3 ties per agent, weight 128) and 2 links per business branch to the branch's
  world (weight 96).
- The total never exceeds `light.maxAgents`; the library caps at 2^20.

A daily census (`Populace::Census`) then keeps it current. The newborn join and tie to their family and into their
world; the dead, citizens whose house scattered and every agent of a fallen world leave; a world new to the society
is topped up and gets its own web (US-T982).

## Perception: events to pushes

`Populace::Perceive` (Populace.Perceive.cpp) turns what happened since the last look into events for the next batch.
Each push is (topic, direction, magnitude). A world in the same system hears at once at full magnitude; a world
elsewhere hears when the relays bring it (`News::RelayDays`) at 0.35 of the magnitude, and not at all if no relay
reaches it. A civilization event that is `Unverified`, dated before `OriginJd`, or one of the Populace's own
publications (`PollPublished` and every kind after `Embargo`) is skipped (US-T975).

| Source | PolitySupport | PirateFear | TradeTrust | GodFaith                    |
|---|---|---|---|----------------------------------------------------------------|
| `PirateRaid` | -1 at 0.3 | +1 at 0.5 | |                                       |
| `PirateHold` | -1 at 0.4 | +1 at 0.7 | |                                       |
| `HavenStormed` | +1 at 0.4 | -1 at 0.6 | |                                     |
| `BattleFought` | -1 at 0.3 | | |                                               |
| `WorldConquered` | -1 at 0.7 | | |                                             |
| `Scandal` | -1 at 0.3 | | |                                                    |
| Event marked `Steered` | | | | +1 at 0.5, added to the kind's own pushes       |
| Bounty kill (outlaw's fate) | +1 at 0.2 | -1 at 0.6 (feared outlaw) or 0.3 | | |
| Lane robbery (liner boarded) | | +1 at 0.3 | -1 at 0.2 |                       |

Other civilization kinds produce no push. Details:

- Bounty kills: each pass records the outlaws at large. One that is no longer at large, was in that list, has a
  `FateBy` and did not merely `Vanished` counts, heard from its system at `FateJd`.
- Lane robberies: trips whose `Note` starts with "robbed of " and whose `ArriveJd` falls after the last one seen,
  heard from the system the liner was bound for. Saved cursors: `SeenEventId`, `SeenRobberies`, `SeenRobberyJd`,
  `Wanted`.
- Every event time is `max(when, OriginJd) + relayLag - OriginJd`, in society days.

## Feedback: the readout moves the world

Each `Civilization::Update` waits for the last batch, takes its readout and calls `Populace::Feedback`
(Populace.Feedback.cpp, US-T984).

| Feedback | What it does                                                                                                                                                                                                                                           |
|---|---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| Stir | For each day read, `civ.Stir(world, -0.5 * step, step)` with `step = clamp(light.feedbackGain * PolitySupport mood, -light.feedbackCap, light.feedbackCap) * days`. Approval moves by step, unrest by half that the other way. Skipped when the mood is 0. |
| Poll | Every `Civilization::MonthDays`, each world with a non-zero PolitySupport mood publishes `PollPublished` with `Amount` = its supporter share of PolitySupport.                                                                                             |
| Band crossings | A band event is published when a mood enters a band; `Amount` is the mood. See below.                                                                                                                                                            |
| TradeTrust deals | Monthly trust and ultimatum rounds between trading-house heads; see the next section (US-T985).                                                                                                                                                |
| Books | A world's monthly business return gains `0.004 * TradeTrust mood` (Society.cpp).                                                                                                                                                                          |
| Trip planning | A house in a world with TradeTrust mood below 0 drops a planned trip to a pirate-held system with probability `-0.5 * mood` (Society.cpp).                                                                                                        |

Band crossings use a hysteresis so a mood on the edge publishes once: a mood enters a band at 0.5 (or -0.5) and
leaves it only when it falls back inside 0.3.

| Event | Topic | Band entered                                           |
|---|---|----------------------------------------------------------------|
| `OpinionShift` | `PolitySupport` | either end (mood >= 0.5 or <= -0.5) |
| `Panic` | `PirateFear` | positive (mood >= 0.5)                        |
| `TrustCrisis` | `TradeTrust` | negative (mood <= -0.5)                 |
| `Revival` | `GodFaith` | positive (mood >= 0.5)                        |

Leaving a band publishes nothing. The bands are saved in `Populace.Bands`.

## Trade trust

Each month (`Populace::Trade`, Populace.Trade.cpp, US-T985) the head of each trading house in a world deals with the
next such head in that world, in house list order. Each pair plays a trust round (`TrustSend`, `TrustReturn`) and an
ultimatum round (`UltimatumOffer`, `UltimatumAnswer`), answered by the rule `Teacher`. The pair's outcome is
`(trust + ultimatum) / 2` in -1..1. The world's mean outcome, when non-zero, is heard as a `TradeTrust` push at
magnitude 0.3. Worlds with fewer than two trading houses are skipped. The result reaches Books and trip planning
through the readout, as the Feedback table says.

## The Opinion map mode

The galaxy map has an OPINION tab (US-T979). Each system is tinted by the mean mood of its worlds on one topic: for in
orange, neutral in grey, against in blue, full colour at a mean of 0.6 either way. Click the tab again, or use `<`
and `>`, to step through the five topics; the legend names the topic and its two ends and lists the three systems
most for and most against. A system with no standing world is uncoloured; a galaxy without a Populace reads neutral.

## The Society screen Opinion page

The Society screen has an Opinion view (US-T978), the `opinion` view of the civ hands. It lists one row per world the
society holds, in the society's own order, with the mean stance on each topic read so far, for example
`Mars: PolitySupport +0.12, PirateFear -0.05, ...`. Picking a row opens that world. It is read only and empty until the
society has a readout.

A world's detail on the same screen has an OPINION section. Per topic it shows the mean, the shares for, neutral and
against, and the share changing sides (the readout's change rate). The topics are named "Support for the flag", "Fear
of pirates", "Faith in the SpaceGod", "Trust in trade" and "Appetite for war". With no readout it says "no society yet".

## sg.LightSociety

Console command (US-T981, `LightSocietyCommand.cpp`). A trailing `quit` exits once the command is done; for a sandbox
run, once it finishes.

| Command | What it does                                                                                                                                                                                                                                                                                                                                                                                      |
|---|---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `sg.LightSociety` or `summary` | Agents, worlds, readout day and hash, routing, whether the surrogate is switched in, distillation figures (macro-F1, change-rate gap, samples), threads, cache hits and misses.                                                                                                                                                                                            |
| `topics` | Per topic, across the worlds: mean, for / neutral / against shares, change rate a day, the world most for and the one most against.                                                                                                                                                                                                                                                              |
| `world <name>` | One world's reading on every topic.                                                                                                                                                                                                                                                                                                                                                        |
| `router teacher`, `surrogate`, `mix <w>`, `schedule` | Who decides stance changes, from the next batch on. `mix` sends a share `w` (0..1) to the surrogate. `schedule` is the default: the teacher, then the surrogate from `light.distillDays`. Until a surrogate is distilled the teacher answers whatever the routing. Not saved; a load returns to `schedule`. Single-player only.                      |
| `distill` | Distils the surrogate afresh in the next batch; the teacher decides until the schedule switches it back in. Single-player only.                                                                                                                                                                                                                                                                 |
| `scenario <name> [n] [days] [seed]` | Runs one of the paper's scenarios (`diffusion`, `continuous`, `trust`, `dictator`, `repeated`, `ultimatum`) on a society apart from the game's, on its own thread, then prints its blob hash, the time and its curves. `0` or no number takes the scenario's default; seed defaults to 1. One run at a time.                                                          |
| `selftest [seed]` | Sandbox checks, off the game thread. The seed defaults to 981.                                                                                                                                                                                                                                                                                                                          |
| `sway <topic> <world> <push>` | The `sway` hand above, sent through the same path as `sg.Civ sway`, so it travels in co-op.                                                                                                                                                                                                                                                                                 |

Before the populace has built (its first day) the live commands answer "the populace has not woken yet (let a day pass)".

The selftest runs a diffusion (3000 agents, 40 days), a continuous (1500, 8 days) and a trust (500) scenario, hashes
their blobs together, and prints one line per check, then a total. A passing run prints:

    sg.LightSociety selftest: PASS same seed, same blob hash (<h> and <h>)
    sg.LightSociety selftest: PASS 1 thread and 4 threads, same blob hash (<h> and <h>)
    sg.LightSociety selftest: PASS another seed, another blob hash (<h>)
    sg.LightSociety selftest: PASS teacher-vs-surrogate change-rate gap <g> (macro-F1 <f>, <n> samples)
    sg.LightSociety selftest: PASS (4 checks, 0 failed)

A failing check prints `FAIL` in place of `PASS` (as a warning) and the total reads `FAIL (<n> checks, <k> failed)`. If a
sandbox run itself fails, one `FAIL the sandbox runs: ...` line replaces the three hash checks. The gap line fails when
the surrogate would not train or the gap is not finite.

## Knobs

All are registered with `TuningScope::Simulation`.

| Name | Default | Range | Unit | Meaning                                                                                                           |
|---|---|---|---|-----------------------------------------------------------------------------------------------------------------------------------|
| `light.agentsPerSettlement` | 64 | 0 to 1024, step 1 | | Each standing world is topped up with synthetic agents to this count.                    |
| `light.maxAgents` | 65536 | 1024 to 1048576, step 1024 | | Most agents in the model, citizens included. Read when it is built.                    |
| `light.distillDays` | 30 | 0 to 365, step 1 | days | Days after build that the rule teacher decides every change before the surrogate takes over. |
| `light.feedbackGain` | 0.02 | 0 to 0.2, step 0.001 | /day | How far a day of PolitySupport moves a world's approval.                              |
| `light.feedbackCap` | 0.01 | 0 to 0.1, step 0.001 | /day | Most a day of opinion moves a world's approval.                                        |

Fixed in code, not knobs: `Populace::MaxStrideDays` = 7, remote hearing share 0.35, trade magnitude 0.3, band enter
0.5 and leave 0.3, within-world ties 3, branch links 2, `Config::slotsPerDay` 10, `memoryK` 4, `chunk` 4096.

## Determinism

The society is lockstep and rewind deterministic. Every peer computes the same readout from the same inputs.

- Counter-based RNG: every draw is `Rng::Hash(seed, agent, round, op)` (SplitMix64 finaliser), so a result never
  depends on thread count, chunk order or how many draws other agents made. The router's choice between teacher and
  surrogate is itself such a roll.
- Fixed chunks: passes split into chunks of `Config::chunk` (4096) agents. Chunk bounds depend only on that size, and
  results merge in chunk order, so one thread and many give the same answer bit for bit.
- List order only: the Populace walks houses, worlds, events and outlaws in their own list order, never a hashed
  container. Anchors are found by binary search over sorted ids.
- The distill switch is keyed to the sim day: the surrogate is registered when the engine's day reaches
  `light.distillDays`, and batches stop at that day. A thread finishing early or late changes nothing.
- The build seed comes from the galaxy seed and the build day; census joins mix in the engine day.

## Threading

One worker thread (`JobRunner`) steps the society; the game thread never does heavy work.

1. `Civilization::Update` calls `Populace::Pump` before its early return, on every update.
2. Pump waits for the previous batch (normally already done), copies the engine's latest readout, runs Feedback.
3. If days are due (`floor(jd) - OriginJd` against the engine day), it runs Perceive, Trade and Census, then submits
   one job: seat the census's joins and leaves, inject the events, `StepTo` the target day.
4. A job covers at most 7 days (`MaxStrideDays`). A backlog is stepped in 7-day strides on later updates.
5. The first update builds the society and submits f_I and the first distillation to the worker; moods read neutral
   until the first day is stepped.
6. A batch that throws drops the society; it is built afresh on the next day.

Inside a job a `ChunkPool` sized to the cores less two (at least 1) runs the policy and agent passes. Destroying a
`Populace` (load, new game) cancels and joins the worker first.

## Save format and old saves

`Populace` is an optional field of `Civilization`. It saves its worlds (`Groups`), `OriginJd`, the perception cursors
and the band, poll and trade state, and the engine as one base64 string node (`State`, `LightStateCodec`), not a byte
array, since a saved array costs a node per element. Saving waits for the worker.

The blob is the library's little-endian format: magic `LSOC`, version 1 (a reader accepts its version and older),
then the whole engine (config, topics, clock, store, graph, queue, environment, readout), then a flag and, when
distilled, the surrogate.

- A save with no Populace builds one on its first day.
- A blank or corrupt blob leaves no society, which is built afresh.
- A blob that ends after the engine has no surrogate yet; the next job distils one, one day long, so the switch still
  lands on a fixed day.
- A bad surrogate is distilled again rather than losing the society.

## Co-op

Co-op runs lockstep: every peer runs the society from the same state and inputs.

- `sway <topic> <world> <push>` is a hand verb (US-T977). `topic` is a topic name (case blind), `world` a world's name,
  `push` a number clamped to -1..1. It queues one push of magnitude 0.5 on that world through `Populace::Hear`, dated
  the current day. Like every hand it travels as a "civ" entry, so every peer queues it on the same day. It answers
  "The people are swayed", or says why not (no populace yet, unknown topic, unknown world).
- The co-op digest has a `light <hash>` line (US-T977): `Populace::ReadoutHash`, an FNV-1a over the readout's day,
  topics, groups and every cell's count, mean, shares, confidence, variance and change rate (floats by their bits).
  Peers in step print the same hash; it is `0` with no populace. Compare it to find a diverged peer.
- `router` and `distill` of `sg.LightSociety` change one peer's society, so they refuse in co-op.

## The Bench

The Bench never creates a `Civilization`: only the game does (`UnitySpaceApp.Influence.cpp`, and the `CivCommand`
console command). The Bench only looks one up with `Civilization::Find`, so there is no Populace, every
`Populace::MoodOf` reads neutral, and Bench digests are unchanged.

## The library boundary and the Prose lift

Rule: files under `LightSociety/` include only standard library headers and `"LightSociety/..."`. They use no `cs::`
types, no Unreal and no `SPACEGODCORE_API`; nothing is exported. Check it with:

    tools/check-lightsociety-boundary.ps1

It prints each offending line and exits 1, else prints `Light Society boundary: ok` and exits 0. The Populace and
everything else that touches SpaceGod types stays outside the folder, in `Systems/Civilization/`.

Plan for Prose (a .NET 10 / C# project): keep the boundary now, since it is cheap. When Prose has a consumer, lift the
library to its own repo (about 2-3 tasks) behind a flat C ABI built into `mindattic_society.dll` and called by
P/Invoke:

    ls_create  ls_add_agents  ls_add_edges  ls_inject  ls_step  ls_readout  ls_save  ls_load

SpaceGod would then vendor the library rather than read a sibling repo. A C# port is rejected.
