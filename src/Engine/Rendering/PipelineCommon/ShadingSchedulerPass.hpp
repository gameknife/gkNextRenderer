#pragma once

#include "Engine/Vulkan/VulkanFwd.hpp"

#include <cstdint>
#include <memory>
#include <vector>

#include <vulkan/vulkan.h>

namespace Assets
{
    class Scene;
    struct GPUScene;
}

namespace Vulkan
{
    class SwapChain;
    class VulkanBaseRenderer;
}

namespace Vulkan::PipelineCommon
{
    class ZeroBindPipeline;

    // Shading Scheduler: tile classification + per-bucket indirect dispatch.
    //
    // Mirrors assets/shaders/Shader/ShadingScheduler.slang. Classification runs once per frame over
    // the Primary Surface and fills, per bucket, a VkDispatchIndirectCommand and a list of 8x8 tiles
    // with a 64-bit per-pixel membership mask. Each bucket is then dispatched indirectly, so a
    // shading kernel only ever sees the pixels it is meant to shade and carries no branches for the
    // other buckets.
    //
    // Tile granularity is deliberate: per-pixel append would cost one atomic per pixel and destroy
    // the spatial locality the shading kernels depend on.
    class ShadingSchedulerPass final
    {
    public:
        // Must match Scheduler.kBucket* in the shader module.
        enum class EBucket : uint32_t
        {
            Background = 0,
            Emissive = 1,
            Standard = 2,
        };
        static constexpr uint32_t kBucketCount = 3;
        static constexpr uint32_t kTileSize = 8;

        ShadingSchedulerPass();
        ~ShadingSchedulerPass();

        void CreateSwapChain(const SwapChain& swapChain, const Assets::Scene& scene);
        void DeleteSwapChain();

        // Resets the per-bucket counters and records the classification dispatch. gpuScene supplies
        // the view bank base and the rate flags; the scheduler buffer address and tile capacity are
        // injected into the copy this pass pushes.
        void Classify(VulkanBaseRenderer& baseRender,
                      VkCommandBuffer commandBuffer,
                      const Assets::GPUScene& gpuScene);

        // Dispatches one bucket indirectly. gpuScene must be the same one Classify received.
        void DispatchBucket(VkCommandBuffer commandBuffer,
                            ZeroBindPipeline& pipeline,
                            const Assets::GPUScene& gpuScene,
                            EBucket bucket) const;

        // Stamps the scheduler buffer address and tile capacity into a GPUScene copy, so the bucket
        // kernels can find their tile lists.
        void ApplyToScene(Assets::GPUScene& gpuScene) const;

        // The tile buffer is sized lazily inside Classify, so readiness is the pipeline alone;
        // asking for the buffer here would make the pass permanently disable itself.
        bool IsValid() const { return classifyPipeline_ != nullptr; }

    private:
        // Declaration order makes Buffer die before its bound DeviceMemory.
        struct FTileBuffer
        {
            std::unique_ptr<Vulkan::DeviceMemory> memory;
            std::unique_ptr<Vulkan::Buffer> buffer;
        };

        void EnsureResources(VulkanBaseRenderer& baseRender, VkExtent2D extent);

        std::unique_ptr<ZeroBindPipeline> classifyPipeline_;
        std::unique_ptr<ZeroBindPipeline> finalizePipeline_;
        std::unique_ptr<Vulkan::DeviceMemory> memory_;
        std::unique_ptr<Vulkan::Buffer> buffer_;
        // Buffers a grow kept alive until DeleteSwapChain, because commands recorded earlier in the
        // frame may still reference them.
        std::vector<FTileBuffer> retired_;
        // Tile capacity of buffer_, i.e. the largest extent this pass has been asked for.
        uint32_t maxTiles_ = 0;
    };
}
