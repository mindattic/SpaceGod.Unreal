# MindAttic project agent entrypoint

Read the shared protocol at ..\mindattic-agent-standard\AGENTS.md and this project's own
documentation. Common prompt commands are implemented by the shared runner; do not add a second
copy under a provider-specific command folder.

This repo, SpaceGod.Unreal (named SpaceGod until US-T451), is the game SpaceGod in Unreal Engine 5.8, C++ only. Its
development tools (the vehicle viewer and port/hardpoint editor) live in the sibling repo SpaceGod.Studio, a C# WPF
app that reads and writes this repo's data files; this repo never reads it. It is the sole, promoted project after the C++ vs C# vs
Unity race (US-T60/US-T69, docs/RACE.md); SpaceGod.Unreal.CSharp and SpaceGod.Unity.CSharp are the
archived candidate repos kept only as historical reference. The simulation core under
`Cpp/Source/SpaceGodCore/` is a faithful port of the original Unity game's `Assets/UnitySpace/Core/`, and
`Cpp/Source/SpaceGod/` of its `Runtime/` (docs/MIGRATION.md). Keep it self-contained: nothing may
read a sibling repo at build or run time. `tools/bench.ps1` must keep matching `Bench/expected-digests.json`.
Deploy with `run.ps1 -Redeploy` (DebugGame and Shipping to `R:\MindAttic\Apps\SpaceGod.Unreal\{Debug,Release}`);
with no switches it is the run menu, which `run.bat` (the Desktop shortcut) and `deploy.bat` open.
Unreal Engine is not installed on every workstation; when it is missing, say so instead of claiming
a build passed.
