#include "Modules/NextUI/UserInterface.hpp"
#include "Modules/NextUI/UI/UiTheme.hpp"

#include "Engine/Assets/GPU/Texture.hpp"
#include "Engine/Assets/GPU/TextureImage.hpp"
#include "Engine/Options.hpp"
#include "Engine/Rendering/VulkanBaseRenderer.hpp"
#include "Engine/Runtime/Config/UserSettings.hpp"
#include "Modules/NextUI/FontLoader.hpp"
#include "Modules/NextUI/ImGuiContextHost.hpp"
#include "Modules/NextUI/ImGuiVulkanRenderer.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/Interface/DebugUiProvider.hpp"
#include "Modules/SceneContent/SceneList.hpp"
#include "Engine/Runtime/Subsystems/TaskCoordinator.hpp"
#include "Engine/Utilities/Exception.hpp"
#include "Engine/Utilities/FileHelper.hpp"
#include "Engine/Utilities/ImGui.hpp"
#include "Engine/Utilities/Math.hpp"
#include "Engine/Utilities/StbImage.hpp"
#include "Engine/Vulkan/CommandExecution.hpp"
#include "Engine/Vulkan/Device.hpp"
#include "Engine/Vulkan/GpuResources.hpp"
#include "Engine/Vulkan/Instance.hpp"
#include "Engine/Vulkan/MemoryAndShader.hpp"
#include "Engine/Vulkan/RenderingPipeline.hpp"
#include "Engine/Vulkan/SwapChain.hpp"
#include "Engine/Vulkan/WindowSurface.hpp"

#include "ThirdParty/fontawesome/IconsFontAwesome6.h"
#include "ThirdParty/imgui-custom/imgui_impl_sdl3_custom.h"

#include <SDL3/SDL.h>
#include <fmt/chrono.h>
#include <fmt/format.h>
#include <imgui.h>
#include <imgui_freetype.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>

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

    constexpr const char* kUiFontAtlasTextureName = "__imgui_font_atlas__";
    constexpr float kUiHdrReferenceWhiteNit = 203.0f;
    constexpr uint32_t kUiTextureFlagRawOutput = 1u << 0u;

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
        uint32_t textureFlags = 0;
    };

    struct UiRendererRenderState
    {
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkPipeline pipeline = VK_NULL_HANDLE;
        VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    };

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
UserInterface::UserInterface(NextEngine* engine, Vulkan::CommandPool& commandPool, const Vulkan::SwapChain& swapChain,
                             const Vulkan::DepthBuffer&, Runtime::Config::UserSettings& userSettings,
                             std::function<void()> funcPreConfig, std::function<void()> funcInit,
                             std::unique_ptr<IMultiViewportBackend> multiViewportBackend) :
    userSettings_(userSettings), multiViewportBackend_(std::move(multiViewportBackend)),
    textureResolver_(std::make_unique<FUiTextureResolver>()),
    contextHost_(std::make_unique<FImGuiContextHost>()),
    vulkanRenderer_(std::make_unique<FImGuiVulkanRenderer>()), engine_(engine)
{
    const auto& window = swapChain.Device().Surface().Instance().Window();

    // Initialise ImGui
    contextHost_->Create();

    auto& io = ImGui::GetIO();
    fontAtlas_ = io.Fonts;
    imguiIniPath_ = Utilities::FileHelper::GetWritableFilePath("imgui.ini");
    io.IniFilename = imguiIniPath_.c_str();
    io.WantCaptureMouse = false;
    io.WantCaptureKeyboard = false;

    funcPreConfig();

    // A VK_EXT_headless_surface has no SDL_Window. Dear ImGui's Vulkan renderer
    // can still render into it, but the SDL platform backend cannot be initialized.
    sdlPlatformBackendInitialized_ = !window.IsHeadless();
    if (!sdlPlatformBackendInitialized_)
    {
        // Docking remains useful in the main viewport, but detached ImGui windows
        // require SDL platform callbacks and their own Vulkan surfaces.
        io.ConfigFlags &= ~ImGuiConfigFlags_ViewportsEnable;
    }
    if (sdlPlatformBackendInitialized_ && !ImGui_ImplSDL3_InitForVulkan(window.Handle()))
    {
        Throw(std::runtime_error("failed to initialise ImGui SDL adapter"));
    }
    if (!sdlPlatformBackendInitialized_)
    {
        const auto extent = swapChain.Extent();
        io.DisplaySize = ImVec2(static_cast<float>(extent.width), static_cast<float>(extent.height));
        io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
        io.DeltaTime = 1.0f / 60.0f;
    }

    InitializeRendererBackend();

    // Window scaling and style.
#if ANDROID
    // Android reports the physical display density. Keep the existing DPI mapping,
    // but target 70% of its previous on-screen size for the touch UI.
    constexpr float androidUiScale = 0.70f;
    const float scaleFactor = std::max(1.0f, window.ContentScale()) * androidUiScale;
#elif WIN32
    // Keep ImGui in the scaleFactor=1 logical coordinate space used by all existing UI code.
    // PreRender maps that coordinate space onto the DPI-sized framebuffer and
    // normalizes input to the same space.
    const float scaleFactor = std::max(1.0f, window.ContentScale());
#else
    const float scaleFactor = 1.0f;
#endif
    uiScale_ = scaleFactor;
    constexpr float fontSize = 16.0f;

    NextUI::Foundation::ApplyTheme();
// The high-DPI path below reduces DisplaySize and scales the framebuffer
// transform, which scales every ImGui primitive (including text) uniformly.
// Do not additionally scale style metrics on those platforms.
#if !WIN32 && !ANDROID
    ImGui::GetStyle().ScaleAllSizes(scaleFactor);
#endif

    // Upload ImGui fonts (use ImGuiFreeType for better font rendering, see
    // https://github.com/ocornut/imgui/tree/master/misc/freetype).
    io.Fonts->SetFontLoader(ImGuiFreeType::GetFontLoader());
    io.Fonts->FontLoaderFlags = ImGuiFreeTypeLoaderFlags_NoHinting;
    // const ImWchar* glyphRange = GOption->locale == "RU" ? io.Fonts->GetGlyphRangesCyrillic()
    //     : GOption->locale == "zhCN"                     ? io.Fonts->GetGlyphRangesChineseFull()
    //                                                     : io.Fonts->GetGlyphRangesDefault();

    defaultFont_ = NextUI::FontLoader::Load(NextUI::FontLoader::FFontRequest{
            .filePath = "assets/fonts/Roboto-Regular.ttf",
            .pixelSize = fontSize,
            .includeChineseFull = true,
            .rasterizerDensity = scaleFactor,
        });
    if (defaultFont_ == nullptr)
    {
        Throw(std::runtime_error("failed to load basic ImGui Text font"));
    }
    io.FontDefault = defaultFont_;

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
        .pixelSize = fontSize - 2,
        .includeChineseFull = false,
        .glyphRanges = iconRange,
        .fontConfig = &config,
        .rasterizerDensity = scaleFactor,
        .warnOnFailure = false,
    });
    NextUI::FontLoader::Load(NextUI::FontLoader::FFontRequest{
        .filePath = "assets/fonts/fa-solid-900.ttf",
        .pixelSize = fontSize - 2,
        .includeChineseFull = false,
        .glyphRanges = iconRange,
        .fontConfig = &config,
        .rasterizerDensity = scaleFactor,
        .warnOnFailure = false,
    });
    NextUI::FontLoader::Load(NextUI::FontLoader::FFontRequest{
        .filePath = "assets/fonts/fa-brands-400.ttf",
        .pixelSize = fontSize - 2,
        .includeChineseFull = false,
        .glyphRanges = iconRange,
        .fontConfig = &config,
        .rasterizerDensity = scaleFactor,
        .warnOnFailure = false,
    });

    ImFontConfig configLocale;
    configLocale.MergeMode = true;
    NextUI::FontLoader::Load(NextUI::FontLoader::FFontRequest{
        .filePath = "assets/fonts/DroidSansFallback.ttf",
        .pixelSize = fontSize,
        .glyphRanges = io.Fonts->GetGlyphRangesChineseSimplifiedCommon(),
        .fontConfig = &configLocale,
        .rasterizerDensity = scaleFactor,
        .warnOnFailure = false,
    });

    constexpr float titleFontSize = 18.0f;
    titleBarFont_ = NextUI::FontLoader::Load(NextUI::FontLoader::FFontRequest{
        .filePath = "assets/fonts/Roboto-BoldCondensed.ttf",
        .pixelSize = titleFontSize,
        .includeChineseFull = false,
        .extraGlyphsUtf8 = "gkNextRenderer gkNextEditor SCAD Studio SCAD Library",
    });
    if (titleBarFont_ != nullptr)
    {
        ImFontConfig titleIconConfig;
        titleIconConfig.MergeMode = true;
        titleIconConfig.GlyphMinAdvanceX = titleFontSize;
        titleIconConfig.GlyphOffset = ImVec2(0, 0);

        NextUI::FontLoader::Load(NextUI::FontLoader::FFontRequest{
            .filePath = "assets/fonts/fa-regular-400.ttf",
            .pixelSize = titleFontSize - 2.0f,
            .includeChineseFull = false,
            .glyphRanges = iconRange,
            .fontConfig = &titleIconConfig,
            .rasterizerDensity = scaleFactor,
            .warnOnFailure = false,
        });
        NextUI::FontLoader::Load(NextUI::FontLoader::FFontRequest{
            .filePath = "assets/fonts/fa-solid-900.ttf",
            .pixelSize = titleFontSize - 2.0f,
            .includeChineseFull = false,
            .glyphRanges = iconRange,
            .fontConfig = &titleIconConfig,
            .rasterizerDensity = scaleFactor,
            .warnOnFailure = false,
        });
        NextUI::FontLoader::Load(NextUI::FontLoader::FFontRequest{
            .filePath = "assets/fonts/fa-brands-400.ttf",
            .pixelSize = titleFontSize - 2.0f,
            .includeChineseFull = false,
            .glyphRanges = iconRange,
            .fontConfig = &titleIconConfig,
            .rasterizerDensity = scaleFactor,
            .warnOnFailure = false,
        });
    }

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

    if (sdlPlatformBackendInitialized_)
    {
        ImGui_ImplSDL3_Shutdown();
    }
    contextHost_->Destroy();
}

void UserInterface::InitializeRendererBackend()
{
    auto& io = ImGui::GetIO();
    if (io.BackendRendererUserData != nullptr)
    {
        Throw(std::runtime_error("imgui renderer backend already initialized"));
    }

    AttachRendererBackendToCurrentContext();

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
    hasPreparedDrawData_ = false;
    DestroyUiPipeline();
    renderPass_.reset();
    uiFrameBuffers_.clear();
    uiRenderBuffers_.clear();
}

ImTextureID UserInterface::EncodeBindlessTextureId(uint32_t textureIndex, uint32_t textureFlags)
{
    const uint64_t encoded = (static_cast<uint64_t>(textureFlags) << 32u) | static_cast<uint64_t>(textureIndex + 1u);
    return (ImTextureID)(static_cast<intptr_t>(encoded));
}

bool UserInterface::DecodeBindlessTextureId(ImTextureID textureId, uint32_t& outTextureIndex, uint32_t& outTextureFlags)
{
    const uint64_t rawValue = static_cast<uint64_t>((intptr_t)textureId);
    const uint64_t encodedIndex = rawValue & 0xFFFFFFFFull;
    if (encodedIndex == 0)
    {
        return false;
    }

    const uint64_t textureIndex = encodedIndex - 1u;
    const auto* texturePool = Assets::GlobalTexturePool::GetInstance();
    // Allow registered textures and any explicitly-bound bindless slot (e.g. render-view outputs
    // bound above the registered range via BindSampleTexture).
    if (texturePool == nullptr || textureIndex >= Assets::GlobalTexturePool::kMaxBindlessSlots)
    {
        return false;
    }

    outTextureIndex = static_cast<uint32_t>(textureIndex);
    outTextureFlags = static_cast<uint32_t>(rawValue >> 32u);
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

ImTextureID UserInterface::RequestImTextureIdRawOutput(uint32_t bindlessSampleSlot)
{
    return EncodeBindlessTextureId(bindlessSampleSlot, kUiTextureFlagRawOutput);
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

UserInterface::FUiTextureHandle UserInterface::RequestUiTexture(const std::string& path, bool srgb,
                                                                EUiTextureLifetime lifetime)
{
    return textureResolver_->Request(
        path, srgb, lifetime, [this](const std::string& name) { return RequestImTextureByName(name); });
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
    uiPipeline_ = vulkanRenderer_->CreateGraphicsPipeline(device, uiPipelineLayout_, renderPass_->Handle());
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

VkPipeline UserInterface::CreateViewportPipeline(VkRenderPass renderPass) const
{
    if (renderPass == VK_NULL_HANDLE)
    {
        return VK_NULL_HANDLE;
    }
    return vulkanRenderer_->CreateGraphicsPipeline(engine_->GetRenderer().Device(), uiPipelineLayout_, renderPass);
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

ImFontAtlas* UserInterface::GetFontAtlas() const
{
    return fontAtlas_;
}

ImFont* UserInterface::GetDefaultFont() const
{
    return defaultFont_;
}

void UserInterface::AttachRendererBackendToCurrentContext() const
{
    auto& io = ImGui::GetIO();
    io.BackendRendererUserData = const_cast<UserInterface*>(this);
    io.BackendRendererName = "gk_imgui_renderer";
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
    if (io.FontDefault == nullptr)
    {
        io.FontDefault = defaultFont_;
    }
    if (fontTextureIndex_ != UINT32_MAX)
    {
        io.Fonts->TexID = EncodeBindlessTextureId(fontTextureIndex_);
    }
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
    // Android's camera projection rotates the portrait swapchain +90 degrees
    // into the landscape SDL coordinate space. Keep UI in that same space: the
    // opposite rotation would leave every widget upside down relative to scene
    // content.
    pushConsts.rotation[1] = -1.0f;
    pushConsts.rotation[2] = 1.0f;
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
            uint32_t textureFlags = 0;
            if (!DecodeBindlessTextureId(drawCmd->GetTexID(), textureIndex, textureFlags))
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
                batchedVertex.textureFlags = textureFlags;
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
    if (sdlPlatformBackendInitialized_)
    {
        ImGui_ImplSDL3_SetFramebufferScaleBias(Vulkan::SwapChain::UiContentScale());
        ImGui_ImplSDL3_NewFrame();
    }
    else
    {
        auto& io = ImGui::GetIO();
        const auto extent = GetEngine().GetRenderer().SwapChain().Extent();
        io.DisplaySize = ImVec2(static_cast<float>(extent.width), static_cast<float>(extent.height));
        io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
        io.DeltaTime = 1.0f / 60.0f;
    }
#if WIN32 || ANDROID
    auto& io = ImGui::GetIO();
#if ANDROID
    if (uiScale_ != 1.0f)
#else
    if (uiScale_ > 1.0f)
#endif
    {
        io.DisplaySize.x /= uiScale_;
        io.DisplaySize.y /= uiScale_;
        io.DisplayFramebufferScale.x *= uiScale_;
        io.DisplayFramebufferScale.y *= uiScale_;

        // SDL reports desktop monitor bounds in physical screen coordinates,
        // while the editor keeps Dear ImGui in logical coordinates. Normalize
        // monitor work areas as well so popup and tooltip clamping uses the
        // same space.
#if WIN32
        auto& monitors = ImGui::GetPlatformIO().Monitors;
        for (int monitorIndex = 0; monitorIndex < monitors.Size; ++monitorIndex)
        {
            ImGuiPlatformMonitor& monitor = monitors[monitorIndex];
            monitor.MainPos.x /= uiScale_;
            monitor.MainPos.y /= uiScale_;
            monitor.MainSize.x /= uiScale_;
            monitor.MainSize.y /= uiScale_;
            monitor.WorkPos.x /= uiScale_;
            monitor.WorkPos.y /= uiScale_;
            monitor.WorkSize.x /= uiScale_;
            monitor.WorkSize.y /= uiScale_;
        }
#endif

        // SDL input events and global mouse state are also in physical pixels.
        // Feed ImGui the logical position after the platform backend has updated
        // its event queue.
#if WIN32
        SDL_Window* mouseWindow = SDL_GetMouseFocus();
        if (mouseWindow != nullptr && !SDL_GetWindowRelativeMouseMode(mouseWindow))
        {
            float globalX = 0.0f;
            float globalY = 0.0f;
            int windowX = 0;
            int windowY = 0;
            SDL_GetGlobalMouseState(&globalX, &globalY);

            ImVec2 logicalPosition;
            if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
            {
                // During an OS title-bar drag the window position and the mouse
                // focus window are updated asynchronously. Use the global cursor
                // position directly instead of reconstructing it from a moving
                // window origin, which otherwise feeds a one-frame feedback loop
                // into ImGui's viewport move handling.
                logicalPosition = ImVec2(globalX / uiScale_, globalY / uiScale_);
            }
            else
            {
                SDL_GetWindowPosition(mouseWindow, &windowX, &windowY);
                logicalPosition = ImVec2(
                    (globalX - static_cast<float>(windowX)) / uiScale_,
                    (globalY - static_cast<float>(windowY)) / uiScale_);
            }
            io.AddMousePosEvent(logicalPosition.x, logicalPosition.y);
        }
#endif
    }
#endif
    contextHost_->BeginFrame();
}

void UserInterface::PrepareDrawData()
{
    constexpr double loadingIndicatorDelaySeconds = 0.5;
    const bool isLoading = GetEngine().GetEngineStatus() == NextRenderer::EApplicationStatus::Loading;
    const Vulkan::FPipelineWarmupProgress& pipelineWarmup =
        GetEngine().GetRenderer().PipelineWarmupProgress();
    if (isLoading)
    {
        // Pipeline warm-up starts only after this UI has presented once. Show its detailed
        // progress immediately; unlike ordinary scene loading, hiding it for 500ms would make
        // the first cold compiler stall appear as an unresponsive window.
        if (pipelineWarmup.active)
        {
            loadingStartedAt_ = -1.0;
            DrawIndicator(GetEngine().GetTotalFrames(), true);
        }
        else
        {
            if (loadingStartedAt_ < 0.0)
            {
                loadingStartedAt_ = ImGui::GetTime();
            }
            if (ImGui::GetTime() - loadingStartedAt_ >= loadingIndicatorDelaySeconds)
            {
                DrawIndicator(GetEngine().GetTotalFrames(), true);
            }
        }
    }
    else
    {
        loadingStartedAt_ = -1.0;
        if (loadingIndicatorOpen_)
        {
            DrawIndicator(GetEngine().GetTotalFrames(), false);
        }
    }

    // aux
    for (auto& req : auxDrawRequest_)
    {
        req();
    }
    auxDrawRequest_.clear();

    ImGui::Render();
    hasPreparedDrawData_ = ImGui::GetDrawData() != nullptr;
}

void UserInterface::RenderPreparedDrawData(VkCommandBuffer commandBuffer, const Vulkan::SwapChain& swapChain,
                                           uint32_t imageIdx, bool suppressAllUi)
{
    if (suppressAllUi || !hasPreparedDrawData_)
    {
        return;
    }

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

#if WIN32
    if (uiScale_ > 1.0f && event->type == SDL_EVENT_MOUSE_MOTION)
    {
        SDL_Window* window = SDL_GetWindowFromID(event->motion.windowID);
        ImGuiViewport* viewport = ImGui::FindViewportByPlatformHandle(
            reinterpret_cast<void*>(static_cast<intptr_t>(event->motion.windowID)));
        if (window != nullptr && viewport != nullptr && !SDL_GetWindowRelativeMouseMode(window))
        {
            ImVec2 logicalPosition(event->motion.x / uiScale_, event->motion.y / uiScale_);
            if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
            {
                int windowX = 0;
                int windowY = 0;
                SDL_GetWindowPosition(window, &windowX, &windowY);
                logicalPosition.x += static_cast<float>(windowX) / uiScale_;
                logicalPosition.y += static_cast<float>(windowY) / uiScale_;
            }

            auto& io = ImGui::GetIO();
            io.AddMouseSourceEvent(event->motion.which == SDL_TOUCH_MOUSEID
                                       ? ImGuiMouseSource_TouchScreen
                                       : ImGuiMouseSource_Mouse);
            io.AddMousePosEvent(logicalPosition.x, logicalPosition.y);
            return;
        }
    }
#elif ANDROID
    if (uiScale_ != 1.0f && event->type == SDL_EVENT_MOUSE_MOTION)
    {
        SDL_Window* window = SDL_GetWindowFromID(event->motion.windowID);
        if (window != nullptr && !SDL_GetWindowRelativeMouseMode(window))
        {
            auto& io = ImGui::GetIO();
            io.AddMouseSourceEvent(event->motion.which == SDL_TOUCH_MOUSEID
                                       ? ImGuiMouseSource_TouchScreen
                                       : ImGuiMouseSource_Mouse);
            io.AddMousePosEvent(event->motion.x / uiScale_, event->motion.y / uiScale_);
            return;
        }
    }
#endif
    if (sdlPlatformBackendInitialized_)
    {
        ImGui_ImplSDL3_ProcessEvent(event);
    }
}

bool UserInterface::WantsToCaptureKeyboard() const { return contextHost_->WantsToCaptureKeyboard(); }

bool UserInterface::WantsToCaptureMouse() const { return contextHost_->WantsToCaptureMouse(); }



void UserInterface::DrawIndicator(uint32_t frameCount, bool show)
{
    frameCount /= 60;
    const Vulkan::FPipelineWarmupProgress& pipelineWarmup =
        GetEngine().GetRenderer().PipelineWarmupProgress();
    const bool isPipelineWarmup = pipelineWarmup.active;
    if (isPipelineWarmup)
    {
        DrawPipelineWarmupOverlay(pipelineWarmup);
        return;
    }

    if (show)
    {
        ImGui::OpenPopup("Loading");
        loadingIndicatorOpen_ = true;
    }
    if (Utilities::UI::BeginAnchoredPopupModal(
            "Loading",
            NULL,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoResize,
            Utilities::UI::FModalPopupOptions{.RequestedSize = ImVec2(200, 40)}))
    {
        if (show)
        {
            ImGui::Text("Loading%s",
                        frameCount % 4 == 0       ? ""
                            : frameCount % 4 == 1 ? "."
                            : frameCount % 4 == 2 ? ".."
                                                  : "...");
        }
        else
        {
            ImGui::CloseCurrentPopup();
            loadingIndicatorOpen_ = false;
        }
        ImGui::EndPopup();
    }
}

void UserInterface::DrawPipelineWarmupOverlay(const Vulkan::FPipelineWarmupProgress& pipelineWarmup)
{
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowBgAlpha(0.0f);
    constexpr ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoBackground;

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 6.0f * uiScale_));
    if (!ImGui::Begin("##PipelineWarmup", nullptr, flags))
    {
        ImGui::End();
        ImGui::PopStyleVar();
        return;
    }

    ImFont* titleFont = titleBarFont_ != nullptr ? titleBarFont_ : defaultFont_;
    if (titleFont == nullptr)
    {
        titleFont = ImGui::GetFont();
    }

    // Line 1: Header (Condensed font with icon + right-aligned group progress)
    ImGui::PushFont(titleFont);
    const std::string titleText = ICON_FA_MICROCHIP "  OPTIMIZING PIPELINE CACHE";
    const std::string groupProgressText = fmt::format("{}/{}", pipelineWarmup.completedRenderers, pipelineWarmup.totalRenderers);
    const float titleTextWidth = ImGui::CalcTextSize(titleText.c_str()).x;
    const float groupTextWidth = ImGui::CalcTextSize(groupProgressText.c_str()).x;
    const float minRequiredWidth = titleTextWidth + groupTextWidth + 24.0f * uiScale_;
    const float panelWidth = std::max(360.0f * uiScale_, minRequiredWidth);
    const float startPosX = ImGui::GetCursorPosX();

    ImGui::PushStyleColor(ImGuiCol_Text, Foundation::Color(Foundation::EColor::Text));
    ImGui::TextUnformatted(titleText.c_str());
    ImGui::PopStyleColor();

    ImGui::SameLine(startPosX + panelWidth - groupTextWidth);
    ImGui::PushStyleColor(ImGuiCol_Text, Foundation::Color(Foundation::EColor::TextMuted));
    ImGui::TextUnformatted(groupProgressText.c_str());
    ImGui::PopStyleColor();
    ImGui::PopFont();

    // Line 2: Minimal Progress Bar (3px height)
    const float totalRenderers = static_cast<float>(std::max(pipelineWarmup.totalRenderers, 1u));
    const float progressFraction = std::clamp(static_cast<float>(pipelineWarmup.completedRenderers) / totalRenderers, 0.0f, 1.0f);
    const float barHeight = std::max(3.0f * uiScale_, 3.0f);
    const ImVec2 barPos = ImGui::GetCursorScreenPos();
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    drawList->AddRectFilled(
        barPos,
        ImVec2(barPos.x + panelWidth, barPos.y + barHeight),
        Foundation::ColorU32(Foundation::EColor::BorderStrong, 0.45f),
        1.5f * uiScale_);

    if (progressFraction > 0.0f)
    {
        const float fillWidth = std::max(2.0f, panelWidth * progressFraction);
        drawList->AddRectFilled(
            barPos,
            ImVec2(barPos.x + fillWidth, barPos.y + barHeight),
            Foundation::ColorU32(Foundation::EColor::AccentHover),
            1.5f * uiScale_);
    }
    ImGui::Dummy(ImVec2(panelWidth, barHeight));

    // Line 3: Active Renderer Stage
    const std::string activeRenderer = pipelineWarmup.currentRenderer.empty()
        ? "Preparing pipeline cache..."
        : pipelineWarmup.currentRenderer;

    ImGui::PushStyleColor(ImGuiCol_Text, Foundation::Color(Foundation::EColor::AccentHover));
    ImGui::TextUnformatted(ICON_FA_SLIDERS);
    ImGui::PopStyleColor();
    ImGui::SameLine(0.0f, 8.0f * uiScale_);
    ImGui::PushStyleColor(ImGuiCol_Text, Foundation::Color(Foundation::EColor::Text));
    ImGui::TextUnformatted(activeRenderer.c_str());
    ImGui::PopStyleColor();

    // Line 4: Pipelines Count & Baseline Detail
    ImGui::PushStyleColor(ImGuiCol_Text, Foundation::Color(Foundation::EColor::TextDim));
    ImGui::TextUnformatted(ICON_FA_BOLT);
    ImGui::PopStyleColor();
    ImGui::SameLine(0.0f, 8.0f * uiScale_);

    std::string statsText = fmt::format("{} pipelines warmed", pipelineWarmup.completedPipelines);
    if (pipelineWarmup.pipelinesCreatedBeforeWarmup > 0)
    {
        statsText += fmt::format(" ({} baseline)", pipelineWarmup.pipelinesCreatedBeforeWarmup);
    }
    ImGui::PushStyleColor(ImGuiCol_Text, Foundation::Color(Foundation::EColor::TextMuted));
    ImGui::TextUnformatted(statsText.c_str());
    ImGui::PopStyleColor();

    ImGui::End();
    ImGui::PopStyleVar();
}


}
