#pragma once

#include <cstdint>

// The only ABI that crosses between native and managed code.
//
// This header is deliberately free of engine includes: it is compiled both into the engine module
// and into the standalone Phase 0 probe, and every declaration here has an exact counterpart in
// assets/csharp/GkNext.Engine/. Changing a layout means changing both sides and bumping
// GK_DOTNET_ABI_VERSION.
//
// FEngineApi is not written by hand — it is expanded from EngineApi.def.h, which is the single
// source of truth for the binding surface.
//
// See docs/designs/dotnet-scripting-design.md sections 3.1 and 4.4.

namespace Modules::NextDotNet
{
    /// Bumped whenever the layout of FEngineApi or FManagedApi changes. Bootstrap rejects a
    /// mismatch instead of reading a table with a different shape.
    inline constexpr uint32_t GK_DOTNET_ABI_VERSION = 3;

    // --- primitive cross-boundary types --------------------------------------------------------

    /// Returned by every binding that produces a node id when there is no node. Node instance ids
    /// start at 0 and 0 is a perfectly valid node, so "no node" needs a value of its own — using 0
    /// as the sentinel silently drops whatever was built first in an empty scene.
    inline constexpr uint32_t GK_INVALID_NODE_ID = 0xFFFFFFFFu;

    /// A UTF-8 range owned by the caller for the duration of the call. Not necessarily
    /// null-terminated: callees must honour length.
    struct GkStr
    {
        const char* Data;
        int32_t Length;
    };

    /// Booleans never cross the boundary as `bool`: its size is implementation-defined in C++ and
    /// C#'s `bool` is not blittable, so it would need a marshalling attribute that design section
    /// 3.4 rule 4 forbids. Values are 0 or 1; treat anything non-zero as true.
    using GkBool = int32_t;

    /// Packed color in IM_COL32 layout: byte order in memory is R, G, B, A, i.e. the literal value
    /// is 0xAABBGGRR. Chosen so the native side hands it straight to ImGui with no conversion; the
    /// generated C# wrapper exposes a Color struct with 0..1 float components and packs at the
    /// call site, which is how script authors saw colors under QuickJS.
    using GkColor32 = uint32_t;

    // --- cross-boundary structs ----------------------------------------------------------------
    // All fixed-layout, all blittable, no optional fields. Defaults live in the generated C#
    // wrapper, never in the wire format.

    struct FVec2
    {
        float X, Y;
    };

    struct FVec3
    {
        float X, Y, Z;
    };

    struct FVec4
    {
        float X, Y, Z, W;
    };

    struct FRenderNodeSpec
    {
        uint32_t ModelId;
        uint32_t MaterialId;
        FVec3 Translation;
        FVec3 Scale;
        GkBool Visible;
    };

    struct FCameraOverride
    {
        FVec3 Position;
        FVec3 Target;
        FVec3 Up;
        float FieldOfView;
    };

    enum class EInputEventType : int32_t
    {
        KeyDown = 0,
        KeyUp = 1,
        MouseButtonDown = 2,
        MouseButtonUp = 3,
        GamepadButtonDown = 4,
        GamepadButtonUp = 5,
    };

    /// A single input event forwarded to managed code. Key and gamepad identities cross as the
    /// raw SDL codes rather than names: names would mean a string allocation per event, and the
    /// managed side can map codes to whatever enum it likes.
    struct FInputEvent
    {
        int32_t Type;
        int32_t KeyCode;
        int32_t MouseButton;
        int32_t GamepadButton;
        GkBool Repeated;
    };

    /// Lifecycle hooks raised on managed code. The set matches the hooks the QuickJS runtime
    /// raised, so gameplay semantics carry over unchanged.
    enum class EScriptHook : int32_t
    {
        OnInit = 0,
        OnDestroy = 1,
        BeforeSceneRebuild = 2,
        OnSceneLoaded = 3,
        OnRenderUI = 4,
    };

    // --- the two function tables ----------------------------------------------------------------

    /// Native services handed to managed code at bootstrap. Expanded from EngineApi.def.h.
    struct FEngineApi
    {
        uint32_t Version;

#define GK_API(ns, name, ret, params) ret (*ns##_##name) params;
#include "Modules/NextDotNet/EngineApi.def.h"
#undef GK_API
    };

    /// Number of bindings in the table. Used by the consistency test that keeps the def file, the
    /// struct and the generated C# in agreement.
    inline constexpr uint32_t GK_ENGINE_API_COUNT =
#define GK_API(ns, name, ret, params) 1u +
#include "Modules/NextDotNet/EngineApi.def.h"
#undef GK_API
        0u;

    /// Managed entry points handed back to the host at bootstrap.
    ///
    /// These pointers are filled once and stay valid for the process lifetime, hot reload
    /// included: a reload is entirely a managed-side operation behind ReloadGame, so the host
    /// never rebinds anything.
    struct FManagedApi
    {
        uint32_t Version;
        int32_t (*LoadGame)(GkStr assemblyPath);
        int32_t (*UnloadGame)();
        int32_t (*ReloadGame)(GkStr assemblyPath);
        void (*Tick)(double deltaSeconds);
        /// Returns non-zero when the script consumed the hook. Only OnRenderUI acts on it today,
        /// matching the QuickJS `onRenderUI` contract.
        GkBool (*Lifecycle)(int32_t hook, double deltaSeconds);
        /// Returns non-zero when the script consumed the event.
        GkBool (*InputEvent)(const FInputEvent* event);
        /// Returns non-zero when the script wants to drive the render camera this frame.
        GkBool (*OverrideCamera)(FCameraOverride* outCamera);
    };

    /// Status codes returned by the managed lifetime calls. Mirrors GameHost in
    /// assets/csharp/GkNext.Bootstrap/GameHost.cs.
    enum class EGameStatus : int32_t
    {
        Ok = 0,
        ReloadUnavailable = 2,
        UnloadPending = 3,
        NotFound = -10,
        ManagedException = -100,
    };

    /// The one symbol that crosses the boundary. Identical under both backends: NativeAOT exports
    /// it from the produced library, CoreCLR resolves it through hostfxr.
    using FBootstrapFn = int32_t (*)(const FEngineApi* engineApi, FManagedApi* outManagedApi);

    inline GkStr MakeStr(const char* text, int32_t length)
    {
        return GkStr{text, length};
    }
}
