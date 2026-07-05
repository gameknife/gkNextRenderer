# MagicaLego Asset Pack

MagicaLego's BGM and placement SFX (`assets/sfx/`) are packed into
`assets/paks/magicalego.pak`. MagicaLego mounts this pak on startup and keeps
using the same logical paths, so `NextAudio` can resolve the sound data from
the pak when the loose files are absent.

## Rebuilding the pak

Edit [`magicalego-files.txt`](./magicalego-files.txt) to add or remove entries
(paths are relative to the repo root). Then:

```bash
# Build the Packager first:
./gnb.sh build Packager

# Then pack the listed files:
./tools/magicalego-pak/build-magicalego-pak.sh
```

PowerShell equivalent:

```powershell
./gnb.bat build Packager
pwsh ./tools/magicalego-pak/build-magicalego-pak.ps1
```

The script stages the listed files under `out/build/<preset>/magicalego_pak_stage/`,
invokes Packager against that stage, writes `assets/paks/magicalego.pak`, and
cleans up the stage directory (pass `--keep-stage` to keep it for debugging).

Missing source files are reported as warnings and skipped, so the script can
still produce a partial pak from a trimmed working tree.
