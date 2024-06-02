#include "Application.hpp"
#include "UserInterface.hpp"
#include "UserSettings.hpp"
#include "Assets/Model.hpp"
#include "Assets/Scene.hpp"
#include "Assets/Texture.hpp"
#include "Assets/UniformBuffer.hpp"
#include "Utilities/Exception.hpp"
#include "Utilities/Glm.hpp"
#include "Vulkan/Window.hpp"
#include "Vulkan/SwapChain.hpp"
#include "Vulkan/Device.hpp"
#include <iostream>
#include <sstream>

#include "Options.hpp"
#include "curl/curl.h"
#include "avif/avif.h"
#include "cpp-base64/base64.cpp"
#include "ThirdParty/json11/json11.hpp"

namespace
{
    const bool EnableValidationLayers =
#ifdef NDEBUG
        false;
#else
        true;
#endif
}

template <typename Renderer>
NextRendererApplication<Renderer>::NextRendererApplication(const UserSettings& userSettings,
                                                           const Vulkan::WindowConfig& windowConfig,
                                                           const VkPresentModeKHR presentMode) :
    Renderer(windowConfig, presentMode, EnableValidationLayers),
    userSettings_(userSettings)
{
    CheckFramebufferSize();
}

template <typename Renderer>
NextRendererApplication<Renderer>::~NextRendererApplication()
{
    scene_.reset();
}

template <typename Renderer>
Assets::UniformBufferObject NextRendererApplication<Renderer>::GetUniformBufferObject(const VkExtent2D extent) const
{
    const auto& init = cameraInitialSate_;

    Assets::UniformBufferObject ubo = {};
    ubo.ModelView = modelViewController_.ModelView();
    ubo.Projection = glm::perspective(glm::radians(userSettings_.FieldOfView),
                                      extent.width / static_cast<float>(extent.height), 0.1f, 10000.0f);
    ubo.Projection[1][1] *= -1;
    // Inverting Y for Vulkan, https://matthewwellings.com/blog/the-new-vulkan-coordinate-system/
    ubo.ModelViewInverse = glm::inverse(ubo.ModelView);
    ubo.ProjectionInverse = glm::inverse(ubo.Projection);
    ubo.ViewProjection = ubo.Projection * ubo.ModelView;
    ubo.PrevViewProjection = prevUBO_.RandomSeed != 0 ? prevUBO_.ViewProjection : ubo.ViewProjection;

    ubo.Aperture = userSettings_.Aperture;
    ubo.FocusDistance = userSettings_.FocusDistance;
    ubo.SkyRotation = userSettings_.SkyRotation;
    ubo.TotalNumberOfSamples = totalNumberOfSamples_;
    ubo.TotalFrames = totalFrames_;
    ubo.NumberOfSamples = numberOfSamples_;
    ubo.NumberOfBounces = userSettings_.NumberOfBounces;
    ubo.RandomSeed = rand();
    ubo.HasSky = init.HasSky;
    ubo.ShowHeatmap = userSettings_.ShowHeatmap;
    ubo.HeatmapScale = userSettings_.HeatmapScale;
    ubo.UseCheckerBoard = userSettings_.UseCheckerBoardRendering;
    ubo.TemporalFrames = userSettings_.TemporalFrames;

    ubo.ColorPhi = userSettings_.ColorPhi;
    ubo.DepthPhi = userSettings_.DepthPhi;
    ubo.NormalPhi = userSettings_.NormalPhi;
    ubo.PaperWhiteNit = userSettings_.PaperWhiteNit;

    ubo.LightCount = scene_->GetLightCount();

    prevUBO_ = ubo;

    return ubo;
}

template <typename Renderer>
void NextRendererApplication<Renderer>::SetPhysicalDeviceImpl(
    VkPhysicalDevice physicalDevice,
    std::vector<const char*>& requiredExtensions,
    VkPhysicalDeviceFeatures& deviceFeatures,
    void* nextDeviceFeatures)
{
    // Required extensions. windows only
#if WIN32
    requiredExtensions.insert(requiredExtensions.end(),
                              {
                                  // VK_KHR_SHADER_CLOCK is required for heatmap
                                  VK_KHR_SHADER_CLOCK_EXTENSION_NAME
                              });
#endif

    // Opt-in into mandatory device features.
    VkPhysicalDeviceShaderClockFeaturesKHR shaderClockFeatures = {};
    shaderClockFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_CLOCK_FEATURES_KHR;
    shaderClockFeatures.pNext = nextDeviceFeatures;
    shaderClockFeatures.shaderSubgroupClock = true;

    deviceFeatures.fillModeNonSolid = true;
    deviceFeatures.samplerAnisotropy = true;
    deviceFeatures.shaderInt64 = true;

    Renderer::SetPhysicalDeviceImpl(physicalDevice, requiredExtensions, deviceFeatures, &shaderClockFeatures);
}

template <typename Renderer>
void NextRendererApplication<Renderer>::OnDeviceSet()
{
    Renderer::OnDeviceSet();

    LoadScene(userSettings_.SceneIndex);

    Renderer::OnPostLoadScene();
}

template <typename Renderer>
void NextRendererApplication<Renderer>::CreateSwapChain()
{
    Renderer::CreateSwapChain();

    userInterface_.reset(new UserInterface(Renderer::CommandPool(), Renderer::SwapChain(), Renderer::DepthBuffer(),
                                           userSettings_));
    resetAccumulation_ = true;

    CheckFramebufferSize();
}

template <typename Renderer>
void NextRendererApplication<Renderer>::DeleteSwapChain()
{
    userInterface_.reset();

    Renderer::DeleteSwapChain();
}

template <typename Renderer>
void NextRendererApplication<Renderer>::DrawFrame()
{
    // Check if the scene has been changed by the user.
    if (sceneIndex_ != static_cast<uint32_t>(userSettings_.SceneIndex))
    {
        Renderer::Device().WaitIdle();
        DeleteSwapChain();
        Renderer::OnPreLoadScene();
        LoadScene(userSettings_.SceneIndex);
        Renderer::OnPostLoadScene();
        CreateSwapChain();
        return;
    }

    // Check if the accumulation buffer needs to be reset.
    if (resetAccumulation_ ||
        userSettings_.RequiresAccumulationReset(previousSettings_) ||
        !userSettings_.AccumulateRays)
    {
        totalNumberOfSamples_ = 0;
        resetAccumulation_ = false;
    }

    previousSettings_ = userSettings_;

    // Keep track of our sample count.
    numberOfSamples_ = glm::clamp(userSettings_.MaxNumberOfSamples - totalNumberOfSamples_, 0u,
                                  userSettings_.NumberOfSamples);
    totalNumberOfSamples_ += numberOfSamples_;

    Renderer::DrawFrame();

    totalFrames_ += 1;
}

template <typename Renderer>
void NextRendererApplication<Renderer>::Render(VkCommandBuffer commandBuffer, const uint32_t imageIndex)
{
    // Record delta time between calls to Render.
    const auto prevTime = time_;
    time_ = Renderer::Window().GetTime();
    const auto timeDelta = time_ - prevTime;

    // Update the camera position / angle.
    resetAccumulation_ = modelViewController_.UpdateCamera(cameraInitialSate_.ControlSpeed, timeDelta);

    // Check the current state of the benchmark, update it for the new frame.
    CheckAndUpdateBenchmarkState(prevTime);

    Renderer::denoiseIteration_ = userSettings_.DenoiseIteration;
    Renderer::checkerboxRendering_ = userSettings_.UseCheckerBoardRendering;

    // Render the scene
    Renderer::Render(commandBuffer, imageIndex);

    // Render the UI
    Statistics stats = {};
    stats.FramebufferSize = Renderer::Window().FramebufferSize();
    stats.FrameRate = static_cast<float>(1 / timeDelta);

    stats.CamPosX = modelViewController_.Position()[0];
    stats.CamPosY = modelViewController_.Position()[1];
    stats.CamPosZ = modelViewController_.Position()[2];

    if (userSettings_.IsRayTraced)
    {
        const auto extent = Renderer::SwapChain().Extent();

        stats.RayRate = static_cast<float>(
            double(extent.width * extent.height) * numberOfSamples_
            / (timeDelta * 1000000000));

        stats.TotalSamples = totalNumberOfSamples_;
    }

    userInterface_->Render(commandBuffer, Renderer::SwapChainFrameBuffer(imageIndex), stats);
}

template <typename Renderer>
void NextRendererApplication<Renderer>::OnKey(int key, int scancode, int action, int mods)
{
    if (userInterface_->WantsToCaptureKeyboard())
    {
        return;
    }

    if (action == GLFW_PRESS)
    {
        switch (key)
        {
        case GLFW_KEY_ESCAPE: Renderer::Window().Close();
            break;
        default: break;
        }

        // Settings (toggle switches)
        if (!userSettings_.Benchmark)
        {
            switch (key)
            {
            case GLFW_KEY_F1: userSettings_.ShowSettings = !userSettings_.ShowSettings;
                break;
            case GLFW_KEY_F2: userSettings_.ShowOverlay = !userSettings_.ShowOverlay;
                break;
            case GLFW_KEY_R: userSettings_.IsRayTraced = !userSettings_.IsRayTraced;
                break;
            case GLFW_KEY_H: userSettings_.ShowHeatmap = !userSettings_.ShowHeatmap;
                break;
            default: break;
            }
        }
    }

    // Camera motions
    if (!userSettings_.Benchmark)
    {
        resetAccumulation_ |= modelViewController_.OnKey(key, scancode, action, mods);
    }
}

template <typename Renderer>
void NextRendererApplication<Renderer>::OnCursorPosition(const double xpos, const double ypos)
{
    if (!Renderer::HasSwapChain() ||
        userSettings_.Benchmark ||
        userInterface_->WantsToCaptureKeyboard() ||
        userInterface_->WantsToCaptureMouse())
    {
        return;
    }

    // Camera motions
    resetAccumulation_ |= modelViewController_.OnCursorPosition(xpos, ypos);
}

template <typename Renderer>
void NextRendererApplication<Renderer>::OnMouseButton(const int button, const int action, const int mods)
{
    if (!Renderer::HasSwapChain() ||
        userSettings_.Benchmark ||
        userInterface_->WantsToCaptureMouse())
    {
        return;
    }

    // Camera motions
    resetAccumulation_ |= modelViewController_.OnMouseButton(button, action, mods);
}

template <typename Renderer>
void NextRendererApplication<Renderer>::OnScroll(const double xoffset, const double yoffset)
{
    if (!Renderer::HasSwapChain() ||
        userSettings_.Benchmark ||
        userInterface_->WantsToCaptureMouse())
    {
        return;
    }

    const auto prevFov = userSettings_.FieldOfView;
    userSettings_.FieldOfView = std::clamp(
        static_cast<float>(prevFov - yoffset),
        UserSettings::FieldOfViewMinValue,
        UserSettings::FieldOfViewMaxValue);

    resetAccumulation_ = prevFov != userSettings_.FieldOfView;
}

template <typename Renderer>
void NextRendererApplication<Renderer>::LoadScene(const uint32_t sceneIndex)
{
    std::vector<Assets::Model> models;
    std::vector<Assets::Texture> textures;
    std::vector<Assets::Node> nodes;
    std::vector<Assets::Material> materials;
    std::vector<Assets::LightObject> lights;

    // texture id 0: global sky
    textures.push_back(Assets::Texture::LoadHDRTexture("../assets/textures/StinsonBeach.hdr", Vulkan::SamplerConfig()));

    SceneList::AllScenes[sceneIndex].second(cameraInitialSate_, nodes, models, textures, materials, lights);

    // If there are no texture, add a dummy one. It makes the pipeline setup a lot easier.
    if (textures.empty())
    {
        textures.push_back(Assets::Texture::LoadTexture("../assets/textures/white.png", Vulkan::SamplerConfig()));
    }

    scene_.reset(new Assets::Scene(Renderer::CommandPool(), std::move(nodes), std::move(models), std::move(textures),
                                   std::move(materials), std::move(lights), Renderer::supportRayTracing_));
    sceneIndex_ = sceneIndex;

    userSettings_.FieldOfView = cameraInitialSate_.FieldOfView;
    userSettings_.Aperture = cameraInitialSate_.Aperture;
    userSettings_.FocusDistance = cameraInitialSate_.FocusDistance;

    modelViewController_.Reset(cameraInitialSate_.ModelView);

    periodTotalFrames_ = 0;
    benchmarkTotalFrames_ = 0;
    resetAccumulation_ = true;
    sceneInitialTime_ = Renderer::Window().GetTime();
}

template <typename Renderer>
void NextRendererApplication<Renderer>::CheckAndUpdateBenchmarkState(double prevTime)
{
    if (!userSettings_.Benchmark)
    {
        return;
    }

    // Initialise scene benchmark timers
    if (periodTotalFrames_ == 0)
    {
        std::cout << std::endl;
        std::cout << "Renderer: " << Renderer::StaticClass() << std::endl;
        std::cout << "Benchmark: Start scene #" << sceneIndex_ << " '" << SceneList::AllScenes[sceneIndex_].first << "'"
            << std::endl;
        periodInitialTime_ = time_;
    }

    // Print out the frame rate at regular intervals.
    {
        const double period = 5;
        const double prevTotalTime = prevTime - periodInitialTime_;
        const double totalTime = time_ - periodInitialTime_;

        if (periodTotalFrames_ != 0 && static_cast<uint64_t>(prevTotalTime / period) != static_cast<uint64_t>(totalTime
            / period))
        {
            std::cout << "Benchmark: " << periodTotalFrames_ / totalTime << " fps" << std::endl;
            periodInitialTime_ = time_;
            periodTotalFrames_ = 0;
        }

        periodTotalFrames_++;
        benchmarkTotalFrames_++;
    }

    // If in benchmark mode, bail out from the scene if we've reached the time or sample limit.
    {
        const bool timeLimitReached = periodTotalFrames_ != 0 && Renderer::Window().GetTime() - sceneInitialTime_ >
            userSettings_.BenchmarkMaxTime;
        const bool sampleLimitReached = numberOfSamples_ == 0;

        if (timeLimitReached || sampleLimitReached)
        {
            {
                const double totalTime = time_ - sceneInitialTime_;
                std::string SceneName = SceneList::AllScenes[userSettings_.SceneIndex].first;
                double fps = benchmarkTotalFrames_ / totalTime;
                Report(static_cast<int>(floor(fps)), SceneName, false, GOption->SaveFile);
                
            }

            if (!userSettings_.BenchmarkNextScenes || static_cast<size_t>(userSettings_.SceneIndex) ==
                SceneList::AllScenes.size() - 1)
            {
                Renderer::Window().Close();
            }

            std::cout << std::endl;
            userSettings_.SceneIndex += 1;
        }
    }
}

template <typename Renderer>
void NextRendererApplication<Renderer>::CheckFramebufferSize() const
{
    // Check the framebuffer size when requesting a fullscreen window, as it's not guaranteed to match.
    const auto& cfg = Renderer::Window().Config();
    const auto fbSize = Renderer::Window().FramebufferSize();

    if (userSettings_.Benchmark && cfg.Fullscreen && (fbSize.width != cfg.Width || fbSize.height != cfg.Height))
    {
        std::ostringstream out;
        out << "framebuffer fullscreen size mismatch (requested: ";
        out << cfg.Width << "x" << cfg.Height;
        out << ", got: ";
        out << fbSize.width << "x" << fbSize.height << ")";

        Throw(std::runtime_error(out.str()));
    }
}

template <typename Renderer>
void NextRendererApplication<Renderer>::Report(int fps, const std::string& sceneName, bool upload_screen, bool save_screen)
{
    VkPhysicalDeviceDriverProperties driverProp{};
    driverProp.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES;

    VkPhysicalDeviceProperties2 deviceProp{};
    deviceProp.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    deviceProp.pNext = &driverProp;

    vkGetPhysicalDeviceProperties2(Renderer::Device().PhysicalDevice(), &deviceProp);

    std::string img_encoded = "";
    if (upload_screen || save_screen)
    {
        // screenshot stuffs
        const Vulkan::SwapChain& swapChain = Renderer::SwapChain();

        avifImage* image = avifImageCreate(swapChain.Extent().width, swapChain.Extent().height, 10,
                                           AVIF_PIXEL_FORMAT_YUV444); // these values dictate what goes into the final AVIF
        if (!image)
        {
            Throw(std::runtime_error("avif image creation failed"));
        }
        image->yuvRange = AVIF_RANGE_FULL;
        image->colorPrimaries = AVIF_COLOR_PRIMARIES_BT2020;
        image->transferCharacteristics = AVIF_TRANSFER_CHARACTERISTICS_SMPTE2084;
        image->matrixCoefficients = AVIF_MATRIX_COEFFICIENTS_IDENTITY;
        image->clli.maxCLL = userSettings_.PaperWhiteNit; //maxCLLNits;
        image->clli.maxPALL = 0; //maxFALLNits;

        avifEncoder* encoder = NULL;
        avifRWData avifOutput = AVIF_DATA_EMPTY;

        avifRGBImage rgbAvifImage{};
        avifRGBImageSetDefaults(&rgbAvifImage, image);
        rgbAvifImage.format = AVIF_RGB_FORMAT_BGR;
        rgbAvifImage.ignoreAlpha = AVIF_TRUE;

        //if (  VK_FORMAT_A2R10G10B10_UNORM_PACK32 )
        uint16_t* data = (uint16_t*)malloc(rgbAvifImage.width * rgbAvifImage.height * 3 * 2);
        {
            Vulkan::DeviceMemory* vkMemory = Renderer::GetScreenShotMemory();

            constexpr uint32_t kCompCnt = 3;
            int imageSize = swapChain.Extent().width * swapChain.Extent().height * kCompCnt;

            uint8_t* mappedData = (uint8_t*)vkMemory->Map(0, imageSize);

            for (uint32_t y = 0; y < swapChain.Extent().height; y++)
            {
                for (uint32_t x = 0; x < swapChain.Extent().width; x++)
                {
                    uint32_t* pInPixel = (uint32_t*)&mappedData[(y * swapChain.Extent().width * 4) + x * 4];
                    uint32_t uInPixel = *pInPixel;

                    data[y * swapChain.Extent().width * kCompCnt + x * kCompCnt + 0] = (uInPixel & (0b1111111111 << 20)) >> 20;
                    data[y * swapChain.Extent().width * kCompCnt + x * kCompCnt + 1] = (uInPixel & (0b1111111111 << 10)) >> 10;
                    data[y * swapChain.Extent().width * kCompCnt + x * kCompCnt + 2] = (uInPixel & (0b1111111111 << 0)) >> 0;
                }
            }
            vkMemory->Unmap();
        }

        rgbAvifImage.pixels = (uint8_t*)data;
        rgbAvifImage.rowBytes = rgbAvifImage.width * 3 * sizeof(uint16_t);

        avifResult convertResult = avifImageRGBToYUV(image, &rgbAvifImage);
        if (convertResult != AVIF_RESULT_OK)
        {
            Throw(std::runtime_error("Failed to convert RGB to YUV: " + std::string(avifResultToString(convertResult))));
        }
        encoder = avifEncoderCreate();
        if (!encoder)
        {
            Throw(std::runtime_error("Failed to create encoder"));
        }
        encoder->quality = 80;
        encoder->qualityAlpha = AVIF_QUALITY_LOSSLESS;
        encoder->speed = AVIF_SPEED_FASTEST;

        avifResult addImageResult = avifEncoderAddImage(encoder, image, 1, AVIF_ADD_IMAGE_FLAG_SINGLE);
        if (addImageResult != AVIF_RESULT_OK)
        {
            Throw(std::runtime_error("Failed to add image: " + std::string(avifResultToString(addImageResult))));
        }
        avifResult finishResult = avifEncoderFinish(encoder, &avifOutput);
        if (finishResult != AVIF_RESULT_OK)
        {
            Throw(std::runtime_error("Failed to finish encoding: " + std::string(avifResultToString(finishResult))));
        }
        free(data);

        // save to file with scenename
        std::string filename = sceneName + ".avif";
        FILE* f = fopen(filename.c_str(), "wb");
        if (!f)
        {
            Throw(std::runtime_error("Failed to open file for writing"));
        }
        fwrite(avifOutput.data, 1, avifOutput.size, f);
        fclose(f);
        
        
        // send to server
        //img_encoded = base64_encode(avifOutput.data, avifOutput.size, false);
    }


    json11::Json my_json = json11::Json::object{
        {"renderer", Renderer::StaticClass()},
        {"scene", sceneName},
        {"gpu", std::string(deviceProp.properties.deviceName)},
        {"driver", std::string(driverProp.driverInfo)},
        {"fps", fps},
    };
    std::string json_str = my_json.dump();

    std::cout << "Sending benchmark to perf server..." << std::endl;
    // upload from curl
    CURL* curl;
    CURLcode res;
    curl_global_init(CURL_GLOBAL_ALL);
    curl = curl_easy_init();
    if (curl)
    {
        curl_slist* slist1 = nullptr;
        slist1 = curl_slist_append(slist1, "Content-Type: application/json");
        slist1 = curl_slist_append(slist1, "Accept: application/json");

        /* set custom headers */
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, slist1);

        curl_easy_setopt(curl, CURLOPT_URL, "http://gameknife.site:60010/rt_benchmark");
        //curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, avifOutput.size);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_str.c_str());

        /* Perform the request, res gets the return code */
        res = curl_easy_perform(curl);
        /* Check for errors */
        if (res != CURLE_OK)
            fprintf(stderr, "curl_easy_perform() failed: %s\n",
                    curl_easy_strerror(res));

        /* always cleanup */
        curl_easy_cleanup(curl);
    }
}

// export it 
template class NextRendererApplication<Vulkan::RayTracing::RayTracingRenderer>;
template class NextRendererApplication<Vulkan::ModernDeferred::ModernDeferredRenderer>;
template class NextRendererApplication<Vulkan::LegacyDeferred::LegacyDeferredRenderer>;
template class NextRendererApplication<Vulkan::VulkanBaseRenderer>;
