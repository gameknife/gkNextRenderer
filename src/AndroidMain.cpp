#include "Engine/Utilities/Exception.hpp"
#include "Engine/Options.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/Platform/PlatformCommon.hpp"
// Android compiles every module into the application library, so a header is always there to
// include. Which modules the application actually asked for is still a per-application choice, and
// GK_MODULE_* is how gk_add_application states it -- exactly as in DesktopMain.
#if GK_MODULE_DEVTOOLS
#include "Modules/DevTools/RenderDoc.hpp"
#include "Modules/DevTools/DevToolsDebugUiProvider.hpp"
#endif
#if GK_MODULE_GLTFLOADER
#include "Modules/GltfLoader/GltfModule.hpp"
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
#if GK_MODULE_NEXTTEMPORALUPSCALER
#include "Modules/NextTemporalUpscaler/NextTemporalUpscalerModule.hpp"
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

#include <fmt/format.h>
#include <filesystem>

#define SDL_MAIN_USE_CALLBACKS
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <cstdlib>
#include <vector>

std::unique_ptr<NextEngine> GApplication;
std::unique_ptr<Runtime::Config::Options> GOptionPtr;

#if WITH_RENDERDOC && GK_MODULE_DEVTOOLS
namespace
{
    bool GRenderDocAutoCaptureRequested = false;
}
#endif

SDL_AppResult SDL_AppIterate(void *appstate)
{
    if( GApplication->Tick() )
    {
        return SDL_APP_SUCCESS;
    }
#if WITH_RENDERDOC && GK_MODULE_DEVTOOLS
    const bool renderDocRequested = GOption != nullptr && GOption->RenderDoc;
    if (renderDocRequested && !GRenderDocAutoCaptureRequested && Runtime::RenderDoc::IsSupported() &&
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

SDL_AppResult SDL_AppInit(void **appstate, int argc, char *argv[])
{
#if ANDROID
    // There is no command line on Android, so the application identity is baked in. Scene loading
    // stays with the application; each application owns its own initial-scene policy.
#ifndef GK_APPLICATION_NAME
#define GK_APPLICATION_NAME "gkNextRenderer"
#endif
    NextRenderer::SetApplicationIdentity(GK_APPLICATION_NAME);
    std::vector<const char*> androidArguments = {
        GK_APPLICATION_NAME,
        "--gpu=0",
        "--fullscreen"
    };
    for (int argumentIndex = 1; argumentIndex < argc; ++argumentIndex)
    {
        if (argv != nullptr && argv[argumentIndex] != nullptr)
        {
            androidArguments.push_back(argv[argumentIndex]);
        }
    }
    GOptionPtr.reset(new Runtime::Config::Options(
        static_cast<int>(androidArguments.size()), androidArguments.data()));
#else
    // Handle command line options.
    GOptionPtr.reset(new Runtime::Config::Options(argc, const_cast<const char**>(argv)));
#endif
    // Global GOption, can access from everywhere
    GOption = GOptionPtr.get();


#if __APPLE__
    setenv("MVK_CONFIG_USE_METAL_ARGUMENT_BUFFERS", "1", 1);
#endif   
    if(GOption->RenderDoc)
    {
#if __linux__
        setenv("ENABLE_VULKAN_RENDERDOC_CAPTURE", "1", 1);
#endif

#if __APPLE__
        setenv("MVK_CONFIG_AUTO_GPU_CAPTURE_OUTPUT_FILE", "~/capture/cap.gputrace", 1);
        setenv("MVK_CONFIG_DEFAULT_GPU_CAPTURE_SCOPE_QUEUE_FAMILY_INDEX", "0", 1);
        setenv("MVK_CONFIG_DEFAULT_GPU_CAPTURE_SCOPE_QUEUE_INDEX", "0", 1);
        setenv("MTL_CAPTURE_ENABLED", "1", 1);
        setenv("MVK_CONFIG_AUTO_GPU_CAPTURE_SCOPE","2",1);
#endif
    }

    // Init
    NextRenderer::PlatformInit();
        
    // Start the application.
    // Create the DevTools provider first: its constructor attaches the console
    // log sink so engine startup logs are captured.
#if GK_MODULE_DEVTOOLS
    Runtime::IDebugUiProvider& debugUiProvider = DevTools::DefaultDebugUiProvider();
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
    GApplication->SetDebugUiProvider(&debugUiProvider);
#endif
#if GK_MODULE_NEXTAUDIO
    Modules::Audio::Install(*GApplication);
#endif
#if GK_MODULE_NEXTPHYSICS
    Modules::Physics::Install(*GApplication);
#endif
#if GK_MODULE_LIVECODING
    Modules::LiveCoding::Install(*GApplication);
#endif
#if GK_MODULE_GLTFLOADER
    Modules::Gltf::Register();
#endif
#if GK_MODULE_NEXTREMOTE
    if (GOption->RemoteMode)
    {
        GApplication->AddRenderFrameConsumer(Modules::NextRemote::CreateRemoteServer(*GOption));
    }
#endif
    GApplication->Start();
#if WITH_RENDERDOC && GK_MODULE_DEVTOOLS
    // The launch path controls whether the RenderDoc layer is available;
    // --renderdoc controls whether we resolve its API and capture. Keep
    // normal launches completely free of RenderDoc state.
    if (GOption->RenderDoc)
    {
        Runtime::RenderDoc::Initialize();
    }
#endif
    
    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *appstate, SDL_AppResult result)
{
    // Shutdown
    GApplication->End();
    
    if (GOption->FastExit)
    {
        std::quick_exit(0);
    }

    GApplication.reset();
    GOptionPtr.reset();
}
