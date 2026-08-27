// Phase 0 acceptance probe for the .NET scripting runtime.
//
// A deliberately tiny native program — no engine, no Vulkan, no window — that answers the four
// questions Phase 0 exists to answer (docs/plans/dotnet-scripting-plan.md section 2):
//
//   1. does GkNext_Bootstrap exchange tables and call in both directions?
//   2. does the *same* C# source run under CoreCLR and NativeAOT with identical output?
//   3. can a collectible AssemblyLoadContext swap game code and be collected afterwards?
//   4. does the managed side survive the round trip of every type in the ABI?
//
// Output is split into two channels: CORE| lines must be byte-identical between the backends and
// are what the harness diffs, INFO| lines carry backend-specific detail. Anything that would make
// the transcript backend- or machine-dependent (wall clock, paths, addresses) stays out of CORE.

#include "Modules/NextDotNet/Host/IManagedHost.hpp"
#include "Modules/NextDotNet/Interop.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

using namespace Modules::NextDotNet;

namespace
{
    // --- native services handed to managed code -----------------------------------------------

    struct FProbeState
    {
        std::vector<std::string> transcript;
        uint32_t totalFrames = 0;
        double virtualTime = 0.0;
        int errorCount = 0;

        // Which channel managed log output lands in. The shared lifecycle is CORE; the hot reload
        // section legitimately differs between the backends (that difference is the whole point of
        // section 3.5) and would otherwise poison the cross-backend diff.
        const char* managedChannel = "CORE";
    };

    FProbeState GState;

    void Emit(const std::string& channel, const std::string& text)
    {
        const std::string line = channel + "| " + text;
        GState.transcript.push_back(line);
        std::printf("%s\n", line.c_str());
        std::fflush(stdout);
    }

    void Core(const std::string& text) { Emit("CORE", text); }
    void Info(const std::string& text) { Emit("INFO", text); }

    void Fail(const std::string& text)
    {
        GState.errorCount++;
        Emit("FAIL", text);
    }

    std::string ToString(GkStr value)
    {
        return value.Data != nullptr && value.Length > 0
                   ? std::string(value.Data, static_cast<size_t>(value.Length))
                   : std::string();
    }

    // Managed code calls these. They are the "engine" as far as the probe is concerned.
    void ApiLogInfo(GkStr message) { Emit(GState.managedChannel, ToString(message)); }

    void ApiLogError(GkStr message) { Fail(ToString(message)); }

    uint32_t ApiGetTotalFrames() { return GState.totalFrames; }

    // A virtual clock, not the wall clock: the transcript has to be reproducible for the
    // cross-backend diff to mean anything.
    double ApiGetTime() { return GState.virtualTime; }

    // A fixed screen size keeps the transcript reproducible; the probe has no window.
    void ApiGetScreenSize(FVec2* outSize)
    {
        if (outSize != nullptr)
        {
            *outSize = FVec2{1280.0f, 720.0f};
        }
    }

    // Every entry the probe does not implement still has to be callable: managed code holds the
    // whole table, and a null pointer would crash instead of failing a check.
    template <typename T>
    struct FStub
    {
        static T Value() { return T{}; }
    };

    template <>
    struct FStub<void>
    {
        static void Value() {}
    };

    FEngineApi MakeEngineApi()
    {
        FEngineApi api{};
        api.Version = GK_DOTNET_ABI_VERSION;

        // Expanded from the same def file as the table itself, so the probe keeps building as the
        // binding surface grows without needing a stub written by hand for each new entry.
#define GK_API(ns, name, ret, params) api.ns##_##name = [] params -> ret { return FStub<ret>::Value(); };
#include "Modules/NextDotNet/EngineApi.def.h"
#undef GK_API

        api.Log_Info = &ApiLogInfo;
        api.Log_Warn = &ApiLogInfo;
        api.Log_Error = &ApiLogError;
        api.Engine_GetTotalFrames = &ApiGetTotalFrames;
        api.Engine_GetTime = &ApiGetTime;
        api.UI_GetScreenSize = &ApiGetScreenSize;
        return api;
    }

    // --- helpers ------------------------------------------------------------------------------

    GkStr Str(const std::string& value)
    {
        return GkStr{value.c_str(), static_cast<int32_t>(value.size())};
    }

    void AdvanceFrame(const FManagedApi& managed, double deltaSeconds)
    {
        GState.totalFrames++;
        GState.virtualTime += deltaSeconds;
        managed.Tick(deltaSeconds);
    }

    bool TranscriptContainsSince(size_t startIndex, const std::string& needle)
    {
        for (size_t index = startIndex; index < GState.transcript.size(); index++)
        {
            if (GState.transcript[index].find(needle) != std::string::npos)
            {
                return true;
            }
        }
        return false;
    }

    std::string ArgValue(int argc, char** argv, const char* name, const std::string& fallback = {})
    {
        for (int index = 1; index + 1 < argc; index++)
        {
            if (std::strcmp(argv[index], name) == 0)
            {
                return argv[index + 1];
            }
        }
        return fallback;
    }
}

int main(int argc, char** argv)
{
    FHostConfig config;
    config.managedRootDir = ArgValue(argc, argv, "--managed-root");
    config.dotnetRoot = ArgValue(argc, argv, "--dotnet-root");

    const std::string gamePath = ArgValue(argc, argv, "--game");
    const std::string reloadGamePath = ArgValue(argc, argv, "--reload-game");
    const std::string transcriptPath = ArgValue(argc, argv, "--transcript");

    std::unique_ptr<IManagedHost> host = CreateManagedHost(config);

    const FEngineApi engineApi = MakeEngineApi();
    std::string error;
    if (!host->Initialize(engineApi, error))
    {
        Fail("host initialize: " + error);
        std::printf("PROBE: FAILED\n");
        return 1;
    }

    Info(std::string("backend ") + host->BackendName());
    Info(std::string("hot reload ") + (host->SupportsHotReload() ? "supported" : "unavailable"));

    const FManagedApi* managed = host->Managed();
    if (managed == nullptr)
    {
        Fail("managed API table is null after bootstrap");
        std::printf("PROBE: FAILED\n");
        return 1;
    }

    Core("bootstrap ok");

    // 1. load the game module and run the common lifecycle. Every line below is produced by C#
    //    and must be identical under both backends.
    const int32_t loadStatus = managed->LoadGame(Str(gamePath));
    if (loadStatus != static_cast<int32_t>(EGameStatus::Ok))
    {
        Fail("LoadGame returned " + std::to_string(loadStatus));
        std::printf("PROBE: FAILED\n");
        return 1;
    }

    managed->Lifecycle(static_cast<int32_t>(EScriptHook::OnInit), 0.0);
    for (int frame = 0; frame < 3; frame++)
    {
        AdvanceFrame(*managed, 1.0 / 60.0);
    }
    managed->Lifecycle(static_cast<int32_t>(EScriptHook::OnSceneLoaded), 0.0);

    // 2. hot reload. Under CoreCLR this must swap in a differently built copy of GkNext.Game;
    //    under NativeAOT it must report itself unavailable rather than silently doing nothing.
    //    From here on the two backends diverge by design, so managed output leaves the CORE
    //    channel and stops participating in the cross-backend diff.
    GState.managedChannel = "INFO";
    const size_t reloadMark = GState.transcript.size();
    const int32_t reloadStatus = managed->ReloadGame(Str(reloadGamePath.empty() ? gamePath : reloadGamePath));
    if (host->SupportsHotReload())
    {
        if (reloadStatus != static_cast<int32_t>(EGameStatus::Ok))
        {
            Fail("ReloadGame returned " + std::to_string(reloadStatus) +
                 " (3 = the previous load context was still alive, i.e. a leak)");
        }
        else
        {
            AdvanceFrame(*managed, 1.0 / 60.0);
            if (!reloadGamePath.empty() && !TranscriptContainsSince(reloadMark, "[game B]"))
            {
                Fail("hot reload did not swap in the rebuilt assembly");
            }
            else
            {
                Info("hot reload swapped assembly and collected the old load context");
            }
        }
    }
    else if (reloadStatus != static_cast<int32_t>(EGameStatus::ReloadUnavailable))
    {
        Fail("ReloadGame under NativeAOT returned " + std::to_string(reloadStatus) +
             ", expected ReloadUnavailable");
    }
    else
    {
        Info("hot reload correctly reported as unavailable");
    }

    // 2b. the remaining two managed entry points. They are exercised rather than asserted on: the
    //     probe has no game logic to consume an event, only a table that must be callable.
    {
        FInputEvent event{};
        event.Type = static_cast<int32_t>(EInputEventType::KeyDown);
        event.KeyCode = 32; // space
        if (managed->InputEvent(&event) != 0)
        {
            Info("input event consumed by script");
        }

        FCameraOverride camera{};
        if (managed->OverrideCamera(&camera) != 0)
        {
            Info("script drove the render camera");
        }
    }

    // 3. shut down and confirm the managed side released its load context.
    const int32_t unloadStatus = managed->UnloadGame();
    if (unloadStatus == static_cast<int32_t>(EGameStatus::UnloadPending))
    {
        Fail("UnloadGame left the load context alive: something still references the game assembly");
    }
    else if (unloadStatus != static_cast<int32_t>(EGameStatus::Ok))
    {
        Fail("UnloadGame returned " + std::to_string(unloadStatus));
    }

    if (!transcriptPath.empty())
    {
        std::ofstream file(transcriptPath, std::ios::binary | std::ios::trunc);
        for (const std::string& line : GState.transcript)
        {
            file << line << "\n";
        }
    }

    std::printf("PROBE: %s\n", GState.errorCount == 0 ? "OK" : "FAILED");
    return GState.errorCount == 0 ? 0 : 1;
}
