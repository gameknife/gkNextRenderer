#pragma once

#include "Engine/Common/CoreMinimal.hpp"

class NextEngine;

namespace Modules::NextDotNet
{
    class DotNetRuntime;

    struct FConfig
    {
        /// Managed game assembly, relative to the managed output directory. Empty means the runtime
        /// starts with no game loaded, which is a valid state: the host is still initialised and a
        /// game can be loaded later.
        std::string gameAssembly = "game/GkNext.Game.dll";

        /// Rebuild assets/csharp before starting when sources are newer than the last build.
        /// Ignored under NativeAOT, where the managed code is already linked in.
        bool compileManagedSources = true;

        /// Watch the game assembly and hot reload it. CoreCLR only; see design 3.5.
        bool enableHotReload = true;
    };

    void Install(NextEngine& engine, FConfig config);
    DotNetRuntime* Get(NextEngine& engine);
    const DotNetRuntime* Get(const NextEngine& engine);
}
