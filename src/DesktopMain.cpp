#include "Utilities/Exception.hpp"
#include "Options.hpp"
#include "Runtime/Engine.hpp"

#include <fmt/format.h>
#include <iostream>
#include <filesystem>
#include <cpptrace/cpptrace.hpp>
#include "Runtime/Platform/PlatformCommon.h"

#if WIN32
#include "ThirdParty/renderdoc/renderdoc_app.h"
#endif

int main(int argc, const char* argv[]) noexcept
{
    // Runtime Main Routine
    try
    {
        // Handle command line options.
        Options options(argc, argv);
        // Global GOption, can access from everywhere
        GOption = &options;
        
        // Init environment variables
#if __APPLE__
        setenv("MVK_CONFIG_USE_METAL_ARGUMENT_BUFFERS", "1", 1);
#endif   
        if(options.RenderDoc)
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

#if __APPLE__
            setenv("MVK_CONFIG_AUTO_GPU_CAPTURE_OUTPUT_FILE", "~/capture/cap.gputrace", 1);
            setenv("MVK_CONFIG_DEFAULT_GPU_CAPTURE_SCOPE_QUEUE_FAMILY_INDEX", "0", 1);
            setenv("MVK_CONFIG_DEFAULT_GPU_CAPTURE_SCOPE_QUEUE_INDEX", "0", 1);
            setenv("MTL_CAPTURE_ENABLED", "1", 1);
            setenv("MVK_CONFIG_AUTO_GPU_CAPTURE_SCOPE","2",1);
#endif
        }

        NextRenderer::PlatformInit();
        
        // Start the application.
        std::unique_ptr<NextEngine> GApplication;
        GApplication.reset( new NextEngine(options) );

        // Application Main Loop
        GApplication->Start();
        while (1)
        {
            if( GApplication->Tick() )
            {
                break;
            }
        }
        GApplication->End();
        
        // Shutdown
        GApplication.reset();

        return EXIT_SUCCESS;
    }
    // Exception Handle
    catch (const std::exception& exception)
    {
        SPDLOG_ERROR("FATAL: {}", exception.what());
        std::cerr << "FATAL: " << exception.what() << std::endl;
    }
    // catch (...)
    // {
    //     Utilities::Console::Write(Utilities::Severity::Fatal, []()
    //     {
    //         fmt::print(stderr, "FATAL: caught unhandled exception\n");
    //     });
    // }
    return EXIT_FAILURE;
}
