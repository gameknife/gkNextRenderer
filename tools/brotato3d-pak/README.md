# Brotato3D Asset Pack

Brotato3D's SFX (`assets/sounds/brotato3d/sfx/`) and UI icons
(`assets/textures/brotato3d/icons/`) are packed into `assets/paks/brotato3d.pak`
and mounted at runtime by `Brotato3DGameInstance`. `FPackageFileSystem` falls
back to pak entries for any file missing on disk, so the pak is the canonical
shipping format.

The source binaries currently come from `scripts/import_brotato_placeholder.py`
(Brotato vendor reference material — proprietary, do not distribute). After the
planned refresh replaces them with self-authored or CC0 content, the same paths
remain valid and the pak can be redistributed.

## Rebuilding the pak

Edit [`brotato3d-files.txt`](./brotato3d-files.txt) to add or remove entries
(paths are relative to the repo root). Then:

```bash
# Build the Packager first:
./gnb.sh build Packager

# Then pack the listed files:
./tools/brotato3d-pak/build-brotato3d-pak.sh
```

PowerShell equivalent:

```powershell
./gnb.bat build Packager
pwsh ./tools/brotato3d-pak/build-brotato3d-pak.ps1
```

The script stages the listed files under `out/build/<preset>/brotato3d_pak_stage/`,
invokes Packager against that stage, writes `assets/paks/brotato3d.pak`, and
cleans up the stage directory (pass `--keep-stage` to keep it for debugging).

Missing source files are reported as warnings and skipped, so the script can
still produce a partial pak from a trimmed working tree.
