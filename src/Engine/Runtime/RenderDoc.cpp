#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Runtime/RenderDoc.hpp"

#if WITH_RENDERDOC
#include <renderdoc_app.h>

#include "Engine/Runtime/Platform/PlatformCommon.hpp"
#endif

namespace Runtime::RenderDoc
{
#if WITH_RENDERDOC
    namespace
    {
        HMODULE renderDocModule = nullptr;
        RENDERDOC_API_1_6_0* renderDocApi = nullptr;
        uint32_t captureCountBeforeRequest = 0;
        bool capturePending = false;

        bool TryInitialize()
        {
            if (renderDocApi != nullptr)
            {
                return true;
            }

            renderDocModule = GetModuleHandleW(L"renderdoc.dll");
            if (renderDocModule == nullptr)
            {
                renderDocModule = LoadLibraryA(GK_RENDERDOC_DLL_PATH);
            }
            if (renderDocModule == nullptr)
            {
                SPDLOG_WARN("RenderDoc is enabled but renderdoc.dll could not be loaded from {}",
                            GK_RENDERDOC_DLL_PATH);
                return false;
            }

            const auto getApi = reinterpret_cast<pRENDERDOC_GetAPI>(
                GetProcAddress(renderDocModule, "RENDERDOC_GetAPI"));
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
            return true;
        }
    }
#endif

    bool IsSupported()
    {
#if WITH_RENDERDOC
        return true;
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
