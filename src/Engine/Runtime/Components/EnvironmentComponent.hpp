#pragma once

#include "Engine/Assets/Core/Component.hpp"
#include "Engine/Assets/Core/Model.hpp"
#include "Engine/Runtime/Reflection/ReflectionMacros.hpp"

namespace Runtime
{
    class EnvironmentComponent final : public Assets::Component, public Assets::EnvironmentSetting
    {
    public:
        REFLECT_COMPONENT(EnvironmentComponent)

        EnvironmentComponent() = default;

        void SetSettings(const Assets::EnvironmentSetting& settings)
        {
            static_cast<Assets::EnvironmentSetting&>(*this) = settings;
        }

        const Assets::EnvironmentSetting& GetSettings() const
        {
            return static_cast<const Assets::EnvironmentSetting&>(*this);
        }

        void SetControlSpeed(float value) { ControlSpeed = value; }
        float GetControlSpeed() const { return ControlSpeed; }

        void SetGammaCorrection(bool value) { GammaCorrection = value; }
        bool GetGammaCorrection() const { return GammaCorrection; }

        void SetHasSky(bool value) { HasSky = value; }
        bool GetHasSky() const { return HasSky; }

        void SetHasSun(bool value) { HasSun = value; }
        bool GetHasSun() const { return HasSun; }

        void SetSkyIdx(int32_t value) { SkyIdx = value; }
        int32_t GetSkyIdx() const { return SkyIdx; }

        void SetSunRotation(float value) { SunRotation = value; }
        float GetSunRotation() const { return SunRotation; }

        void SetSunElevation(float value) { SunElevation = value; }
        float GetSunElevation() const { return SunElevation; }

        void SetSkyRotation(float value) { SkyRotation = value; }
        float GetSkyRotation() const { return SkyRotation; }

        void SetSkyIntensity(float value) { SkyIntensity = value; }
        float GetSkyIntensity() const { return SkyIntensity; }

        void SetSunIntensity(float value) { SunIntensity = value; }
        float GetSunIntensity() const { return SunIntensity; }
    };
}
