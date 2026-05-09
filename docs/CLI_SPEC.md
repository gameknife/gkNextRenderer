# CLI Spec

The repository build CLI is `gnb`.

## Build

```bash
./gnb.sh build [target]
gnb.bat build [target]
```

Options: `--clean`, `--reconfigure`, `--jobs N`, `--no-unity`, `--lto`, `--print-cmd`.

## Run

```bash
./gnb.sh run [target] [-- app-args]
gnb.bat run [target] [-- app-args]
```

Options: `--bin-dir`, `--present-mode`, `--scene`, `--list`, `--dry-run`.

## Setup

```bash
./gnb.sh setup
gnb.bat setup
```

Options: `--skip-paks`, `--vcpkg-only`, `--refresh`.

See [gnb-cli.md](gnb-cli.md) for the full command manual.
