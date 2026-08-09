#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "TestCommon.hpp"

#include "Engine/Assets/GPU/Texture.hpp"
#include "Engine/Assets/GPU/UniformBuffer.hpp"
#include "Engine/Rendering/PipelineCommon/CommonComputePipeline.hpp"
#include "Engine/Rendering/VulkanBaseRenderer.hpp"
#include "Engine/Vulkan/CommandExecution.hpp"
#include "Engine/Vulkan/GpuResources.hpp"
#include "Engine/Vulkan/MemoryAndShader.hpp"
#include "Engine/Vulkan/SyncAndTiming.hpp"

#include <array>
#include <cstring>
#include <vector>

// End-to-end check of the 3D bindless path: a volume image is written by one compute pass through
// RWTexture3D, then read back by a second pass through Sampler3D. Exact voxel-centre taps prove the
// write and the addressing; midpoint taps prove trilinear filtering, including along W.
namespace
{
    constexpr uint32_t kVolumeDim = 8;
    constexpr uint32_t kVolumeSlot = static_cast<uint32_t>(Assets::Bindless::RES_VOLUME_TEST);

    // Mirrors the params block of Util.Bindless3DSample.comp.slang (after the 16-byte view header).
    struct FSampleParams
    {
        uint32_t volumeSlot;
        uint32_t sampleCount;
        uint32_t pad0;
        uint32_t pad1;
        VkDeviceAddress coordAddress;
        VkDeviceAddress resultAddress;
    };
    static_assert(sizeof(FSampleParams) == 32, "push-constant layout must match the shader");

    // Mirrors the params block of Util.Bindless3DWrite.comp.slang.
    struct FWriteParams
    {
        uint32_t volumeSlot;
    };

    // The ramp Util.Bindless3DWrite.comp.slang stores into each voxel.
    float ExpectedVoxel(uint32_t x, uint32_t y, uint32_t z)
    {
        return static_cast<float>(x) +
               static_cast<float>(y) * static_cast<float>(kVolumeDim) +
               static_cast<float>(z) * static_cast<float>(kVolumeDim * kVolumeDim);
    }

    // Normalised coordinate of a voxel centre.
    glm::vec3 VoxelCentre(uint32_t x, uint32_t y, uint32_t z)
    {
        return (glm::vec3(x, y, z) + 0.5f) / static_cast<float>(kVolumeDim);
    }

    struct FHostBuffer
    {
        std::unique_ptr<Vulkan::Buffer> buffer;
        std::unique_ptr<Vulkan::DeviceMemory> memory;

        FHostBuffer(const Vulkan::Device& device, size_t size)
        {
            buffer = std::make_unique<Vulkan::Buffer>(
                device, size,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);
            memory = std::make_unique<Vulkan::DeviceMemory>(buffer->AllocateMemory(
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                {.AllocateFlags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT}));
        }

        ~FHostBuffer()
        {
            // The buffer owns the binding, so it must go before the memory backing it.
            buffer.reset();
            memory.reset();
        }

        VkDeviceAddress Address() const { return buffer->GetDeviceAddress(); }
    };
}

TEST_CASE_METHOD(EngineTestFixture, "Bindless 3D storage image round-trips through Sampler3D",
                 "[GPU][Unit][Bindless3D]")
{
    auto& renderer = engine_->GetRenderer();
    const auto& device = renderer.Device();

    // --- Resources -----------------------------------------------------------------------------
    REQUIRE_NOTHROW(renderer.CreateStorageImage3D(
        kVolumeSlot,
        VkExtent3D{kVolumeDim, kVolumeDim, kVolumeDim},
        VK_FORMAT_R32_SFLOAT,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        "Bindless3D Test Volume"));

    const Vulkan::RenderImage* volume = renderer.GetStorageImage3D(kVolumeSlot);
    REQUIRE(volume != nullptr);
    CHECK(volume->GetImage().ImageType() == VK_IMAGE_TYPE_3D);
    CHECK(volume->GetImage().Extent3D().depth == kVolumeDim);
    // Extent() keeps its 2D meaning for every existing caller.
    CHECK(volume->GetImage().Extent().width == kVolumeDim);

    // Taps: one exact voxel centre per axis-aligned corner plus interior samples, then two
    // midpoints that must come back as the arithmetic mean of their two neighbours.
    std::vector<glm::vec4> coords;
    std::vector<float> expected;

    const std::array<std::array<uint32_t, 3>, 5> exactVoxels{{
        {0, 0, 0}, {7, 0, 0}, {0, 7, 0}, {0, 0, 7}, {3, 5, 6},
    }};
    for (const auto& voxel : exactVoxels)
    {
        coords.emplace_back(VoxelCentre(voxel[0], voxel[1], voxel[2]), 0.0f);
        expected.push_back(ExpectedVoxel(voxel[0], voxel[1], voxel[2]));
    }

    // Halfway between (2,3,4) and (3,3,4): exercises U-axis filtering.
    coords.emplace_back(0.5f * (VoxelCentre(2, 3, 4) + VoxelCentre(3, 3, 4)), 0.0f);
    expected.push_back(0.5f * (ExpectedVoxel(2, 3, 4) + ExpectedVoxel(3, 3, 4)));

    // Halfway between (2,3,4) and (2,3,5): exercises W-axis filtering, the axis a 2D atlas
    // fallback would have had to emulate by hand.
    coords.emplace_back(0.5f * (VoxelCentre(2, 3, 4) + VoxelCentre(2, 3, 5)), 0.0f);
    expected.push_back(0.5f * (ExpectedVoxel(2, 3, 4) + ExpectedVoxel(2, 3, 5)));

    const uint32_t tapCount = static_cast<uint32_t>(coords.size());

    FHostBuffer coordBuffer(device, sizeof(glm::vec4) * tapCount);
    FHostBuffer resultBuffer(device, sizeof(float) * tapCount);
    {
        void* mapped = coordBuffer.memory->Map(0, sizeof(glm::vec4) * tapCount);
        std::memcpy(mapped, coords.data(), sizeof(glm::vec4) * tapCount);
        coordBuffer.memory->Unmap();
    }
    {
        void* mapped = resultBuffer.memory->Map(0, sizeof(float) * tapCount);
        std::memset(mapped, 0, sizeof(float) * tapCount);
        resultBuffer.memory->Unmap();
    }

    Vulkan::PipelineCommon::ZeroBindCustomPushConstantPipeline writePipeline(
        renderer.SwapChain(), "assets/shaders/Util.Bindless3DWrite.comp.slang.spv", sizeof(FWriteParams));
    Vulkan::PipelineCommon::ZeroBindCustomPushConstantPipeline samplePipeline(
        renderer.SwapChain(), "assets/shaders/Util.Bindless3DSample.comp.slang.spv", sizeof(FSampleParams));

    // --- Dispatch ------------------------------------------------------------------------------
    const FWriteParams writeParams{kVolumeSlot};
    const FSampleParams sampleParams{
        kVolumeSlot, tapCount, 0, 0, coordBuffer.Address(), resultBuffer.Address()};

    VkImageSubresourceRange wholeImage{};
    wholeImage.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    wholeImage.baseMipLevel = 0;
    wholeImage.levelCount = 1;
    wholeImage.baseArrayLayer = 0;
    wholeImage.layerCount = 1;

    Vulkan::SingleTimeCommands::Submit(renderer.CommandPool(), [&](VkCommandBuffer commandBuffer)
    {
        // UNDEFINED -> GENERAL: the storage descriptor was written with GENERAL.
        Vulkan::ImageMemoryBarrier::Insert(
            commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            volume->GetImage().Handle(), wholeImage, 0, VK_ACCESS_SHADER_WRITE_BIT,
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);

        writePipeline.BindPipeline(commandBuffer, &writeParams);
        vkCmdDispatch(commandBuffer, kVolumeDim / 4, kVolumeDim / 4, kVolumeDim / 4);

        // GENERAL -> SHADER_READ_ONLY_OPTIMAL: matches the layout the sampled descriptor declares.
        Vulkan::ImageMemoryBarrier::Insert(
            commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            volume->GetImage().Handle(), wholeImage, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
            VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        samplePipeline.BindPipeline(commandBuffer, &sampleParams);
        vkCmdDispatch(commandBuffer, 1, 1, 1);

        Vulkan::BufferMemoryBarrier::Insert(
            commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
            resultBuffer.buffer->Handle(), VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_HOST_READ_BIT);
    });

    device.WaitIdle();

    // --- Assertions ----------------------------------------------------------------------------
    std::vector<float> results(tapCount, 0.0f);
    {
        const void* mapped = resultBuffer.memory->Map(0, sizeof(float) * tapCount);
        std::memcpy(results.data(), mapped, sizeof(float) * tapCount);
        resultBuffer.memory->Unmap();
    }

    for (uint32_t tap = 0; tap < tapCount; ++tap)
    {
        INFO("tap " << tap << " uvw=(" << coords[tap].x << ", " << coords[tap].y << ", " << coords[tap].z << ")");
        CHECK(results[tap] == Catch::Approx(expected[tap]).margin(1e-3f));
    }

    renderer.DestroyStorageImage3D(kVolumeSlot);
    CHECK(renderer.GetStorageImage3D(kVolumeSlot) == nullptr);
}
