#pragma once

#include "Utilities/Glm.hpp"

namespace Assets
{

	struct alignas(16) Material final
	{
		static Material Lambertian(const glm::vec3& diffuse, const int32_t textureId = -1)
		{
			return Material{ glm::vec4(diffuse, 1), textureId, 1.0f, 1.45f, Enum::Lambertian };
		}

		static Material Metallic(const glm::vec3& diffuse, const float fuzziness, const int32_t textureId = -1)
		{
			return Material{ glm::vec4(diffuse, 1), textureId, fuzziness, 1.45f, Enum::Metallic };
		}

		static Material Dielectric(const float refractionIndex, const float fuzziness, const int32_t textureId = -1)
		{
			return Material{ glm::vec4(0.7f, 0.7f, 1.0f, 1), textureId,  fuzziness, refractionIndex, Enum::Dielectric };
		}

		static Material Isotropic(const glm::vec3& diffuse, const float refractionIndex, const float fuzziness, const int32_t textureId = -1)
		{
			return Material{ glm::vec4(diffuse, 1), textureId, fuzziness, refractionIndex, Enum::Isotropic };
		}

		static Material DiffuseLight(const glm::vec3& diffuse, const int32_t textureId = -1)
		{
			return Material{ glm::vec4(diffuse, 1), textureId, 0.0f, 0.0f, Enum::DiffuseLight };
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

		// Metal fuzziness
		float Fuzziness;

		// Dielectric refraction index
		float RefractionIndex;

		// Which material are we dealing with
		Enum MaterialModel;

		// metalness
		float Metalness;

		float Reserverd0;
		float Reserverd1;
		float Reserverd2;
	};

}