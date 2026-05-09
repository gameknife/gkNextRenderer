# gnb

`gnb` is the gkNextRenderer build helper. It wraps setup, CMake presets, running,
tests, optional pak assets, packaging, and mobile build entry points.

Build locally from the repository root:

```bash
cd tools/gnb
go build -trimpath -ldflags="-s -w" -o ../../gnb ./cmd/gnb
```

On Windows:

```powershell
cd tools/gnb
go build -trimpath -ldflags="-s -w" -o ../../gnb.exe ./cmd/gnb
```

Common commands:

```bash
./gnb setup
./gnb build
./gnb run
./gnb test
./gnb doctor
```
