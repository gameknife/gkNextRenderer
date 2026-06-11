#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Rendering/Upscaler/StreamlineIntegration.hpp"
#include "Engine/Rendering/VulkanBaseRenderer.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/Config/UserSettings.hpp"
#include "Engine/Vulkan/Device.hpp"
#include "Engine/Vulkan/Instance.hpp"
#include "Engine/Vulkan/SwapChain.hpp"
#include "Engine/Vulkan/GpuResources.hpp"


#if WITH_STREAMLINE && WIN32
#include <dxgi1_6.h>
#endif

#if WITH_STREAMLINE
#include <sl.h>
#include <sl_consts.h>
#include <sl_dlss.h>
#include <sl_dlss_d.h>
#include <sl_helpers_vk.h>
#endif

#if WITH_STREAMLINE
static sl::Resource toSlResource(const Vulkan::Image& image, VkDeviceMemory memory, VkImageView view, VkImageLayout layout)
{
    sl::Resource res(sl::ResourceType::eTex2d, (void*)image.Handle(), (void*)memory, (void*)view, (uint32_t)layout);
    res.width = image.Extent().width;
    res.height = image.Extent().height;
    res.nativeFormat = (uint32_t)image.Format();
    res.mipLevels = 1;
    res.arrayLayers = 1;
    return res;
}

static sl::float4x4 toSlMatrix(const glm::mat4& m)
{
    sl::float4x4 res;
    res.row[0] = sl::float4(m[0][0], m[1][0], m[2][0], m[3][0]);
    res.row[1] = sl::float4(m[0][1], m[1][1], m[2][1], m[3][1]);
    res.row[2] = sl::float4(m[0][2], m[1][2], m[2][2], m[3][2]);
    res.row[3] = sl::float4(m[0][3], m[1][3], m[2][3], m[3][3]);
    return res;
}

static bool HasNvidiaAdapter()
{
#if WIN32
    HMODULE dxgiModule = LoadLibraryW(L"dxgi.dll");
    if (!dxgiModule)
    {
        return false;
    }

    using CreateDXGIFactory1Fn = HRESULT(WINAPI*)(REFIID, void**);
    auto createFactory = reinterpret_cast<CreateDXGIFactory1Fn>(
        GetProcAddress(dxgiModule, "CreateDXGIFactory1"));
    if (!createFactory)
    {
        FreeLibrary(dxgiModule);
        return false;
    }

    IDXGIFactory1* factory = nullptr;
    if (FAILED(createFactory(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory))))
    {
        FreeLibrary(dxgiModule);
        return false;
    }

    bool hasNvidiaAdapter = false;
    for (UINT adapterIndex = 0;; ++adapterIndex)
    {
        IDXGIAdapter1* adapter = nullptr;
        const HRESULT result = factory->EnumAdapters1(adapterIndex, &adapter);
        if (result == DXGI_ERROR_NOT_FOUND)
        {
            break;
        }
        if (FAILED(result))
        {
            continue;
        }

        DXGI_ADAPTER_DESC1 desc{};
        if (SUCCEEDED(adapter->GetDesc1(&desc)) && desc.VendorId == 0x10DE)
        {
            hasNvidiaAdapter = true;
        }
        adapter->Release();

        if (hasNvidiaAdapter)
        {
            break;
        }
    }

    factory->Release();
    FreeLibrary(dxgiModule);
    return hasNvidiaAdapter;
#else
    return false;
#endif
}
#endif

namespace StreamlineWrapper
{
    bool GStreamLineInit = false;
    bool GStreamLineInitAttempted = false;
    bool GStreamLineEnabled = false;
    bool GStreamLineVulkanInfoSet = false;

    bool ShouldInitialize()
    {
#if WITH_STREAMLINE
        return HasNvidiaAdapter();
#else
        return false;
#endif
    }

    void Initialize()
    {
#if WITH_STREAMLINE
        if (GStreamLineInitAttempted)
        {
            return;
        }
        GStreamLineInitAttempted = true;

        sl::Preferences pref{};
        //pref.showConsole = true; // for debugging, set to false in production
        //pref.logLevel = sl::LogLevel::eVerbose;
        pref.pathsToPlugins = {}; // change this if Streamline plugins are not located next to the executable
        pref.numPathsToPlugins = 0; // change this if Streamline plugins are not located next to the executable
        pref.pathToLogsAndData = {}; // change this to enable logging to a file
        //pref.logMessageCallback = myLogMessageCallback; // highly recommended to track warning/error messages in your callback
        pref.applicationId = 12345678; // Provided by NVDA, required if using NGX components (DLSS 2/3)
        pref.engine = sl::EngineType::eCustom; // If using UE or Unity
        pref.engineVersion = "1.0.0"; // Optional version
        pref.projectId = "36cf6361-1044-4603-9ef3-066606660666"; // Optional project id (GUID format)
        pref.flags |= sl::PreferenceFlags::eUseFrameBasedResourceTagging;

        sl::Feature features[] = { sl::kFeatureDLSS, sl::kFeatureDLSS_RR };
        pref.featuresToLoad = features;
        pref.numFeaturesToLoad = sizeof(features) / sizeof(sl::Feature);
        //pref.renderAPI = sl::RenderAPI::eVulkan;

        sl::Result res;
        if (SL_FAILED(res, slInit(pref)))
        {
            SPDLOG_ERROR("Streamline slInit failed: {}", (int)res);
            return;
        }

        GStreamLineInit = true;
        GStreamLineEnabled = true;
#endif
    }

   void LazyInit(VkDevice device, VkInstance instance, VkPhysicalDevice physicalDevice, uint32_t computeQueueIdx, uint32_t computeQueueFamily, uint32_t graphicsQueueIdx, uint32_t graphicsQueueFamily, bool& outSupportDLSS, bool& outSupportDLSSRR)
   {
#if WITH_STREAMLINE
       Initialize();
       if (!GStreamLineInit)
       {
           outSupportDLSS = false;
           outSupportDLSSRR = false;
           return;
       }

       if (GStreamLineVulkanInfoSet)
       {
           return;
       }
       GStreamLineVulkanInfoSet = true;

       sl::Result res;
       sl::VulkanInfo slVulkanInfo{};
       slVulkanInfo.device = device;
       slVulkanInfo.instance = instance;
       slVulkanInfo.physicalDevice = physicalDevice;
       slVulkanInfo.computeQueueIndex = computeQueueIdx;
       slVulkanInfo.computeQueueFamily = computeQueueFamily;
       slVulkanInfo.graphicsQueueIndex = graphicsQueueIdx;
       slVulkanInfo.graphicsQueueFamily = graphicsQueueFamily;
       
       if(SL_FAILED(res, slSetVulkanInfo(slVulkanInfo)))
        {
            SPDLOG_ERROR("Streamline slSetVulkanInfo failed: {}", (int)res);
        }
       else
       {
            SPDLOG_INFO("Streamline Initialized Successfully.");
            
            sl::AdapterInfo adapterInfo{};
            adapterInfo.vkPhysicalDevice = physicalDevice;
            
            sl::Result checkRes = slIsFeatureSupported(sl::kFeatureDLSS, adapterInfo);
            outSupportDLSS = (checkRes == sl::Result::eOk);
            
            checkRes = slIsFeatureSupported(sl::kFeatureDLSS_RR, adapterInfo);
            outSupportDLSSRR = (checkRes == sl::Result::eOk);
            
            SPDLOG_INFO("DLSS Support: {}, RR Support: {}", outSupportDLSS, outSupportDLSSRR);
       }
#else
       outSupportDLSS = false;
       outSupportDLSSRR = false;
#endif
   }


    void Shutdown()
   {
#if WITH_STREAMLINE
       if (GStreamLineEnabled)
       {
           sl::Result res;
           if(SL_FAILED(res, slShutdown()))
           {
               SPDLOG_ERROR("Streamline slShutdown failed: {}", (int)res);
           }
       }
#endif
   }
}


namespace Vulkan
{
    void VulkanBaseRenderer::UpdateStreamline(VkCommandBuffer commandBuffer, uint32_t imageIndex)
    {
#if WITH_STREAMLINE
        if (!caps_.supportDLSS) return;
        
        StreamlineWrapper::LazyInit(ctx_.device->Handle(), ctx_.instance->Handle(), ctx_.device->PhysicalDevice(), 0, ctx_.device->ComputeFamilyIndex(), 0, ctx_.device->GraphicsFamilyIndex(), caps_.supportDLSS, caps_.supportDLSSRR);

        auto& settings = NextEngine::GetInstance()->GetUserSettings();
        
        sl::ViewportHandle viewport(0);
        sl::FrameToken* frameToken;
        uint32_t uintFrameCount = (uint32_t)frame_.frameCount;
        if (SL_FAILED(res0, slGetNewFrameToken(frameToken, &uintFrameCount)))
        {
            SPDLOG_ERROR("slGetNewFrameToken failed: {}", (int)res0);
            return;
        }

        bool useDLSSRR = SupportDLSSRR() && settings.DLSSRR;
        
        // 1. DLSS Runtime::Config::Options
        if (useDLSSRR)
        {
            sl::DLSSDOptions dlssOptions;
            switch (settings.SuperResolution)
            {
                case 0: dlssOptions.mode = sl::DLSSMode::eMaxQuality; break;
                case 1: dlssOptions.mode = sl::DLSSMode::eBalanced; break;
                case 2: dlssOptions.mode = sl::DLSSMode::eMaxPerformance; break;
                case 3: dlssOptions.mode = sl::DLSSMode::eUltraPerformance; break;
                case 4: dlssOptions.mode = sl::DLSSMode::eDLAA; break;
                default: dlssOptions.mode = sl::DLSSMode::eBalanced; break;
            }
            dlssOptions.dlaaPreset = sl::DLSSDPreset::ePresetE;
            dlssOptions.qualityPreset = sl::DLSSDPreset::ePresetE;
            dlssOptions.balancedPreset = sl::DLSSDPreset::ePresetE;
            dlssOptions.performancePreset = sl::DLSSDPreset::ePresetE;
            dlssOptions.ultraPerformancePreset = sl::DLSSDPreset::ePresetE;
            dlssOptions.outputWidth = SwapChain().Extent().width;
            dlssOptions.outputHeight = SwapChain().Extent().height;
            dlssOptions.colorBuffersHDR = sl::Boolean::eTrue;
            dlssOptions.normalRoughnessMode = sl::DLSSDNormalRoughnessMode::ePacked;
        
            if (SL_FAILED(res1, slDLSSDSetOptions(viewport, dlssOptions)))
            {
                SPDLOG_ERROR("slDLSSDSetOptions failed: {}", (int)res1);
            }
        }
        else
        {
            sl::DLSSOptions dlssOptions;
            switch (settings.SuperResolution)
            {
                case 0: dlssOptions.mode = sl::DLSSMode::eMaxQuality; break;
                case 1: dlssOptions.mode = sl::DLSSMode::eBalanced; break;
                case 2: dlssOptions.mode = sl::DLSSMode::eMaxPerformance; break;
                case 3: dlssOptions.mode = sl::DLSSMode::eUltraPerformance; break;
                case 4: dlssOptions.mode = sl::DLSSMode::eDLAA; break;
                default: dlssOptions.mode = sl::DLSSMode::eBalanced; break;
            }
            dlssOptions.outputWidth = SwapChain().Extent().width;
            dlssOptions.outputHeight = SwapChain().Extent().height;
            dlssOptions.colorBuffersHDR = sl::Boolean::eTrue;
            
            if (SL_FAILED(res1, slDLSSSetOptions(viewport, dlssOptions)))
            {
                SPDLOG_ERROR("slDLSSSetOptions failed: {}", (int)res1);
            }
        }

        // 2. Constants
        sl::Constants constants{};
        constants.cameraViewToClip = toSlMatrix(frame_.lastUBO.ProjectionUnJit);
        constants.clipToCameraView = toSlMatrix(frame_.lastUBO.ProjectionInverseUnJit);
        constants.clipToPrevClip = toSlMatrix(frame_.lastUBO.PrevViewProjectionUnJit * frame_.lastUBO.ModelViewInverse * frame_.lastUBO.ProjectionInverseUnJit);
        constants.prevClipToClip = toSlMatrix(frame_.lastUBO.ProjectionUnJit * frame_.lastUBO.ModelView * frame_.lastUBO.PrevViewProjectionUnJit);
        
        constants.jitterOffset = sl::float2(frame_.lastUBO.Jitter.x, frame_.lastUBO.Jitter.y);
        constants.mvecScale = {1.0f / (float)SwapChain().RenderExtent().width,1.0f / (float)SwapChain().RenderExtent().height}; 
        
        constants.cameraPos = sl::float3(frame_.lastUBO.ModelViewInverse[3][0], frame_.lastUBO.ModelViewInverse[3][1], frame_.lastUBO.ModelViewInverse[3][2]);
        constants.cameraFwd = sl::float3(-frame_.lastUBO.ModelViewInverse[2][0], -frame_.lastUBO.ModelViewInverse[2][1], -frame_.lastUBO.ModelViewInverse[2][2]);
        constants.cameraUp = sl::float3(frame_.lastUBO.ModelViewInverse[1][0], frame_.lastUBO.ModelViewInverse[1][1], frame_.lastUBO.ModelViewInverse[1][2]);
        constants.cameraRight = sl::float3(frame_.lastUBO.ModelViewInverse[0][0], frame_.lastUBO.ModelViewInverse[0][1], frame_.lastUBO.ModelViewInverse[0][2]);
        
        auto& camera = GetScene().GetRenderCamera();
        constants.cameraNear = camera.NearPlane;
        constants.cameraFar = camera.FarPlane;
        constants.cameraFOV = glm::radians(camera.FieldOfView); 
        constants.cameraAspectRatio = (float)SwapChain().Extent().width / (float)SwapChain().Extent().height;
        
        constants.depthInverted = sl::Boolean::eFalse;
        constants.cameraMotionIncluded = sl::Boolean::eTrue;
        constants.motionVectors3D = sl::Boolean::eFalse;
        constants.reset = frame_.frameCount < 2 ? sl::Boolean::eTrue : sl::Boolean::eFalse;
        constants.cameraPinholeOffset = sl::float2(0.0f, 0.0f);
        
        if (SL_FAILED(res2, slSetConstants(constants, *frameToken, viewport)))
        {
            SPDLOG_ERROR("slSetConstants failed: {}", (int)res2);
        }

        // 3. Tags
        // Depth
        auto slDepth = toSlResource(frame_.depthBuffer->GetImage(), frame_.depthBuffer->GetImageMemory().Handle(), frame_.depthBuffer->ImageView().Handle(), VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
        slDepth.width = SwapChain().RenderExtent().width;
        slDepth.height = SwapChain().RenderExtent().height;
        sl::ResourceTag tagDepth(&slDepth, sl::kBufferTypeDepth, sl::eOnlyValidNow);
        slSetTagForFrame(*frameToken, viewport, &tagDepth, 1, commandBuffer);

        // Motion Vectors
        auto& resMV = bindless_.images[Assets::Bindless::RT_MOTIONVECTOR];
        auto slMV = toSlResource(resMV->GetImage(), resMV->GetImageMemory().Handle(), resMV->GetImageView().Handle(), VK_IMAGE_LAYOUT_GENERAL);
        sl::ResourceTag tagMV(&slMV, sl::kBufferTypeMotionVectors, sl::eOnlyValidNow);
        slSetTagForFrame(*frameToken, viewport, &tagMV, 1, commandBuffer);

        // Scaling Input (Color)
        auto& resInput = bindless_.images[Assets::Bindless::RT_DENOISED];
        auto slInput = toSlResource(resInput->GetImage(), resInput->GetImageMemory().Handle(), resInput->GetImageView().Handle(), VK_IMAGE_LAYOUT_GENERAL);
        sl::ResourceTag tagInput(&slInput, sl::kBufferTypeScalingInputColor, sl::eOnlyValidNow);
        slSetTagForFrame(*frameToken, viewport, &tagInput, 1, commandBuffer);

        // Scaling Output
        sl::Resource slOutput(sl::ResourceType::eTex2d, (void*)frame_.swapChain->Images()[imageIndex], nullptr, (void*)frame_.swapChain->ImageViews()[imageIndex]->Handle(), (uint32_t)VK_IMAGE_LAYOUT_GENERAL);
        slOutput.width = SwapChain().Extent().width;
        slOutput.height = SwapChain().Extent().height;
        slOutput.nativeFormat = (uint32_t)frame_.swapChain->Format();
        sl::ResourceTag tagOutput(&slOutput, sl::kBufferTypeScalingOutputColor, sl::eOnlyValidNow);
        slSetTagForFrame(*frameToken, viewport, &tagOutput, 1, commandBuffer);
        
        if (useDLSSRR)
        {
            // Albedo
            auto& resAlbedo = bindless_.images[Assets::Bindless::RT_ALBEDO];
            auto slAlbedo = toSlResource(resAlbedo->GetImage(), resAlbedo->GetImageMemory().Handle(), resAlbedo->GetImageView().Handle(), VK_IMAGE_LAYOUT_GENERAL);
            sl::ResourceTag tagAlbedo(&slAlbedo, sl::kBufferTypeAlbedo, sl::eOnlyValidNow);
            slSetTagForFrame(*frameToken, viewport, &tagAlbedo, 1, commandBuffer);

            // Specular Albedo
            auto& resSpecAlbedo = bindless_.images[Assets::Bindless::RT_SPECULAR_ALBEDO];
            auto slSpecAlbedo = toSlResource(resSpecAlbedo->GetImage(), resSpecAlbedo->GetImageMemory().Handle(), resSpecAlbedo->GetImageView().Handle(), VK_IMAGE_LAYOUT_GENERAL);
            sl::ResourceTag tagSpecAlbedo(&slSpecAlbedo, sl::kBufferTypeSpecularAlbedo, sl::eOnlyValidNow);
            slSetTagForFrame(*frameToken, viewport, &tagSpecAlbedo, 1, commandBuffer);

            // Normals
            auto& resNormal = bindless_.images[Assets::Bindless::RT_NORMAL];
            auto slNormal = toSlResource(resNormal->GetImage(), resNormal->GetImageMemory().Handle(), resNormal->GetImageView().Handle(), VK_IMAGE_LAYOUT_GENERAL);
            sl::ResourceTag tagNormal(&slNormal, sl::kBufferTypeNormalRoughness, sl::eOnlyValidNow);
            slSetTagForFrame(*frameToken, viewport, &tagNormal, 1, commandBuffer);
            
            // auto& resMV = bindless_.images[Assets::Bindless::RT_MOTIONVECTOR];
            // auto slMV = toSlResource(resMV->GetImage(), resMV->GetImageMemory().Handle(), resMV->GetImageView().Handle(), VK_IMAGE_LAYOUT_GENERAL);
            // sl::ResourceTag tagMV(&slMV, sl::kBufferTypeSpecularMotionVectors, sl::eOnlyValidNow);
            // slSetTagForFrame(*frameToken, viewport, &tagMV, 1, commandBuffer);

            // Diffuse Noisy
            // auto& resDiffNoisy = bindless_.images[Assets::Bindless::RT_ACCUMLATE_DIFFUSE];
            // auto slDiffNoisy = toSlResource(resDiffNoisy->GetImage(), resDiffNoisy->GetImageMemory().Handle(), resDiffNoisy->GetImageView().Handle(), VK_IMAGE_LAYOUT_GENERAL);
            // sl::ResourceTag tagDiffNoisy(&slDiffNoisy, sl::kBufferTypeDiffuseHitNoisy, sl::eOnlyValidNow);
            // slSetTagForFrame(*frameToken, viewport, &tagDiffNoisy, 1, commandBuffer);
            //
            // // Specular Noisy
            // auto& resSpecNoisy = bindless_.images[Assets::Bindless::RT_ACCUMLATE_SPECULAR];
            // auto slSpecNoisy = toSlResource(resSpecNoisy->GetImage(), resSpecNoisy->GetImageMemory().Handle(), resSpecNoisy->GetImageView().Handle(), VK_IMAGE_LAYOUT_GENERAL);
            // sl::ResourceTag tagSpecNoisy(&slSpecNoisy, sl::kBufferTypeSpecularHitNoisy, sl::eOnlyValidNow);
            // slSetTagForFrame(*frameToken, viewport, &tagSpecNoisy, 1, commandBuffer);

            // Diffuse Hit Dist
            auto& resDiffHitDist = bindless_.images[Assets::Bindless::RT_DIFFUSE_HITDIST];
            auto slDiffHitDist = toSlResource(resDiffHitDist->GetImage(), resDiffHitDist->GetImageMemory().Handle(), resDiffHitDist->GetImageView().Handle(), VK_IMAGE_LAYOUT_GENERAL);
            sl::ResourceTag tagDiffHitDist(&slDiffHitDist, sl::kBufferTypeDiffuseHitDistance, sl::eOnlyValidNow);
            slSetTagForFrame(*frameToken, viewport, &tagDiffHitDist, 1, commandBuffer);
            //
            // // Specular Hit Dist
            // auto& resSpecHitDist = bindless_.images[Assets::Bindless::RT_SPECULAR_HITDIST];
            // auto slSpecHitDist = toSlResource(resSpecHitDist->GetImage(), resSpecHitDist->GetImageMemory().Handle(), resSpecHitDist->GetImageView().Handle(), VK_IMAGE_LAYOUT_GENERAL);
            // sl::ResourceTag tagSpecHitDist(&slSpecHitDist, sl::kBufferTypeSpecularHitDistance, sl::eOnlyValidNow);
            // slSetTagForFrame(*frameToken, viewport, &tagSpecHitDist, 1, commandBuffer);
        }

        // 4. Evaluate
        const sl::BaseStructure* inputs[] = { &viewport };
        if (SL_FAILED(res3, slEvaluateFeature(useDLSSRR ? sl::kFeatureDLSS_RR : sl::kFeatureDLSS, *frameToken, inputs, 1, commandBuffer)))
        {
            SPDLOG_ERROR("slEvaluateFeature DLSS failed: {}", (int)res3);
        }
#endif
    }

}
