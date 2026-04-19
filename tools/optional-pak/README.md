# Optional Asset Pack

Large sample assets live in `assets/paks/optional.pak` rather than directly in
the git repository to keep clone size small. When the file is present next to
`assets/paks/`, the engine mounts it automatically and `FPackageFileSystem`
falls back to pak entries for any file missing on disk.

## Downloading the prebuilt pak

```bash
# bash / zsh
./tools/optional-pak/fetch-optional-pak.sh

# PowerShell
pwsh ./tools/optional-pak/fetch-optional-pak.ps1
```

The default URL is a `TODO` placeholder. Either edit the script or export
`OPTIONAL_PAK_URL` (or `$env:OPTIONAL_PAK_URL` on Windows) to point at your
CDN/COS bucket.

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
