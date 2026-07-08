#pragma once

#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Runtime/Interface/DebugUiProvider.hpp"

namespace DevTools
{
    // Default IDebugUiProvider backed by the DevTools overlays/panels.
    // Register on the engine from the application entry point:
    //     engine.SetDebugUiProvider(&DevTools::DefaultDebugUiProvider());
    Runtime::IDebugUiProvider& DefaultDebugUiProvider();
}
