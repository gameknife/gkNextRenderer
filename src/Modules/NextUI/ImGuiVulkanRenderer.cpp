#include "Engine/Common/CoreMinimal.hpp"

#include "Modules/NextUI/ImGuiVulkanRenderer.hpp"

#include "Engine/Vulkan/Device.hpp"
#include "Engine/Vulkan/GraphicsPipelineBuilder.hpp"
#include "Engine/Vulkan/MemoryAndShader.hpp"

#include <array>
#include <imgui.h>

namespace NextUI
{
    namespace
    {
        constexpr const char* vertexShaderPath = "assets/shaders/UI.ImGui.vert.slang.spv";
        constexpr const char* fragmentShaderPath = "assets/shaders/UI.ImGui.frag.slang.spv";

        struct FUiBatchedVertex
        {
            ImVec2 position;
            ImVec2 uv;
            ImU32 color = 0;
            float clipRect[4]{};
            uint32_t textureIndex = 0;
            uint32_t textureFlags = 0;
        };
    }

    VkPipeline FImGuiVulkanRenderer::CreateGraphicsPipeline(
        const Vulkan::Device& device, const VkPipelineLayout pipelineLayout, const VkRenderPass renderPass) const
    {
        const Vulkan::ShaderModule vertexShader(device, vertexShaderPath);
        const Vulkan::ShaderModule fragmentShader(device, fragmentShaderPath);
        VkVertexInputBindingDescription binding{};
        binding.binding = 0;
        binding.stride = sizeof(FUiBatchedVertex);
        binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        std::array<VkVertexInputAttributeDescription, 6> attributes{};
        attributes[0] = {0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(FUiBatchedVertex, position)};
        attributes[1] = {1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(FUiBatchedVertex, uv)};
        attributes[2] = {2, 0, VK_FORMAT_R8G8B8A8_UNORM, offsetof(FUiBatchedVertex, color)};
        attributes[3] = {3, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(FUiBatchedVertex, clipRect)};
        attributes[4] = {4, 0, VK_FORMAT_R32_UINT, offsetof(FUiBatchedVertex, textureIndex)};
        attributes[5] = {5, 0, VK_FORMAT_R32_UINT, offsetof(FUiBatchedVertex, textureFlags)};

        return Vulkan::GraphicsPipelineBuilder(device)
            .SetShaders(vertexShader, fragmentShader)
            .SetVertexInput(binding, attributes.data(), static_cast<uint32_t>(attributes.size()))
            .SetDynamicViewportAndScissor()
            .SetAlphaBlend(VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA)
            .Build(pipelineLayout, renderPass, "create ui pipeline");
    }
}
