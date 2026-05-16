#include "Runtime/Editor/UserInterface.hpp"

#include "Runtime/Engine.hpp"
#include "Runtime/Scene/SceneList.hpp"
#include "Runtime/Config/UserSettings.hpp"
#include "Runtime/Config/CVarSystem.hpp"
#include "Runtime/Editor/ConsoleLogBuffer.hpp"
#include "Runtime/Editor/FontLoader.h"
#include "Runtime/Editor/ProfessionalUI.hpp"
#include "Utilities/Exception.hpp"
#include "Vulkan/DescriptorSystem.hpp"
#include "Vulkan/Device.hpp"
#include "Vulkan/MemoryAndShader.hpp"
#include "Vulkan/Instance.hpp"
#include "Vulkan/RenderingPipeline.hpp"
#include "Vulkan/CommandExecution.hpp"
#include "Vulkan/SwapChain.hpp"
#include "Vulkan/WindowSurface.hpp"

#include <imgui.h>
#include <imgui_freetype.h>
#include <imgui_stdlib.h>
#include <SDL3/SDL.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>


#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fmt/chrono.h>
#include <fmt/format.h>
#include <spdlog/spdlog.h>

#include "Assets/GPU/TextureImage.hpp"
#include "Assets/GPU/Texture.hpp"
#include "Options.hpp"
#include "Rendering/VulkanBaseRenderer.hpp"
#include "Runtime/Subsystems/TaskCoordinator.hpp"
#include "ThirdParty/fontawesome/IconsFontAwesome6.h"
#include "Utilities/FileHelper.hpp"
#include "Utilities/ImGui.hpp"
#include "Utilities/Math.hpp"
#include "Utilities/StbImage.hpp"
#include "Vulkan/GpuResources.hpp"

extern float GAndroidMagicScale;
extern std::unique_ptr<Vulkan::VulkanBaseRenderer> GApplication;

namespace
{
    constexpr const char* kUiVertexShaderPath = "assets/shaders/UI.ImGui.vert.slang.spv";
    constexpr const char* kUiFragmentShaderPath = "assets/shaders/UI.ImGui.frag.slang.spv";
    constexpr float kUiHdrReferenceWhiteNit = 203.0f;

    struct UiPushConstants
    {
        float scale[2];
        float translate[2];
        float rotation[4];
        uint32_t hdrOutput;
        float hdrReferenceWhiteNit;
        float padding[2];
    };

    std::string ExtractConsolePrefix(const std::string& input)
    {
        size_t start = input.find_first_not_of(" \t\r\n");
        if (start == std::string::npos)
        {
            return {};
        }
        size_t end = input.find_first_of(" =\t\r\n", start);
        if (end == std::string::npos)
        {
            return input.substr(start);
        }
        return input.substr(start, end - start);
    }

    ImVec2 TransformUiPointToFramebuffer(const ImVec2 point, const UiPushConstants& pushConsts, const VkExtent2D& extent)
    {
        const float x = point.x * pushConsts.scale[0] + pushConsts.translate[0];
        const float y = point.y * pushConsts.scale[1] + pushConsts.translate[1];
        const float rx = x * pushConsts.rotation[0] + y * pushConsts.rotation[1];
        const float ry = x * pushConsts.rotation[2] + y * pushConsts.rotation[3];

        return ImVec2((rx * 0.5f + 0.5f) * static_cast<float>(extent.width),
                      (ry * 0.5f + 0.5f) * static_cast<float>(extent.height));
    }
} // namespace

UserInterface::UserInterface(NextEngine* engine, Vulkan::CommandPool& commandPool, const Vulkan::SwapChain& swapChain,
                             const Vulkan::DepthBuffer& depthBuffer, UserSettings& userSettings,
                             std::function<void()> funcPreConfig, std::function<void()> funcInit) :
    userSettings_(userSettings), engine_(engine)
{
    const auto& device = swapChain.Device();
    const auto& window = device.Surface().Instance().Window();

    // Initialise descriptor pool and render pass for ImGui.
    const std::vector<Vulkan::DescriptorBinding> descriptorBindings = {
        {0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 0},
    };
    descriptorPool_.reset(new Vulkan::DescriptorPool(device, descriptorBindings, swapChain.MinImageCount() + 2048));
    renderPass_.reset(new Vulkan::RenderPass(swapChain, depthBuffer, VK_ATTACHMENT_LOAD_OP_LOAD));
    renderPass_->SetDebugName("ImGui Render Pass");
    CreateUiPipeline(swapChain);

    // Initialise ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    auto& io = ImGui::GetIO();
    // No ini file.
    io.IniFilename = "imgui.ini";
    io.WantCaptureMouse = false;
    io.WantCaptureKeyboard = false;

    funcPreConfig();

    // Initialise ImGui GLFW adapter
    if (!ImGui_ImplSDL3_InitForVulkan(window.Handle()))
    {
        Throw(std::runtime_error("failed to initialise ImGui GLFW adapter"));
    }

    // Initialise ImGui Vulkan adapter
    ImGui_ImplVulkan_InitInfo vulkanInit = {};
    vulkanInit.Instance = device.Surface().Instance().Handle();
    vulkanInit.PhysicalDevice = device.PhysicalDevice();
    vulkanInit.Device = device.Handle();
    vulkanInit.QueueFamily = device.GraphicsFamilyIndex();
    vulkanInit.Queue = device.GraphicsQueue();
    vulkanInit.PipelineCache = nullptr;
    vulkanInit.DescriptorPool = descriptorPool_->Handle();
    vulkanInit.MinImageCount = swapChain.MinImageCount();
    vulkanInit.ImageCount = static_cast<uint32_t>(swapChain.Images().size());
    vulkanInit.Allocator = nullptr;
    vulkanInit.RenderPass = renderPass_->Handle();

    if (!ImGui_ImplVulkan_Init(&vulkanInit))
    {
        Throw(std::runtime_error("failed to initialise ImGui vulkan adapter"));
    }

    // Window scaling and style.
#if ANDROID
    const float scaleFactor = 0.75f / static_cast<float>(GAndroidMagicScale);
#else
    const float scaleFactor = 1.0f;
#endif
    constexpr float fontSize = 16.0f;

    UserInterface::SetStyle();
    ImGui::GetStyle().ScaleAllSizes(scaleFactor);

    // Upload ImGui fonts (use ImGuiFreeType for better font rendering, see
    // https://github.com/ocornut/imgui/tree/master/misc/freetype).
    io.Fonts->FontBuilderIO = ImGuiFreeType::GetBuilderForFreeType();
    io.Fonts->FontBuilderFlags = ImGuiFreeTypeBuilderFlags_NoHinting;
    // const ImWchar* glyphRange = GOption->locale == "RU" ? io.Fonts->GetGlyphRangesCyrillic()
    //     : GOption->locale == "zhCN"                     ? io.Fonts->GetGlyphRangesChineseFull()
    //                                                     : io.Fonts->GetGlyphRangesDefault();

    if (!FontLoader::Load(FontLoader::FFontRequest{
            .filePath = "assets/fonts/Roboto-Regular.ttf",
            .pixelSize = fontSize * scaleFactor,
            .includeChineseFull = true,
        }))
    {
        Throw(std::runtime_error("failed to load basic ImGui Text font"));
    }

    static const ImWchar iconRange[] = {
        ICON_MIN_FA,
        ICON_MAX_FA, // Basic Latin + Latin Supplement
        0,
    };
    ImFontConfig config;
    config.MergeMode = true;
    config.GlyphMinAdvanceX = fontSize;
    config.GlyphOffset = ImVec2(0, 0);

    FontLoader::Load(FontLoader::FFontRequest{
        .filePath = "assets/fonts/fa-regular-400.ttf",
        .pixelSize = fontSize * scaleFactor,
        .includeChineseFull = false,
        .glyphRanges = iconRange,
        .fontConfig = &config,
        .warnOnFailure = false,
    });
    FontLoader::Load(FontLoader::FFontRequest{
        .filePath = "assets/fonts/fa-solid-900.ttf",
        .pixelSize = fontSize * scaleFactor,
        .includeChineseFull = false,
        .glyphRanges = iconRange,
        .fontConfig = &config,
        .warnOnFailure = false,
    });
    FontLoader::Load(FontLoader::FFontRequest{
        .filePath = "assets/fonts/fa-brands-400.ttf",
        .pixelSize = fontSize * scaleFactor,
        .includeChineseFull = false,
        .glyphRanges = iconRange,
        .fontConfig = &config,
        .warnOnFailure = false,
    });

    ImFontConfig configLocale;
    configLocale.MergeMode = true;
    FontLoader::Load(FontLoader::FFontRequest{
        .filePath = "assets/fonts/DroidSansFallback.ttf",
        .pixelSize = (fontSize + 2.0f) * scaleFactor,
        .includeChineseFull = true,
        .fontConfig = &configLocale,
        .warnOnFailure = false,
    });

    if (funcInit != nullptr)
    {
        funcInit();
    }

    Vulkan::SingleTimeCommands::Submit(commandPool,
                                       [](VkCommandBuffer commandBuffer)
                                       {
                                           if (!ImGui_ImplVulkan_CreateFontsTexture())
                                           {
                                               Throw(std::runtime_error("failed to create ImGui font textures"));
                                           }
                                       });
}

UserInterface::~UserInterface()
{
    DestroyUiPipeline();
    uiFrameBuffers_.clear();
    uiRenderBuffers_.clear();

    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
}

void UserInterface::OnCreateSurface(const Vulkan::SwapChain& swapChain, const Vulkan::DepthBuffer& depthBuffer)
{
    renderPass_.reset(new Vulkan::RenderPass(swapChain, depthBuffer, VK_ATTACHMENT_LOAD_OP_LOAD));
    renderPass_->SetDebugName("ImGui Render Pass");
    CreateUiPipeline(swapChain);

    for (const auto& imageView : swapChain.ImageViews())
    {
        uiFrameBuffers_.emplace_back(swapChain.Extent(), *imageView, *renderPass_, false);
    }
    uiRenderBuffers_.resize(swapChain.Images().size());
}

void UserInterface::OnDestroySurface()
{
    DestroyUiPipeline();
    renderPass_.reset();
    uiFrameBuffers_.clear();
    uiRenderBuffers_.clear();
    for (auto image : imTextureIdMap_)
    {
        ImGui_ImplVulkan_RemoveTexture(image.second);
    }
    imTextureIdMap_.clear();
}

VkDescriptorSet UserInterface::RequestImTextureId(uint32_t globalTextureId)
{
    auto texture = Assets::GlobalTexturePool::GetTextureImage(globalTextureId);
    if (texture == nullptr)
        return VK_NULL_HANDLE;

    if (imTextureIdMap_.find(globalTextureId) == imTextureIdMap_.end())
    {
        imTextureIdMap_[globalTextureId] = ImGui_ImplVulkan_AddTexture(
            texture->Sampler().Handle(), texture->ImageView().Handle(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }

    return imTextureIdMap_[globalTextureId];
}

VkDescriptorSet UserInterface::RequestImTextureByName(const std::string& name)
{
    uint32_t id = Assets::GlobalTexturePool::GetTextureIndexByName(name);
    if (id == -1)
    {
        return VK_NULL_HANDLE;
    }
    return RequestImTextureId(id);
}

UserInterface::FUiTextureHandle UserInterface::RequestUiTexture(const std::string& path, bool srgb)
{
    FUiTextureHandle handle{};
    if (path.empty() || !Utilities::FileHelper::IsAssetAvailable(path))
    {
        return handle;
    }

    if (uiTextureLoadRequests_.insert(path).second)
    {
        Assets::GlobalTexturePool::LoadTexture(path, srgb);
    }

    const VkDescriptorSet descriptor = RequestImTextureByName(path);
    handle.textureId = descriptor != VK_NULL_HANDLE ? reinterpret_cast<ImTextureID>(descriptor) : static_cast<ImTextureID>(0);
    handle.valid = handle.textureId != static_cast<ImTextureID>(0);

    if (const auto sizeIt = uiTexturePixelSizeCache_.find(path); sizeIt != uiTexturePixelSizeCache_.end())
    {
        handle.pixelSize = sizeIt->second;
        return handle;
    }

    int width = 0;
    int height = 0;
    int comp = 0;
    const std::string platformPath = Utilities::FileHelper::GetPlatformFilePath(path.c_str());
    if (stbi_info(platformPath.c_str(), &width, &height, &comp) != 0 && width > 0 && height > 0)
    {
        handle.pixelSize = ImVec2(static_cast<float>(width), static_cast<float>(height));
    }
    uiTexturePixelSizeCache_[path] = handle.pixelSize;
    return handle;
}

void UserInterface::CreateUiPipeline(const Vulkan::SwapChain& swapChain)
{
    DestroyUiPipeline();
    if (renderPass_ == nullptr)
    {
        return;
    }

    const auto& device = swapChain.Device();

    VkDescriptorSetLayoutBinding samplerBinding{};
    samplerBinding.binding = 0;
    samplerBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplerBinding.descriptorCount = 1;
    samplerBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo descriptorSetLayoutInfo{};
    descriptorSetLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    descriptorSetLayoutInfo.bindingCount = 1;
    descriptorSetLayoutInfo.pBindings = &samplerBinding;

    Vulkan::Check(vkCreateDescriptorSetLayout(device.Handle(), &descriptorSetLayoutInfo, nullptr, &uiDescriptorSetLayout_),
                  "create ui descriptor set layout");

    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(UiPushConstants);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &uiDescriptorSetLayout_;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    Vulkan::Check(vkCreatePipelineLayout(device.Handle(), &pipelineLayoutInfo, nullptr, &uiPipelineLayout_),
                  "create ui pipeline layout");

    const Vulkan::ShaderModule vertShader(device, kUiVertexShaderPath);
    const Vulkan::ShaderModule fragShader(device, kUiFragmentShaderPath);

    VkVertexInputBindingDescription vertexBinding{};
    vertexBinding.binding = 0;
    vertexBinding.stride = sizeof(ImDrawVert);
    vertexBinding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    std::array<VkVertexInputAttributeDescription, 3> vertexAttributes{};
    vertexAttributes[0].location = 0;
    vertexAttributes[0].binding = 0;
    vertexAttributes[0].format = VK_FORMAT_R32G32_SFLOAT;
    vertexAttributes[0].offset = IM_OFFSETOF(ImDrawVert, pos);
    vertexAttributes[1].location = 1;
    vertexAttributes[1].binding = 0;
    vertexAttributes[1].format = VK_FORMAT_R32G32_SFLOAT;
    vertexAttributes[1].offset = IM_OFFSETOF(ImDrawVert, uv);
    vertexAttributes[2].location = 2;
    vertexAttributes[2].binding = 0;
    vertexAttributes[2].format = VK_FORMAT_R8G8B8A8_UNORM;
    vertexAttributes[2].offset = IM_OFFSETOF(ImDrawVert, col);

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &vertexBinding;
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexAttributes.size());
    vertexInputInfo.pVertexAttributeDescriptions = vertexAttributes.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.blendEnable = VK_TRUE;
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;

    VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(std::size(dynamicStates));
    dynamicState.pDynamicStates = dynamicStates;

    VkPipelineShaderStageCreateInfo shaderStages[] = {
        vertShader.CreateShaderStage(VK_SHADER_STAGE_VERTEX_BIT),
        fragShader.CreateShaderStage(VK_SHADER_STAGE_FRAGMENT_BIT)};

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = static_cast<uint32_t>(std::size(shaderStages));
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = uiPipelineLayout_;
    pipelineInfo.renderPass = renderPass_->Handle();
    pipelineInfo.subpass = 0;

    Vulkan::Check(vkCreateGraphicsPipelines(device.Handle(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &uiPipeline_),
                  "create ui pipeline");
}

void UserInterface::DestroyUiPipeline()
{
    if (engine_ == nullptr)
    {
        return;
    }

    const auto& device = engine_->GetRenderer().Device();
    if (uiPipeline_ != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(device.Handle(), uiPipeline_, nullptr);
        uiPipeline_ = VK_NULL_HANDLE;
    }
    if (uiPipelineLayout_ != VK_NULL_HANDLE)
    {
        vkDestroyPipelineLayout(device.Handle(), uiPipelineLayout_, nullptr);
        uiPipelineLayout_ = VK_NULL_HANDLE;
    }
    if (uiDescriptorSetLayout_ != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorSetLayout(device.Handle(), uiDescriptorSetLayout_, nullptr);
        uiDescriptorSetLayout_ = VK_NULL_HANDLE;
    }
}

void UserInterface::RenderDrawData(ImDrawData* drawData, VkCommandBuffer commandBuffer, const Vulkan::SwapChain& swapChain,
                                   uint32_t imageIdx)
{
    if (drawData == nullptr || drawData->TotalVtxCount <= 0 || drawData->CmdListsCount <= 0 || imageIdx >= uiRenderBuffers_.size())
    {
        return;
    }
    if (drawData->DisplaySize.x <= 0.0f || drawData->DisplaySize.y <= 0.0f)
    {
        return;
    }

    const VkExtent2D framebufferExtent = swapChain.Extent();
    if (framebufferExtent.width == 0 || framebufferExtent.height == 0)
    {
        return;
    }

    auto& renderBuffers = uiRenderBuffers_[imageIdx];
    const VkDeviceSize vertexSize = static_cast<VkDeviceSize>(drawData->TotalVtxCount) * sizeof(ImDrawVert);
    const VkDeviceSize indexSize = static_cast<VkDeviceSize>(drawData->TotalIdxCount) * sizeof(ImDrawIdx);
    const auto& device = swapChain.Device();

    if (!renderBuffers.vertexBuffer || renderBuffers.vertexBufferSize < vertexSize)
    {
        renderBuffers.vertexBuffer.reset(new Vulkan::Buffer(device, vertexSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT));
        renderBuffers.vertexBufferMemory.reset(
            new Vulkan::DeviceMemory(renderBuffers.vertexBuffer->AllocateMemory(0, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)));
        renderBuffers.vertexBufferSize = vertexSize;
    }
    if (!renderBuffers.indexBuffer || renderBuffers.indexBufferSize < indexSize)
    {
        renderBuffers.indexBuffer.reset(new Vulkan::Buffer(device, indexSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT));
        renderBuffers.indexBufferMemory.reset(
            new Vulkan::DeviceMemory(renderBuffers.indexBuffer->AllocateMemory(0, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)));
        renderBuffers.indexBufferSize = indexSize;
    }

    auto* vertexDst = static_cast<ImDrawVert*>(renderBuffers.vertexBufferMemory->Map(0, vertexSize));
    auto* indexDst = static_cast<ImDrawIdx*>(renderBuffers.indexBufferMemory->Map(0, indexSize));
    for (int listIndex = 0; listIndex < drawData->CmdListsCount; ++listIndex)
    {
        const ImDrawList* drawList = drawData->CmdLists[listIndex];
        memcpy(vertexDst, drawList->VtxBuffer.Data, static_cast<size_t>(drawList->VtxBuffer.Size) * sizeof(ImDrawVert));
        memcpy(indexDst, drawList->IdxBuffer.Data, static_cast<size_t>(drawList->IdxBuffer.Size) * sizeof(ImDrawIdx));
        vertexDst += drawList->VtxBuffer.Size;
        indexDst += drawList->IdxBuffer.Size;
    }
    renderBuffers.vertexBufferMemory->Unmap();
    renderBuffers.indexBufferMemory->Unmap();

    UiPushConstants pushConsts{};
    pushConsts.scale[0] = 2.0f / drawData->DisplaySize.x;
    pushConsts.scale[1] = 2.0f / drawData->DisplaySize.y;
    pushConsts.translate[0] = -1.0f - drawData->DisplayPos.x * pushConsts.scale[0];
    pushConsts.translate[1] = -1.0f - drawData->DisplayPos.y * pushConsts.scale[1];
    pushConsts.rotation[0] = 1.0f;
    pushConsts.rotation[1] = 0.0f;
    pushConsts.rotation[2] = 0.0f;
    pushConsts.rotation[3] = 1.0f;
#if ANDROID
    pushConsts.rotation[0] = 0.0f;
    pushConsts.rotation[1] = 1.0f;
    pushConsts.rotation[2] = -1.0f;
    pushConsts.rotation[3] = 0.0f;
#endif
    pushConsts.hdrOutput = swapChain.IsHDR() ? 1u : 0u;
    pushConsts.hdrReferenceWhiteNit = kUiHdrReferenceWhiteNit;

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(framebufferExtent.width);
    viewport.height = static_cast<float>(framebufferExtent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

    VkDeviceSize vertexOffset = 0;
    const VkBuffer vertexBufferHandle = renderBuffers.vertexBuffer->Handle();
    const VkBuffer indexBufferHandle = renderBuffers.indexBuffer->Handle();
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, uiPipeline_);
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vertexBufferHandle, &vertexOffset);
    vkCmdBindIndexBuffer(commandBuffer, indexBufferHandle, 0,
                         sizeof(ImDrawIdx) == 2 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32);
    vkCmdPushConstants(commandBuffer, uiPipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                       sizeof(UiPushConstants), &pushConsts);

    ImGuiPlatformIO& platformIo = ImGui::GetPlatformIO();
    ImGui_ImplVulkan_RenderState renderState{};
    renderState.CommandBuffer = commandBuffer;
    renderState.Pipeline = uiPipeline_;
    renderState.PipelineLayout = uiPipelineLayout_;
    platformIo.Renderer_RenderState = &renderState;

    int globalVertexOffset = 0;
    int globalIndexOffset = 0;
    for (int listIndex = 0; listIndex < drawData->CmdListsCount; ++listIndex)
    {
        const ImDrawList* drawList = drawData->CmdLists[listIndex];
        for (int cmdIndex = 0; cmdIndex < drawList->CmdBuffer.Size; ++cmdIndex)
        {
            const ImDrawCmd* drawCmd = &drawList->CmdBuffer[cmdIndex];
            if (drawCmd->UserCallback != nullptr)
            {
                if (drawCmd->UserCallback == ImDrawCallback_ResetRenderState)
                {
                    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, uiPipeline_);
                    vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vertexBufferHandle, &vertexOffset);
                    vkCmdBindIndexBuffer(commandBuffer, indexBufferHandle, 0,
                                         sizeof(ImDrawIdx) == 2 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32);
                    vkCmdPushConstants(commandBuffer, uiPipelineLayout_,
                                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                                       sizeof(UiPushConstants), &pushConsts);
                    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
                }
                else
                {
                    drawCmd->UserCallback(drawList, drawCmd);
                }
                continue;
            }

            const ImVec2 corners[] = {
                TransformUiPointToFramebuffer(ImVec2(drawCmd->ClipRect.x, drawCmd->ClipRect.y), pushConsts, framebufferExtent),
                TransformUiPointToFramebuffer(ImVec2(drawCmd->ClipRect.z, drawCmd->ClipRect.y), pushConsts, framebufferExtent),
                TransformUiPointToFramebuffer(ImVec2(drawCmd->ClipRect.z, drawCmd->ClipRect.w), pushConsts, framebufferExtent),
                TransformUiPointToFramebuffer(ImVec2(drawCmd->ClipRect.x, drawCmd->ClipRect.w), pushConsts, framebufferExtent),
            };

            float clipMinX = corners[0].x;
            float clipMinY = corners[0].y;
            float clipMaxX = corners[0].x;
            float clipMaxY = corners[0].y;
            for (const ImVec2 corner : corners)
            {
                clipMinX = std::min(clipMinX, corner.x);
                clipMinY = std::min(clipMinY, corner.y);
                clipMaxX = std::max(clipMaxX, corner.x);
                clipMaxY = std::max(clipMaxY, corner.y);
            }

            clipMinX = std::clamp(clipMinX, 0.0f, static_cast<float>(framebufferExtent.width));
            clipMinY = std::clamp(clipMinY, 0.0f, static_cast<float>(framebufferExtent.height));
            clipMaxX = std::clamp(clipMaxX, 0.0f, static_cast<float>(framebufferExtent.width));
            clipMaxY = std::clamp(clipMaxY, 0.0f, static_cast<float>(framebufferExtent.height));
            if (clipMaxX <= clipMinX || clipMaxY <= clipMinY)
            {
                continue;
            }

            VkRect2D scissor{};
            scissor.offset.x = static_cast<int32_t>(std::floor(clipMinX));
            scissor.offset.y = static_cast<int32_t>(std::floor(clipMinY));
            scissor.extent.width = static_cast<uint32_t>(std::ceil(clipMaxX) - std::floor(clipMinX));
            scissor.extent.height = static_cast<uint32_t>(std::ceil(clipMaxY) - std::floor(clipMinY));
            vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

            const VkDescriptorSet descriptorSet = reinterpret_cast<VkDescriptorSet>(drawCmd->GetTexID());
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, uiPipelineLayout_, 0, 1,
                                    &descriptorSet, 0, nullptr);
            vkCmdDrawIndexed(commandBuffer, drawCmd->ElemCount, 1, drawCmd->IdxOffset + globalIndexOffset,
                             drawCmd->VtxOffset + globalVertexOffset, 0);
        }
        globalIndexOffset += drawList->IdxBuffer.Size;
        globalVertexOffset += drawList->VtxBuffer.Size;
    }

    platformIo.Renderer_RenderState = nullptr;
}

void UserInterface::SetStyle()
{
    // NOTE: Do not override io.IniFilename here.
    // The app/editor is responsible for choosing its ini file in the PreConfig hook.
    Runtime::UiTheme::ApplyProfessionalTheme();
}

void UserInterface::DrawPoint(float x, float y, float size, glm::vec4 color)
{
    // in viewport mode, the start from the display
    auxDrawRequest_.push_back(
        [=]()
        {
            ImVec2 startPos = ImGui::GetMainViewport()->Pos;
            ImGui::GetBackgroundDrawList()->AddRectFilled(startPos + ImVec2{x - size, y - size},
                                                          startPos + ImVec2{x + size, y + size},
                                                          Utilities::UI::Vec4ToImU32(color));
        });
}

void UserInterface::DrawLine(float fromx, float fromy, float tox, float toy, float size, glm::vec4 color)
{
    auxDrawRequest_.push_back(
        [=]()
        {
            ImVec2 startPos = ImGui::GetMainViewport()->Pos;
            ImGui::GetBackgroundDrawList()->AddLine(startPos + ImVec2(fromx, fromy), startPos + ImVec2(tox, toy),
                                                    Utilities::UI::Vec4ToImU32(color), size);
        });
}

void UserInterface::SubmitConsoleCommand(const std::string& command)
{
    if (command.empty())
    {
        return;
    }

    spdlog::info("> {}", command);

    consoleHistory_.push_back(command);
    constexpr size_t kConsoleHistoryLimit = 128;
    if (consoleHistory_.size() > kConsoleHistoryLimit)
    {
        consoleHistory_.erase(consoleHistory_.begin(),
                              consoleHistory_.begin() + static_cast<std::ptrdiff_t>(consoleHistory_.size() - kConsoleHistoryLimit));
    }
    consoleHistoryIndex_ = static_cast<int>(consoleHistory_.size());

    const auto result = GetEngine().GetCVarSystem().ExecuteCommand(command);
    if (!result.message.empty())
    {
        if (!result.success)
        {
            spdlog::error("{}", result.message);
        }
        else
        {
            spdlog::info("{}", result.message);
        }
    }

    for (const auto& line : result.output)
    {
        spdlog::info("  {}", line);
    }

    consoleScrollToBottom_ = true;
}

void UserInterface::RefreshConsoleMatches(size_t matchLimit)
{
    if (consoleInput_ != consoleLastInput_)
    {
        consoleLastInput_ = consoleInput_;
        consoleMatchIndex_ = 0;
        consoleCompletionBase_.clear();
        consoleHistoryIndex_ = static_cast<int>(consoleHistory_.size());
    }

    std::string matchBase = consoleCompletionBase_.empty() ? ExtractConsolePrefix(consoleInput_) : consoleCompletionBase_;
    if (!matchBase.empty())
    {
        consoleMatches_ = GetEngine().GetCVarSystem().Match(matchBase, {.limit = matchLimit});
    }
    else
    {
        consoleMatches_.clear();
    }
}

void UserInterface::DrawConsoleMatchPopup(float width, const char* popupId)
{
    if (popupId == nullptr || consoleMatches_.empty() || !ImGui::IsItemActive())
    {
        return;
    }

    const ImVec2 itemMin = ImGui::GetItemRectMin();
    const ImVec2 itemMax = ImGui::GetItemRectMax();
    const float itemWidth = (width > 0.0f) ? width : (itemMax.x - itemMin.x);
    const float rowHeight = ImGui::GetTextLineHeightWithSpacing();
    const float popupHeight = std::min(rowHeight * (static_cast<float>(consoleMatches_.size()) + 1.5f), rowHeight * 9.0f);
    const float offset = 2.0f;

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float viewportBottom = viewport->Pos.y + viewport->Size.y;
    const float yBelow = itemMax.y + offset;
    const float yAbove = itemMin.y - popupHeight - offset;
    const float popupY = (yBelow + popupHeight <= viewportBottom) ? yBelow : std::max(viewport->Pos.y + offset, yAbove);

    ImGui::SetNextWindowPos(ImVec2(itemMin.x, popupY));
    ImGui::SetNextWindowSize(ImVec2(itemWidth, popupHeight));
    ImGui::SetNextWindowBgAlpha(0.95f);
    ImGuiWindowFlags popupFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoDocking;
    if (ImGui::Begin(popupId, nullptr, popupFlags))
    {
        ImGui::Text("Matches:");
        ImGui::Separator();
        for (const auto& name : consoleMatches_)
        {
            ImGui::TextUnformatted(name.c_str());
        }
    }
    ImGui::End();
}

bool UserInterface::DrawConsoleCommandInput(
    const char* label, const char* hint, float width, bool closeConsoleOnSubmit, bool showMatchPopup,
    const char* matchPopupId, bool refreshMatches)
{
    constexpr size_t kMatchLimit = 8;
    if (refreshMatches)
    {
        RefreshConsoleMatches(kMatchLimit);
    }

    ImGuiInputTextFlags inputFlags = ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackHistory |
        ImGuiInputTextFlags_CallbackCompletion | ImGuiInputTextFlags_CallbackEdit;
    if (width > 0.0f)
    {
        ImGui::SetNextItemWidth(width);
    }

    const bool executeCommand =
        ImGui::InputTextWithHint(label, hint, &consoleInput_, inputFlags, &UserInterface::ConsoleInputTextCallback, this);
    if (showMatchPopup)
    {
        DrawConsoleMatchPopup(width, matchPopupId);
    }

    if (!executeCommand || consoleInput_.empty())
    {
        return false;
    }

    SubmitConsoleCommand(consoleInput_);
    consoleInput_.clear();
    consoleLastInput_.clear();
    consoleMatches_.clear();
    consoleCompletionBase_.clear();
    consoleMatchIndex_ = 0;
    if (closeConsoleOnSubmit)
    {
        showConsole_ = false;
        requestConsoleFocus_ = false;
    }
    return true;
}

int UserInterface::ConsoleInputTextCallback(ImGuiInputTextCallbackData* data)
{
    auto* ui = static_cast<UserInterface*>(data->UserData);
    if (ui == nullptr)
    {
        return 0;
    }
    return ui->HandleConsoleInputTextCallback(data);
}

int UserInterface::HandleConsoleInputTextCallback(ImGuiInputTextCallbackData* data)
{
    if (data->EventFlag == ImGuiInputTextFlags_CallbackEdit)
    {
        if (consoleSkipEditReset_)
        {
            consoleSkipEditReset_ = false;
            return 0;
        }
        consoleCompletionBase_.clear();
        consoleMatchIndex_ = 0;
        return 0;
    }

    if (data->EventFlag == ImGuiInputTextFlags_CallbackHistory)
    {
        if (data->EventKey == ImGuiKey_UpArrow)
        {
            if (consoleHistoryIndex_ > 0)
            {
                consoleHistoryIndex_--;
            }
            else if (!consoleHistory_.empty())
            {
                consoleHistoryIndex_ = 0;
            }
        }
        else if (data->EventKey == ImGuiKey_DownArrow)
        {
            if (consoleHistoryIndex_ + 1 < static_cast<int>(consoleHistory_.size()))
            {
                consoleHistoryIndex_++;
            }
            else
            {
                consoleHistoryIndex_ = static_cast<int>(consoleHistory_.size());
            }
        }

        std::string historyCmd;
        if (!consoleHistory_.empty() && consoleHistoryIndex_ >= 0 &&
            consoleHistoryIndex_ < static_cast<int>(consoleHistory_.size()))
        {
            historyCmd = consoleHistory_[consoleHistoryIndex_];
        }

        data->DeleteChars(0, data->BufTextLen);
        if (!historyCmd.empty())
        {
            data->InsertChars(0, historyCmd.c_str());
        }
        return 0;
    }

    if (data->EventFlag == ImGuiInputTextFlags_CallbackCompletion)
    {
        std::string buffer(data->Buf, data->BufTextLen);
        size_t start = buffer.find_first_not_of(" \t\r\n");
        if (start == std::string::npos)
        {
            return 0;
        }
        size_t end = buffer.find_first_of(" =\t\r\n", start);
        if (end == std::string::npos)
        {
            end = buffer.size();
        }
        if (data->CursorPos > static_cast<int>(end))
        {
            return 0;
        }

        std::string prefix = buffer.substr(start, end - start);
        if (prefix.empty())
        {
            return 0;
        }

        if (consoleCompletionBase_.empty())
        {
            consoleCompletionBase_ = prefix;
            consoleMatchIndex_ = 0;
        }

        constexpr size_t kMatchLimit = 8;
        auto matches = GetEngine().GetCVarSystem().Match(consoleCompletionBase_, {.limit = kMatchLimit});
        consoleMatches_ = matches;
        if (matches.empty())
        {
            return 0;
        }

        int index = consoleMatchIndex_ % static_cast<int>(matches.size());
        const std::string& match = matches[index];
        consoleMatchIndex_ = (index + 1) % static_cast<int>(matches.size());

        std::string rest = end < buffer.size() ? buffer.substr(end) : "";
        std::string newBuffer = match + rest;

        consoleSkipEditReset_ = true;
        data->DeleteChars(0, data->BufTextLen);
        data->InsertChars(0, newBuffer.c_str());
        data->CursorPos = static_cast<int>(match.size());
    }

    return 0;
}

void UserInterface::DrawConsoleLogOutput(const char* childId, const ImVec2& size, bool bordered)
{
    DrawConsoleLogOutputInternal(childId, size, bordered);
}

void UserInterface::DrawConsoleLogOutputInternal(const char* childId, const ImVec2& size, bool bordered)
{
    const auto logSink = Runtime::Editor::GetConsoleLogSink();
    const std::vector<spdlog::details::log_msg_buffer> lines = logSink ? logSink->last_raw() : std::vector<spdlog::details::log_msg_buffer>{};
    const uint64_t revision = Runtime::Editor::GetConsoleLogSequence();
    static ImGuiTextFilter consoleFilter;
    static bool showInfo = true;
    static bool showWarn = true;
    static bool showError = true;
    static bool showDebug = true;
    static size_t clearedLineOffset = 0;

    if (clearedLineOffset > lines.size())
    {
        clearedLineOffset = 0;
    }

    auto LevelInfo = [](spdlog::level::level_enum level) -> std::pair<const char*, ImVec4>
    {
        switch (level)
        {
        case spdlog::level::trace:
        case spdlog::level::debug:
            return {"[Debug]", ImVec4(0.55f, 0.9f, 0.95f, 1.0f)};
        case spdlog::level::info:
            return {"[Info]", ImVec4(0.76f, 0.86f, 1.0f, 1.0f)};
        case spdlog::level::warn:
            return {"[Warn]", ImVec4(1.0f, 0.82f, 0.35f, 1.0f)};
        case spdlog::level::err:
        case spdlog::level::critical:
            return {"[Error]", ImVec4(1.0f, 0.45f, 0.45f, 1.0f)};
        case spdlog::level::off:
        case spdlog::level::n_levels:
            return {"[Info]", ImVec4(0.78f, 0.78f, 0.78f, 1.0f)};
        }
        return {"[Info]", ImVec4(0.78f, 0.78f, 0.78f, 1.0f)};
    };

    auto ShouldShowLevel = [&](spdlog::level::level_enum level)
    {
        switch (level)
        {
        case spdlog::level::trace:
        case spdlog::level::debug:
            return showDebug;
        case spdlog::level::info:
            return showInfo;
        case spdlog::level::warn:
            return showWarn;
        case spdlog::level::err:
        case spdlog::level::critical:
            return showError;
        case spdlog::level::off:
        case spdlog::level::n_levels:
            return showInfo;
        }
        return true;
    };

    if (ImGui::Button("Clear"))
    {
        clearedLineOffset = lines.size();
        consoleScrollToBottom_ = true;
    }
    ImGui::SameLine();
    consoleFilter.Draw("Filter##ConsoleFilter", 220.0f);
    ImGui::SameLine();
    ImGui::Checkbox("Info", &showInfo);
    ImGui::SameLine();
    ImGui::Checkbox("Warn", &showWarn);
    ImGui::SameLine();
    ImGui::Checkbox("Error", &showError);
    ImGui::SameLine();
    ImGui::Checkbox("Debug", &showDebug);

    std::vector<size_t> visibleLines;
    visibleLines.reserve(lines.size());
    for (size_t i = clearedLineOffset; i < lines.size(); ++i)
    {
        const auto& line = lines[i];
        const std::string payload(line.payload.data(), line.payload.size());
        if (!ShouldShowLevel(line.level))
        {
            continue;
        }
        if (consoleFilter.IsActive() && !consoleFilter.PassFilter(payload.c_str()))
        {
            continue;
        }
        visibleLines.push_back(i);
    }

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.55f));
    const ImGuiChildFlags childFlags = bordered ? ImGuiChildFlags_Borders : ImGuiChildFlags_None;
    if (ImGui::BeginChild(childId, size, childFlags))
    {
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(visibleLines.size()));
        while (clipper.Step())
        {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
            {
                const auto& line = lines[visibleLines[static_cast<size_t>(i)]];
                const auto [prefix, prefixColor] = LevelInfo(line.level);

                const char* payloadStart = line.payload.data();
                const char* payloadEnd = payloadStart + line.payload.size();
                ImGui::PushStyleColor(ImGuiCol_Text, prefixColor);
                ImGui::TextUnformatted(prefix);
                ImGui::PopStyleColor();
                ImGui::SameLine();
                ImGui::PushStyleColor(ImGuiCol_Text, Runtime::UiTheme::Color(Runtime::UiTheme::EColor::Text));
                ImGui::TextUnformatted(payloadStart, payloadEnd);
                ImGui::PopStyleColor();
            }
        }

        if (consoleScrollToBottom_ || revision != consoleLogRevision_)
        {
            ImGui::SetScrollHereY(1.0f);
            consoleScrollToBottom_ = false;
            consoleLogRevision_ = revision;
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

void UserInterface::PreRender()
{
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL3_NewFrame();
#if ANDROID
    auto& io = ImGui::GetIO();
    io.DisplayFramebufferScale.x *= GAndroidMagicScale;
    io.DisplayFramebufferScale.y *= GAndroidMagicScale;
#endif
    ImGui::NewFrame();

    // update texture to ui
    uint32_t maxId = Assets::GlobalTexturePool::GetInstance()->TotalTextures();
    for (uint32_t idx = 0; idx < maxId; ++idx)
    {
        RequestImTextureId(idx);
    }
}

void UserInterface::Render(const Statistics& statistics, VulkanGpuTimer* gpuTimer, Assets::Scene* scene,
                           bool suppressStatisticsOverlay)
{
    if (!suppressStatisticsOverlay)
    {
        DrawOverlay(statistics, gpuTimer);
    }
    DrawConsoleWindow();
}

void UserInterface::PostRender(VkCommandBuffer commandBuffer, const Vulkan::SwapChain& swapChain, uint32_t imageIdx,
                               bool suppressAllUi)
{
    if (suppressAllUi)
    {
        ImGui::EndFrame();
        return;
    }

    if (GetEngine().GetEngineStatus() == NextRenderer::EApplicationStatus::Loading)
        DrawIndicator(GetEngine().GetTotalFrames());

    // aux
    for (auto& req : auxDrawRequest_)
    {
        req();
    }
    auxDrawRequest_.clear();

    ImGui::Render();

    VkRenderPassBeginInfo renderPassInfo = {};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = renderPass_->Handle();
    renderPassInfo.framebuffer = uiFrameBuffers_[imageIdx].Handle();
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = renderPass_->SwapChain().Extent();
    renderPassInfo.clearValueCount = 0;
    renderPassInfo.pClearValues = nullptr;

    vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
    RenderDrawData(ImGui::GetDrawData(), commandBuffer, swapChain, imageIdx);
    vkCmdEndRenderPass(commandBuffer);

    auto& io = ImGui::GetIO();
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
    }
}

void UserInterface::HandleEvent(const SDL_Event* event)
{
    ImGui_ImplSDL3_ProcessEvent(event);
    if (!event)
    {
        return;
    }

    if (event->type == SDL_EVENT_KEY_DOWN && event->key.key == SDLK_GRAVE)
    {
        //if (!ImGui::GetIO().WantCaptureKeyboard)
        {
            showConsole_ = !showConsole_;
            requestConsoleFocus_ = showConsole_;
        }
    }
}

bool UserInterface::WantsToCaptureKeyboard() const { return ImGui::GetIO().WantCaptureKeyboard; }

bool UserInterface::WantsToCaptureMouse() const { return ImGui::GetIO().WantCaptureMouse; }

void UserInterface::ToggleConsole()
{
    showConsole_ = !showConsole_;
    requestConsoleFocus_ = showConsole_;
}

void UserInterface::DrawOverlay(const Statistics& statistics, VulkanGpuTimer* gpuTimer)
{
    if (!Settings().ShowOverlay)
    {
        return;
    }

    frameRateSamples_[overlaySampleCursor_] = statistics.FrameRate;
    frameTimeSamples_[overlaySampleCursor_] = statistics.FrameTime;
    overlaySampleCursor_ = (overlaySampleCursor_ + 1) % kOverlaySparklineSampleCount;
    overlaySampleFilled_ = std::min(overlaySampleFilled_ + 1, kOverlaySparklineSampleCount);

    const auto& io = ImGui::GetIO();
    constexpr float distance = 12.0f;
    constexpr float panelWidth = 380.0f;
    const ImVec2 pos = ImVec2(io.DisplaySize.x - distance - panelWidth, distance + 44.0f);
    const float panelHeight = std::max(420.0f, io.DisplaySize.y - pos.y - 42.0f);

    if (!Runtime::UiTheme::BeginFloatingPanel(
            "##ProfilerPanel", ICON_FA_CHART_LINE, "Profiler", &Settings().ShowOverlay,
            pos, ImVec2(panelWidth, panelHeight)))
    {
        return;
    }

    ImGui::BeginChild("##ProfilerBody", ImVec2(0, 0), false, ImGuiWindowFlags_NoBackground);

    auto BeginCard = [&](const char* id, float height, ImGuiWindowFlags extraFlags = 0)
    {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 10.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, Runtime::UiTheme::Color(Runtime::UiTheme::EColor::SurfaceElevated, 0.38f));
        ImGui::PushStyleColor(ImGuiCol_Border, Runtime::UiTheme::Color(Runtime::UiTheme::EColor::Border, 0.84f));
        ImGui::BeginChild(id, ImVec2(0.0f, height), true, extraFlags);
    };

    auto EndCard = [&]()
    {
        ImGui::EndChild();
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(3);
    };

    auto BuildOrdered = [&](const std::array<float, kOverlaySparklineSampleCount>& src,
                            std::array<float, kOverlaySparklineSampleCount>& dst,
                            int& outCount)
    {
        outCount = overlaySampleFilled_;
        if (overlaySampleFilled_ < kOverlaySparklineSampleCount)
        {
            for (int i = 0; i < outCount; ++i)
            {
                dst[i] = src[i];
            }
        }
        else
        {
            for (int i = 0; i < kOverlaySparklineSampleCount; ++i)
            {
                dst[i] = src[(overlaySampleCursor_ + i) % kOverlaySparklineSampleCount];
            }
        }
    };

    std::array<float, kOverlaySparklineSampleCount> orderedFps{};
    std::array<float, kOverlaySparklineSampleCount> orderedFt{};
    int orderedCount = 0;
    BuildOrdered(frameRateSamples_, orderedFps, orderedCount);
    BuildOrdered(frameTimeSamples_, orderedFt, orderedCount);

    const ImVec4 colHeader = Runtime::UiTheme::Color(Runtime::UiTheme::EColor::Blue);
    const ImVec4 colLabel = Runtime::UiTheme::Color(Runtime::UiTheme::EColor::TextMuted);
    const ImVec4 colVal = Runtime::UiTheme::Color(Runtime::UiTheme::EColor::Text);
    const ImVec4 colGood = Runtime::UiTheme::Color(Runtime::UiTheme::EColor::Success);
    const ImVec4 colWarn = Runtime::UiTheme::Color(Runtime::UiTheme::EColor::Warning);
    const ImVec4 colBad = Runtime::UiTheme::Color(Runtime::UiTheme::EColor::Danger);

    auto LabelVal = [&](const char* label, const char* fmt, auto... args)
    {
        ImGui::TextColored(colLabel, "%s", label);
        ImGui::SameLine(132.0f);
        ImGui::TextColored(colVal, fmt, args...);
    };

    {
        const VkPhysicalDeviceProperties deviceProperties =
            NextEngine::GetInstance()->GetRenderer().Device().DeviceProperties();

        BeginCard("##ProfilerDeviceCard", 76.0f);
        if (ImGui::BeginTable("##ProfilerDeviceHeader", 2, ImGuiTableFlags_SizingStretchProp))
        {
            ImGui::TableNextColumn();
            ImGui::TextColored(colHeader, "Device");
            ImGui::TableNextColumn();
            ImGui::TextColored(colLabel, "Resolution");
            ImGui::SameLine(0.0f, 8.0f);
            ImGui::TextColored(colVal, "%ux%u", statistics.FramebufferSize.width,
                               statistics.FramebufferSize.height);
            ImGui::EndTable();
        }
        ImGui::TextColored(colVal, "%s", deviceProperties.deviceName);
        EndCard();
        ImGui::Dummy(ImVec2(0.0f, 8.0f));
    }

    {
        const float panelAvail = ImGui::GetContentRegionAvail().x;
        const float gap = 8.0f;
        const float halfWidth = (panelAvail - gap) * 0.5f;

        auto DrawCard = [&](const char* title, const char* value, ImVec4 valueColor,
                            const float* samples, int count, ImVec4 sparkColor, float width)
        {
            ImVec2 cardPos = ImGui::GetCursorScreenPos();
            const float height = 78.0f;
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddRectFilled(cardPos, ImVec2(cardPos.x + width, cardPos.y + height),
                              Runtime::UiTheme::ColorU32(Runtime::UiTheme::EColor::SurfaceElevated, 0.65f), 6.0f);
            dl->AddRect(cardPos, ImVec2(cardPos.x + width, cardPos.y + height),
                        Runtime::UiTheme::ColorU32(Runtime::UiTheme::EColor::Border, 0.85f), 6.0f);

            ImGui::SetCursorScreenPos(ImVec2(cardPos.x + 10.0f, cardPos.y + 6.0f));
            ImGui::TextColored(colHeader, "%s", title);

            ImGui::SetCursorScreenPos(ImVec2(cardPos.x + 10.0f, cardPos.y + 22.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, valueColor);
            ImGui::Text("%s", value);
            ImGui::PopStyleColor();

            ImGui::SetCursorScreenPos(ImVec2(cardPos.x + 10.0f, cardPos.y + height - 26.0f));
            Runtime::UiTheme::Sparkline(samples, count, ImVec2(width - 20.0f, 22.0f), sparkColor);

            ImGui::SetCursorScreenPos(ImVec2(cardPos.x + width, cardPos.y));
            ImGui::Dummy(ImVec2(width, height));
        };

        const ImVec4 fpsColor = statistics.FrameRate > 55.0f ? colGood
            : (statistics.FrameRate > 30.0f ? colWarn : colBad);
        const std::string fpsText = fmt::format("{:.0f}  FPS", statistics.FrameRate);
        const std::string ftText = fmt::format("{:.2f}  ms", statistics.FrameTime);

        DrawCard("Frame Rate", fpsText.c_str(), fpsColor,
                 orderedFps.data(), orderedCount, colGood, halfWidth);
        ImGui::SameLine(0.0f, gap);
        DrawCard("Frame Time", ftText.c_str(), colVal,
                 orderedFt.data(), orderedCount, Runtime::UiTheme::Color(Runtime::UiTheme::EColor::Blue), halfWidth);
        ImGui::Dummy(ImVec2(0.0f, 8.0f));
    }

    auto& gpuDrivenStat = NextEngine::GetInstance()->GetScene().GetGpuDrivenStat();
    const uint32_t instanceCount = gpuDrivenStat.ProcessedCount > gpuDrivenStat.CulledCount
        ? gpuDrivenStat.ProcessedCount - gpuDrivenStat.CulledCount
        : 0;
    const uint32_t triangleCount = gpuDrivenStat.TriangleCount > gpuDrivenStat.CulledTriangleCount
        ? gpuDrivenStat.TriangleCount - gpuDrivenStat.CulledTriangleCount
        : 0;

    BeginCard("##ProfilerSceneStatsCard", 156.0f);
    ImGui::TextColored(colHeader, "Scene Stats");
    ImGui::Dummy(ImVec2(0.0f, 4.0f));
    if (ImGui::BeginTable("##SceneStatsTable", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
    {
        ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthFixed, 132.0f);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
        auto DrawSceneStat = [&](const char* label, const std::string& value)
        {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextColored(colLabel, "%s", label);
            ImGui::TableSetColumnIndex(1);
            ImGui::TextColored(colVal, "%s", value.c_str());
        };
        DrawSceneStat("Nodes", Utilities::metricFormatter(static_cast<double>(statistics.NodeCount), ""));
        DrawSceneStat("Instances", Utilities::metricFormatter(static_cast<double>(statistics.InstanceCount), ""));
        DrawSceneStat("Textures", std::to_string(statistics.TextureCount));
        DrawSceneStat("Draws", fmt::format("{} / {}",
                                           Utilities::metricFormatter(static_cast<double>(instanceCount), ""),
                                           Utilities::metricFormatter(static_cast<double>(gpuDrivenStat.ProcessedCount), "")));
        DrawSceneStat("Triangles", fmt::format("{} / {}",
                                               Utilities::metricFormatter(static_cast<double>(triangleCount), ""),
                                               Utilities::metricFormatter(static_cast<double>(gpuDrivenStat.TriangleCount), "")));
        ImGui::EndTable();
    }

    const uint32_t mainTasks = TaskCoordinator::GetInstance()->GetMainTaskCount();
    const uint32_t lowTasks = TaskCoordinator::GetInstance()->GetParralledTaskCount();
    const uint32_t completeTasks = TaskCoordinator::GetInstance()->GetComleteTaskQueueCount();
    LabelVal("Tasks:", "%d / %d / %d", mainTasks, lowTasks, completeTasks);
    EndCard();
    ImGui::Dummy(ImVec2(0.0f, 8.0f));

    struct TimingRow
    {
        std::string name;
        int depth = 0;
        float average = 0.0f;
        float minimum = 0.0f;
        float maximum = 0.0f;
        uint32_t displayOrder = 0;
        bool active = true;
    };

    constexpr double timingHistoryWindowSeconds = 2.0;
    constexpr double timingStaleSeconds = 3.0;
    const double now = ImGui::GetTime();

    auto BuildTimingRows = [&](const std::vector<VulkanGpuTimer::TimerStat>& times,
                               std::unordered_map<std::string, TimingHistory>& historyMap)
    {
        uint32_t currentDisplayOrder = 0;
        for (const auto& time : times)
        {
            const std::string& historyKey = time.stableKey;
            auto historyIter = historyMap.try_emplace(historyKey).first;
            auto& history = historyIter->second;

            history.displayOrder = currentDisplayOrder++;
            history.displayName = time.name;
            history.depth = time.depth;
            history.lastSeenTime = now;
            history.samples.push_back({now, time.milliseconds});

            while (!history.samples.empty() &&
                   now - history.samples.front().sampleTime > timingHistoryWindowSeconds)
            {
                history.samples.pop_front();
            }

            float sum = 0.0f;
            float minimum = 1000000.0f;
            float maximum = 0.0f;
            for (const auto& sample : history.samples)
            {
                sum += sample.milliseconds;
                minimum = std::min(minimum, sample.milliseconds);
                maximum = std::max(maximum, sample.milliseconds);
            }

            history.average = history.samples.empty() ? time.milliseconds : sum / static_cast<float>(history.samples.size());
            history.minimum = minimum;
            history.maximum = maximum;
        }

        for (auto iter = historyMap.begin(); iter != historyMap.end();)
        {
            auto& history = iter->second;
            while (!history.samples.empty() &&
                   now - history.samples.front().sampleTime > timingHistoryWindowSeconds)
            {
                history.samples.pop_front();
            }

            if (now - iter->second.lastSeenTime > timingStaleSeconds)
            {
                iter = historyMap.erase(iter);
            }
            else
            {
                ++iter;
            }
        }

        std::vector<TimingRow> timingRows;
        timingRows.reserve(historyMap.size());
        for (const auto& [key, history] : historyMap)
        {
            timingRows.push_back({history.displayName,
                                  history.depth,
                                  history.average,
                                  history.minimum,
                                  history.maximum,
                                  history.displayOrder,
                                  now - history.lastSeenTime <= 0.1});
        }
        std::sort(timingRows.begin(), timingRows.end(), [](const TimingRow& lhs, const TimingRow& rhs)
        {
            if (lhs.displayOrder != rhs.displayOrder)
            {
                return lhs.displayOrder < rhs.displayOrder;
            }
            if (lhs.active != rhs.active)
            {
                return lhs.active;
            }
            return lhs.name < rhs.name;
        });
        return timingRows;
    };

    auto DrawTimingSection = [&](const char* label, const char* tableId, const std::vector<TimingRow>& timingRows)
    {
        float totalTime = 0.0f;
        for (const auto& row : timingRows)
        {
            if (row.depth == 0)
            {
                totalTime += row.average;
            }
        }

        ImGui::TextColored(colHeader, "%s (avg %.2fms / %.1fs)", label, totalTime, timingHistoryWindowSeconds);

        auto TimingBarColor = [&](float milliseconds)
        {
            if (milliseconds < 1.0f)
            {
                return colGood;
            }
            if (milliseconds < 4.0f)
            {
                return colWarn;
            }
            return colBad;
        };

        if (ImGui::BeginTable(tableId, 5,
                              ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                                  ImGuiTableFlags_SizingFixedFit))
        {
            ImGui::TableSetupColumn("Pass", ImGuiTableColumnFlags_WidthFixed, 150.0f);
            ImGui::TableSetupColumn("Avg (ms)", ImGuiTableColumnFlags_WidthFixed, 62.0f);
            ImGui::TableSetupColumn("Min (ms)", ImGuiTableColumnFlags_WidthFixed, 62.0f);
            ImGui::TableSetupColumn("Max (ms)", ImGuiTableColumnFlags_WidthFixed, 62.0f);
            ImGui::TableSetupColumn("Graph", ImGuiTableColumnFlags_WidthFixed, 74.0f);
            ImGui::TableHeadersRow();

            for (const auto& row : timingRows)
            {
                const float ratio = totalTime > 0.001f ? row.average / totalTime : 0.0f;
                const ImVec4 rowColor = row.depth == 0 ? colVal : colLabel;

                ImGui::TableNextRow();
                ImGui::PushStyleVar(ImGuiStyleVar_Alpha, row.active ? 1.0f : 0.45f);
                ImGui::TableNextColumn();
                ImGui::Indent(static_cast<float>(row.depth) * 12.0f);
                ImGui::TextColored(rowColor, "%s", row.name.c_str());
                ImGui::Unindent(static_cast<float>(row.depth) * 12.0f);

                ImGui::TableNextColumn();
                ImGui::TextColored(rowColor, "%.2f", row.average);
                ImGui::TableNextColumn();
                ImGui::TextColored(colLabel, "%.2f", row.minimum);
                ImGui::TableNextColumn();
                ImGui::TextColored(colLabel, "%.2f", row.maximum);
                ImGui::TableNextColumn();
                Runtime::UiTheme::DrawProgressBar(std::min(ratio, 1.0f),
                                                  TimingBarColor(row.average),
                                                  ImVec2(70.0f, ImGui::GetTextLineHeight()));
                ImGui::PopStyleVar();
            }
            ImGui::EndTable();
        }
    };

    const float timingCardHeight = std::max(180.0f, ImGui::GetContentRegionAvail().y - 42.0f);
    BeginCard("##ProfilerTimingCard", timingCardHeight, ImGuiWindowFlags_HorizontalScrollbar);
    if (gpuTimer)
    {
        const auto gpuTimingRows = BuildTimingRows(gpuTimer->FetchAllTimes(4), gpuTimeHistory_);
        DrawTimingSection("Pass Timing", "##GpuTimeTable", gpuTimingRows);

        const auto cpuTimingRows = BuildTimingRows(gpuTimer->FetchAllCpuTimes(5), cpuTimeHistory_);
        if (!cpuTimingRows.empty())
        {
            ImGui::Dummy(ImVec2(0.0f, 6.0f));
            Runtime::UiTheme::DrawThinSeparator(0.55f);
            DrawTimingSection("CPU Time", "##CpuTimeTable", cpuTimingRows);
        }
    }
    else
    {
        ImGui::TextColored(colLabel, "Timing data is unavailable.");
    }
    EndCard();

    ImGui::Dummy(ImVec2(0.0f, 8.0f));
    LabelVal("Frame:", "%d", statistics.TotalFrames);
    LabelVal("Time:", "%s",
             fmt::format("{:%H:%M:%S}", std::chrono::seconds(static_cast<long long>(statistics.RenderTime))).c_str());

    ImGui::EndChild();
    Runtime::UiTheme::EndFloatingPanel();
}

void UserInterface::DrawIndicator(uint32_t frameCount)
{
    frameCount /= 60;
    ImGui::OpenPopup("Loading");
    // Always center this window when appearing
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(200, 40));

    if (ImGui::BeginPopupModal("Loading", NULL,
                               ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoResize))
    {
        ImGui::Text("Loading%s",
                    frameCount % 4 == 0       ? ""
                        : frameCount % 4 == 1 ? "."
                        : frameCount % 4 == 2 ? ".."
                                              : "...");
        ImGui::EndPopup();
    }
}

void UserInterface::DrawConsoleWindow()
{
    if (!showConsole_)
    {
        return;
    }

    const ImVec2 displaySize = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(displaySize, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.7f);

    const auto flags = ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove;
    if (ImGui::Begin("Console", &showConsole_, flags))
    {
        const size_t kConsoleMatchLimit = 8;
        RefreshConsoleMatches(kConsoleMatchLimit);

        float hintHeight = 0.0f;
        if (!consoleMatches_.empty())
        {
            hintHeight = ImGui::GetTextLineHeightWithSpacing() * (static_cast<float>(consoleMatches_.size()) + 1.0f);
        }

        float inputHeight = ImGui::GetFrameHeightWithSpacing();
        float outputHeight = ImGui::GetContentRegionAvail().y - inputHeight - hintHeight;
        outputHeight = std::max(outputHeight, ImGui::GetFontSize() * 5.0f);

        DrawConsoleLogOutputInternal("ConsoleOutput", ImVec2(0, outputHeight), true);

        if (!consoleMatches_.empty())
        {
            ImGui::BeginChild("ConsoleMatches", ImVec2(0, hintHeight), false, ImGuiWindowFlags_NoScrollbar);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
            ImGui::Text("Matches (%zu/%zu):", consoleMatches_.size(), kConsoleMatchLimit);
            ImGui::PopStyleColor();
            for (const auto& name : consoleMatches_)
            {
                ImGui::Text("%s", name.c_str());
            }
            ImGui::EndChild();
        }

        if (requestConsoleFocus_)
        {
            ImGui::SetKeyboardFocusHere();
            requestConsoleFocus_ = false;
        }

        ImGui::PushItemWidth(-1);
        DrawConsoleCommandInput("##ConsoleInput", "", -1.0f, true, false, nullptr, false);
        ImGui::PopItemWidth();
    }
    ImGui::End();
}
