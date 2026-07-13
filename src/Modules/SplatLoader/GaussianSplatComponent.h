#pragma once

#include "Engine/Assets/Core/Component.hpp"
#include "Engine/Runtime/Reflection/ReflectionMacros.hpp"
#include "Modules/SplatLoader/GaussianSplat.hpp"

#include <algorithm>

namespace Runtime
{
    class GaussianSplatComponent final : public Assets::Component
    {
    public:
        REFLECT_COMPONENT(GaussianSplatComponent)
        GaussianSplatComponent() = default;
        explicit GaussianSplatComponent(std::shared_ptr<const Assets::FGaussianSplatData> data)
            : data_(std::move(data)) {}

        const std::shared_ptr<const Assets::FGaussianSplatData>& GetData() const { return data_; }
        void SetVisible(bool value) { visible_ = value; }
        bool GetVisible() const { return visible_; }
        bool ToggleVisible() { return visible_ = !visible_; }
        void SetRayCastVisible(bool value) { rayCastVisible_ = value; }
        bool GetRayCastVisible() const { return rayCastVisible_; }
        void SetOpacityScale(float value) { opacityScale_ = std::max(value, 0.0f); }
        float GetOpacityScale() const { return opacityScale_; }
        void SetCastShadow(bool value) { castShadow_ = value; }
        bool GetCastShadow() const { return castShadow_; }
        void SetRayTraceOccluder(bool value) { rayTraceOccluder_ = value; }
        bool GetRayTraceOccluder() const { return rayTraceOccluder_; }
        void SetReceiveLighting(bool value) { receiveLighting_ = value; }
        bool GetReceiveLighting() const { return receiveLighting_; }
        void SetLightingStrength(float value) { lightingStrength_ = std::clamp(value, 0.0f, 1.0f); }
        float GetLightingStrength() const { return lightingStrength_; }
        void SetProxyDensityScale(float value) { proxyDensityScale_ = std::max(value, 0.0f); }
        float GetProxyDensityScale() const { return proxyDensityScale_; }
        void SetProxyAlphaThreshold(float value) { proxyAlphaThreshold_ = std::clamp(value, 0.0f, 1.0f); }
        float GetProxyAlphaThreshold() const { return proxyAlphaThreshold_; }

    private:
        std::shared_ptr<const Assets::FGaussianSplatData> data_;
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
