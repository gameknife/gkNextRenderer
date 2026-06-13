# Sim Kit

`src/Gameplay/Sim/` provides shared simulation infrastructure for applications such as
AirportSim and StudioSim. Domain state remains in each application; Sim Kit owns only
navigation, anchors, character visuals, and reusable movement behavior.

## Anchor Map

Use `NextGameplay::Sim::FAnchorMap` to parse named scene nodes:

```cpp
NextGameplay::Sim::FAnchorParseConfig config;
config.acceptCategories = {"desk", "meet_seat", "pantry"};
anchors.BuildFromScene(scene, config);
```

Applications may normalize parsed categories or maintain side tables for domain-only
metadata. StudioSim keeps desk role tags outside `FAnchorPoi`; AirportSim keeps its
service-point semantics in `AirportMap`.

Important conventions:

- `occupiedBy == -1` means unoccupied.
- `enabled == false` means the point cannot currently be selected.
- Use `ClaimFree`/`Release` and `ClaimSeat`/`ReleaseSeat` instead of modifying occupancy
  fields directly.

## Character Pool

`FCharacterPool` owns the NavGrid, path following, separation, and visual instances.
Configure and inject assets before scene rebuild, then initialize against the loaded
scene:

```cpp
NextGameplay::Sim::FCharacterPoolConfig config;
config.poolCapacity = 16;
config.useRig = true;
config.rigPath = "assets/scad/characters/agent_basic.scad";
config.slotTints = roleColors;

pool.Configure(config);
pool.InjectAssets(models, materials);
pool.OnSceneLoaded(scene);
```

Domain character types should inherit from `FSimCharacter`, as `AirportSim::FAgent`
and `StudioSim::FEmployee` do. Move their visual pointers from the pool slots into the
domain objects after `OnSceneLoaded`, then pass a span of base pointers to `Tick`.

`MoveTo` uses NavGrid A* when available and falls back to a direct path. Movement is
integrated in real seconds; applications decide whether time scaling should affect the
delta passed to the pool.

## Visuals And Animation

`ISimVisual` has geometry and ScadRig implementations. The pool automatically falls
back to box geometry when rig loading fails.

Animation hints are deliberately small:

- `Idle`
- `Walk`
- `Sit`
- `Work`

`FCharacterPool` controls `Walk` while moving. Applications set the stationary hint
from domain context before calling `Tick`, for example `Work` at a desk or `Sit` in a
meeting area.

## Round 2 TODO

The following systems are intentionally still application-local. Extract them only
after AirportSim and StudioSim have converged on compatible behavior:

- `FDecisionScheduler`: serial AI request, timeout, fallback, and main-thread apply.
- `FWorldClock`: game minutes, speed, pause, day index, and optional daylight.
- `FObservationCamera`: overview, follow target, picking, pan, and zoom.
- `FWorldOverlay`: projection, labels, speech bubbles, mood icons, and floating text.
- `FPerception`: event and nearby-state scans that trigger decisions.
- Shared domain-neutral mood, decision-result, and time-formatting types.

## Verification

Build shared consumers after changing Sim Kit:

```bash
./gnb build gkNextRenderer
./gnb build gkNextUnitTests
./out/build/<preset>/bin/gkNextUnitTests "[Unit][SimKit]"
```

Then build and visually verify the affected application with `gnb shot`.
