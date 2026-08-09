#pragma once

#include "Engine/Assets/Core/Component.hpp"
#include "Engine/Runtime/Reflection/ReflectionMacros.hpp"

#include <cstdint>
#include <string>

namespace Runtime
{
    enum class ESceneReferenceStatus : uint8_t
    {
        Unloaded,
        Loaded,
        Missing,
        CycleDetected,
        Unsupported,
        Failed,
    };

    class SceneReferenceComponent final : public Assets::Component
    {
    public:
        REFLECT_COMPONENT(SceneReferenceComponent)

        void SetAssetPath(std::string assetPath) { assetPath_ = std::move(assetPath); }
        const std::string& GetAssetPath() const { return assetPath_; }

        void SetStatus(ESceneReferenceStatus status) { status_ = status; }
        ESceneReferenceStatus GetStatus() const { return status_; }
        std::string GetStatusName() const;

        void SetResolvedPath(std::string resolvedPath) { resolvedPath_ = std::move(resolvedPath); }
        const std::string& GetResolvedPath() const { return resolvedPath_; }

        void SetError(std::string error) { error_ = std::move(error); }
        const std::string& GetError() const { return error_; }

        void SetLoadedNodeCount(uint32_t loadedNodeCount) { loadedNodeCount_ = loadedNodeCount; }
        uint32_t GetLoadedNodeCount() const { return loadedNodeCount_; }

    private:
        std::string assetPath_;
        std::string resolvedPath_;
        std::string error_;
        ESceneReferenceStatus status_ = ESceneReferenceStatus::Unloaded;
        uint32_t loadedNodeCount_ = 0;
    };
}
