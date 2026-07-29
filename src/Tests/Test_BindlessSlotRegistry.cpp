#include <catch2/catch_test_macros.hpp>

#include "TestCommon.hpp"

#include "Engine/Assets/GPU/Texture.hpp"
#include "Engine/Assets/GPU/UniformBuffer.hpp"
#include "Engine/Rendering/RenderView.hpp"
#include "Engine/Rendering/VulkanBaseRenderer.hpp"
#include "Engine/Vulkan/GpuResources.hpp"
#include "Engine/Vulkan/MemoryAndShader.hpp"

#include <algorithm>
#include <vector>

// Guards the bindless slot registry in assets/shaders/common/BindlessTexture.slang.
//
// The registry sizes both descriptor arrays, so an off-by-one in a range base or count either
// silently overwrites another owner's descriptors (the class of bug that put the remote composite
// range on top of the material thumbnails) or writes past the array. The partition itself is
// static_asserted where it is declared; what needs a device to check is that the *allocated* arrays
// really do cover every registered slot -- so this binds a real image at the first and last slot of
// every range and lets the validation layer object if any of them is out of bounds.
namespace
{
    namespace Slots = Assets::Bindless;

    struct FRange
    {
        const char* name;
        uint32_t first;
        uint32_t count;
    };

    std::vector<FRange> RegisteredRanges()
    {
        return {
            {"view sample", static_cast<uint32_t>(Slots::RES_VIEW_SAMPLE_BASE),
             static_cast<uint32_t>(Slots::RES_VIEW_SAMPLE_COUNT)},
            {"remote composite", static_cast<uint32_t>(Slots::RES_REMOTE_COMPOSITE_BASE),
             static_cast<uint32_t>(Slots::RES_REMOTE_COMPOSITE_COUNT)},
            {"remote encode", static_cast<uint32_t>(Slots::RES_REMOTE_ENCODE_BASE),
             static_cast<uint32_t>(Slots::RES_REMOTE_ENCODE_COUNT)},
            {"volume", static_cast<uint32_t>(Slots::RES_VOLUME_BASE),
             static_cast<uint32_t>(Slots::RES_VOLUME_COUNT)},
            {"material preview", static_cast<uint32_t>(Slots::RES_MATERIAL_PREVIEW), 1u},
            {"mesh thumbnail", static_cast<uint32_t>(Slots::RES_MESH_THUMBNAIL_BASE),
             static_cast<uint32_t>(Slots::RES_MESH_THUMBNAIL_COUNT)},
            {"material thumbnail", static_cast<uint32_t>(Slots::RES_MATERIAL_THUMBNAIL_BASE),
             static_cast<uint32_t>(Slots::RES_MATERIAL_THUMBNAIL_COUNT)},
        };
    }
}

TEST_CASE("Bindless slot registry partitions the address space", "[Unit][Bindless]")
{
    std::vector<FRange> ranges = RegisteredRanges();
    std::sort(ranges.begin(), ranges.end(),
              [](const FRange& lhs, const FRange& rhs) { return lhs.first < rhs.first; });

    // Every explicitly-bound range sits above both dynamic regions.
    for (const FRange& range : ranges)
    {
        INFO(range.name);
        CHECK(range.count > 0u);
        CHECK(range.first >= static_cast<uint32_t>(Slots::RES_HIGH_BASE));
        CHECK(range.first + range.count <= static_cast<uint32_t>(Slots::RES_SLOT_COUNT));
    }

    // ...and no two of them touch.
    for (size_t i = 1; i < ranges.size(); ++i)
    {
        INFO(ranges[i - 1].name << " -> " << ranges[i].name);
        CHECK(ranges[i - 1].first + ranges[i - 1].count <= ranges[i].first);
    }

    // The dynamic regions must fit under the explicit region in both arrays.
    CHECK(Assets::GlobalTexturePool::kMaxSceneTextures <= static_cast<uint32_t>(Slots::RES_HIGH_BASE));
    CHECK(Vulkan::FBankAllocator::kMaxConcurrentBanks * static_cast<uint32_t>(Slots::kViewRtBankStride) <=
          static_cast<uint32_t>(Slots::RES_HIGH_BASE));
    CHECK(Assets::GlobalTexturePool::kMaxBindlessSlots == static_cast<uint32_t>(Slots::RES_SLOT_COUNT));
}

TEST_CASE_METHOD(EngineTestFixture, "Bindless descriptor arrays cover every registered slot",
                 "[Unit][Bindless]")
{
    auto* texturePool = Assets::GlobalTexturePool::GetInstance();
    REQUIRE(texturePool != nullptr);

    auto& renderer = engine_->GetRenderer();

    // One tiny image is enough: this checks descriptor addressing, not image contents.
    Vulkan::RenderImage probe(
        renderer.Device(), VkExtent2D{1, 1}, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, false, "Bindless slot probe");

    for (const FRange& range : RegisteredRanges())
    {
        const uint32_t lastSlot = range.first + range.count - 1u;
        INFO(range.name << " [" << range.first << ".." << lastSlot << "]");

        for (const uint32_t slot : {range.first, lastSlot})
        {
            REQUIRE(slot < Assets::GlobalTexturePool::kMaxBindlessSlots);
            REQUIRE_NOTHROW(texturePool->BindSampleTexture(slot, probe.GetImageView(), probe.Sampler()));
            REQUIRE_NOTHROW(texturePool->BindStorageTexture(slot, probe.GetImageView()));
        }
    }

    // Descriptor writes are recorded lazily; make sure they land before the probe image dies.
    renderer.Device().WaitIdle();
}
