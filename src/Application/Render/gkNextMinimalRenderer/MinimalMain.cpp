#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Options.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/Platform/PlatformCommon.hpp"
#include "MinimalRenderer.hpp"

#define SDL_MAIN_USE_CALLBACKS
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <cstdlib>

namespace
{
    std::unique_ptr<NextEngine> application;
    std::unique_ptr<Runtime::Config::Options> options;
}

SDL_AppResult SDL_AppInit(void**, int argc, char* argv[])
{
    options = std::make_unique<Runtime::Config::Options>(argc, const_cast<const char**>(argv));
    GOption = options.get();

    NextRenderer::PlatformInit();
    application = std::make_unique<NextEngine>(*options);
    application->Start();
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void*)
{
    return application->Tick() ? SDL_APP_SUCCESS : SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void*, SDL_Event* event)
{
    return application->HandleEvent(*event) ? SDL_APP_SUCCESS : SDL_APP_CONTINUE;
}

void SDL_AppQuit(void*, SDL_AppResult)
{
    const int exitCode = application ? application->GetRequestedExitCode() : 0;
    if (application)
    {
        application->End();
    }

    if (options && options->FastExit)
    {
        std::quick_exit(exitCode);
    }

    application.reset();
    options.reset();
}
