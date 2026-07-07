# gnb

`gnb` is the gkNextEngine build helper. It wraps setup, CMake presets, running,
tests, optional pak assets, packaging, mobile build entry points, and a
dashboard (native Wails window on Windows/macOS; browser/server-only on Linux).

Architecture and stack overview: [`docs/guides/gnb-tech-stack.md`](../../docs/guides/gnb-tech-stack.md)

Source-level code & architecture guide (for editing `gnb` itself): [`docs/guides/gnb-architecture.md`](../../docs/guides/gnb-architecture.md)

Build locally from the repository root:

```bash
cd tools/gnb
go build -tags "desktop,production,wv2runtime.embed" -trimpath -ldflags="-s -w" -o ../../gnb ./cmd/gnb
```

On Windows:

```powershell
cd tools/gnb
go build -tags "desktop,production,wv2runtime.embed" -trimpath -ldflags="-s -w" -o ../../gnb.exe ./cmd/gnb
```

Common commands:

```bash
./gnb setup
./gnb build
./gnb run
./gnb test
./gnb doctor
./gnb dashboard
```

Bare `gnb` and `gnb dashboard` open the native Wails window on Windows/macOS.
On Linux the Wails native window is not built; `gnb`/`gnb dashboard` fall back
to opening the dashboard in the system browser, and `gnb dashboard --no-open`
runs the server without opening anything (full CLI mode, interact via
`curl 127.0.0.1:7777`).
