# Optional Asset Pack

Large sample assets live in `assets/paks/optional.pak` rather than directly in
the git repository to keep clone size small. When the file is present next to
`assets/paks/`, the engine mounts it automatically and `FPackageFileSystem`
falls back to pak entries for any file missing on disk.

## Downloading the prebuilt pak

The canonical fetcher is `gnb paks fetch` and pulls optional paks from the
project's GitHub release:

```bash
# bash / zsh / Git Bash - fetch every optional pak
./gnb.sh paks fetch

# Only this pak
./gnb.sh paks fetch optional

# Windows
./gnb.bat paks fetch optional
```

The legacy `tools/optional-pak/fetch-optional-pak.{sh,ps1}` scripts still work
and now delegate to gnb. Environment overrides remain supported:
`PAKS_REPO`, `PAKS_RELEASE_TAG`, and `PAKS_BASE_URL`.

## Rebuilding the pak

Edit [`optional-files.txt`](./optional-files.txt) to add or remove entries
(paths are relative to the repo root). Then:

```bash
# Build the Packager first:
./gnb.sh build Packager

# Then pack the listed files:
./tools/optional-pak/build-optional-pak.sh
```

PowerShell equivalent:

```powershell
./gnb.bat build Packager
pwsh ./tools/optional-pak/build-optional-pak.ps1
```

The script stages the listed files under `out/build/<preset>/optional_pak_stage/`,
invokes Packager against that stage, writes `assets/paks/optional.pak`, and
cleans up the stage directory (pass `--keep-stage` to keep it for debugging).

Missing source files are reported as warnings and skipped, so the script can
still produce a partial pak from a trimmed working tree.
