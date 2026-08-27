#pragma once

namespace Vulkan
{
    class Buffer;
    class CommandBuffers;
    class CommandPool;
    class DepthBuffer;
    class DescriptorSetLayout;
    class DescriptorSetManager;
    class Device;
    class DeviceMemory;
    class DeviceProcedures;
    class FrameBuffer;
    class Image;
    class ImageView;
    class Instance;
    class PipelineLayout;
    class RenderImage;
    class RenderPass;
    class Sampler;
    class SwapChain;
    class TracyGpuProfilerBackend;
    class VulkanBaseRenderer;
    class Window;

    struct WindowConfig;
}

namespace Vulkan::PipelineCommon
{
    class GraphicsPipeline;
    class VisibilityPipeline;
    class ZeroBindCustomPushConstantPipeline;
    class ZeroBindPipeline;
    class ZeroBindWithTLASPipeline;
}

namespace Vulkan::RayTracing
{
    class RayTracingProperties;
}

namespace Vulkan::Shadow
{
    class ShadowMapPass;
}
