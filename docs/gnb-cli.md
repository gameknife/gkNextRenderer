# gnb CLI

`gnb` is the single build entry point for gkNextRenderer. Use `gnb.bat` on
Windows and `./gnb.sh` on macOS/Linux when a system-wide `gnb` is not installed.

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
./gnb.sh ios --skip-codesign
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

`gnb.bat` and `gnb.sh` download bootstrap binaries from the `paks-latest`
GitHub release. Build and replace those assets from a machine with Go and
GitHub CLI installed:

```powershell
gh auth login
.\scripts\publish-gnb.ps1
```

Preview the local build and target release URL without uploading:

```powershell
.\scripts\publish-gnb.ps1 -DryRun
```
