# gnb CLI

`gnb` is the single build entry point for gkNextRenderer. Use `gnb.bat` on
Windows and `./gnb.sh` on macOS/Linux when a system-wide `gnb` is not installed.

For implementation details and architecture notes, see
[gnb-tech-stack.md](gnb-tech-stack.md).

## Setup

Prepare vcpkg, external SDKs, TypeScript compiler, Slang, and optional assets:

```bash
./gnb.sh setup
```

Skip optional assets:

```bash
./gnb.sh setup --skip-paks
```

Refresh vcpkg:

```bash
./gnb.sh setup --refresh
```

## Build

Build the default platform preset:

```bash
./gnb.sh build
```

Build a target:

```bash
./gnb.sh build gkNextEditor
```

Clean and reconfigure:

```bash
./gnb.sh build --clean --reconfigure
```

Disable unity builds or enable LTO:

```bash
./gnb.sh build --no-unity
./gnb.sh build --lto
```

## Run

List runnable applications:

```bash
./gnb.sh run
```

Run a target:

```bash
./gnb.sh run BrickPlayer
```

Pass application arguments:

```bash
./gnb.sh run -- --scene=foo --present-mode=mailbox
```

## Test And Visual

Run unit tests:

```bash
./gnb.sh test "[Unit]"
```

Run visual tests:

```bash
./gnb.sh visual
```

## Mobile

Android:

```bash
./gnb.sh android release
```

iOS:

```bash
./gnb.sh ios              # default: skip code signing
./gnb.sh ios --codesign   # enable code signing
```

## Paks

Fetch all optional assets:

```bash
./gnb.sh paks fetch
```

Fetch selected groups:

```bash
./gnb.sh paks fetch optional ldraw
```

List status:

```bash
./gnb.sh paks list
```

Publish selected assets:

```bash
GITHUB_TOKEN=... ./gnb.sh paks publish optional
```

## Package

Create a desktop package:

```bash
./gnb.sh package linux --version v1.0.0
```

Create a MagicaLego package:

```bash
gnb.bat package magicalego --version v1.0.0
```

## Info And Doctor

Print environment info:

```bash
./gnb.sh info
```

Check required tools:

```bash
./gnb.sh doctor
```

## AVIF

AVIF remains a manual CMake feature and is not exposed as a `gnb build` flag:

```bash
cmake --preset windows -DENABLE_AVIF=ON -DVCPKG_MANIFEST_FEATURES=avif
gnb.bat build
```

## Publish gnb Binaries

`gnb.bat` and `gnb.sh` use the `paks-latest` GitHub release as their single
bootstrap source. Pushes to `main` / `dev` that modify `tools/gnb` Go sources
automatically rebuild and publish the platform binaries plus `gnb-version.txt`
through `.github/workflows/gnb-release.yml`.

The shims compare the local cached version with `gnb-version.txt` and refresh
their cached bootstrap binary automatically when a newer release is available.
If Go is installed locally, the shims still prefer a local rebuild from
`tools/gnb`.

For manual publishing or backfilling from a machine with Go and GitHub CLI
installed:

```powershell
gh auth login
.\scripts\publish-gnb.ps1
```

Preview the local build and target release URL without uploading:

```powershell
.\scripts\publish-gnb.ps1 -DryRun
```

## Todo Workflow

List and inspect `.spec/TODO.md` tasks:

```bash
./gnb.sh todo list
./gnb.sh todo show 00021
./gnb.sh todo next --json
```

Add a task, optionally creating a linked `specs/<id>.md` background file:

```bash
./gnb.sh todo add -t feat -p P1 "重构材质缓存" --spec
./gnb.sh todo add -t feat "重构材质缓存" --spec-from docs/material-cache.md
```

Maintain ordering between `下一步` and `待规划`:

```bash
./gnb.sh todo move 00021 --to next
./gnb.sh todo move 00021 --before 00018
./gnb.sh todo swap 00018 00021
```

Delete a task (defaults to also removing `specs/<id>.md`):

```bash
./gnb.sh todo delete 00021          # dry-run: show what would be removed
./gnb.sh todo delete 00021 -y       # actually remove the task line + its spec
./gnb.sh todo delete 00021 -y --keep-spec
./gnb.sh todo delete 00021 -y --also-files   # also drop journal/blocker
```
