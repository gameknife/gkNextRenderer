# Optional Asset Pack

Large sample assets live in `assets/paks/optional.pak` rather than directly in
the git repository to keep clone size small. When the file is present next to
`assets/paks/`, the engine mounts it automatically and `FPackageFileSystem`
falls back to pak entries for any file missing on disk.

## Downloading the prebuilt pak

The canonical fetcher is at the top-level `scripts/fetch-paks.*` and pulls every
optional pak (`ldraw.pak` and `optional.pak`) from the project's GitHub release:

```bash
# bash / zsh / Git Bash — fetch every optional pak
./scripts/fetch-paks.sh

# Only this pak
./scripts/fetch-paks.sh --optional

# Windows
scripts\fetch-paks.bat --optional
```

The legacy `tools/optional-pak/fetch-optional-pak.{sh,ps1}` scripts still work
and now delegate to the canonical fetcher. See `scripts/fetch-paks.sh --help`
for environment overrides (`PAKS_REPO`, `PAKS_RELEASE_TAG`, `PAKS_BASE_URL`).

## Rebuilding the pak

Edit [`optional-files.txt`](./optional-files.txt) to add or remove entries
(paths are relative to the repo root). Then:

```bash
# Build the Packager first (any preset works):
cmake --build --preset full-macos-arm64 --target Packager

# Then pack the listed files:
./tools/optional-pak/build-optional-pak.sh
```

PowerShell equivalent:

```powershell
cmake --build --preset full-windows --target Packager
pwsh ./tools/optional-pak/build-optional-pak.ps1
```

The script stages the listed files under `out/build/<preset>/optional_pak_stage/`,
invokes Packager against that stage, writes `assets/paks/optional.pak`, and
cleans up the stage directory (pass `--keep-stage` to keep it for debugging).

Missing source files are reported as warnings and skipped, so the script can
still produce a partial pak from a trimmed working tree.
