# QuickJS Bindings Cookbook

This note documents the binding path used by the Flappy parity demo. Keep it aligned with `src/Runtime/Subsystems/QuickJSEngine.cpp`.

## TypeScript Entry And Modules

- TypeScript sources live under `assets/typescript`.
- `QuickJSEngine` compiles the root `assets/typescript/tsconfig.json` into `assets/scripts` when sources are newer than `.tsc.stamp`.
- The runtime module loader resolves relative ESM imports from the compiled `assets/scripts` tree and appends `.js` when needed.
- Use `import * as NE from "../Engine"` or `import * as NE from "Engine"` depending on the compiled module depth. The loader maps `./Engine`, `../Engine`, and `assets/scripts/Engine` style imports back to the built-in `Engine` module.

## Adding A Binding

1. Add the C++ function in `QuickJSEngine.cpp`.
2. Register it in `ResetContextAndLoadScript()` on the `Engine` module or one of its namespace objects.
3. Add matching declarations in `BuildTypeScriptDefinitions()`.
4. Add a minimal call in `assets/typescript/test.ts` unless the binding is only meaningful for a dedicated host.
5. Build with the platform `full-*` preset and start an app until the log reaches `uploaded scene [...] to gpu`.

Prefer raw `JS_NewCFunction` for object-shaped arguments, JSON values, optional arguments, or functions returning ad-hoc JS objects. Use `quickjspp` member bindings for simple C++ classes with stable signatures.

## Current Engine Module Surface

- `Global.GetEngine()`, `Global.GetScene()`, `Global.spdlog(...)`
- `NextEngine.GetTotalFrames()`, `GetTime()`, `GetDeltaSeconds()`, `GetSmoothDeltaSeconds()`, `RegisterJSCallback(...)`
- `Input.IsKeyDown()`, `IsKeyPressed()`, `IsMouseButtonDown()`, `IsMouseButtonPressed()`, `GetGamepadButton()`
- `Audio.PlaySfx()`, `PlayMusic()`, `StopMusic()`
- `UI.Begin()`, `End()`, `Text()`, `SetCursorPos()`, `GetWindowSize()`, `SetWindowFontScale()`, `GetScreenSize()`
- `LoadJson()`, `RequestLoadScene()`, `RequestClose()`, `GetScreenSize()`, `SetOverrideCamera()`, `IsReplayMode()`, `WriteFile()`
- Lifecycle hooks include `onInit`, `onDestroy`, `onSceneLoaded`, `onRenderUI`, and `onInputEvent(event)` for event-driven input parity with native game instances
- Dynamic scene helpers: `Scene.AddBoxNode()`, `AddSphereNode()`, `RemoveNodeById()`, `MarkTransformDirty()`, `GetNodeById()`
- Dynamic node helper: `Node.RecalcTransform()`

## Scene And Node Notes

`Global.GetScene()` returns the engine-owned scene as a non-owning pointer wrapped by quickjspp. Dynamic scene methods are attached to the shared quickjspp scene prototype after `module.class_<Assets::Scene>("Scene")` registers the class.

`Scene.GetNodeById()` returns a lightweight JS object backed by a node id. Property reads/writes go through reflection each time, so script code can safely assign whole values like:

```ts
node.Translation = { x, y, z };
node.Scale = { x, y, z };
node.RecalcTransform(true);
```

Do not mutate nested fields on a returned vector object, such as `node.Translation.x = 1`; that mutates a temporary JS object, not the C++ node.

## Flappy Parity Regression

`FlappyCpp` and `FlappyJs` exercise the binding set with deterministic replay:

```powershell
.\out\build\full-windows\bin\FlappyCpp.exe --flappy-replay
.\out\build\full-windows\bin\FlappyJs.exe --flappy-replay
python tools\flappy\diff_traces.py
```

The replay traces must match exactly for `birdY`, `birdVelocityY`, `score`, `state`, frame count, and death frame.
