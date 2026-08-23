#pragma once

#include "Engine/Assets/GPU/UniformBuffer.hpp"

#include <array>
#include <cstdint>

namespace Vulkan::PipelineCommon
{
    // C++ mirror of assets/shaders/Shader/SurfaceBuffer.slang. The shader module owns the encoding;
    // this header owns the slot/format table the renderers need for resource transitions, plus the
    // handful of constants both sides must agree on. Keep the two in sync by editing them together:
    // there is no generator, and a silent divergence here shows up as a mis-decoded G-buffer.
    struct FSurfacePlaneDesc
    {
        uint32_t storageSlot;
        VkFormat format;
        const char* semantic;
    };

    // Everything Core.SurfaceBuild writes. RT_MOTIONMOMENT is written indirectly, as the
    // read-modify-write side effect inside Common.CalculateMotionVector; ownership of that side
    // effect moves to Build along with the motion vector itself, so it belongs in this table.
    inline constexpr std::array<FSurfacePlaneDesc, 7> kPrimarySurfacePlanes = {{
        {Assets::Bindless::RT_PREV_DEPTHBUFFER, VK_FORMAT_R32_SFLOAT, "NDC depth"},
        {Assets::Bindless::RT_NORMAL, VK_FORMAT_R16G16B16A16_SFLOAT, "xyz world normal, w roughness"},
        {Assets::Bindless::RT_ALBEDO, VK_FORMAT_R16G16B16A16_SFLOAT, "base color"},
        {Assets::Bindless::RT_OBJECTID_0, VK_FORMAT_R32_UINT, "encoded object id"},
        {Assets::Bindless::RT_MOTIONVECTOR, VK_FORMAT_R32G32_SFLOAT, "render-pixel motion"},
        {Assets::Bindless::RT_BSDF_DATA, VK_FORMAT_R32G32_UINT, "x material id, y f16 metalness | flags"},
        {Assets::Bindless::RT_MOTIONMOMENT, VK_FORMAT_R16_UINT, "motion discontinuity counter"},
    }};

    // Written only when a consumer asks for it (kSurfaceBuildWriteSpecularAlbedo).
    inline constexpr FSurfacePlaneDesc kSurfaceSpecularAlbedoPlane{
        Assets::Bindless::RT_SPECULAR_ALBEDO, VK_FORMAT_R16G16B16A16_SFLOAT, "specular albedo"};

    // Depth written where the primary ray hit nothing; must equal Surface.kBackgroundDepth.
    inline constexpr float kSurfaceBackgroundDepth = -1.0f;

    // RT_BSDF_DATA.x for background / degenerate samples; must equal Surface.kInvalidMaterialId.
    inline constexpr uint32_t kSurfaceInvalidMaterialId = 0xFFFFFFFFu;

    // Feature flags in the high 16 bits of RT_BSDF_DATA.y; must equal the Surface.kFlag* set.
    inline constexpr uint32_t kSurfaceFlagEmissive = 1u << 0u;
    inline constexpr uint32_t kSurfaceFlagMetallic = 1u << 1u;
    inline constexpr uint32_t kSurfaceFlagDielectric = 1u << 2u;
    inline constexpr uint32_t kSurfaceFlagHasMra = 1u << 3u;
    inline constexpr uint32_t kSurfaceFlagHasNormalMap = 1u << 4u;

    // GPUScene.CustomData2 now carries a single pass-local bit, checkerboardShadingFlag
    // (CheckerboardRendering.hpp). Bits 1-3 are retired: they selected the surface path, the tile
    // scheduler and GTAO-in-Core back when a renderer could run without them; all three are now
    // unconditional on every surface renderer.

    // GPUScene.CustomData1 bits for Core.SurfaceBuild; must equal Surface.kBuildWrite*.
    inline constexpr uint32_t kSurfaceBuildWriteSpecularAlbedo = 1u << 0u;
}
