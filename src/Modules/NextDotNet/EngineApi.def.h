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
GK_API(Input, GetMousePosition,      void,   (FVec2* outPosition))
// SDL_GamepadAxis values: left X/Y, right X/Y, left/right trigger are 0..5. The returned value is
// normalised to [-1, 1]; games own their dead-zone policy.
GK_API(Input, GetGamepadAxis,        float,  (int32_t axis))

// --- Audio ---------------------------------------------------------------------------------
GK_API(Audio, PlaySfx,   void, (GkStr path, float volume))   //= volume:1.0f
GK_API(Audio, PlaySfxEx, void, (GkStr path, float volume, uint32_t minIntervalMs))
GK_API(Audio, PlayMusic, void, (GkStr path, float volume))   //= volume:1.0f
GK_API(Audio, StopMusic, void, ())
GK_API(Audio, SetMusicVolume, void, (float volume))

// --- Physics -------------------------------------------------------------------------------
// Bodies are opaque uint32 handles owned by the native physics world. Primitive creation stays
// backend-neutral; no Jolt types cross the ABI. GetBodyState returns the last state published by
// the async physics step, while mutation calls safely ignore stale/invalid handles.
GK_API(Physics, IsAvailable,        GkBool,  ())
GK_API(Physics, SetWorldPaused,     void,    (GkBool paused))
GK_API(Physics, IsWorldPaused,      GkBool,  ())
GK_API(Physics, CreateSphereBody,   uint32_t,(const FVec3* position, float radius, GkPhysicsMotionType motionType))
GK_API(Physics, CreateBoxBody,      uint32_t,(const FVec3* position, const FVec4* rotation, const FVec3* extent, GkPhysicsMotionType motionType))
GK_API(Physics, RemoveBody,         void,    (uint32_t bodyId))
GK_API(Physics, SetBodyActive,      void,    (uint32_t bodyId, GkBool active))
GK_API(Physics, SetBodyTransform,   void,    (uint32_t bodyId, const FVec3* position, const FVec4* rotation, GkBool resetVelocity)) //= resetVelocity:false
GK_API(Physics, MoveKinematicBody,  void,    (uint32_t bodyId, const FVec3* position, const FVec4* rotation, float deltaSeconds))
GK_API(Physics, SetBodyVelocity,    void,    (uint32_t bodyId, const FVec3* linearVelocity, const FVec3* angularVelocity))
GK_API(Physics, AddForceToBody,     void,    (uint32_t bodyId, const FVec3* force))
GK_API(Physics, GetBodyState,       void,    (uint32_t bodyId, FPhysicsBodyState* outState))

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
// Managed code owns immediate-mode widgets and composes a complete draw list. Native receives the
// three borrowed spans once per frame and only translates primitive commands to ImDrawList; no
// ImGui window/control/style API is mirrored across the ABI.
GK_API(UI, RequestTexture,     void,   (GkStr path, GkBool srgb, GkBool persistent, FUiTexture* outTexture)) //= srgb:true, persistent:true
GK_API(UI, SubmitDrawList,     void,   (const FUiDrawCommand* commands, int32_t commandCount, const FVec2* points, int32_t pointCount, const uint8_t* utf8, int32_t utf8Length))

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
// Direct node transform. The node's transform is also reachable through the reflected property
// access below (Node::Translation and friends), and both routes reach the same setter; these exist
// because gameplay moves nodes every frame and should not pay a property lookup to do it.
GK_API(Scene, SetNodeTranslation,      void,     (uint32_t nodeId, const FVec3* translation))
GK_API(Scene, SetNodeRotation,         void,     (uint32_t nodeId, const FVec4* rotation))
GK_API(Scene, SetNodeScale,            void,     (uint32_t nodeId, const FVec3* scale))
GK_API(Scene, SetNodeTransform,        void,     (const FNodeTransform* transform))
GK_API(Scene, SetNodeTransforms,       void,     (const FNodeTransform* transforms, int32_t transformCount))
GK_API(Scene, SetNodeVisible,          void,     (uint32_t nodeId, GkBool visible))
GK_API(Scene, SetNodeVisibleRecursive, void,     (uint32_t nodeId, GkBool visible))
GK_API(Scene, SetNodeParent,           GkBool,   (uint32_t childId, uint32_t parentId))
GK_API(Scene, SetNodePrimaryMaterial,  void,     (uint32_t nodeId, uint32_t materialId))
GK_API(Scene, SetNodeMaterialRecursive,void,     (uint32_t nodeId, uint32_t materialId))
GK_API(Scene, SetNodeOutlineFlags,     void,     (uint32_t nodeId, uint32_t outlineFlags))
GK_API(Scene, AddLambertianMaterial,   uint32_t, (const FVec3* color))
GK_API(Scene, AddDiffuseLightMaterial, uint32_t, (const FVec3* color, float intensity)) //= intensity:1.0f
GK_API(Scene, BindPhysicsBody,          GkBool,   (uint32_t nodeId, uint32_t bodyId, GkNodeMobility mobility))
// The node carrying the scene's EnvironmentComponent, created if the scene has none — the same
// on-demand behaviour C++ gameplay gets from Scene::GetEnvSettings(). A procedural scene built
// from SceneBuild has no environment node, so without this there would be no way for managed code
// to reach sun and sky at all. The settings themselves are reflected properties and are reached
// through the generated wrapper: `new NodeRef(Scene.GetEnvironmentNodeId()).Environment`.
GK_API(Scene, GetEnvironmentNodeId,    uint32_t, ())

// --- Scene construction ---------------------------------------------------------------------
// Only valid inside the BeforeSceneRebuild hook window; calls outside it are rejected and logged.
// The QuickJS-era AddProceduralModel took a tagged-union spec because JS has no overloads; C# does,
// so each primitive is its own entry and every parameter stays a scalar (design 4.4).
GK_API(SceneBuild, AddBoxModel,              uint32_t, (const FVec3* min, const FVec3* max))
GK_API(SceneBuild, AddSphereModel,           uint32_t, (const FVec3* center, float radius))
GK_API(SceneBuild, AddLambertianMaterial,    uint32_t, (const FVec3* color))
GK_API(SceneBuild, AddDiffuseLightMaterial,  uint32_t, (const FVec3* color, float intensity))   //= intensity:1.0f
GK_API(SceneBuild, AddRenderNode,            uint32_t, (GkStr name, const FRenderNodeSpec* spec))
GK_API(SceneBuild, BindPhysicsBody,          GkBool,   (uint32_t nodeId, uint32_t bodyId, GkNodeMobility mobility))

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

// --- Component and node property access --------------------------------------------------------
// Reflected properties, addressed by (nodeId, typeId, propId).
//
// This is the only part of the binding surface not written by hand: the property *names* live in
// entt::meta, not here, so these entries are the typed transport and the generated wrappers in
// Components.g.cs are what a game author actually calls
// (`node.Render.Visible = false`). Adding a property to a component therefore needs no change to
// this file — register it with REFLECT_COMPONENT, refresh the manifest, regenerate.
//
// typeId is the meta type id, which is the hash of the name the type was registered under
// ("RenderComponent"_hs). The node's own properties use "Node"_hs, so one addressing scheme covers
// both and the generated Node wrapper is not a special case.
//
// propId comes from the committed reflection manifest and is a compile-time constant in the
// generated C#: a per-frame property write must not do string comparisons. A stale manifest would
// therefore address a property that no longer exists, which is why a unit test compares the
// committed manifest against live reflection.
//
// One accessor pair per property type rather than a single variant-shaped entry: a variant would
// need a tagged union crossing the boundary, and the generator already knows each property's exact
// type, so the type information would be discarded at the boundary only to be re-checked at
// runtime. Getters report failure as a zero value and a logged warning, matching the rest of the
// surface (Scene_FindNodeIdWithComponent returns GK_INVALID_NODE_ID rather than an error code).
GK_API(Component, Has,       GkBool,   (uint32_t nodeId, uint32_t typeId))
GK_API(Component, GetBool,   GkBool,   (uint32_t nodeId, uint32_t typeId, uint32_t propId))
GK_API(Component, SetBool,   void,     (uint32_t nodeId, uint32_t typeId, uint32_t propId, GkBool value))
GK_API(Component, GetInt32,  int32_t,  (uint32_t nodeId, uint32_t typeId, uint32_t propId))
GK_API(Component, SetInt32,  void,     (uint32_t nodeId, uint32_t typeId, uint32_t propId, int32_t value))
GK_API(Component, GetUInt32, uint32_t, (uint32_t nodeId, uint32_t typeId, uint32_t propId))
GK_API(Component, SetUInt32, void,     (uint32_t nodeId, uint32_t typeId, uint32_t propId, uint32_t value))
GK_API(Component, GetFloat,  float,    (uint32_t nodeId, uint32_t typeId, uint32_t propId))
GK_API(Component, SetFloat,  void,     (uint32_t nodeId, uint32_t typeId, uint32_t propId, float value))
GK_API(Component, GetDouble, double,   (uint32_t nodeId, uint32_t typeId, uint32_t propId))
GK_API(Component, SetDouble, void,     (uint32_t nodeId, uint32_t typeId, uint32_t propId, double value))
GK_API(Component, GetVec2,   void,     (uint32_t nodeId, uint32_t typeId, uint32_t propId, FVec2* outValue))
GK_API(Component, SetVec2,   void,     (uint32_t nodeId, uint32_t typeId, uint32_t propId, const FVec2* value))
GK_API(Component, GetVec3,   void,     (uint32_t nodeId, uint32_t typeId, uint32_t propId, FVec3* outValue))
GK_API(Component, SetVec3,   void,     (uint32_t nodeId, uint32_t typeId, uint32_t propId, const FVec3* value))
GK_API(Component, GetVec4,   void,     (uint32_t nodeId, uint32_t typeId, uint32_t propId, FVec4* outValue))
GK_API(Component, SetVec4,   void,     (uint32_t nodeId, uint32_t typeId, uint32_t propId, const FVec4* value))
// Quaternions cross as FVec4 in (x, y, z, w) order. glm::quat stores w first, so the conversion is
// not a memcpy — doing it in one place here keeps every caller from getting the order wrong.
GK_API(Component, GetQuat,   void,     (uint32_t nodeId, uint32_t typeId, uint32_t propId, FVec4* outValue))
GK_API(Component, SetQuat,   void,     (uint32_t nodeId, uint32_t typeId, uint32_t propId, const FVec4* value))
GK_API(Component, GetString, int32_t,  (uint32_t nodeId, uint32_t typeId, uint32_t propId, char* buffer, int32_t capacity))
GK_API(Component, SetString, void,     (uint32_t nodeId, uint32_t typeId, uint32_t propId, GkStr value))
// Mat4, Array and AssetRef properties are reflected but not bound. Mat4 and AssetRef have no
// managed counterpart yet, and an array needs element-level accessors rather than a value copy;
// the generator skips them and says so in a comment on the wrapper, so the omission is visible in
// the generated file rather than silently absent.
