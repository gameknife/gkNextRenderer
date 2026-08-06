#pragma once
#include "Engine/Vulkan/VulkanFwd.hpp"
#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Utilities/Glm.hpp"
#include <algorithm>
#include <cmath>

namespace Assets
{
    using namespace glm;


    const int ACGI_PAGE_COUNT = 64; // 64x64
    const float ACGI_PAGE_SIZE = 16; // 16m
    const vec3 ACGI_PAGE_OFFSET = vec3( -512, 0, -512);
    const int SHADOWMAP_SIZE = 4096;
    const int CUBE_SIZE_XY = 192;//256;
    const int CUBE_SIZE_Z = 48;
    const int CUBE_CASCADE_MAX = 4;
    const float CUBE_UNIT = 0.25f;
    const vec3 CUBE_OFFSET = vec3(-CUBE_SIZE_XY / 2, -1.375f, -CUBE_SIZE_XY / 2) * CUBE_UNIT;

    inline float SanitizeAmbientCubeUnit(float unit)
    {
        return std::max(unit, 0.001f);
    }

    inline vec3 CalculateAmbientCubeOffset(float unit)
    {
        return vec3(-CUBE_SIZE_XY / 2.0f, -1.375f, -CUBE_SIZE_XY / 2.0f) * unit;
    }

    inline vec3 CalculateAmbientCubeOffset(float unit, vec3 offsetBias)
    {
        return CalculateAmbientCubeOffset(unit) + offsetBias;
    }

    inline uint32_t SanitizeAmbientCubeCascadeCount(int count)
    {
        return static_cast<uint32_t>(std::clamp(count, 1, CUBE_CASCADE_MAX));
    }

    inline float SanitizeAmbientCubeCascadeRatio(float ratio)
    {
        return std::max(ratio, 1.0f);
    }

    inline float CalculateAmbientCubeCascadeUnit(float baseUnit, float ratio, uint32_t cascadeIndex)
    {
        return baseUnit * std::pow(ratio, static_cast<float>(cascadeIndex));
    }

#define float3 vec3
#define float4 vec4
#define float4x4 mat4
#define uint8_t4_packed uint
#define half4 glm::detail::hdata
#define public
#define bool uint32_t

    #include "../assets/shaders/common/Atmosphere.slang"
    #include "../assets/shaders/common/BasicTypes.slang"
    #include "../assets/shaders/common/BindlessTexture.slang"

#undef public
#undef bool
#undef half4
#undef float3
#undef float4
#undef float4x4
#undef uint8_t4_packed

    // The light grid only earns its build cost once the scene holds more lights than a single cell
    // can, because below that every query returns the whole light set anyway and the global
    // fallback is exactly equivalent and free. Returning 0 disables the grid.
    //
    // The UBO fill and the build dispatch must both resolve the count through here: if they
    // disagree, shading samples a grid nobody rebuilt that frame.
    inline uint32_t ResolveLightGridCascadeCount(int configuredCascades, uint32_t lightCount)
    {
        const int clamped = std::clamp(configuredCascades, 0, GPU_SCENE_LIGHT_GRID_CASCADE_MAX);
        if (clamped <= 0 || lightCount <= static_cast<uint32_t>(GPU_SCENE_LIGHT_GRID_CAPACITY))
        {
            return 0;
        }
        return static_cast<uint32_t>(clamped);
    }

    class UniformBuffer
    {
    public:

        UniformBuffer(const UniformBuffer&) = delete;
        UniformBuffer& operator = (const UniformBuffer&) = delete;
        UniformBuffer& operator = (UniformBuffer&&) = delete;

        explicit UniformBuffer(const Vulkan::Device& device);
        UniformBuffer(UniformBuffer&& other) noexcept;
        ~UniformBuffer();

        const Vulkan::Buffer& Buffer() const { return *buffer_; }

        void SetValue(const UniformBufferObject& ubo);

    private:

        std::unique_ptr<Vulkan::Buffer> buffer_;
        std::unique_ptr<Vulkan::DeviceMemory> memory_;
    };

    struct RayCastResult
    {
        vec4 HitPoint;
        vec4 Normal;
        float T;
        uint32_t InstanceId;
        uint32_t MaterialId;
        uint32_t Hit;
    };

}
