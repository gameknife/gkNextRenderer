#pragma once

#include "Engine/Assets/Core/Component.h"
#include "Engine/Runtime/Reflection/ReflectionMacros.h"

#include <algorithm>
#include <cstdint>

namespace Runtime
{
    class GaussianSplatComponent final : public Assets::Component
    {
    public:
        REFLECT_COMPONENT(GaussianSplatComponent)

        GaussianSplatComponent() = default;
        explicit GaussianSplatComponent(uint32_t splatModelId) : splatModelId_(splatModelId) {}

        void SetSplatModelId(uint32_t splatModelId) { splatModelId_ = splatModelId; }
        uint32_t GetSplatModelId() const { return splatModelId_; }

        void SetVisible(bool visible) { visible_ = visible; }
        bool GetVisible() const { return visible_; }
        bool ToggleVisible()
        {
            visible_ = !visible_;
            return visible_;
        }

        void SetRayCastVisible(bool visible) { rayCastVisible_ = visible; }
        bool GetRayCastVisible() const { return rayCastVisible_; }

        void SetOpacityScale(float opacityScale) { opacityScale_ = std::max(opacityScale, 0.0f); }
        float GetOpacityScale() const { return opacityScale_; }

    private:
        uint32_t splatModelId_ = static_cast<uint32_t>(-1);
        bool visible_ = true;
        bool rayCastVisible_ = true;
        float opacityScale_ = 1.0f;
    };
}
