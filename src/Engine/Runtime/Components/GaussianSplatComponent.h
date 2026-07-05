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

        void SetCastShadow(bool castShadow) { castShadow_ = castShadow; }
        bool GetCastShadow() const { return castShadow_; }

        void SetRayTraceOccluder(bool rayTraceOccluder) { rayTraceOccluder_ = rayTraceOccluder; }
        bool GetRayTraceOccluder() const { return rayTraceOccluder_; }

        void SetReceiveLighting(bool receiveLighting) { receiveLighting_ = receiveLighting; }
        bool GetReceiveLighting() const { return receiveLighting_; }

        void SetLightingStrength(float strength) { lightingStrength_ = std::clamp(strength, 0.0f, 1.0f); }
        float GetLightingStrength() const { return lightingStrength_; }

        void SetProxyDensityScale(float scale) { proxyDensityScale_ = std::max(scale, 0.0f); }
        float GetProxyDensityScale() const { return proxyDensityScale_; }

        void SetProxyAlphaThreshold(float threshold) { proxyAlphaThreshold_ = std::clamp(threshold, 0.0f, 1.0f); }
        float GetProxyAlphaThreshold() const { return proxyAlphaThreshold_; }

    private:
        uint32_t splatModelId_ = static_cast<uint32_t>(-1);
        bool visible_ = true;
        bool rayCastVisible_ = true;
        float opacityScale_ = 1.0f;
        bool castShadow_ = true;
        bool rayTraceOccluder_ = true;
        bool receiveLighting_ = true;
        float lightingStrength_ = 0.35f;
        float proxyDensityScale_ = 1.0f;
        float proxyAlphaThreshold_ = 0.35f;
    };
}
