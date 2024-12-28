#pragma once

#include "Common/CoreMinimal.hpp"
#include "Utilities/Glm.hpp"

namespace Assets
{
    struct alignas(16) Material final
    {
        static Material Lambertian(const glm::vec3& diffuse, const int32_t textureId = -1)
        {
            return Material{glm::vec4(diffuse, 1), textureId, -1, -1, 1.0f, 1.45f, Enum::Lambertian, 0};
        }

        static Material Metallic(const glm::vec3& diffuse, const float fuzziness, const int32_t textureId = -1)
        {
            return Material{glm::vec4(diffuse, 1), textureId, -1, -1, fuzziness, 1.45f, Enum::Metallic, 1};
        }

        static Material Dielectric(const float refractionIndex, const float fuzziness, const int32_t textureId = -1)
        {
            return Material{glm::vec4(1.0f, 1.0f, 1.0f, 1), textureId, -1, -1, fuzziness, refractionIndex, Enum::Dielectric, 0, refractionIndex};
        }

        static Material Isotropic(const glm::vec3& diffuse, const float refractionIndex, const float fuzziness, const int32_t textureId = -1)
        {
            return Material{glm::vec4(diffuse, 1), textureId, -1, -1, fuzziness, refractionIndex, Enum::Isotropic};
        }

        static Material DiffuseLight(const glm::vec3& diffuse, const int32_t textureId = -1)
        {
            return Material{glm::vec4(diffuse, 1), textureId, -1, -1, 0.0f, 0.0f, Enum::DiffuseLight};
        }

        enum class Enum : uint32_t
        {
            Lambertian = 0,
            Metallic = 1,
            Dielectric = 2,
            Isotropic = 3,
            DiffuseLight = 4,
            Mixture = 5,
        };

        // Note: vec3 and vec4 gets aligned on 16 bytes in Vulkan shaders. 

        // Base material
        glm::vec4 Diffuse;
        int32_t DiffuseTextureId;
        int32_t MRATextureId;
        int32_t NormalTextureId;

        // Metal fuzziness
        float Fuzziness;

        // Dielectric refraction index
        float RefractionIndex;

        // Which material are we dealing with
        Enum MaterialModel;

        // metalness
        float Metalness;

        // Second IOR for calculating refraction
        float RefractionIndex2;
        float NormalTextureScale;
        float Reserverd2;
    };

    struct FMaterial final
    {
        std::string name_;
        uint32_t globalId_;
        Material gpuMaterial_;
    };
}
