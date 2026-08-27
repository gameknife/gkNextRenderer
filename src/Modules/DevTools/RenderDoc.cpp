#include "Engine/Common/CoreMinimal.hpp"
#include "Modules/DevTools/RenderDoc.hpp"

#if WITH_RENDERDOC
#include <renderdoc_app.h>

#if GK_RENDERDOC_ANDROID
#include <dlfcn.h>
#else
#include "Engine/Runtime/Platform/PlatformCommon.hpp"
#endif
#endif

namespace Runtime::RenderDoc
{
#if WITH_RENDERDOC
    namespace
    {
#if GK_RENDERDOC_ANDROID
        void* renderDocModule = nullptr;
#else
        HMODULE renderDocModule = nullptr;
#endif
        RENDERDOC_API_1_6_0* renderDocApi = nullptr;
        uint32_t captureCountBeforeRequest = 0;
        bool capturePending = false;

        bool TryInitialize()
        {
            if (renderDocApi != nullptr)
            {
                return true;
            }

#if GK_RENDERDOC_ANDROID
            // The Android Vulkan loader loads this layer when the app is started
            // through RenderDoc. First use the documented passive lookup so a
            // normal launch never loads RenderDoc speculatively.
            constexpr const char* renderDocLibrary = "libVkLayer_GLES_RenderDoc.so";
            renderDocModule = dlopen(renderDocLibrary, RTLD_NOW | RTLD_NOLOAD);
            if (renderDocModule == nullptr)
            {
                const char* loadError = dlerror();
                SPDLOG_WARN("RenderDoc Android layer is not already loaded ({}); trying linker fallback",
                            loadError != nullptr ? loadError : "unknown linker error");
                // Some vendor Android linker namespaces hide a layer loaded by
                // the Vulkan loader from RTLD_NOLOAD. The gnb --renderdoc path
                // has already selected the layer, so loading the same module
                // explicitly is a safe compatibility fallback here.
                renderDocModule = dlopen(renderDocLibrary, RTLD_NOW | RTLD_GLOBAL);
            }
#else
            renderDocModule = GetModuleHandleW(L"renderdoc.dll");
            if (renderDocModule == nullptr)
            {
                renderDocModule = LoadLibraryA(GK_RENDERDOC_DLL_PATH);
            }
#endif
#if GK_RENDERDOC_ANDROID
            if (renderDocModule == nullptr)
            {
                SPDLOG_DEBUG("RenderDoc Android layer handle is unavailable; trying the default linker scope");
            }
#else
            if (renderDocModule == nullptr)
            {
                SPDLOG_WARN("RenderDoc is enabled but renderdoc.dll could not be loaded from {}",
                            GK_RENDERDOC_DLL_PATH);
                return false;
            }
#endif

#if GK_RENDERDOC_ANDROID
            auto getApi = reinterpret_cast<pRENDERDOC_GetAPI>(
                renderDocModule != nullptr ? dlsym(renderDocModule, "RENDERDOC_GetAPI") : nullptr);
            if (getApi == nullptr)
            {
                // Android's linker namespaces can make an already-loaded
                // debug layer unavailable through its basename even though
                // its exported symbols are visible to the process.
                getApi = reinterpret_cast<pRENDERDOC_GetAPI>(
                    dlsym(RTLD_DEFAULT, "RENDERDOC_GetAPI"));
            }
#else
            const auto getApi = reinterpret_cast<pRENDERDOC_GetAPI>(
                GetProcAddress(renderDocModule, "RENDERDOC_GetAPI"));
#endif
            if (getApi == nullptr ||
                getApi(eRENDERDOC_API_Version_1_6_0, reinterpret_cast<void**>(&renderDocApi)) == 0 ||
                renderDocApi == nullptr)
            {
                SPDLOG_WARN("RenderDoc is enabled but its application API is unavailable");
                return false;
            }

            int major = 0;
            int minor = 0;
            int patch = 0;
            renderDocApi->GetAPIVersion(&major, &minor, &patch);
            SPDLOG_INFO("RenderDoc {}.{}.{} application API attached", major, minor, patch);
            return true;
        }

        bool OpenLatestCapture()
        {
            const uint32_t captureCount = renderDocApi->GetNumCaptures();
            if (captureCount <= captureCountBeforeRequest)
            {
                return false;
            }

            const uint32_t captureIndex = captureCount - 1;
            uint32_t pathLength = 0;
            uint64_t timestamp = 0;
            if (renderDocApi->GetCapture(captureIndex, nullptr, &pathLength, &timestamp) == 0 ||
                pathLength == 0)
            {
                SPDLOG_WARN("RenderDoc captured a frame but did not return its capture path");
                return true;
            }

            std::string capturePath(pathLength, '\0');
            if (renderDocApi->GetCapture(captureIndex, capturePath.data(), &pathLength, &timestamp) == 0)
            {
                SPDLOG_WARN("RenderDoc captured a frame but its capture path could not be read");
                return true;
            }
            capturePath.resize(std::char_traits<char>::length(capturePath.c_str()));

#if GK_RENDERDOC_ANDROID
            // The Android layer cannot launch the Windows replay UI. When the
            // app is started through qrenderdoc --replayhost adb://..., the
            // target-control connection receives this capture automatically.
            SPDLOG_INFO("RenderDoc Android capture saved to {}; the connected desktop replay UI will open it",
                        capturePath);
#else
            const uint32_t replayUiPid = renderDocApi->LaunchReplayUI(0, capturePath.c_str());
            if (replayUiPid == 0)
            {
                SPDLOG_WARN("RenderDoc capture saved to {} but the replay UI could not be opened",
                            capturePath);
            }
            else
            {
                SPDLOG_INFO("RenderDoc capture saved to {}; replay UI started with PID {}",
                            capturePath, replayUiPid);
            }
#endif
            return true;
        }
    }
#endif

    bool IsSupported()
    {
#if WITH_RENDERDOC
        return renderDocApi != nullptr;
#else
        return false;
#endif
    }

    bool Initialize()
    {
#if WITH_RENDERDOC
        return TryInitialize();
#else
        return false;
#endif
    }

    bool RequestCapture()
    {
#if WITH_RENDERDOC
        if (!TryInitialize())
        {
            return false;
        }
        if (capturePending)
        {
            SPDLOG_INFO("RenderDoc capture request is already pending");
            return false;
        }

        captureCountBeforeRequest = renderDocApi->GetNumCaptures();
        renderDocApi->TriggerCapture();
        capturePending = true;
        SPDLOG_INFO("RenderDoc capture requested for the next presented frame");
        return true;
#else
        return false;
#endif
    }

    void Poll()
    {
#if WITH_RENDERDOC
        if (!capturePending || renderDocApi == nullptr)
        {
            return;
        }
        if (OpenLatestCapture())
        {
            capturePending = false;
        }
#endif
    }
}
