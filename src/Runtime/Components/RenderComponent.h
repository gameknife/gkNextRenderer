#pragma once
#include "Assets/Core/Component.h"
#include "Runtime/Reflection/ReflectionMacros.h"
#include <array>
#include <cstdint>

namespace Runtime
{
    class RenderComponent : public Assets::Component
    {
    public:
        REFLECT_COMPONENT(RenderComponent)
        
        RenderComponent() = default;

        void SetModelId(uint32_t modelId) { modelId_ = modelId; }
        uint32_t GetModelId() const { return modelId_; }

        const std::array<uint32_t, 16>& GetMaterials() const { return materialIdx_; }
        void SetMaterials(const std::array<uint32_t, 16>& materials) { materialIdx_ = materials; }

        void SetVisible(bool visible) { visible_ = visible; }
        bool GetVisible() const { return visible_; }

        void SetRayCastVisible(bool visible) { rayCastVisible_ = visible; }
        bool GetRayCastVisible() const { return rayCastVisible_; }

        bool ToggleVisible()
        {
            visible_ = !visible_;
            return visible_;
        }

        bool ToggleRayCastVisible()
        {
            rayCastVisible_ = !rayCastVisible_;
            return rayCastVisible_;
        }
        
        bool IsDrawable() const { return modelId_ != static_cast<uint32_t>(-1); }

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
