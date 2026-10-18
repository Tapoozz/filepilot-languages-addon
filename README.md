# filepilot-languages-addon

A small, open-source add-on that makes [File Pilot](https://filepilot.tech/) render
filenames in **all major languages**, with correct right-to-left order and Arabic
shaping. Personal use, on your own installed copy.

Out of the box File Pilot draws filename glyphs one at a time, left to right, from a
fixed set of scripts — so Hebrew, Arabic, CJK, Thai and others show as `?`, and Hebrew/
Arabic come out mirrored. This add-on fixes both, and figures out each name's direction
from its first letter (the Unicode first-strong rule), matching how Windows Explorer
renders the same names.

## What you get

- **Glyph coverage** for Hebrew, Arabic, Devanagari…Thai, and CJK / Japanese / Korean
  (previously missing scripts stop showing as `?`).
- **Right-to-left order** for Hebrew and Arabic names, with trailing punctuation and
  mixed Latin/number segments placed the way Explorer places them.
- **Arabic shaping** (letters connect into their contextual forms).
- **Version-generic**: the glyph part is a data-only change found by content pattern;
  the ordering part is located by signature, and is skipped (with a message) if a future
  File Pilot build changes too much — the glyph part still applies.

## Build

Needs MSYS2 UCRT64 gcc (`C:\msys64\ucrt64\bin`):

```bat
build.bat
```

Produces `FilePilotUniversal.exe` (the one-click patcher) plus a transform self-test.

## Use

1. Close File Pilot completely (including the tray icon).
2. Double-click **`FilePilotUniversal.exe`** (or pass a path to `FPilot.exe`). It patches
   `%LOCALAPPDATA%\Voidstar\FilePilot\FPilot.exe` in place and keeps backups.
3. Start File Pilot.

Re-run after each File Pilot update. To revert: `FilePilotUniversal.exe --undo`.

## How it fits together

| File | Role |
|---|---|
| `fprtl.c` | the text transform: first-strong base direction, bidi reordering, Arabic shaping (with a self-test) |
| `fpglyphs.c` | adds the missing scripts to the font-atlas range table (data-only) |
| `fphook.c` | loads the transform into File Pilot and routes its filename drawing through it |
| `fpaddimport.c` | registers the helper so Windows loads it at startup |
| `fppatch.c` | the one-click patcher that applies the above and keeps backups |
| `build.bat` | builds everything |

The transform runs only at draw time on a copy of the text; filenames in memory and on
disk are never modified, so open/rename/copy are unaffected.

## Notes & limits

- Fully correct: Latin/Greek/Cyrillic, Hebrew (order), Arabic (order + shaping),
  CJK/Kana/Hangul (no shaping needed). Indic/Thai glyphs appear but complex shaping
  (conjuncts, reordering) is not done — that needs the renderer itself.
- The patch breaks File Pilot's code signature and may need re-applying after updates.
  It's an unofficial personal modification; the durable fix is upstream support from the
  File Pilot developer.

## License

MIT — see [LICENSE](LICENSE).
