#pragma once
#include "Assets/Component.h"
#include <array>
#include <cstdint>

namespace Runtime
{
    class RenderComponent : public Assets::Component
    {
    public:
        RenderComponent() = default;

        void SetModelId(uint32_t modelId) { modelId_ = modelId; }
        uint32_t GetModelId() const { return modelId_; }

        void SetMaterial(const std::array<uint32_t, 16>& materials) { materialIdx_ = materials; }
        std::array<uint32_t, 16>& Materials() { return materialIdx_; }
        const std::array<uint32_t, 16>& Materials() const { return materialIdx_; }

        void SetVisible(bool visible) { visible_ = visible; }
        bool IsVisible() const { return visible_; }

        void SetRayCastVisible(bool visible) { rayCastVisible_ = visible; }
        bool IsRayCastVisible() const { return rayCastVisible_; }
        
        bool IsDrawable() const { return modelId_ != -1; }

        void SetSkinIndex(int32_t skinIndex) { skinIndex_ = skinIndex; }
        int32_t GetSkinIndex() const { return skinIndex_; }

    private:
        uint32_t modelId_ = -1;
        std::array<uint32_t, 16> materialIdx_ = {0}; // Initialize with defaults
        bool visible_ = true;
        bool rayCastVisible_ = true;
        int32_t skinIndex_ = -1;
    };
}
