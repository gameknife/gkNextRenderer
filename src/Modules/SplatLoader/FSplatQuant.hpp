#pragma once

#include "Engine/Common/CoreMinimal.hpp"

#include <glm/mat3x3.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>

namespace Assets::Sog
{
    float DecodeLogPosition(float encoded);
    glm::quat DecodeQuaternion(uint8_t a, uint8_t b, uint8_t c, uint8_t tag);
    glm::mat3 BuildCovariance(const glm::quat& rotation, const glm::vec3& logScale);
}
