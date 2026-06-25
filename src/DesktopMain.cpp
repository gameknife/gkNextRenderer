#include "Engine/Utilities/Exception.hpp"
#include "Engine/Options.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Tests/GltfTestRunner.hpp"
#include "Engine/Runtime/Platform/PlatformCommon.h"
#include "Modules/DevTools/DevToolsDebugUiProvider.hpp"
#include "Modules/NextRemote/NextRemoteModule.hpp"
#if GK_WITH_TUI
#include "Modules/NextTui/NextTuiModule.hpp"
#endif
#include "Modules/NextRmlUi/NextRmlUiModule.hpp"

#if WIN32
#include "ThirdParty/renderdoc/renderdoc_app.h"
#endif

#define SDL_MAIN_USE_CALLBACKS
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <cstdlib>

std::unique_ptr<NextEngine> GApplication;
std::unique_ptr<Runtime::Config::Options> GOptionPtr;
std::unique_ptr<Runtime::Scene::GltfTestRunner> GTestRunner;

SDL_AppResult SDL_AppIterate(void *appstate)
{
    if( GApplication->Tick() )
    {
        return SDL_APP_SUCCESS;
    }
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
    // Handle command line options.
#if IOS
    const char* argv1[] = { "gkNextRenderer", "--load-scene=assets/models/playground.glb" };
    GOptionPtr.reset(new Runtime::Config::Options(2, argv1));
#else
    GOptionPtr.reset(new Runtime::Config::Options(argc, const_cast<const char**>(argv)));
#endif
    // Global GOption, can access from everywhere
    GOption = GOptionPtr.get();

    if(GOption->RenderDoc)
    {
#if WIN32
        RENDERDOC_API_1_1_2* rdoc_api = NULL;
        const auto mod = LoadLibrary(L"renderdoc.dll");
        if (mod)
        {
            pRENDERDOC_GetAPI RENDERDOC_GetAPI =
                (pRENDERDOC_GetAPI)GetProcAddress(mod, "RENDERDOC_GetAPI");
            RENDERDOC_GetAPI(eRENDERDOC_API_Version_1_1_2, (void**)&rdoc_api);
        }
#endif
            
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
    Runtime::IDebugUiProvider& debugUiProvider = DevTools::DefaultDebugUiProvider();
    GApplication.reset( new NextEngine(*GOption) );
    GApplication->SetDebugUiProvider(&debugUiProvider);
    Modules::NextRmlUi::Install(*GApplication);
    if (GOption->RemoteMode)
    {
        GApplication->AddRenderFrameConsumer(Modules::NextRemote::CreateRemoteServer(*GOption));
    }
#if GK_WITH_TUI
    if (GOption->Tui)
    {
        GApplication->AddRenderFrameConsumer(Modules::NextTui::CreateTuiPresenter(*GApplication, *GOption));
    }
#endif

    if (GOption->TestGltfRobustness)
    {
        GTestRunner = std::make_unique<Runtime::Scene::GltfTestRunner>(GApplication.get());
        GApplication->AddTickedTask([](double dt){ return GTestRunner->Update(dt); });
    }

    GApplication->Start();
    
    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *appstate, SDL_AppResult result)
{
    // Shutdown
    GApplication->End();
    
    if (GOption->FastExit)
    {
#if __APPLE__
        std::exit(0);
#else
        std::quick_exit(0);
#endif
    }

    GTestRunner.reset();
    GApplication.reset();
    GOptionPtr.reset();
}
