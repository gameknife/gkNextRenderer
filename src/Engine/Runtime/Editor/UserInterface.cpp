#include "Engine/Runtime/Editor/UserInterface.hpp"
#include "Engine/Runtime/Editor/UserInterface.Internal.hpp"

#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/DebugUiProvider.hpp"
#include "Engine/Runtime/Scene/SceneList.hpp"
#include "Engine/Runtime/Config/UserSettings.hpp"
#include "Engine/Runtime/Editor/FontLoader.h"
#include "ThirdParty/imgui-custom/imgui_impl_sdl3_custom.h"
#include "Engine/Utilities/Exception.hpp"
#include "Engine/Utilities/FileHelper.hpp"
#include "Engine/Vulkan/Device.hpp"
#include "Engine/Vulkan/MemoryAndShader.hpp"
#include "Engine/Vulkan/Instance.hpp"
#include "Engine/Vulkan/GraphicsPipelineBuilder.hpp"
#include "Engine/Vulkan/RenderingPipeline.hpp"
#include "Engine/Vulkan/CommandExecution.hpp"
#include "Engine/Vulkan/SwapChain.hpp"
#include "Engine/Vulkan/WindowSurface.hpp"

#include <imgui.h>
#include <imgui_freetype.h>
#include <imgui_stdlib.h>
#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fmt/chrono.h>
#include <fmt/format.h>

#include "Engine/Assets/GPU/TextureImage.hpp"
#include "Engine/Assets/GPU/Texture.hpp"
#include "Engine/Options.hpp"
#include "Engine/Rendering/VulkanBaseRenderer.hpp"
#include "Engine/Runtime/Subsystems/TaskCoordinator.hpp"
#include "ThirdParty/fontawesome/IconsFontAwesome6.h"
#include "Engine/Utilities/FileHelper.hpp"
#include "Engine/Utilities/ImGui.hpp"
#include "Engine/Utilities/Math.hpp"
#include "Engine/Utilities/StbImage.hpp"
#include "Engine/Vulkan/GpuResources.hpp"

extern float GAndroidMagicScale;
extern std::unique_ptr<Vulkan::VulkanBaseRenderer> GApplication;

namespace NextUI
{

struct UiRenderBuffer::Impl
{
    struct DrawSegment
    {
        uint32_t vertexOffset = 0;
        uint32_t vertexCount = 0;
    };

    struct DrawOp
    {
        enum class EType : uint8_t
        {
            Draw,
            Callback,
        };

        EType type = EType::Draw;
        DrawSegment segment{};
        const ImDrawList* drawList = nullptr;
        const ImDrawCmd* drawCmd = nullptr;
    };

    std::unique_ptr<Vulkan::Buffer> vertexBuffer;
    std::unique_ptr<Vulkan::DeviceMemory> vertexBufferMemory;
    VkDeviceSize vertexBufferSize = 0;
    std::vector<DrawOp> drawOps;
};

UiRenderBuffer::UiRenderBuffer() : impl_(std::make_unique<Impl>()) {}
UiRenderBuffer::~UiRenderBuffer() = default;
UiRenderBuffer::UiRenderBuffer(UiRenderBuffer&&) noexcept = default;
UiRenderBuffer& UiRenderBuffer::operator=(UiRenderBuffer&&) noexcept = default;

namespace
{

    constexpr const char* kUiVertexShaderPath = "assets/shaders/UI.ImGui.vert.slang.spv";
    constexpr const char* kUiFragmentShaderPath = "assets/shaders/UI.ImGui.frag.slang.spv";
    constexpr const char* kUiFontAtlasTextureName = "__imgui_font_atlas__";
    constexpr float kUiHdrReferenceWhiteNit = 203.0f;

    struct UiPushConstants
    {
        float scale[2];
        float translate[2];
        float rotation[4];
        uint32_t hdrOutputMode;
        float hdrReferenceWhiteNit;
        float padding[2];
    };

    struct UiBatchedVertex
    {
        ImVec2 position;
        ImVec2 uv;
        ImU32 color = 0;
        float clipRect[4]{};
        uint32_t textureIndex = 0;
    };

    struct UiRendererRenderState
    {
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkPipeline pipeline = VK_NULL_HANDLE;
        VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    };

    std::string ShaderFilename(const std::string& shaderFile)
    {
        return std::filesystem::path(shaderFile).filename().string();
    }

    bool MarkChangedShaderFile(
        const std::string& shaderFile,
        const std::set<std::string>& changedShaderFiles,
        std::set<std::string>& handledShaderFiles)
    {
        const std::string filename = ShaderFilename(shaderFile);
        if (changedShaderFiles.find(filename) == changedShaderFiles.end())
        {
            return false;
        }
        handledShaderFiles.insert(filename);
        return true;
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

    void BindUiRenderState(VkCommandBuffer commandBuffer, VkPipeline pipeline, VkPipelineLayout pipelineLayout,
                           VkDescriptorSet bindlessDescriptorSet, VkBuffer vertexBuffer, const VkViewport& viewport,
                           const VkRect2D& scissor, const UiPushConstants& pushConsts)
    {
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        if (bindlessDescriptorSet != VK_NULL_HANDLE)
        {
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1,
                                    &bindlessDescriptorSet, 0, nullptr);
        }
        if (vertexBuffer != VK_NULL_HANDLE)
        {
            constexpr VkDeviceSize vertexOffset = 0;
            vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vertexBuffer, &vertexOffset);
        }
        vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
        vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
        vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                           sizeof(UiPushConstants), &pushConsts);
    }

} // namespace

VkPipeline CreateUiGraphicsPipeline(const Vulkan::Device& device, VkPipelineLayout pipelineLayout,
                                    VkRenderPass renderPass)
{
    const Vulkan::ShaderModule vertShader(device, kUiVertexShaderPath);
    const Vulkan::ShaderModule fragShader(device, kUiFragmentShaderPath);

    VkVertexInputBindingDescription vertexBinding{};
    vertexBinding.binding = 0;
    vertexBinding.stride = sizeof(UiBatchedVertex);
    vertexBinding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    std::array<VkVertexInputAttributeDescription, 5> vertexAttributes{};
    vertexAttributes[0].location = 0;
    vertexAttributes[0].binding = 0;
    vertexAttributes[0].format = VK_FORMAT_R32G32_SFLOAT;
    vertexAttributes[0].offset = static_cast<uint32_t>(offsetof(UiBatchedVertex, position));
    vertexAttributes[1].location = 1;
    vertexAttributes[1].binding = 0;
    vertexAttributes[1].format = VK_FORMAT_R32G32_SFLOAT;
    vertexAttributes[1].offset = static_cast<uint32_t>(offsetof(UiBatchedVertex, uv));
    vertexAttributes[2].location = 2;
    vertexAttributes[2].binding = 0;
    vertexAttributes[2].format = VK_FORMAT_R8G8B8A8_UNORM;
    vertexAttributes[2].offset = static_cast<uint32_t>(offsetof(UiBatchedVertex, color));
    vertexAttributes[3].location = 3;
    vertexAttributes[3].binding = 0;
    vertexAttributes[3].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    vertexAttributes[3].offset = static_cast<uint32_t>(offsetof(UiBatchedVertex, clipRect));
    vertexAttributes[4].location = 4;
    vertexAttributes[4].binding = 0;
    vertexAttributes[4].format = VK_FORMAT_R32_UINT;
    vertexAttributes[4].offset = static_cast<uint32_t>(offsetof(UiBatchedVertex, textureIndex));

    return Vulkan::GraphicsPipelineBuilder(device)
        .SetShaders(vertShader, fragShader)
        .SetVertexInput(vertexBinding, vertexAttributes.data(), static_cast<uint32_t>(vertexAttributes.size()))
        .SetDynamicViewportAndScissor()
        .SetAlphaBlend(VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA)
        .Build(pipelineLayout, renderPass, "create ui pipeline");
}

UserInterface::UserInterface(NextEngine* engine, Vulkan::CommandPool& commandPool, const Vulkan::SwapChain& swapChain,
                             const Vulkan::DepthBuffer& depthBuffer, Runtime::Config::UserSettings& userSettings,
                             std::function<void()> funcPreConfig, std::function<void()> funcInit,
                             std::unique_ptr<IMultiViewportBackend> multiViewportBackend) :
    userSettings_(userSettings), multiViewportBackend_(std::move(multiViewportBackend)), engine_(engine)
{
    const auto& window = swapChain.Device().Surface().Instance().Window();

    renderPass_.reset(new Vulkan::RenderPass(swapChain, depthBuffer, VK_ATTACHMENT_LOAD_OP_LOAD));
    renderPass_->SetDebugName("ImGui Render Pass");
    CreateUiPipeline(swapChain);

    // Initialise ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    auto& io = ImGui::GetIO();
    imguiIniPath_ = Utilities::FileHelper::GetPlatformFilePath("imgui.ini");
    io.IniFilename = imguiIniPath_.c_str();
    io.WantCaptureMouse = false;
    io.WantCaptureKeyboard = false;

    funcPreConfig();

    // Initialise ImGui GLFW adapter
    if (!ImGui_ImplSDL3_InitForVulkan(window.Handle()))
    {
        Throw(std::runtime_error("failed to initialise ImGui GLFW adapter"));
    }

    InitializeRendererBackend();

    // Window scaling and style.
#if ANDROID
    const float scaleFactor = 0.75f / static_cast<float>(GAndroidMagicScale);
#else
    const float scaleFactor = 1.0f;
#endif
    constexpr float fontSize = 16.0f;

    if (Runtime::IDebugUiProvider* styleProvider = engine->GetDebugUiProvider())
    {
        styleProvider->ApplyUiStyle();
    }
    ImGui::GetStyle().ScaleAllSizes(scaleFactor);

    // Upload ImGui fonts (use ImGuiFreeType for better font rendering, see
    // https://github.com/ocornut/imgui/tree/master/misc/freetype).
    io.Fonts->SetFontLoader(ImGuiFreeType::GetFontLoader());
    io.Fonts->FontLoaderFlags = ImGuiFreeTypeLoaderFlags_NoHinting;
    // const ImWchar* glyphRange = GOption->locale == "RU" ? io.Fonts->GetGlyphRangesCyrillic()
    //     : GOption->locale == "zhCN"                     ? io.Fonts->GetGlyphRangesChineseFull()
    //                                                     : io.Fonts->GetGlyphRangesDefault();

    if (!NextUI::FontLoader::Load(NextUI::FontLoader::FFontRequest{
            .filePath = "assets/fonts/Roboto-Regular.ttf",
            .pixelSize = fontSize * scaleFactor,
            .includeChineseFull = false,
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

    NextUI::FontLoader::Load(NextUI::FontLoader::FFontRequest{
        .filePath = "assets/fonts/fa-regular-400.ttf",
        .pixelSize = (fontSize - 2) * scaleFactor,
        .includeChineseFull = false,
        .glyphRanges = iconRange,
        .fontConfig = &config,
        .warnOnFailure = false,
    });
    NextUI::FontLoader::Load(NextUI::FontLoader::FFontRequest{
        .filePath = "assets/fonts/fa-solid-900.ttf",
        .pixelSize = (fontSize - 2) * scaleFactor,
        .includeChineseFull = false,
        .glyphRanges = iconRange,
        .fontConfig = &config,
        .warnOnFailure = false,
    });
    NextUI::FontLoader::Load(NextUI::FontLoader::FFontRequest{
        .filePath = "assets/fonts/fa-brands-400.ttf",
        .pixelSize = (fontSize - 2) * scaleFactor,
        .includeChineseFull = false,
        .glyphRanges = iconRange,
        .fontConfig = &config,
        .warnOnFailure = false,
    });

    ImFontConfig configLocale;
    configLocale.MergeMode = true;
    NextUI::FontLoader::Load(NextUI::FontLoader::FFontRequest{
        .filePath = "assets/fonts/DroidSansFallback.ttf",
        .pixelSize = fontSize * scaleFactor,
        .glyphRanges = io.Fonts->GetGlyphRangesChineseSimplifiedCommon(),
        .fontConfig = &configLocale,
        .warnOnFailure = false,
    });

    if (funcInit != nullptr)
    {
        funcInit();
    }
    InitializeFontTexture(commandPool);
}

UserInterface::~UserInterface()
{
    ShutdownRendererBackend();
    DestroyUiPipeline();
    uiFrameBuffers_.clear();
    uiRenderBuffers_.clear();

    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
}

void UserInterface::InitializeRendererBackend()
{
    auto& io = ImGui::GetIO();
    if (io.BackendRendererUserData != nullptr)
    {
        Throw(std::runtime_error("imgui renderer backend already initialized"));
    }

    io.BackendRendererUserData = this;
    io.BackendRendererName = "gk_imgui_renderer";
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;

    if (multiViewportBackend_)
    {
        multiViewportBackend_->Initialize(*this);
    }
}

void UserInterface::ShutdownRendererBackend()
{
    if (ImGui::GetCurrentContext() == nullptr)
    {
        return;
    }

    if (multiViewportBackend_)
    {
        multiViewportBackend_->Shutdown();
    }

    auto& io = ImGui::GetIO();
    ImGui::GetPlatformIO().Renderer_RenderState = nullptr;
    io.BackendRendererName = nullptr;
    io.BackendRendererUserData = nullptr;
    io.BackendFlags &= ~ImGuiBackendFlags_RendererHasVtxOffset;
}

void UserInterface::BeginRendererBackendFrame() {}

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
}

ImTextureID UserInterface::EncodeBindlessTextureId(uint32_t textureIndex)
{
    return (ImTextureID)(static_cast<intptr_t>(textureIndex + 1u));
}

bool UserInterface::DecodeBindlessTextureId(ImTextureID textureId, uint32_t& outTextureIndex)
{
    const uint64_t rawValue = static_cast<uint64_t>((intptr_t)textureId);
    if (rawValue == 0)
    {
        return false;
    }

    const uint64_t textureIndex = rawValue - 1u;
    const auto* texturePool = Assets::GlobalTexturePool::GetInstance();
    // Allow registered textures and any explicitly-bound bindless slot (e.g. render-view outputs
    // bound above the registered range via BindSampleTexture).
    if (texturePool == nullptr || textureIndex >= Assets::GlobalTexturePool::kMaxBindlessSlots)
    {
        return false;
    }

    outTextureIndex = static_cast<uint32_t>(textureIndex);
    return true;
}

void UserInterface::InitializeFontTexture(Vulkan::CommandPool& commandPool)
{
    auto& io = ImGui::GetIO();
    unsigned char* pixels = nullptr;
    int width = 0;
    int height = 0;
    io.Fonts->GetTexDataAsAlpha8(&pixels, &width, &height);

    if (pixels == nullptr || width <= 0 || height <= 0)
    {
        Throw(std::runtime_error("failed to build imgui font atlas"));
    }

    const uint32_t fontTextureSize = static_cast<uint32_t>(width * height);
    const VkComponentMapping fontComponentMapping{
        VK_COMPONENT_SWIZZLE_ONE,
        VK_COMPONENT_SWIZZLE_ONE,
        VK_COMPONENT_SWIZZLE_ONE,
        VK_COMPONENT_SWIZZLE_R,
    };
    auto fontTexture = std::make_unique<Assets::TextureImage>(
        commandPool, static_cast<size_t>(width), static_cast<size_t>(height), 1, VK_FORMAT_R8_UNORM, pixels,
        fontTextureSize, fontComponentMapping);
    fontTexture->MainThreadPostLoading(commandPool);
    fontTexture->SetDebugName(kUiFontAtlasTextureName);
    io.Fonts->ClearTexData();

    auto* texturePool = Assets::GlobalTexturePool::GetInstance();
    if (texturePool == nullptr)
    {
        Throw(std::runtime_error("global texture pool is unavailable for imgui font atlas"));
    }
    
    fontTextureIndex_ = texturePool->RegisterTexture(
        kUiFontAtlasTextureName, std::move(fontTexture), Assets::ETextureLifetime::ETL_Persistent);

    io.Fonts->TexID = EncodeBindlessTextureId(fontTextureIndex_);
}

ImTextureID UserInterface::RequestImTextureId(uint32_t globalTextureId)
{
    if (Assets::GlobalTexturePool::GetTextureImage(globalTextureId) == nullptr)
    {
        return 0;
    }

    return EncodeBindlessTextureId(globalTextureId);
}

ImTextureID UserInterface::RequestImTextureByName(const std::string& name)
{
    uint32_t id = Assets::GlobalTexturePool::GetTextureIndexByName(name);
    if (id == static_cast<uint32_t>(-1))
    {
        return 0;
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

    handle.textureId = RequestImTextureByName(path);
    handle.valid = handle.textureId != 0;

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
    const auto* texturePool = Assets::GlobalTexturePool::GetInstance();
    if (texturePool == nullptr)
    {
        Throw(std::runtime_error("global texture pool is unavailable for ui pipeline"));
    }

    const VkDescriptorSetLayout bindlessLayout = texturePool->Layout();

    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(UiPushConstants);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &bindlessLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    Vulkan::Check(vkCreatePipelineLayout(device.Handle(), &pipelineLayoutInfo, nullptr, &uiPipelineLayout_),
                  "create ui pipeline layout");
    uiPipeline_ = CreateUiGraphicsPipeline(device, uiPipelineLayout_, renderPass_->Handle());
}

void UserInterface::DestroyUiPipeline()
{
    if (engine_ == nullptr)
    {
        return;
    }

    const auto& device = engine_->GetRenderer().Device();
    if (multiViewportBackend_)
    {
        multiViewportBackend_->OnUiPipelineDestroyed();
    }
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
}

void UserInterface::ReloadShaders(
    const std::set<std::string>& changedShaderFiles,
    std::set<std::string>& handledShaderFiles)
{
    const bool reloadVertex = changedShaderFiles.find(ShaderFilename(kUiVertexShaderPath)) != changedShaderFiles.end();
    const bool reloadFragment = changedShaderFiles.find(ShaderFilename(kUiFragmentShaderPath)) != changedShaderFiles.end();
    if (!reloadVertex && !reloadFragment)
    {
        return;
    }
    if (renderPass_ == nullptr || uiPipelineLayout_ == VK_NULL_HANDLE)
    {
        return;
    }

    const auto& device = engine_->GetRenderer().Device();
    if (multiViewportBackend_)
    {
        multiViewportBackend_->OnUiPipelineDestroyed();
    }
    if (uiPipeline_ != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(device.Handle(), uiPipeline_, nullptr);
        uiPipeline_ = VK_NULL_HANDLE;
    }
    uiPipeline_ = CreateUiGraphicsPipeline(device, uiPipelineLayout_, renderPass_->Handle());

    MarkChangedShaderFile(kUiVertexShaderPath, changedShaderFiles, handledShaderFiles);
    MarkChangedShaderFile(kUiFragmentShaderPath, changedShaderFiles, handledShaderFiles);
}

VkPipeline UserInterface::CreateViewportPipeline(VkRenderPass renderPass) const
{
    if (renderPass == VK_NULL_HANDLE)
    {
        return VK_NULL_HANDLE;
    }
    return CreateUiGraphicsPipeline(engine_->GetRenderer().Device(), uiPipelineLayout_, renderPass);
}

void UserInterface::DestroyViewportPipeline(VkPipeline pipeline) const
{
    if (pipeline != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(engine_->GetRenderer().Device().Handle(), pipeline, nullptr);
    }
}

void UserInterface::RenderViewportDrawData(ImDrawData* drawData, VkCommandBuffer commandBuffer,
                                           UiRenderBuffer& renderBuffer, VkExtent2D framebufferExtent,
                                           uint32_t hdrOutputMode, VkPipeline pipeline)
{
    RenderDrawData(drawData, commandBuffer, renderBuffer, framebufferExtent, hdrOutputMode, pipeline);
}

void UserInterface::RenderDrawData(ImDrawData* drawData, VkCommandBuffer commandBuffer, UiRenderBuffer& renderBuffer,
                                   VkExtent2D framebufferExtent, uint32_t hdrOutputMode, VkPipeline pipeline)
{
    if (drawData == nullptr || drawData->CmdListsCount <= 0 || pipeline == VK_NULL_HANDLE)
    {
        return;
    }
    if (drawData->DisplaySize.x <= 0.0f || drawData->DisplaySize.y <= 0.0f)
    {
        return;
    }
    if (framebufferExtent.width == 0 || framebufferExtent.height == 0)
    {
        return;
    }

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
    pushConsts.hdrOutputMode = hdrOutputMode;
    pushConsts.hdrReferenceWhiteNit = kUiHdrReferenceWhiteNit;

    UiRenderBuffer::Impl& renderBuffers = *renderBuffer.impl_;
    using DrawOp = UiRenderBuffer::Impl::DrawOp;
    using DrawSegment = UiRenderBuffer::Impl::DrawSegment;

    const auto& device = engine_->GetRenderer().Device();
    const size_t maxBatchedVertexCount = static_cast<size_t>(std::max(drawData->TotalIdxCount, 0));
    const VkDeviceSize maxVertexSize = static_cast<VkDeviceSize>(maxBatchedVertexCount) * sizeof(UiBatchedVertex);
    if (maxVertexSize > 0 && (!renderBuffers.vertexBuffer || renderBuffers.vertexBufferSize < maxVertexSize))
    {
        renderBuffers.vertexBuffer.reset(new Vulkan::Buffer(device, maxVertexSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT));
        renderBuffers.vertexBufferMemory.reset(new Vulkan::DeviceMemory(renderBuffers.vertexBuffer->AllocateMemory(
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)));
        device.DebugUtils().SetObjectName(renderBuffers.vertexBuffer->Handle(), "ImGui Batched Vertex Buffer");
        renderBuffers.vertexBufferMemory->SetName("ImGui Batched Vertex Buffer Memory");
        renderBuffers.vertexBufferSize = maxVertexSize;
    }

    UiBatchedVertex* mappedVertices = nullptr;
    if (maxVertexSize > 0 && renderBuffers.vertexBufferMemory)
    {
        mappedVertices = static_cast<UiBatchedVertex*>(renderBuffers.vertexBufferMemory->Map(0, maxVertexSize));
    }

    std::vector<DrawOp>& drawOps = renderBuffers.drawOps;
    drawOps.clear();
    drawOps.reserve(static_cast<size_t>(drawData->CmdListsCount) * 2);

    uint32_t currentBatchedVertexCount = 0;
    auto FlushPendingDraw = [&](uint32_t& segmentStartVertex)
    {
        const uint32_t vertexCount = currentBatchedVertexCount - segmentStartVertex;
        if (vertexCount == 0)
        {
            return;
        }

        drawOps.push_back(
            DrawOp{DrawOp::EType::Draw, DrawSegment{segmentStartVertex, vertexCount}, nullptr, nullptr});
        segmentStartVertex = currentBatchedVertexCount;
    };

    uint32_t currentSegmentStartVertex = 0;
    for (int listIndex = 0; listIndex < drawData->CmdListsCount; ++listIndex)
    {
        const ImDrawList* drawList = drawData->CmdLists[listIndex];
        if (drawList == nullptr)
        {
            continue;
        }

        for (int cmdIndex = 0; cmdIndex < drawList->CmdBuffer.Size; ++cmdIndex)
        {
            const ImDrawCmd* drawCmd = &drawList->CmdBuffer[cmdIndex];
            if (drawCmd->UserCallback != nullptr)
            {
                FlushPendingDraw(currentSegmentStartVertex);
                drawOps.push_back(DrawOp{DrawOp::EType::Callback, DrawSegment{}, drawList, drawCmd});
                continue;
            }

            uint32_t textureIndex = fontTextureIndex_;
            if (!DecodeBindlessTextureId(drawCmd->GetTexID(), textureIndex))
            {
                continue;
            }

            const ImVec2 corners[] = {
                TransformUiPointToFramebuffer(ImVec2(drawCmd->ClipRect.x, drawCmd->ClipRect.y), pushConsts,
                                              framebufferExtent),
                TransformUiPointToFramebuffer(ImVec2(drawCmd->ClipRect.z, drawCmd->ClipRect.y), pushConsts,
                                              framebufferExtent),
                TransformUiPointToFramebuffer(ImVec2(drawCmd->ClipRect.z, drawCmd->ClipRect.w), pushConsts,
                                              framebufferExtent),
                TransformUiPointToFramebuffer(ImVec2(drawCmd->ClipRect.x, drawCmd->ClipRect.w), pushConsts,
                                              framebufferExtent),
            };

            float clipMinX = corners[0].x;
            float clipMinY = corners[0].y;
            float clipMaxX = corners[0].x;
            float clipMaxY = corners[0].y;
            for (const ImVec2& corner : corners)
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

            for (uint32_t elemIndex = 0; elemIndex < drawCmd->ElemCount; ++elemIndex)
            {
                if (mappedVertices == nullptr || currentBatchedVertexCount >= maxBatchedVertexCount)
                {
                    break;
                }

                const uint32_t vertexIndex = static_cast<uint32_t>(drawList->IdxBuffer[drawCmd->IdxOffset + elemIndex]) +
                                             drawCmd->VtxOffset;
                if (vertexIndex >= static_cast<uint32_t>(drawList->VtxBuffer.Size))
                {
                    continue;
                }

                const ImDrawVert& sourceVertex = drawList->VtxBuffer[vertexIndex];
                UiBatchedVertex& batchedVertex = mappedVertices[currentBatchedVertexCount++];
                batchedVertex.position = sourceVertex.pos;
                batchedVertex.uv = sourceVertex.uv;
                batchedVertex.color = sourceVertex.col;
                batchedVertex.clipRect[0] = clipMinX;
                batchedVertex.clipRect[1] = clipMinY;
                batchedVertex.clipRect[2] = clipMaxX;
                batchedVertex.clipRect[3] = clipMaxY;
                batchedVertex.textureIndex = textureIndex;
            }
        }
    }
    FlushPendingDraw(currentSegmentStartVertex);

    if (mappedVertices != nullptr)
    {
        renderBuffers.vertexBufferMemory->Unmap();
    }

    const VkDeviceSize vertexSize = static_cast<VkDeviceSize>(currentBatchedVertexCount) * sizeof(UiBatchedVertex);

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(framebufferExtent.width);
    viewport.height = static_cast<float>(framebufferExtent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = framebufferExtent;

    const VkDescriptorSet bindlessDescriptorSet = Assets::GlobalTexturePool::GetInstance()->DescriptorSet(0);
    const VkBuffer vertexBufferHandle =
        vertexSize > 0 && renderBuffers.vertexBuffer ? renderBuffers.vertexBuffer->Handle() : VK_NULL_HANDLE;
    BindUiRenderState(commandBuffer, pipeline, uiPipelineLayout_, bindlessDescriptorSet, vertexBufferHandle,
                      viewport, scissor, pushConsts);

    ImGuiPlatformIO& platformIo = ImGui::GetPlatformIO();
    UiRendererRenderState renderState{};
    renderState.commandBuffer = commandBuffer;
    renderState.pipeline = pipeline;
    renderState.pipelineLayout = uiPipelineLayout_;
    platformIo.Renderer_RenderState = &renderState;

    for (const DrawOp& drawOp : drawOps)
    {
        if (drawOp.type == DrawOp::EType::Draw)
        {
            if (drawOp.segment.vertexCount > 0)
            {
                vkCmdDraw(commandBuffer, drawOp.segment.vertexCount, 1, drawOp.segment.vertexOffset, 0);
            }
            continue;
        }

        if (drawOp.drawCmd == nullptr || drawOp.drawCmd->UserCallback == nullptr)
        {
            continue;
        }

        if (drawOp.drawCmd->UserCallback == ImDrawCallback_ResetRenderState)
        {
            BindUiRenderState(commandBuffer, pipeline, uiPipelineLayout_, bindlessDescriptorSet, vertexBufferHandle,
                              viewport, scissor, pushConsts);
            continue;
        }

        drawOp.drawCmd->UserCallback(drawOp.drawList, drawOp.drawCmd);
        BindUiRenderState(commandBuffer, pipeline, uiPipelineLayout_, bindlessDescriptorSet, vertexBufferHandle,
                          viewport, scissor, pushConsts);
    }

    platformIo.Renderer_RenderState = nullptr;
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


void UserInterface::PreRender()
{
    BeginRendererBackendFrame();
    ImGui_ImplSDL3_NewFrame();
#if ANDROID
    auto& io = ImGui::GetIO();
    io.DisplayFramebufferScale.x *= GAndroidMagicScale;
    io.DisplayFramebufferScale.y *= GAndroidMagicScale;
#endif
    ImGui::NewFrame();
}

void UserInterface::Render(const Statistics& statistics, VulkanGpuTimer* gpuTimer, Assets::Scene* scene,
                           bool suppressStatisticsOverlay)
{
    if (Runtime::IDebugUiProvider* provider = GetEngine().GetDebugUiProvider())
    {
        provider->DrawUiPanels(GetEngine(), statistics, gpuTimer, suppressStatisticsOverlay);
    }
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
    RenderDrawData(ImGui::GetDrawData(), commandBuffer, uiRenderBuffers_[imageIdx], swapChain.Extent(),
                   swapChain.HDROutputMode(), uiPipeline_);
    vkCmdEndRenderPass(commandBuffer);

    auto& io = ImGui::GetIO();
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        if (multiViewportBackend_)
        {
            multiViewportBackend_->RenderPlatformWindows();
        }
    }
}

void UserInterface::HandleEvent(const SDL_Event* event)
{
    if (!event)
    {
        return;
    }

    if (Runtime::IDebugUiProvider* provider = GetEngine().GetDebugUiProvider())
    {
        if (provider->HandleUiEvent(*event))
        {
            return;
        }
    }

    ImGui_ImplSDL3_ProcessEvent(event);
}

bool UserInterface::WantsToCaptureKeyboard() const { return ImGui::GetIO().WantCaptureKeyboard; }

bool UserInterface::WantsToCaptureMouse() const { return ImGui::GetIO().WantCaptureMouse; }



void UserInterface::DrawIndicator(uint32_t frameCount)
{
    frameCount /= 60;
    ImGui::OpenPopup("Loading");
    if (Utilities::UI::BeginAnchoredPopupModal(
            "Loading",
            NULL,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoResize,
            Utilities::UI::FModalPopupOptions{.RequestedSize = ImVec2(200, 40)}))
    {
        ImGui::Text("Loading%s",
                    frameCount % 4 == 0       ? ""
                        : frameCount % 4 == 1 ? "."
                        : frameCount % 4 == 2 ? ".."
                                              : "...");
        ImGui::EndPopup();
    }
}


}
