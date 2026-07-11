#pragma once

#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Runtime/Interface/DebugUiProvider.hpp"

class NextEngine;

namespace DevTools
{
    void Install(NextEngine& engine);

    // Default IDebugUiProvider backed by the DevTools overlays/panels.
    // Register on the engine from the application entry point:
    //     engine.SetDebugUiProvider(&DevTools::DefaultDebugUiProvider());
    Runtime::IDebugUiProvider& DefaultDebugUiProvider();
}
