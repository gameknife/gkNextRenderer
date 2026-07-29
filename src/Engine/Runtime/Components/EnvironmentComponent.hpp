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

        void SetSkyColor(const glm::vec3& value) { SkyColor = value; }
        const glm::vec3& GetSkyColor() const { return SkyColor; }

        void SetSunIntensity(float value) { SunIntensity = value; }
        float GetSunIntensity() const { return SunIntensity; }

        void SetSunColor(const glm::vec3& value) { SunColor = value; }
        const glm::vec3& GetSunColor() const { return SunColor; }

        void SetRayleighScattering(const glm::vec3& value) { Atmosphere.RayleighScattering = value; }
        const glm::vec3& GetRayleighScattering() const { return Atmosphere.RayleighScattering; }
        void SetRayleighDensityH(float value) { Atmosphere.RayleighDensityH = value; }
        float GetRayleighDensityH() const { return Atmosphere.RayleighDensityH; }
        void SetMieScattering(const glm::vec3& value) { Atmosphere.MieScattering = value; }
        const glm::vec3& GetMieScattering() const { return Atmosphere.MieScattering; }
        void SetMieDensityH(float value) { Atmosphere.MieDensityH = value; }
        float GetMieDensityH() const { return Atmosphere.MieDensityH; }
        void SetMieAbsorption(const glm::vec3& value) { Atmosphere.MieAbsorption = value; }
        const glm::vec3& GetMieAbsorption() const { return Atmosphere.MieAbsorption; }
        void SetMiePhaseG(float value) { Atmosphere.MiePhaseG = value; }
        float GetMiePhaseG() const { return Atmosphere.MiePhaseG; }
        void SetOzoneAbsorption(const glm::vec3& value) { Atmosphere.OzoneAbsorption = value; }
        const glm::vec3& GetOzoneAbsorption() const { return Atmosphere.OzoneAbsorption; }
        void SetOzoneCenterAltitude(float value) { Atmosphere.OzoneCenterAltitude = value; }
        float GetOzoneCenterAltitude() const { return Atmosphere.OzoneCenterAltitude; }
        void SetGroundAlbedo(const glm::vec3& value) { Atmosphere.GroundAlbedo = value; }
        const glm::vec3& GetGroundAlbedo() const { return Atmosphere.GroundAlbedo; }
        void SetOzoneWidth(float value) { Atmosphere.OzoneWidth = value; }
        float GetOzoneWidth() const { return Atmosphere.OzoneWidth; }
        void SetBottomRadius(float value) { Atmosphere.BottomRadius = value; }
        float GetBottomRadius() const { return Atmosphere.BottomRadius; }
        void SetTopRadius(float value) { Atmosphere.TopRadius = value; }
        float GetTopRadius() const { return Atmosphere.TopRadius; }
        void SetWorldUnitsPerKm(float value) { Atmosphere.WorldUnitsPerKm = value; }
        float GetWorldUnitsPerKm() const { return Atmosphere.WorldUnitsPerKm; }
        void SetWorldOriginAltitude(float value) { Atmosphere.WorldOriginAltitude = value; }
        float GetWorldOriginAltitude() const { return Atmosphere.WorldOriginAltitude; }
        void SetAerialPerspectiveMaxDistance(float value) { Atmosphere.AerialPerspectiveMaxDistance = value; }
        float GetAerialPerspectiveMaxDistance() const { return Atmosphere.AerialPerspectiveMaxDistance; }
        void SetSkyLuminanceScale(float value) { Atmosphere.SkyLuminanceScale = value; }
        float GetSkyLuminanceScale() const { return Atmosphere.SkyLuminanceScale; }
        void SetFogInscatteringColor(const glm::vec3& value) { Atmosphere.FogInscatteringColor = value; }
        const glm::vec3& GetFogInscatteringColor() const { return Atmosphere.FogInscatteringColor; }
        void SetFogDensity(float value) { Atmosphere.FogDensity = value; }
        float GetFogDensity() const { return Atmosphere.FogDensity; }
        void SetFogHeightFalloff(float value) { Atmosphere.FogHeightFalloff = value; }
        float GetFogHeightFalloff() const { return Atmosphere.FogHeightFalloff; }
        void SetFogBaseHeight(float value) { Atmosphere.FogBaseHeight = value; }
        float GetFogBaseHeight() const { return Atmosphere.FogBaseHeight; }
        void SetFogStartDistance(float value) { Atmosphere.FogStartDistance = value; }
        float GetFogStartDistance() const { return Atmosphere.FogStartDistance; }
        void SetFogMaxOpacity(float value) { Atmosphere.FogMaxOpacity = value; }
        float GetFogMaxOpacity() const { return Atmosphere.FogMaxOpacity; }
    };
}
