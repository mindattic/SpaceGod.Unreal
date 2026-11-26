# SpaceGod

> This repo is **SpaceGod.Unreal** (renamed from `SpaceGod`, US-T451). Its development tools live in the sibling repo
> **SpaceGod.Studio**.

The real Sol system in Unreal Engine 5, played co-op, written in **C++**. The planets, dwarf planets, moons and
asteroids sit where they really are for today's date, placed from JPL orbital elements, and you and your friends fly
around them together on one shared clock.

This repo was ported twice from `Unity.SpaceGod`, once in C++ and once in C# (UnrealSharp), and the two ports raced
head to head (docs/RACE.md, US-T60/US-T69). The C++ track won on nearly every measure that mattered and was promoted
to be the one `SpaceGod` project going forward; the C# and original-Unity copies are kept only as historical
reference. Nothing here reads a sibling repo: a fresh clone builds and runs with only Unreal Engine 5.8 and Visual
Studio's C++ tools.

## Deploy and play

| Script | What it does |
|---|---|
| `run.bat` | The Desktop shortcut's target. Asks **1) Deploy** (`run.ps1`) or **2) Deploy & Run** (`run.ps1 -Launch`); both repackage only when something changed. Arguments skip the menu (`run.bat -Force` deploys, rebuilding anyway, and plays). |
| `deploy.bat` | Asks **1) Deploy & Run** (runs `run.ps1 -Launch -Force`) or **2) Run** (starts the published copy; deploys first if there is none). |
| `run.ps1` | Non-interactive: `git pull`, package Shipping (`tools/package.ps1`), publish to `C:\Apps\SpaceGod\`, refresh the Desktop shortcut `SpaceGod`. `-Launch` starts the game; `-Force` packages even when nothing changed; `-NoPull` skips the pull. |

Every game these scripts start records: they pass `-capture video hq` (see Recording below).

```powershell
./run.ps1 -Launch          # deploy and play
./run.bat                  # what the Desktop shortcut runs: the 1) Deploy / 2) Deploy & Run menu
./deploy.bat               # the menu
```

## Requirements

- Unreal Engine 5.8 (found via `UE_ROOT`, the registry, the Epic launcher's install list, or `<drive>:\Games\UE_5.8`;
  see `tools/engine.ps1`).
- Visual Studio 2022 with the "Game development with C++" workload (MSVC x64 tools).
- MindAttic.Media.Capture checked out next to this repo: `Cpp/SpaceGod.uproject` loads its
  MindAttic.Media.Capture.Unreal plugin, and `tools/package.ps1` publishes its capture agent into the package (the
  .NET 10 SDK builds it). From source, `-capture` uses the installed agent (`%LOCALAPPDATA%\MindAttic\Capture\bin`).
  ffmpeg on PATH to record video with `-capture` (not bundled).

## Building and running from source

```powershell
./tools/build.ps1                                   # the editor target (UBT, MaxParallelActions 4)
./tools/play.ps1                                    # a game window from the editor binaries
./tools/play.ps1 -Mode host                         # host on UDP 7777
./tools/play.ps1 -Mode join -Address 192.168.1.20   # join a host
./tools/package.ps1 -Configuration Shipping         # Builds/Cpp/Windows/SpaceGod.exe
./tools/bench.ps1                                   # the headless Bench: 5 scenarios, digests checked
./tools/play.ps1 -Mode coop-smoke -PackageDir Builds/Cpp/Windows   # two packaged instances, co-op checked
..\Prose\consumer-kit\kit.ps1 push --map --prune    # catalogue the universe into Prose's 'spacegod'; pull/apply bring story back (docs/PROSE.md)
```

Opening `Cpp/SpaceGod.uproject` in the editor works too; `Cpp/Content/Python/init_unreal.py` imports the
colour maps from `Cpp/SourceArt/Textures/` on first open (the results are committed under `Cpp/Content/`).

Co-op is on the main menu: Host Co-op starts a new game or a save that others can join (it lists this machine's
addresses and the UDP port, 7777 unless `-Port=`), and Join Co-op takes a host's address and keeps the recent hosts per
profile; a failed join comes back to that page saying why. A packaged game also hosts with `-CoopHost` and joins with
`-CoopJoin=<address>`. In game, `Host` in the `` ` `` console
turns a solo session into a hosted one, and `open <ip>` joins one.

## The Bench

`Bench/Cpp` runs the simulation Core headless over `Bench/scenarios.json` (sol-idle, high-warp, economy-year, battle,
galaxy-ftl) and writes a digest of the whole state. `tools/bench.ps1` checks each against
`Bench/expected-digests.json`, the digests Unity.SpaceGod's own C# Core produces, so the C++ Core is proven bit for bit
the original. See Bench/README.md.

## Co-op

One player hosts a listen server and the others join it. The server owns the simulation clock (the date, the time
warp and pause), so everyone sees the planets in the same places; any player can change the time, and everyone is told
who did. Each player flies their own camera and sees the others as green markers labelled with the body they are near.
The scale switch, orbits and labels are your own and don't affect anyone else.

## Recording (MindAttic.Media.Capture)

Off unless asked ([docs/CAPTURE.md](docs/CAPTURE.md)):

```powershell
SpaceGod.exe -capture video              # record the whole run (.mkv + .mp4 on exit)
SpaceGod.exe -capture video screen 10    # and a PNG every 10 s
SpaceGod.exe -demo -capture video        # the scripted tour, each stop a screenshot
SpaceGod.exe -capture -capture-config my.json   # your own rules instead of the shipped ones
```

Output goes to `R:\MindAttic\Media\Capture\SpaceGod\`. The game raises its gameplay events (battles, arrivals,
gate jumps, trades, hails, saves, co-op joins...) so the shipped rules save replays of victories, defeats and a lost
ship, and screenshots of arrivals and discoveries. While capturing, **Alt+F9** takes a screenshot, **Alt+F10** starts or
stops a recording and **Alt+F11** saves the last 30 s.

### How the plugin works

Recording is the **MindAttic.Media.Capture.Unreal** plugin (`MindAtticMediaCapture`), which lives in the sibling
MindAttic.Media.Capture checkout (`unreal/`). `Cpp/SpaceGod.uproject` enables it and lists
`../../MindAttic.Media.Capture/unreal` in `AdditionalPluginDirectories`; `SpaceGod.Build.cs` depends on the
`MindAtticMediaCapture` module. C++ can't load .NET, so the plugin starts the capture agent (`capture.exe host`) beside
the game and drives it over stdin/stdout; the agent runs the same CaptureEngine Unity uses and exports the session when
the game quits (the detached export means the game never waits on it).

- **Off unless asked.** Nothing starts unless `-capture` is on the command line.
- **Where the agent is.** `tools/package.ps1` publishes it from the sibling checkout to `<package>/Capture/capture.exe`
  (the .NET 10 SDK builds it). An editor or uncooked run uses the installed agent
  (`%LOCALAPPDATA%\MindAttic\Capturein\capture.exe`); `MINDATTIC_CAPTURE_HOST` overrides both. The .NET 10 runtime
  must be installed to run it.
- **ffmpeg** on PATH for video and replays (not bundled). Screenshots alone don't need it.
- **Project settings.** `Cpp/Config/DefaultGame.ini`, section `[/Script/MindAtticMediaCapture.MindAtticCaptureSettings]`
  (editor: Project Settings > Plugins > Media Capture): `AppName` (the folder under `R:\MindAttic\Media\Capture`, here
  `SpaceGod`) and `DefaultsFile=Content/Capture/SpaceGod.capture.json`, the shipped rules. `Capture` is staged as
  non-UFS so a packaged game finds the file.
- **No window.** Under `-RenderOffScreen` (or `-capture-source=frames`) the plugin reads the GPU frames back and feeds
  them to the agent, so video, screenshots, markers and events work without a window; `-capture-source=window` forces
  window capture. `-CaptureStats` logs feed cost every 5 s. `-nullrhi` renders nothing and can never give images.
  Example: `tools/play.ps1 -Mode dogfight -Offscreen -Seconds 30`.
- **Game side.** `Cpp/Source/SpaceGod/CaptureEvents.h` is the event catalogue the game raises through the plugin's
  `MindAtticCapture.h`; `CaptureRunner` and `CaptureBackend` wire it in. From Blueprints the same calls are on
  `UMindAtticCaptureLibrary` (Raise, Marker, screenshots, recording, replay, preview sets).

## Comms radio

Radio messages travel at light speed from where they were sent, and the lines heard are spoken by Piper (else the Windows
voice) on a background thread. [docs/COMMS_RADIO.md](docs/COMMS_RADIO.md) covers the TTS limits, the propagation design
and its costs, the 0.25 ms frame budget, the radio settings and the `sg.Radio` / `stat SpaceGodRadio` commands.

## Controls

Unity.SpaceGod's own bindings, unchanged (in game, H / F1 shows them all).

## Layout

- `Cpp/Source/SpaceGodCore/`: the engine-independent simulation (pure C++20 on `Cs/Cs.h`, no Unreal types), a file
  for file port of Unity.SpaceGod's `Core/`.
- `Cpp/Source/SpaceGod/`: the Runtime port. `UnitySpaceApp` (+ one `.cpp` per C# partial) drives the Core each
  frame; `Camera/`, `Input/`, `UnityCompat/` (Unity's value types and statics in C++), `Coop/`, `Game/` (the Unreal
  framework classes).
- `Shared/Plugins/SpaceGodRender/`: the original's shaders ported to Unreal, the materials built on them, and the
  imported Resources (textures, ship models, data).
- `Bench/`: the headless runner and its scenarios. `tools/`: build, package, play, bench and race scripts.
- `Cpp/Content/Capture/`: the shipped default capture rules the MindAttic.Media.Capture.Unreal plugin hands its agent.
- `tools/unity_extract.py` and `Shared/Plugins/SpaceGodRender/Content/Python/spacegod_import_resources.py` are
  **optional one-time import tools** that read the original Unity project or player; their output is committed, and
  nothing in the build runs them.
- The game needs no level: it plays on the engine's empty `/Engine/Maps/Entry` and builds everything at runtime.

## History

docs/MIGRATION.md (how the port was made), docs/FAITHFULNESS.md, docs/PERSISTENCE.md and docs/RACE.md (the C++ vs C#
race that this repo won) are kept as the record of `Unreal.SpaceGod`, the pre-split project this repo was promoted
from; some of them still describe the C# track, which lived in the repo's own history before the split and now lives
on only in the archived candidate repos. docs/USER_STORIES.md is this repo's ticket log, carried over unbroken from
`Unreal.SpaceGod` through the split and the promotion.
