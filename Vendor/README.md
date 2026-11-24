# Vendor

Outside code the game uses. Recording no longer has a copy here (US-T693): the MindAttic.Media.Capture.Unreal plugin
comes from the sibling MindAttic.Media.Capture checkout and drives its installed capture agent.

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
