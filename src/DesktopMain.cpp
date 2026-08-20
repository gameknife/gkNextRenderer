#include "Engine/Utilities/Exception.hpp"
#include "Engine/Options.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/Platform/PlatformCommon.hpp"
#include "Engine/Utilities/FileHelper.hpp"
#if GK_MODULE_GLTFLOADER
#include "Modules/GltfLoader/GltfModule.hpp"
#endif
#if GK_MODULE_DEVTOOLS
#include "Modules/DevTools/DevToolsDebugUiProvider.hpp"
#include "Modules/DevTools/ReflectionDump.hpp"
#include "Modules/DevTools/RenderDoc.hpp"
#endif
#if GK_MODULE_NEXTUI
#include "Modules/NextUI/NextUIModule.hpp"
#endif
#if GK_MODULE_NEXTCAPTURE
#include "Modules/NextCapture/NextCaptureModule.hpp"
#endif
#if GK_MODULE_NEXTVALIDATION
#include "Modules/NextValidation/NextValidationModule.hpp"
#endif
#if GK_MODULE_SCENECONTENT
#include "Modules/SceneContent/SceneContentModule.hpp"
#endif
#if GK_MODULE_LIVECODING
#include "Modules/LiveCoding/LiveCodingModule.hpp"
#endif
#if GK_MODULE_NEXTAUDIO
#include "Modules/NextAudio/NextAudioModule.hpp"
#endif
#if GK_MODULE_NEXTPHYSICS
#include "Modules/NextPhysics/NextPhysicsModule.hpp"
#endif
#if GK_MODULE_NEXTREMOTE
#include "Modules/NextRemote/NextRemoteModule.hpp"
#endif
#if GK_MODULE_NEXTSTREAMLINE
#include "Modules/NextStreamline/NextStreamlineModule.hpp"
#endif
#if GK_MODULE_NEXTFIDELITYFX
#include "Modules/NextFidelityFX/NextFidelityFXModule.hpp"
#endif
#if GK_MODULE_NEXTTEMPORALUPSCALER
#include "Modules/NextTemporalUpscaler/NextTemporalUpscalerModule.hpp"
#endif
#if GK_MODULE_SPLATLOADER
#include "Modules/SplatLoader/SplatModule.hpp"
#endif
#if GK_WITH_TUI && GK_MODULE_NEXTTUI
#include "Modules/NextTui/NextTuiModule.hpp"
#endif

#define SDL_MAIN_USE_CALLBACKS
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "Engine/Utilities/LogFile.hpp"

#include <cstdlib>
#include <exception>
#include <string>
#include <spdlog/spdlog.h>

std::unique_ptr<NextEngine> GApplication;
std::unique_ptr<Runtime::Config::Options> GOptionPtr;

namespace
{
#if WITH_RENDERDOC && GK_MODULE_DEVTOOLS
    bool GRenderDocAutoCaptureRequested = false;
#endif

    // Last-resort handler for exceptions that escape a thread or a noexcept context.
    // Without it the process dies through std::terminate with no log line at all.
    void InstallTerminateHandler()
    {
        std::set_terminate([]()
        {
            try
            {
                if (const std::exception_ptr active = std::current_exception())
                {
                    std::rethrow_exception(active);
                }
                SPDLOG_ERROR("TERMINATE: no active exception");
            }
            catch (const std::exception& error)
            {
                SPDLOG_ERROR("TERMINATE: {}", error.what());
            }
            catch (...)
            {
                SPDLOG_ERROR("TERMINATE: unknown exception");
            }
            spdlog::default_logger()->flush();
            std::abort();
        });
    }

    // Startup failures reach an end user who has no console attached, so state the
    // cause and where to find the log instead of letting Windows show a crash box.
    void ReportStartupFailure(const std::string& reason)
    {
        SPDLOG_ERROR("startup failed: {}", reason);
        spdlog::default_logger()->flush();

        std::string message = reason;
        const std::string& logPath = Utilities::Logging::GetLogFilePath();
        if (!logPath.empty())
        {
            message += "\n\nA full log was written to:\n" + logPath;
        }
        message += "\n\nPlease report this at:\nhttps://github.com/gameknife/gkNextRenderer/issues";

        const std::string title = NextRenderer::GetApplicationIdentity() + " failed to start";
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, title.c_str(), message.c_str(), nullptr);
    }
}

SDL_AppResult SDL_AppIterate(void *appstate)
{
#if GK_MODULE_LIVECODING
    Modules::LiveCoding::BeginFrame();
#endif
    if( GApplication->Tick() )
    {
        return SDL_APP_SUCCESS;
    }
#if WITH_RENDERDOC && GK_MODULE_DEVTOOLS
    if (GOption != nullptr && GOption->RenderDoc && !GRenderDocAutoCaptureRequested &&
        GApplication != nullptr &&
        GApplication->GetEngineStatus() == NextRenderer::EApplicationStatus::Running &&
        !GApplication->GetScene().Nodes().empty())
    {
        GRenderDocAutoCaptureRequested = Runtime::RenderDoc::RequestCapture();
    }
    Runtime::RenderDoc::Poll();
#endif
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event)
{
    if ( GApplication->HandleEvent(*event) )
    {
        return SDL_APP_SUCCESS;
    }
    return SDL_APP_CONTINUE;
}

static SDL_AppResult InitializeApplication(int argc, char *argv[])
{
    // Handle command line options.
#if IOS
    const char* argv1[] = { "gkNextRenderer", "--load-scene=assets/models/conf_room.glb" };
    GOptionPtr.reset(new Runtime::Config::Options(2, argv1));
#else
    GOptionPtr.reset(new Runtime::Config::Options(argc, const_cast<const char**>(argv)));
#endif
    // Global GOption, can access from everywhere
    GOption = GOptionPtr.get();
    if (!GOption->AssetTrace.empty())
    {
        Utilities::FileHelper::SetAssetTracePath(GOption->AssetTrace);
    }

    // Before anything creates a device: the manifest comes from entt::meta alone, and requiring a
    // working GPU to regenerate source would make code generation fail on build machines.
#if GK_MODULE_DEVTOOLS
    if (!GOption->DumpReflection.empty())
    {
        const bool dumped = Reflection::DumpManifest(GOption->DumpReflection);
        spdlog::default_logger()->flush();
        std::exit(dumped ? 0 : 1);
    }
#endif

#if WITH_RENDERDOC && GK_MODULE_DEVTOOLS
    // RenderDoc must be opt-in. Its application API is loaded before Vulkan
    // creation only when the user explicitly requests a capture workflow.
    if (GOption->RenderDoc)
    {
        Runtime::RenderDoc::Initialize();
    }
#endif

    if(GOption->RenderDoc)
    {
            
#if __linux__
        setenv("ENABLE_VULKAN_RENDERDOC_CAPTURE", "1", 1);
#endif

// #if __APPLE__
//         setenv("MVK_CONFIG_AUTO_GPU_CAPTURE_OUTPUT_FILE", "~/capture/cap.gputrace", 1);
//         setenv("MVK_CONFIG_DEFAULT_GPU_CAPTURE_SCOPE_QUEUE_FAMILY_INDEX", "0", 1);
//         setenv("MVK_CONFIG_DEFAULT_GPU_CAPTURE_SCOPE_QUEUE_INDEX", "0", 1);
//         setenv("MTL_CAPTURE_ENABLED", "1", 1);
//         setenv("MVK_CONFIG_AUTO_GPU_CAPTURE_SCOPE","2",1);
// #endif
        
#if __APPLE__
    setenv("MESA_KK_GPU_CAPTURE", "1", 1);
    setenv("MESA_KK_GPU_CAPTURE_DIRECTORY", "~/capture", 1);
#endif
    }

    // Init
    NextRenderer::PlatformInit();
        
    // Start the application.
    // Create the DevTools provider first: its constructor attaches the console
    // log sink so engine startup logs are captured.
    // Streamline must hook the Vulkan interposer before the engine creates the instance.
#if GK_MODULE_NEXTSTREAMLINE
    Modules::NextStreamline::Install(*GOption);
#endif
#if GK_MODULE_NEXTFIDELITYFX
    Modules::NextFidelityFX::Install(*GOption);
#endif
#if GK_MODULE_NEXTTEMPORALUPSCALER
    Modules::NextTemporalUpscaler::Install(*GOption);
#endif
    GApplication.reset( new NextEngine(*GOption) );
#if GK_MODULE_SCENECONTENT
    Modules::SceneContent::Install(*GApplication);
#endif
#if GK_MODULE_NEXTVALIDATION
    Modules::NextValidation::Install(*GApplication);
#endif
#if GK_MODULE_NEXTUI
    Modules::NextUI::Install(*GApplication);
#endif
#if GK_MODULE_NEXTCAPTURE
    Modules::NextCapture::Install(*GApplication);
#endif
#if GK_MODULE_DEVTOOLS
    DevTools::Install(*GApplication);
#endif
#if GK_MODULE_NEXTAUDIO
    Modules::Audio::Install(*GApplication);
#endif
#if GK_MODULE_NEXTPHYSICS
    Modules::Physics::Install(*GApplication);
#endif
#if GK_MODULE_LIVECODING
    Modules::LiveCoding::Install(*GApplication, GOption->CppLiveCoding);
#endif
#if GK_MODULE_GLTFLOADER
    Modules::Gltf::Register();
#endif
#if GK_MODULE_SPLATLOADER
    Modules::Splat::Install(*GApplication);
#endif
#if GK_MODULE_NEXTREMOTE
    if (GOption->RemoteMode)
    {
        GApplication->AddRenderFrameConsumer(Modules::NextRemote::CreateRemoteServer(*GOption));
    }
#endif
#if GK_WITH_TUI && GK_MODULE_NEXTTUI
    if (GOption->Tui)
    {
        GApplication->AddRenderFrameConsumer(Modules::NextTui::CreateTuiPresenter(*GApplication, *GOption));
    }
#endif

    GApplication->Start();

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppInit(void **appstate, int argc, char *argv[])
{
    if (argc > 0 && argv[0] != nullptr)
    {
        NextRenderer::SetApplicationIdentity(argv[0]);
    }
    //InstallTerminateHandler();
    Utilities::Logging::InstallFileSink();

    // Everything below can throw: missing Vulkan extensions, no present queue, out of
    // device memory, missing assets. Returning SDL_APP_FAILURE gives SDL a clean
    // shutdown and the process a non-zero exit code.
    try
    {
        return InitializeApplication(argc, argv);
    }
    catch (const std::exception& error)
    {
        ReportStartupFailure(error.what());
    }
    catch (...)
    {
        ReportStartupFailure("Unknown fatal error during startup.");
    }

    try
    {
        // Tearing down a half-initialised engine can fail in turn; the startup error
        // has already been reported, so never let cleanup mask it.
        GApplication.reset();
    }
    catch (...)
    {
        SPDLOG_ERROR("cleanup after failed startup threw; exiting anyway");
    }
    return SDL_APP_FAILURE;
}

void SDL_AppQuit(void *appstate, SDL_AppResult result)
{
    if (!GApplication)
    {
        // Startup failed before the engine existed; nothing to tear down.
        spdlog::default_logger()->flush();
        return;
    }

    const int exitCode = GApplication->GetRequestedExitCode();
    // Shutdown
#if GK_MODULE_LIVECODING
    Modules::LiveCoding::Shutdown();
#endif
    GApplication->End();

    if (GOption && GOption->FastExit)
    {
#if __APPLE__
        std::exit(exitCode);
#else
        std::quick_exit(exitCode);
#endif
    }

    GApplication.reset();
    GOptionPtr.reset();
}
