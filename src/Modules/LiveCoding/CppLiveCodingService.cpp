#include "Engine/Common/CoreMinimal.hpp"
#include "Modules/LiveCoding/CppLiveCodingService.hpp"

#if GK_ENABLE_CPP_LIVE_CODING
#include <cstdlib>

#pragma warning(push, 0)
#include <LPP_API_x64_CPP.h>
#pragma warning(pop)
#endif

namespace Modules::LiveCoding::CppLiveCoding
{
#if GK_ENABLE_CPP_LIVE_CODING
    namespace
    {
        lpp::LppSynchronizedAgent GAgent{};
        bool GStarted = false;

        std::filesystem::path ResolveLivePPRoot()
        {
            const char* environmentRoot = std::getenv("LIVEPP_ROOT");
            std::filesystem::path root = environmentRoot != nullptr && environmentRoot[0] != '\0'
                ? std::filesystem::path(environmentRoot)
                : std::filesystem::path(GK_LIVEPP_ROOT);

            if (std::filesystem::exists(root / "LivePP" / "Agent" / "x64" / "LPP_Agent_x64_CPP.dll"))
            {
                root /= "LivePP";
            }

            std::error_code error;
            const std::filesystem::path canonicalRoot = std::filesystem::weakly_canonical(root, error);
            return error ? root : canonicalRoot;
        }

        const char* GetConnectionStatusName(const lpp::LppConnectionStatus status)
        {
            switch (status)
            {
            case lpp::LPP_CONNECTION_STATUS_SUCCESS:
                return "connected";
            case lpp::LPP_CONNECTION_STATUS_FAILURE:
                return "failed";
            case lpp::LPP_CONNECTION_STATUS_UNEXPECTED_VERSION_BLOB:
                return "unexpected-version-blob";
            case lpp::LPP_CONNECTION_STATUS_VERSION_MISMATCH:
                return "version-mismatch";
            }
            return "unknown";
        }

        void OnConnection(void*, const lpp::LppConnectionStatus status)
        {
            if (status == lpp::LPP_CONNECTION_STATUS_SUCCESS)
            {
                SPDLOG_INFO("[LiveCoding] Live++ Broker connected");
            }
            else
            {
                SPDLOG_ERROR("[LiveCoding] Live++ Broker connection {}", GetConnectionStatusName(status));
            }
        }
    }

    bool Startup()
    {
        if (GStarted)
        {
            return true;
        }

        const std::filesystem::path livePPRoot = ResolveLivePPRoot();
        const std::filesystem::path agentPath = livePPRoot / "Agent" / "x64" / "LPP_Agent_x64_CPP.dll";
        if (!std::filesystem::exists(agentPath))
        {
            SPDLOG_ERROR("[LiveCoding] Live++ agent not found at [{}]", agentPath.string());
            return false;
        }

        const std::wstring livePPRootWide = livePPRoot.wstring();
        GAgent = lpp::LppCreateSynchronizedAgent(nullptr, livePPRootWide.c_str());
        if (!lpp::LppIsValidSynchronizedAgent(&GAgent))
        {
            SPDLOG_ERROR("[LiveCoding] Failed to create Live++ synchronized agent from [{}]", livePPRoot.string());
            return false;
        }

        GStarted = true;
        GAgent.OnConnection(nullptr, &OnConnection);
        GAgent.EnableModule(
            lpp::LppGetCurrentModulePath(),
            lpp::LPP_MODULES_OPTION_NONE,
            nullptr,
            nullptr);

        SPDLOG_INFO(
            "[LiveCoding] C++ Live++ enabled for [{}] with SDK [{}]",
            std::filesystem::path(lpp::LppGetCurrentModulePath()).filename().string(),
            livePPRoot.string());
        return true;
    }

    void BeginFrame()
    {
        if (!GStarted ||
            !GAgent.WantsReload(lpp::LPP_RELOAD_OPTION_SYNCHRONIZE_WITH_RELOAD))
        {
            return;
        }

        SPDLOG_INFO("[LiveCoding] Applying C++ patch at frame boundary");
        GAgent.Reload(lpp::LPP_RELOAD_BEHAVIOUR_WAIT_UNTIL_CHANGES_ARE_APPLIED);
        SPDLOG_INFO("[LiveCoding] C++ patch applied");
    }

    bool RequestReload()
    {
        if (!GStarted)
        {
            return false;
        }

        SPDLOG_INFO("[LiveCoding] C++ reload requested");
        GAgent.ScheduleReload();
        return true;
    }

    bool IsStarted()
    {
        return GStarted;
    }

    void Shutdown()
    {
        if (!GStarted)
        {
            return;
        }

        SPDLOG_INFO("[LiveCoding] Stopping Live++ agent");
        lpp::LppDestroySynchronizedAgent(&GAgent);
        GStarted = false;
    }
#else
    bool Startup()
    {
        return false;
    }

    void BeginFrame()
    {
    }

    bool RequestReload()
    {
        return false;
    }

    bool IsStarted()
    {
        return false;
    }

    void Shutdown()
    {
    }
#endif
}
