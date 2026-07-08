#pragma once

#include "Engine/Common/CoreMinimal.hpp"

class NextEngine;

namespace Runtime
{
    struct FShaderHotReloadStatus
    {
        bool shaderHotReloadEnabled = false;
        bool shaderInitialized = false;
        double shaderPollIntervalSeconds = 0.5;
        std::filesystem::path shaderSourceRoot;
        std::filesystem::path shaderOutputRoot;
        std::filesystem::path shaderCompiler;
    };

    class IShaderHotReloader
    {
    public:
        virtual ~IShaderHotReloader() = default;

        virtual void Tick(double deltaSeconds) = 0;
        virtual void SetEnabled(bool enabled) = 0;
        virtual bool IsEnabled() const = 0;
        virtual void SetPollInterval(double seconds) = 0;
        virtual void RequestRebuildAll() = 0;
        virtual FShaderHotReloadStatus GetStatus() const = 0;
    };

    using ShaderHotReloaderFactory = std::function<std::unique_ptr<IShaderHotReloader>(NextEngine&)>;
}
