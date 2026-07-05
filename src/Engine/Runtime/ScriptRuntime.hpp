#pragma once

#include "Engine/Common/CoreMinimal.hpp"

#include <SDL3/SDL_events.h>

class NextEngine;

namespace Runtime
{
    class IScriptRuntime
    {
    public:
        virtual ~IScriptRuntime() = default;

        virtual void Initialize() = 0;
        virtual void Tick(double deltaSeconds) = 0;
        virtual void HandleEvent(const SDL_Event& event) = 0;
    };

    using ScriptRuntimeFactory = std::function<std::unique_ptr<IScriptRuntime>(NextEngine&)>;
}
