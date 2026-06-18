#include "Engine/Common/CoreMinimal.hpp"
#include "Modules/SplatLoader/FSplatQuant.hpp"

#include <glm/gtc/quaternion.hpp>

namespace Assets::Sog
{
    float DecodeLogPosition(float encoded)
    {
        return std::copysign(std::exp(std::abs(encoded)) - 1.0f, encoded);
    }

    glm::quat DecodeQuaternion(uint8_t a, uint8_t b, uint8_t c, uint8_t tag)
    {
        if (tag < 252 || tag > 255)
        {
            return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        }

        const uint32_t omitted = tag - 252;
        const float inverseSqrt2 = glm::inversesqrt(2.0f);
        const std::array<float, 3> packed{
            (static_cast<float>(a) / 255.0f * 2.0f - 1.0f) * inverseSqrt2,
            (static_cast<float>(b) / 255.0f * 2.0f - 1.0f) * inverseSqrt2,
            (static_cast<float>(c) / 255.0f * 2.0f - 1.0f) * inverseSqrt2,
        };
        std::array<float, 4> components{}; // w, x, y, z
        uint32_t source = 0;
        for (uint32_t component = 0; component < 4; ++component)
        {
            if (component != omitted)
            {
                components[component] = packed[source++];
            }
        }
        float lengthSquared = 0.0f;
        for (float component : components)
        {
            lengthSquared += component * component;
        }
        components[omitted] = std::sqrt(std::max(0.0f, 1.0f - lengthSquared));
        return glm::normalize(glm::quat(components[0], components[1], components[2], components[3]));
    }

    glm::mat3 BuildCovariance(const glm::quat& rotation, const glm::vec3& logScale)
    {
        const glm::vec3 scale = glm::exp(logScale);
        const glm::mat3 transform = glm::mat3_cast(rotation) * glm::mat3(
            scale.x, 0.0f, 0.0f,
            0.0f, scale.y, 0.0f,
            0.0f, 0.0f, scale.z);
        return transform * glm::transpose(transform);
    }
}
