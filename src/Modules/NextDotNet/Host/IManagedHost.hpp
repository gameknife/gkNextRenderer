#pragma once

#include "Modules/NextDotNet/Interop.h"

#include <memory>
#include <string>

// The backend seam. Everything that differs between CoreCLR and NativeAOT lives behind this
// interface, and all of it is about one question: how do we get the address of GkNext_Bootstrap?
// Once that pointer is in hand the two backends are indistinguishable to the rest of the engine,
// and the managed code they run is byte-for-byte the same.
//
// See docs/designs/dotnet-scripting-design.md section 3.2.

namespace Modules::NextDotNet
{
    struct FHostConfig
    {
        /// Directory holding GkNext.Bootstrap.dll and its runtimeconfig.json. Used by the CoreCLR
        /// host; ignored by the AOT host, which has the managed code linked in.
        std::string managedRootDir;

        /// Root of the .NET installation to resolve hostfxr from, normally external/dotnet. Empty
        /// falls back to the machine-wide install. CoreCLR only.
        std::string dotnetRoot;

        /// Managed assembly simple name, without extension.
        std::string bootstrapAssembly = "GkNext.Bootstrap";

        /// Fully qualified type holding the bootstrap entry point.
        std::string bootstrapType = "GkNext.Bootstrap.Bootstrap";

        /// Static [UnmanagedCallersOnly] method on the type above.
        std::string bootstrapMethod = "Initialize";
    };

    class IManagedHost
    {
    public:
        virtual ~IManagedHost() = default;

        /// Loads the managed side and exchanges function tables. engineApi must outlive the host:
        /// managed code stores the pointer and calls through it for the process lifetime.
        virtual bool Initialize(const FEngineApi& engineApi, std::string& outError) = 0;

        /// Managed entry points, valid after a successful Initialize. Never null afterwards, and
        /// stable across hot reloads.
        virtual const FManagedApi* Managed() const = 0;

        /// "CoreCLR" or "NativeAOT". Reported in logs so a session's backend is never in doubt.
        virtual const char* BackendName() const = 0;

        /// Whether this backend can swap game code at runtime. The only allowed behavioural
        /// difference between the backends (design section 3.5).
        virtual bool SupportsHotReload() const = 0;

        /// Whether the game assembly is loaded from disk. False under NativeAOT, where it is
        /// linked into the executable and the path passed to LoadGame is meaningless.
        virtual bool LoadsGameFromDisk() const = 0;
    };

    /// Exactly one implementation is compiled, selected by GK_DOTNET_USE_AOT.
    std::unique_ptr<IManagedHost> CreateManagedHost(FHostConfig config);
}
