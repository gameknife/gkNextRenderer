#pragma once

#include "Engine/Common/CoreMinimal.hpp"

class NextEngine;

namespace Modules::NextQuickJS
{
    class QuickJSEngine;

    struct FConfig
    {
        std::string entryScript;
        bool compileTypeScript = true;
        bool enableHotReload = true;
    };

    void Install(NextEngine& engine, FConfig config);
    QuickJSEngine* Get(NextEngine& engine);
    const QuickJSEngine* Get(const NextEngine& engine);
}
