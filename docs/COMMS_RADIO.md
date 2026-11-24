# Comms radio

How SpaceGod's radio works: what speaks the lines, how a message travels at light speed to the listener, what it may
cost a frame, and the settings and console commands that drive and inspect it. The radio is view-local: it is not
saved, not in the co-op lockstep frames or the bench digests, and never draws from the simulation RNG.

Code: `Cpp/Source/SpaceGodCore/Radio/` (the log, receiver and wavefront maths, engine-independent),
`Cpp/Source/SpaceGod/Radio/` (the host and stats), `Cpp/Source/SpaceGod/Voice/` (TTS and the voice arbiter),
`Cpp/Source/SpaceGod/UnitySpaceApp.Radio.cpp` (the per-frame tick) and `Cpp/Source/SpaceGod/RadioCommand.cpp`.

## Text to speech (US-T574)

Spoken lines are synthesized by `Voice::TtsService`:

- **Engine: Piper, else SAPI.** Piper is used when it is installed and working; otherwise the Windows voice (SAPI)
  speaks. The **Voice engine** setting can force one (see below); the clip cache key follows the engine that speaks.
- **One worker thread** (`SpaceGodTts`) at `TPri_BelowNormal`, so synthesis never competes with the game thread.
  The game thread only enqueues requests and, in `TtsService::Tick`, collects finished clips.
- **Queue of 16.** `TtsService::Capacity` is 16 requests; a request arriving at a full queue evicts the lowest-priority
  one. The worker always takes the best waiting request next.
- **Clip cache: 32 MB in memory, 256 MB on disk.** `VoiceCache` keeps an LRU of clips in memory (32 MB) and stores them
  under `Saved/VoiceCache` (pruned to 256 MB). A repeated line is a cache hit and costs no synthesis.

The limits are pinned by `static_assert`s in `TtsService.cpp`.

## Propagation (US-T575)

Every message has an origin, a send time (`SentJd`), a power class and so a maximum reach. Its wavefront expands at
light speed; a listener hears it when the front reaches the listener's position at that moment, and only within reach.

### RadioLog: one per star system

- An append-only ring of `RadioMessage`, ordered by `SentJd`, **capped at 8192** messages (`RadioLog::Capacity`); past
  the cap the oldest are evicted. Log indices are never reused.
- Indexed by **15-game-minute buckets** (`RadioLog::BucketDays`). Each bucket knows its `[First, Last)` index range,
  the bounding box of its origins and the longest reach among its classes, so a receiver can rule out a whole bucket
  (`Wavefront::BucketMayArrive`) without looking at its messages.
- Append is O(1); retiring messages whose fronts are past all reach is amortised O(1) each. Nothing walks the messages
  per frame.

### RadioReceiver: the local listener's schedule

A binary min-heap of (arrival time, log index) over the listener's system log.

| Operation | Cost | What it does |
|---|---|---|
| `Emit` | O(log n) | Schedules a just-sent message, only when the listener is within its reach plus the travel margin. |
| `Advance` | O(k log n), k capped at **64 pops a frame** (`MaxPopsPerFrame`) | Pops what has arrived, re-solving each against the listener's current path: one not reached yet is pushed back, one heard beyond reach is dropped, the rest are heard. Due entries past the cap wait for the next frame (counted as overflow). |
| `Rebase` | O(buckets + kept) | Rebuilds the heap after a discontinuity (FTL, system change, rewind, load, tour, teleport), skipping ruled-out buckets whole and solving only the kept messages. |
| `DropAfter` | O(n) | Drops entries sent after a time, for a rewind. |

An entry whose message the log has since retired or evicted is simply skipped on pop.

## Frame budget (US-T651)

`UnitySpaceApp::RadioTick` holds the radio to **0.25 ms a frame** (`RadioBudgetMs`), measured with
`FPlatformTime::Cycles64`. The budget includes this frame's `TtsService::Tick`, which runs just before it.

- The deferrable stages are the **ambient sampler, the event chatter pump and the digest**. Each runs while the budget
  lasts; past it, it waits for a later frame.
- **Round-robin turn.** `UnitySpaceApp::radioBudgetTurn` names the stage that always runs this frame whatever the
  budget, so no stage starves; the first stage deferred after the turn takes the next one.
- **Host tick reserved.** Before the sampler and event pump run, the previous frame's `RadioHost::Tick` time is reserved
  out of the budget, because the host tick always runs (its `Advance` is already bounded by the 64-pop cap).
- **Warp above 10x: only urgent lines are spoken.** In `VoiceArbiter` (`WarpQuiet = 10`), non-urgent lines are not
  queued, waiting non-urgent ones are dropped, and a non-urgent line already playing is cut. Urgent lines are distress
  calls on Guard and lines addressed to the player; an urgent line also cuts a non-urgent one playing, with a squelch.
- **Warp above 1000x: the ambient sampler is paused** (`RadioAmbientPauseWarp`). World events still go out as chatter.

## Settings

| Row | Group | Values | Effect |
|---|---|---|---|
| Chatter density | Interface | Off, Low, **Normal**, High | How busy the ambient talk is (traffic control, patrols, traders, raiders). |
| Radio subtitles | Interface | **On** / Off | The radio log written bottom left. |
| Max voices | Interface | **1** to 3 | How many radio lines may be spoken over one another at once. |
| Minimum radio priority | Interface | **0** to 3 | Lines below it are only counted into the channel summaries: 0 hears all chatter, 3 only distress. |
| Spoken comms | Sound | **On** / Off | Off, lines are silent and their text still shows. |
| Voice engine | Sound | **Auto**, Piper, Windows | Auto uses Piper when installed, else the Windows voice; the others use only that one. |
| Radio effect | Sound | **On** / Off | Narrow band, a little static and a squelch click before and after spoken lines. |

Defaults in bold.

## Console commands

| Command | Shows or does |
|---|---|
| `sg.Radio` | The host's state: system, date, log size, heap size, overflow, skipped, heard and rebase counts, and the latest arrivals. |
| `sg.Radio emit <text>` | Sends `<text>` (default "Test on Guard") on Guard from 1 AU off the listener; it is heard about 8.3 minutes of game time later. |
| `sg.Radio stats` | Each stage's last-frame time (tick, host tick, Advance, Rebase, sampler, events, reception, arbiter, digest, TTS tick) and the counters: log, heap, arrivals, digested, TTS queue, cache hits and misses, mean synth ms. |
| `stat SpaceGodRadio` | The same stages and counters live, as an Unreal stats group (compiled out of shipping builds; `sg.Radio stats` is not). Each stage is also an Unreal Insights CPU scope. |
