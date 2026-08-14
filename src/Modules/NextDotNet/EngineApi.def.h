// The binding surface. This file is the single source of truth for every function that managed
// code can call on the engine (design section 4.1).
//
//   GK_API(namespace, name, returnType, (params))
//
// It is included three times with different GK_API definitions:
//   - Interop.h        expands it into the FEngineApi struct fields
//   - EngineApi.cpp    expands it into the table fill
//   - gnb csharpgen    parses it into assets/csharp/GkNext.Engine/Engine.g.cs
//
// Adding a binding is therefore one line here plus one implementation function. Do not declare a
// binding anywhere else, and do not hand-edit Engine.g.cs.
//
// TYPE RULES (design 4.4) — violating these breaks NativeAOT, usually silently:
//   - no bool: use GkBool (int32, 0/1)
//   - no packed-color ambiguity: use GkColor32 (IM_COL32 layout)
//   - strings in: GkStr (UTF-8 range owned by the caller for the duration of the call)
//   - strings out: (char* buffer, int32_t capacity), returns the length that was (or would be)
//     written, so the caller can probe with a null buffer
//   - structs cross by pointer, are fixed-layout, have no optional fields and contain no
//     strings — a string inside a struct would need arena lifetime management the caller cannot
//     see, so names are passed as their own GkStr parameter instead
//
// The trailing `//=` comment declares default argument values for the generated C# wrapper. It is
// invisible to the preprocessor and is the only place defaults are written — C++ function pointer
// declarations cannot carry them. Only compile-time constants are allowed, because that is what a
// C# default argument accepts; anything else needs an explicit parameter.

// --- Logging -------------------------------------------------------------------------------
GK_API(Log, Info,  void, (GkStr message))
GK_API(Log, Warn,  void, (GkStr message))
GK_API(Log, Error, void, (GkStr message))

// --- Engine frame state and process control ------------------------------------------------
GK_API(Engine, GetTotalFrames,        uint32_t, ())
GK_API(Engine, GetTime,               double,   ())
GK_API(Engine, GetDeltaSeconds,       double,   ())
GK_API(Engine, GetSmoothDeltaSeconds, double,   ())
GK_API(Engine, RequestLoadScene,      void,     (GkStr filename))
GK_API(Engine, RequestClose,          void,     ())
GK_API(Engine, IsReplayMode,          GkBool,   ())

// --- Input ---------------------------------------------------------------------------------
// An empty name means "any": IsKeyDown("") is true while any key is held, and IsKeyPressed("")
// is true on any key/mouse/gamepad press this frame. Flappy's replay parity depends on this.
GK_API(Input, IsKeyDown,             GkBool, (GkStr name))
GK_API(Input, IsKeyPressed,          GkBool, (GkStr name))
GK_API(Input, IsMouseButtonDown,     GkBool, (int32_t button))
GK_API(Input, IsMouseButtonPressed,  GkBool, (int32_t button))
GK_API(Input, IsGamepadButtonDown,   GkBool, (GkStr name))

// --- Audio ---------------------------------------------------------------------------------
GK_API(Audio, PlaySfx,   void, (GkStr path, float volume))   //= volume:1.0f
GK_API(Audio, PlayMusic, void, (GkStr path, float volume))   //= volume:1.0f
GK_API(Audio, StopMusic, void, ())

// --- Immediate-mode UI ---------------------------------------------------------------------
GK_API(UI, Begin,              GkBool, (GkStr name, int32_t flags))                                            //= flags:0
GK_API(UI, End,                void,   ())
GK_API(UI, Text,               void,   (GkStr text))
GK_API(UI, SetCursorPos,       void,   (float x, float y))
GK_API(UI, GetWindowSize,      void,   (FVec2* outSize))
GK_API(UI, SetWindowFontScale, void,   (float scale))
GK_API(UI, GetScreenSize,      void,   (FVec2* outSize))
GK_API(UI, CalcTextSize,       void,   (GkStr text, float scale, FVec2* outSize))                              //= scale:1.0f
GK_API(UI, DrawText,           void,   (GkStr text, float x, float y, GkColor32 color, float scale))           //= scale:1.0f
GK_API(UI, DrawRectFilled,     void,   (float x, float y, float width, float height, GkColor32 color, float rounding))                  //= rounding:0.0f
GK_API(UI, DrawRect,           void,   (float x, float y, float width, float height, GkColor32 color, float rounding, float thickness)) //= rounding:0.0f, thickness:1.0f

// --- Live scene ----------------------------------------------------------------------------
// These address nodes by instance id and are only valid once a scene is committed. Inside the
// BeforeSceneRebuild hook the nodes exist only in the vector being built and are not addressable
// yet, so a node's starting transform belongs in the spec it is created with, not in a Scene_Set*
// call made during the hook.
//
// Moving a node updates the scene graph but does not tell the renderer its instance transforms
// changed: call Scene_MarkTransformDirty once per tick after moving things, or nothing appears to
// move on screen.
GK_API(Scene, GetIndicesCount,         uint32_t, ())
GK_API(Scene, FindNodeIdWithComponent, uint32_t, (GkStr componentType))
GK_API(Scene, AddRenderNode,           uint32_t, (GkStr name, const FRenderNodeSpec* spec))
GK_API(Scene, RemoveNodeById,          void,     (uint32_t nodeId))
GK_API(Scene, MarkTransformDirty,      void,     ())
GK_API(Scene, RecalcNodeTransform,     void,     (uint32_t nodeId, GkBool full))   //= full:0
// Direct node transform. Distinct from the component property access reserved below: a node's
// transform is the node's own state, not a reflected component field, and gameplay moves nodes
// every frame.
GK_API(Scene, SetNodeTranslation,      void,     (uint32_t nodeId, const FVec3* translation))
GK_API(Scene, SetNodeScale,            void,     (uint32_t nodeId, const FVec3* scale))
GK_API(Scene, SetNodeVisible,          void,     (uint32_t nodeId, GkBool visible))

// --- Scene construction ---------------------------------------------------------------------
// Only valid inside the BeforeSceneRebuild hook window; calls outside it are rejected and logged.
// The QuickJS-era AddProceduralModel took a tagged-union spec because JS has no overloads; C# does,
// so each primitive is its own entry and every parameter stays a scalar (design 4.4).
GK_API(SceneBuild, AddBoxModel,              uint32_t, (const FVec3* min, const FVec3* max))
GK_API(SceneBuild, AddSphereModel,           uint32_t, (const FVec3* center, float radius))
GK_API(SceneBuild, AddLambertianMaterial,    uint32_t, (const FVec3* color))
GK_API(SceneBuild, AddDiffuseLightMaterial,  uint32_t, (const FVec3* color, float intensity))   //= intensity:1.0f
GK_API(SceneBuild, AddRenderNode,            uint32_t, (GkStr name, const FRenderNodeSpec* spec))

// --- Render camera override -----------------------------------------------------------------
// Not a binding. The engine pulls the camera from the game each frame through
// FManagedApi::OverrideCamera instead. QuickJS needed a push-style SetOverrideCamera because the
// script registered callbacks dynamically; with a typed IGameModule the engine can just ask, which
// removes both a binding and the stale-state problem a push API has when a frame forgets to call it.

// --- Paths and asset I/O ---------------------------------------------------------------------
// File writing is deliberately absent: managed code has the BCL. What the engine must supply is
// the information only it has — where the project lives, and how to read through the package
// filesystem (assets may be inside a .pak). See design 4.4 decision 4.
GK_API(Paths,  GetProjectRoot, int32_t, (char* buffer, int32_t capacity))
GK_API(Paths,  GetOutputDir,   int32_t, (char* buffer, int32_t capacity))
GK_API(Assets, ReadFile,       int32_t, (GkStr path, uint8_t* buffer, int32_t capacity))

// --- Reserved: component property access ------------------------------------------------------
// Node/component property get/set belongs here, but its shape is decided by the generator that
// consumes it (Components.g.cs, phase 5 of docs/plans/dotnet-scripting-plan.md). Adding guessed
// entries now would mean designing an ABI without its only consumer. Scene_FindNodeIdWithComponent
// above is enough for phase 3 and 4.
