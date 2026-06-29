# NextRA Developer Guide

NextRA is the RTS MVP target under `src/Application/Game/NextRA`. It uses a fixed-tick deterministic sim, integer world coordinates, lockstep-style order execution, sync hash checks, and proc-geometry visuals.

## Run

```powershell
.\gnb.bat build NextRA gkNextUnitTests
.\gnb.bat run NextRA
.\gnb.bat shot --target NextRA --ui --frames 2400
.\out\build\windows\bin\gkNextUnitTests.exe "[Unit][NextRA]"
```

## Gameplay Loop

- Left click selects player 0 actors. Drag box selects multiple actors.
- Right click ground issues `Move`.
- Shift + right click ground issues `AttackMove`.
- Right click enemy issues `Attack`.
- Select the blue barracks to produce infantry or tanks.
- Destroy the enemy base to win.

## Determinism Rules

- Sim state lives in `SimWorld`; rendering state is outside the sync hash.
- Gameplay changes must enter through `FOrder`.
- `ComputeSyncHash` excludes `FRenderLink` and UI state.
- `FPathfindGrid` is integer-only and deterministic.
- Replay tests serialize order logs and compare the recorded hash sequence.

## Debug HUD

- `Tick`, `Hash`, and `Peer 0` show the current deterministic frame state.
- `Order latency` delays newly submitted local orders by N sim ticks.
- `Net delay`, `Drop every N`, and `Reorder every N` inject delay/drop/reorder behavior into AI orders for local lockstep demonstration.
- `Grid` toggles A* grid/passability/path overlay.
- `Mini` toggles the minimap.
- `Order log` shows recent orders with tick, player, type, and actor count.

## Current Scope

M0-M6 cover a playable single-machine RTS loop plus lockstep/replay verification. Real socket networking, resource economy, advanced building occupancy, and cross-platform bit-for-bit certification remain outside this MVP.
