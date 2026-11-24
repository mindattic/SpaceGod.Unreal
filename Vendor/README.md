# Vendor

Outside code this repo carries as plain files, so a fresh clone builds with nothing next to it.

| Folder | What | From | Commit |
|---|---|---|---|
| `MindAttic.Capture/src/MindAttic.Capture` | MindAttic.Capture's core: options, the `-capture` flags, trigger rules, the rolling tape, exports, sidecars, the session manifest (netstandard2.1 + net10.0) | [MindAttic.Media.Capture](https://github.com/mindattic/MindAttic.Media.Capture) `src/MindAttic.Capture` (before its rename) | `c4272836318e00dcdbb2965faafa0b03da3c5e66` |
| `MindAttic.Capture/src/MindAttic.Capture.Windows` | Windows capture: Windows Graphics Capture of a window, Desktop Duplication / GDI, WASAPI audio | MindAttic.Media.Capture `src/MindAttic.Capture.Windows` | same |
| `MindAttic.Capture/Directory.Build.props` | the two projects' shared build settings | MindAttic.Media.Capture `Directory.Build.props` | same |

Copied unchanged (US-T72), except that `AgentLocator` also trusts the installed agent's newer product name
`MindAttic.Media.Capture.Agent` (US-T687). The folder and namespaces keep the old `MindAttic.Capture` names until the
next refresh brings the new ones. Only the game's capture host (`Capture/SpaceGod.Capture`) builds them; see
[Capture/README.md](../Capture/README.md). The Windows project's NuGet packages (NAudio.Wasapi, Vortice.Direct3D11/DXGI,
System.Drawing.Common, Microsoft.Extensions.*.Abstractions) restore from nuget.org.

To refresh: copy those folders again from a MindAttic.Media.Capture checkout (without `bin/` and `obj/`) and update the commit.

## Fetched, not committed

| Folder | What | From | Version | Licence |
|---|---|---|---|---|
| `Piper/piper` | Piper, the local text-to-speech engine for spoken comms (US-T574): `piper.exe`, its DLLs (onnxruntime, piper_phonemize, espeak-ng) and `espeak-ng-data` | [rhasspy/piper](https://github.com/rhasspy/piper/releases/tag/2023.11.14-2) `piper_windows_amd64.zip` | `2023.11.14-2` (the last MIT release; not its GPL-3 successor piper1-gpl) | Piper: MIT. espeak-ng and espeak-ng-data: GPL-3, shipped inside Piper's own folder and used only by `piper.exe`; the game never links them and runs Piper as a separate process |
| `Piper/voices` | The voice `en_US-libritts_r-medium` (`.onnx` and `.onnx.json`, 904 speakers, 22,050 Hz) | [rhasspy/piper-voices](https://huggingface.co/rhasspy/piper-voices/tree/e5349433ed883a32f893a434dc43a6c66b95ef7c/en/en_US/libritts_r/medium) `en/en_US/libritts_r/medium` | revision `e5349433ed883a32f893a434dc43a6c66b95ef7c` | CC BY 4.0 |

Voice attribution: the voice en_US-libritts_r-medium is a Piper model by Michael Hansen (rhasspy/piper-voices), trained on
the LibriTTS-R corpus (Koizumi et al., [openslr.org/141](http://www.openslr.org/141/)), licensed CC BY 4.0
([creativecommons.org/licenses/by/4.0](https://creativecommons.org/licenses/by/4.0/)). Used unchanged.

These are large binaries and models, so `.gitignore` keeps `Vendor/Piper/` out of git. `tools/fetch-piper.ps1` downloads
them, checks each file's SHA256, and keeps what already matches; `-Force` fetches everything again. `tools/package.ps1`
copies `Vendor/Piper` to `<archive>/Piper/` when it is present; without it the game speaks through Windows SAPI.

To refresh: change the tag, the voice revision and the hashes in `tools/fetch-piper.ps1`, run it with `-Force`, and
update this table.
