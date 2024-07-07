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
#include <fstream>
#include <sstream>
#include <ctime>
#include <fmt/format.h>
#include <fmt/chrono.h>
#include <Utilities/FileHelper.hpp>
#include <Utilities/Math.hpp>

#include "Options.hpp"
#include "curl/curl.h"
#include "cpp-base64/base64.cpp"
#include "ThirdParty/json11/json11.hpp"

#include "stb_image_write.h"

#define _USE_MATH_DEFINES
#include <math.h>

#if WITH_AVIF
#include "avif/avif.h"
#endif

namespace
{
    const bool EnableValidationLayers =
#if defined(NDEBUG) ||  defined(ANDROID)
        false;
#else
        true;
#endif
}

template <typename Renderer>
NextRendererApplication<Renderer>::NextRendererApplication(const UserSettings& userSettings,
                                                           const Vulkan::WindowConfig& windowConfig,
                                                           const VkPresentModeKHR presentMode) :
    Renderer(Renderer::StaticClass(), windowConfig, presentMode, EnableValidationLayers),
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
    glm::mat4 pre_rotate_mat = glm::mat4(1.0f);
    glm::vec3 rotation_axis = glm::vec3(0.0f, 0.0f, 1.0f);

    //if (pretransformFlag & VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR) {
        pre_rotate_mat = glm::rotate(pre_rotate_mat, glm::radians(90.0f), rotation_axis);
    //}

    const auto& init = cameraInitialSate_;

    Assets::UniformBufferObject ubo = {};
    ubo.ModelView = modelViewController_.ModelView();
    ubo.Projection = glm::perspective(glm::radians(userSettings_.FieldOfView),
                                      extent.width / static_cast<float>(extent.height), 0.1f, 10000.0f);
    ubo.Projection[1][1] *= -1;
#if ANDROID
    ubo.Projection = glm::perspective(glm::radians(userSettings_.FieldOfView),
                                      extent.height / static_cast<float>(extent.width), 0.1f, 10000.0f);
    ubo.Projection[1][1] *= -1;
    ubo.Projection = pre_rotate_mat * ubo.Projection;
#endif
    // Inverting Y for Vulkan, https://matthewwellings.com/blog/the-new-vulkan-coordinate-system/
    ubo.ModelViewInverse = glm::inverse(ubo.ModelView);
    ubo.ProjectionInverse = glm::inverse(ubo.Projection);
    ubo.ViewProjection = ubo.Projection * ubo.ModelView;
    ubo.PrevViewProjection = prevUBO_.TotalFrames != 0 ? prevUBO_.ViewProjection : ubo.ViewProjection;

    ubo.Aperture = userSettings_.Aperture;
    ubo.FocusDistance = userSettings_.FocusDistance;
    ubo.SkyRotation = userSettings_.SkyRotation;
    ubo.MaxNumberOfBounces = userSettings_.MaxNumberOfBounces;
    ubo.TotalFrames = totalFrames_;
    ubo.NumberOfSamples = userSettings_.NumberOfSamples;
    ubo.NumberOfBounces = userSettings_.NumberOfBounces;
    ubo.RR_MIN_DEPTH = userSettings_.RR_MIN_DEPTH;
    ubo.AdaptiveSample = userSettings_.AdaptiveSample;
    ubo.AdaptiveVariance = userSettings_.AdaptiveVariance;
    ubo.AdaptiveSteps = userSettings_.AdaptiveSteps;
    ubo.TAA = userSettings_.TAA;
    ubo.RandomSeed = rand();
    ubo.SunDirection = glm::vec4( glm::normalize(glm::vec3( sinf(float( userSettings_.SunRotation * M_PI )), 0.75f, cosf(float( userSettings_.SunRotation * M_PI )) )), 0.0f );
    ubo.SunColor = glm::vec4(1,1,1, 0) * userSettings_.SunLuminance;
    ubo.SkyIntensity = userSettings_.SkyIntensity;
    ubo.SkyIdx = userSettings_.SkyIdx;
    ubo.BackGroundColor = glm::vec4(0.4, 0.6, 1.0, 0.0) * 4.0f * userSettings_.SkyIntensity;
    ubo.HasSky = init.HasSky;
    ubo.HasSun = init.HasSun && userSettings_.SunLuminance > 0;
    ubo.ShowHeatmap = false;
    ubo.HeatmapScale = userSettings_.HeatmapScale;
    ubo.UseCheckerBoard = userSettings_.UseCheckerBoardRendering;
    ubo.TemporalFrames = userSettings_.TemporalFrames;
    ubo.HDR = Renderer::SwapChain().IsHDR();
    
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
    
#if WIN32
    deviceFeatures.shaderInt64 = true;
    Renderer::SetPhysicalDeviceImpl(physicalDevice, requiredExtensions, deviceFeatures, &shaderClockFeatures);
#else
    Renderer::SetPhysicalDeviceImpl(physicalDevice, requiredExtensions, deviceFeatures, nextDeviceFeatures);
#endif
}

template <typename Renderer>
void NextRendererApplication<Renderer>::OnDeviceSet()
{
    Renderer::OnDeviceSet();

    LoadScene(userSettings_.SceneIndex, userSettings_.HDRIfile);

    Renderer::OnPostLoadScene();
}

template <typename Renderer>
void NextRendererApplication<Renderer>::CreateSwapChain()
{
    Renderer::CreateSwapChain();

    userInterface_.reset(new UserInterface(Renderer::CommandPool(), Renderer::SwapChain(), Renderer::DepthBuffer(),
                                           userSettings_));

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
        LoadScene(userSettings_.SceneIndex, userSettings_.HDRIfile);
        Renderer::OnPostLoadScene();
        CreateSwapChain();
        return;
    }
    
    previousSettings_ = userSettings_;

    Renderer::DrawFrame();

    totalFrames_ += 1;
}

template <typename Renderer>
void NextRendererApplication<Renderer>::Render(VkCommandBuffer commandBuffer, const uint32_t imageIndex)
{
    static float frameRate = 0.0;
    static double lastTime = 0.0;
    // Record delta time between calls to Render.
    if(totalFrames_ % 30 == 0)
    {
        double now = Renderer::Window().GetTime();
        const auto timeDelta = now - lastTime;
        lastTime = now;
        frameRate = static_cast<float>(30 / timeDelta);
    }

    const auto prevTime = time_;
    time_ = Renderer::Window().GetTime();
    const auto timeDelta = time_ - prevTime;

    // Update the camera position / angle.
    modelViewController_.UpdateCamera(cameraInitialSate_.ControlSpeed, timeDelta);
    
    Renderer::supportDenoiser_ = userSettings_.Denoiser;
    Renderer::checkerboxRendering_ = userSettings_.UseCheckerBoardRendering;

    // Render the scene
    Renderer::Render(commandBuffer, imageIndex);

    // Check the current state of the benchmark, update it for the new frame.
    CheckAndUpdateBenchmarkState(prevTime);

    // Render the UI
    Statistics stats = {};

    stats.Stats["gpu"] = Renderer::Device().DeviceProperties().deviceName;
    
    stats.FramebufferSize = Renderer::Window().FramebufferSize();
    stats.FrameRate = frameRate;
    stats.FrameTime = static_cast<float>(timeDelta * 1000);

    stats.TotalFrames = totalFrames_;
    stats.RenderTime = time_ - sceneInitialTime_;

    stats.CamPosX = modelViewController_.Position()[0];
    stats.CamPosY = modelViewController_.Position()[1];
    stats.CamPosZ = modelViewController_.Position()[2];

    stats.InstanceCount = static_cast<uint32_t>(scene_->Nodes().size());
    stats.TriCount = scene_->GetIndicesCount() / 3;
    stats.TextureCount = static_cast<uint32_t>(scene_->TextureSamplers().size());
    stats.ComputePassCount = 0;

    Renderer::visualDebug_ = userSettings_.ShowVisualDebug;

    userInterface_->Render(commandBuffer, Renderer::SwapChainFrameBuffer(imageIndex), stats, Renderer::GpuTimer());
}

template <typename Renderer>
void NextRendererApplication<Renderer>::OnKey(int key, int scancode, int action, int mods)
{
    if (userInterface_->WantsToCaptureKeyboard())
    {
        return;
    }
#if !ANDROID
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
            default: break;
            }
        }
    }
#endif

    // Camera motions
    if (!userSettings_.Benchmark)
    {
        modelViewController_.OnKey(key, scancode, action, mods);
    }
}

template <typename Renderer>
void NextRendererApplication<Renderer>::OnCursorPosition(const double xpos, const double ypos)
{
    if (!Renderer::HasSwapChain() ||
        userSettings_.Benchmark ||
        userInterface_->WantsToCaptureKeyboard() ||
        userInterface_->WantsToCaptureMouse()
        )
    {
        return;
    }

    // Camera motions
    modelViewController_.OnCursorPosition(xpos, ypos);
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
    modelViewController_.OnMouseButton(button, action, mods);
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
}

template <typename Renderer>
void NextRendererApplication<Renderer>::OnTouch(bool down, double xpos, double ypos)
{
    modelViewController_.OnTouch(down, xpos, ypos);
}

template <typename Renderer>
void NextRendererApplication<Renderer>::OnTouchMove(double xpos, double ypos)
{
    modelViewController_.OnCursorPosition(xpos, ypos);
}

template <typename Renderer>
void NextRendererApplication<Renderer>::LoadScene(const uint32_t sceneIndex, const std::string& HDRIfile)
{
    std::vector<Assets::Model> models;
    std::vector<Assets::Texture> textures;
    std::vector<Assets::Node> nodes;
    std::vector<Assets::Material> materials;
    std::vector<Assets::LightObject> lights;

    // texture id 0: global sky
    textures.push_back(Assets::Texture::LoadHDRTexture(Utilities::FileHelper::GetPlatformFilePath("assets/textures/canary_wharf_1k.hdr"), Vulkan::SamplerConfig()));
    textures.push_back(Assets::Texture::LoadHDRTexture(Utilities::FileHelper::GetPlatformFilePath("assets/textures/kloppenheim_01_puresky_1k.hdr"), Vulkan::SamplerConfig()));
    textures.push_back(Assets::Texture::LoadHDRTexture(Utilities::FileHelper::GetPlatformFilePath("assets/textures/kloppenheim_07_1k.hdr"), Vulkan::SamplerConfig()));
    textures.push_back(Assets::Texture::LoadHDRTexture(Utilities::FileHelper::GetPlatformFilePath("assets/textures/river_road_2.hdr"), Vulkan::SamplerConfig()));
    textures.push_back(Assets::Texture::LoadHDRTexture(Utilities::FileHelper::GetPlatformFilePath("assets/textures/rainforest_trail_1k.hdr"), Vulkan::SamplerConfig()));

    textures.push_back(Assets::Texture::LoadHDRTexture(Utilities::FileHelper::GetPlatformFilePath("assets/textures/studio_small_03_1k.hdr"), Vulkan::SamplerConfig()));
    textures.push_back(Assets::Texture::LoadHDRTexture(Utilities::FileHelper::GetPlatformFilePath("assets/textures/studio_small_09_1k.hdr"), Vulkan::SamplerConfig()));
    textures.push_back(Assets::Texture::LoadHDRTexture(Utilities::FileHelper::GetPlatformFilePath("assets/textures/sunset_fairway_1k.hdr"), Vulkan::SamplerConfig()));
    textures.push_back(Assets::Texture::LoadHDRTexture(Utilities::FileHelper::GetPlatformFilePath("assets/textures/umhlanga_sunrise_1k.hdr"), Vulkan::SamplerConfig()));
    textures.push_back(Assets::Texture::LoadHDRTexture(Utilities::FileHelper::GetPlatformFilePath("assets/textures/shanghai_bund_1k.hdr"), Vulkan::SamplerConfig()));

    if(HDRIfile != "") textures.push_back(Assets::Texture::LoadHDRTexture(HDRIfile.c_str(), Vulkan::SamplerConfig()));

	userSettings_.HDRIsLoaded = static_cast<int>( textures.size() );
    SceneList::AllScenes[sceneIndex].second(cameraInitialSate_, nodes, models, textures, materials, lights);

    // If there are no texture, add a dummy one. It makes the pipeline setup a lot easier.
    if (textures.empty())
    {
        textures.push_back(Assets::Texture::LoadTexture("assets/textures/white.png", Vulkan::SamplerConfig()));
    }

    scene_.reset(new Assets::Scene(Renderer::CommandPool(), std::move(nodes), std::move(models), std::move(textures),
                                   std::move(materials), std::move(lights), Renderer::supportRayTracing_));
    sceneIndex_ = sceneIndex;

    userSettings_.FieldOfView = cameraInitialSate_.FieldOfView;
    userSettings_.Aperture = cameraInitialSate_.Aperture;
    userSettings_.FocusDistance = cameraInitialSate_.FocusDistance;
    userSettings_.SkyIdx = HDRIfile != "" ? userSettings_.HDRIsLoaded - 1 : cameraInitialSate_.SkyIdx;
    userSettings_.SunRotation = cameraInitialSate_.SunRotation;

    modelViewController_.Reset(cameraInitialSate_.ModelView);

    periodTotalFrames_ = 0;
    totalFrames_ = 0;
    benchmarkTotalFrames_ = 0;
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
        if (benchmarkNumber_ == 0)
        {
            std::time_t now = std::time(nullptr);
            std::string report_filename = fmt::format("report_{:%d-%m-%Y-%H-%M-%S}.csv", fmt::localtime(now));
            benchmarkCsvReportFile.open(report_filename);
            benchmarkCsvReportFile << "#;scene;FPS" << std::endl;
        }
        std::cout << std::endl;
        std::cout << "Renderer: " << Renderer::StaticClass() << std::endl;
        std::cout << "Benchmark: Start scene #" << sceneIndex_ << " '" << SceneList::AllScenes[sceneIndex_].first << "'" << std::endl;
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
            std::string fps = fmt::format("{:10.5}", float(periodTotalFrames_) / float(totalTime));
            std::cout << "Benchmark: " << fps << " fps" << std::endl;
            periodInitialTime_ = time_;
            periodTotalFrames_ = 0;
            benchmarkNumber_++;
        }

        periodTotalFrames_++;
        benchmarkTotalFrames_++;
    }

    // If in benchmark mode, bail out from the scene if we've reached the time or sample limit.
    {
        const bool timeLimitReached = userSettings_.BenchmarkMaxFrame == 0 && periodTotalFrames_ != 0 && time_ - sceneInitialTime_ >
            userSettings_.BenchmarkMaxTime;
        const bool frameLimitReached = userSettings_.BenchmarkMaxFrame > 0 && benchmarkTotalFrames_ >= userSettings_.BenchmarkMaxFrame;
        if (timeLimitReached || frameLimitReached)
        {
            {
                const double totalTime = time_ - sceneInitialTime_;
                std::string SceneName = SceneList::AllScenes[userSettings_.SceneIndex].first;
                double fps = benchmarkTotalFrames_ / totalTime;

                char buff[50];
                Utilities::get_time_str(buff, static_cast<float>(totalTime));
                printf("\n*** totalTime %s  %.2f fps\n", buff, fps);

                Report(static_cast<int>(floor(fps)), SceneName, false, GOption->SaveFile);
                benchmarkCsvReportFile << sceneIndex_ << ";" << SceneList::AllScenes[sceneIndex_].first <<";" << fps << std::endl;
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

inline const std::string versionToString(const uint32_t version)
{
    std::stringstream ss;
    ss << VK_VERSION_MAJOR(version) << "." << VK_VERSION_MINOR(version) << "." << VK_VERSION_PATCH(version);
    return ss.str();
}

template <typename Renderer>
void NextRendererApplication<Renderer>::Report(int fps, const std::string& sceneName, bool upload_screen, bool save_screen)
{
    VkPhysicalDeviceProperties deviceProp1{};
    VkPhysicalDeviceDriverProperties driverProp{};
    driverProp.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES;

    VkPhysicalDeviceProperties2 deviceProp{};
    deviceProp.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    deviceProp.pNext = &driverProp;
    
    vkGetPhysicalDeviceProperties(Renderer::Device().PhysicalDevice(), &deviceProp1);

    std::string img_encoded = "";
    if (upload_screen || save_screen)
    {
#if WITH_AVIF
        // screenshot stuffs
        const Vulkan::SwapChain& swapChain = Renderer::SwapChain();
        const auto extent = swapChain.Extent();

        // capture and export
        Renderer::CaptureScreenShot();

        constexpr uint32_t kCompCnt = 3;
        int imageSize = extent.width * extent.height * kCompCnt;

        avifImage* image = avifImageCreate(extent.width, extent.height, 8, AVIF_PIXEL_FORMAT_YUV444); // these values dictate what goes into the final AVIF
        if (!image)
        {
            Throw(std::runtime_error("avif image creation failed"));
        }
        image->yuvRange = AVIF_RANGE_FULL;
        image->colorPrimaries = AVIF_COLOR_PRIMARIES_BT2020;
        image->transferCharacteristics = AVIF_TRANSFER_CHARACTERISTICS_SMPTE2084;
        image->matrixCoefficients = AVIF_MATRIX_COEFFICIENTS_IDENTITY;
        image->clli.maxCLL = static_cast<uint16_t>(userSettings_.PaperWhiteNit); //maxCLLNits;
        image->clli.maxPALL = 0; //maxFALLNits;

        avifEncoder* encoder = NULL;
        avifRWData avifOutput = AVIF_DATA_EMPTY;

        avifRGBImage rgbAvifImage{};
        avifRGBImageSetDefaults(&rgbAvifImage, image);
        rgbAvifImage.format = AVIF_RGB_FORMAT_RGB;
        rgbAvifImage.ignoreAlpha = AVIF_TRUE;

        uint8_t* data = (uint8_t*)malloc(imageSize);
        {
            Vulkan::DeviceMemory* vkMemory = Renderer::GetScreenShotMemory();

            uint8_t* mappedData = (uint8_t*)vkMemory->Map(0, imageSize);

            uint32_t yDelta = extent.width * kCompCnt;
            uint32_t xDelta = kCompCnt;
            uint32_t idxDelta = extent.width * 4;
            uint32_t yy = 0;
            uint32_t xx = 0, idx = 0;
            for (uint32_t y = 0; y < extent.height; y++)
            {
                xx = 0;
                for (uint32_t x = 0; x < extent.width; x++)
                {
                    uint32_t* pInPixel = (uint32_t*)&mappedData[idx];
                    uint32_t uInPixel = *pInPixel;

                    data[yy + xx] = (uInPixel & (0b11111111 << 16)) >> 16;
                    data[yy + xx + 1] = (uInPixel & (0b11111111 << 8)) >> 8;
                    data[yy + xx + 2] = (uInPixel & (0b11111111 << 0)) >> 0;

                    idx += 4;
                    xx += xDelta;
                }
                yy += yDelta;
            }
            vkMemory->Unmap();
        }

        rgbAvifImage.pixels = (uint8_t*)data;
        rgbAvifImage.rowBytes = rgbAvifImage.width * 3 * sizeof(uint8_t);

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

        // save to file with scenename
        std::string filename = sceneName + ".avif";
        FILE* f = fopen(filename.c_str(), "wb");
        if (!f)
        {
            Throw(std::runtime_error("Failed to open file for writing"));
        }
        else printf("screenshot saved to %s\n", filename.c_str());
        fwrite(avifOutput.data, 1, avifOutput.size, f);
        fclose(f);
		
        free(data);

        // send to server
        //img_encoded = base64_encode(avifOutput.data, avifOutput.size, false);
#else
        // screenshot stuffs
        const Vulkan::SwapChain& swapChain = Renderer::SwapChain();
        const auto extent = swapChain.Extent();

        // capture and export
        Renderer::CaptureScreenShot();

        constexpr uint32_t kCompCnt = 3;
        int imageSize = extent.width * extent.height * kCompCnt;

        uint8_t* data = (uint8_t*)malloc(imageSize);
        {
            Vulkan::DeviceMemory* vkMemory = Renderer::GetScreenShotMemory();

            uint8_t* mappedData = (uint8_t*)vkMemory->Map(0, imageSize);

            uint32_t yDelta = extent.width * kCompCnt;
            uint32_t xDelta = kCompCnt;
            uint32_t idxDelta = extent.width * 4;
            uint32_t yy = 0;
            uint32_t xx = 0, idx = 0;
            for (uint32_t y = 0; y < extent.height; y++)
            {
                xx = 0;
                for (uint32_t x = 0; x < extent.width; x++)
                {
                    uint32_t* pInPixel = (uint32_t*)&mappedData[idx];
                    uint32_t uInPixel = *pInPixel;

                    data[yy + xx] = (uInPixel & (0b11111111 << 16)) >> 16;
                    data[yy + xx + 1] = (uInPixel & (0b11111111 << 8)) >> 8;
                    data[yy + xx + 2] = (uInPixel & (0b11111111 << 0)) >> 0;

                    idx += 4;
                    xx += xDelta;
                }
                yy += yDelta;
            }
            vkMemory->Unmap();
        }

        // save to file with scenename
        /*
        std::string filename = sceneName + ".png";
        stbi_write_png(filename.c_str(), extent.width, extent.height, kCompCnt, (const void *) data, extent.width * kCompCnt);
        */
        std::string filename = sceneName + ".jpg";
        stbi_write_jpg(filename.c_str(), extent.width, extent.height, kCompCnt, (const void*)data, 91);

        printf("screenshot saved to %s\n", filename.c_str());
        free(data);
#endif
    }

    json11::Json my_json = json11::Json::object{
        {"renderer", Renderer::StaticClass()},
        {"scene", sceneName},
        {"gpu", std::string(deviceProp1.deviceName)},
        {"driver", versionToString(deviceProp1.driverVersion)},
        {"fps", fps},
        {"screenshot", img_encoded}
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
template class NextRendererApplication<Vulkan::RayTracing::RayQueryRenderer>;
template class NextRendererApplication<Vulkan::ModernDeferred::ModernDeferredRenderer>;
template class NextRendererApplication<Vulkan::LegacyDeferred::LegacyDeferredRenderer>;
template class NextRendererApplication<Vulkan::HybridDeferred::HybridDeferredRenderer>;
template class NextRendererApplication<Vulkan::VulkanBaseRenderer>;
