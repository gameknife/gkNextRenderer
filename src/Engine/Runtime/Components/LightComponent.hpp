#pragma once

#include "Engine/Assets/Core/Component.hpp"
#include "Engine/Assets/GPU/UniformBuffer.hpp"
#include "Engine/Runtime/Reflection/ReflectionMacros.hpp"

namespace Runtime
{
    class LightComponent final : public Assets::Component
    {
    public:
        REFLECT_COMPONENT(LightComponent)

        LightComponent() = default;
        explicit LightComponent(const Assets::LightObject& light) { lights_.push_back(light); }

        void AddLight(const Assets::LightObject& light) { lights_.push_back(light); }
        std::vector<Assets::LightObject>& Lights() { return lights_; }
        const std::vector<Assets::LightObject>& Lights() const { return lights_; }

        void SetEnabled(bool enabled) { enabled_ = enabled; }
        bool GetEnabled() const { return enabled_; }
        uint32_t GetLightCount() const { return static_cast<uint32_t>(lights_.size()); }

        void SetLightType(uint32_t value);
        uint32_t GetLightType() const;
        void SetMaterialIndex(uint32_t value);
        uint32_t GetMaterialIndex() const;
        void SetP0(const glm::vec4& value);
        glm::vec4 GetP0() const;
        void SetP1(const glm::vec4& value);
        glm::vec4 GetP1() const;
        void SetP3(const glm::vec4& value);
        glm::vec4 GetP3() const;
        void SetNormalArea(const glm::vec4& value);
        glm::vec4 GetNormalArea() const;

    private:
        Assets::LightObject* PrimaryLight();
        const Assets::LightObject* PrimaryLight() const;

        std::vector<Assets::LightObject> lights_;
        bool enabled_ = true;
    };
}
