#include "Vulkan/Enumerate.hpp"
#include "Vulkan/Strings.hpp"
#include "Utilities/Console.hpp"
#include "Utilities/Exception.hpp"
#include "Options.hpp"
#include "Runtime/Application.hpp"
#include <algorithm>
#include <cstdlib>
#include <fmt/format.h>
#include <iostream>
#include <filesystem>

#define BUILDVER(X) std::string buildver(#X);
#include "build.version"
#include "Vulkan/Window.hpp"

namespace NextRenderer
{
    std::string GetBuildVersion()
    {
        return buildver;
    }
}

#if ANDROID
#include <imgui_impl_android.h>
#include <android/log.h>
#include <game-activity/native_app_glue/android_native_app_glue.h>

static void MakeExternalDirectory( android_app* app, std::string srcPath )
{
    if( std::filesystem::exists(std::string("/sdcard/Android/data/com.gknextrenderer/files/") + srcPath) )
    {
        return;
    }
    
    std::filesystem::create_directories(std::filesystem::path(std::string("/sdcard/Android/data/com.gknextrenderer/files/") + srcPath));
    
    AAssetDir* assetDir = AAssetManager_openDir(
                        app->activity->assetManager, srcPath.c_str());
    const char* filename;
    while ((filename = AAssetDir_getNextFileName(assetDir)) != NULL){
        AAsset* file = AAssetManager_open(app->activity->assetManager, (srcPath + "/" + filename).c_str(),
                                       AASSET_MODE_BUFFER);
        size_t fileLen = AAsset_getLength(file);
        std::vector<char> fileData;
        fileData.resize(fileLen);

        AAsset_read(file, static_cast<void*>(fileData.data()), fileLen);
        AAsset_close(file);

        std::string targetPath = std::string("/sdcard/Android/data/com.gknextrenderer/files/") + srcPath + "/" + filename;
    
        FILE* targetFile = fopen(targetPath.c_str(), "wb");
        fwrite(fileData.data(), 1, fileLen, targetFile);
        fclose(targetFile);
    }
    AAssetDir_close(assetDir);
}

class androidbuf : public std::streambuf {
public:
    enum { bufsize = 512 }; // ... or some other suitable buffer size
    androidbuf() { this->setp(buffer, buffer + bufsize - 1); }

private:
    int overflow(int c)
    {
        if (c == traits_type::eof()) {
            *this->pptr() = traits_type::to_char_type(c);
            this->sbumpc();
        }
        return this->sync()? traits_type::eof(): traits_type::not_eof(c);
    }

    int sync()
    {
        int rc = 0;
        if (this->pbase() != this->pptr()) {
            char writebuf[bufsize+1];
            memcpy(writebuf, this->pbase(), this->pptr() - this->pbase());
            writebuf[this->pptr() - this->pbase()] = '\0';

            rc = __android_log_print(ANDROID_LOG_INFO, "vkdemo", "%s", writebuf) > 0;
            this->setp(buffer, buffer + bufsize - 1);
        }
        return rc;
    }

    char buffer[bufsize];
};
#endif

std::unique_ptr<NextRendererApplication> GApplication = nullptr;

#if ANDROID
void handle_cmd(android_app* app, int32_t cmd) {
    switch (cmd) {
    case APP_CMD_INIT_WINDOW:
        // The window is being shown, get it ready.
        {
            MakeExternalDirectory(app, "assets/fonts");
            MakeExternalDirectory(app, "assets/models");
            MakeExternalDirectory(app, "assets/shaders");
            MakeExternalDirectory(app, "assets/textures");
            MakeExternalDirectory(app, "assets/locale");
            
            const char* argv[] = { "gkNextRenderer", "--renderer=4", "--scene=1", "--load-scene=qx50.glb"};
            const Options options(4, argv);
            GOption = &options;
            GApplication.reset(new NextRendererApplication(options));
            __android_log_print(ANDROID_LOG_INFO, "vkdemo",
                "start gknextrenderer: %d", options.RendererType);
            GApplication->Start();
        }
        break;
    case APP_CMD_TERM_WINDOW:
        // The window is being hidden or closed, clean it up.
        {
            
        }
        break;
    default:
        __android_log_print(ANDROID_LOG_INFO, "Vulkan Tutorials",
                            "event not handled: %d", cmd);
    }
}

static int32_t engine_handle_input(struct android_app* app) {
    ImGuiIO& io = ImGui::GetIO();
    //auto* engine = (struct engine*)app->userData;
    auto ib = android_app_swap_input_buffers(app);
    if (ib && ib->motionEventsCount) {
        for (int i = 0; i < ib->motionEventsCount; i++) {
            auto *event = &ib->motionEvents[i];
            int32_t ptrIdx = 0;
            switch (event->action & AMOTION_EVENT_ACTION_MASK) {
            case AMOTION_EVENT_ACTION_POINTER_DOWN:
            case AMOTION_EVENT_ACTION_POINTER_UP:
                // Retrieve the index for the starting and the ending of any secondary pointers
                ptrIdx = (event->action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >>
                         AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;
            case AMOTION_EVENT_ACTION_DOWN:
            case AMOTION_EVENT_ACTION_UP:
                io.AddMouseSourceEvent(ImGuiMouseSource_Mouse);
                io.AddMousePosEvent(GameActivityPointerAxes_getAxisValue(
                &event->pointers[ptrIdx], AMOTION_EVENT_AXIS_X) * 0.75, GameActivityPointerAxes_getAxisValue(
            &event->pointers[ptrIdx], AMOTION_EVENT_AXIS_Y) * 0.75);
                io.AddMouseButtonEvent(0, event->action == AMOTION_EVENT_ACTION_DOWN);

                GApplication->OnTouch(event->action == AMOTION_EVENT_ACTION_DOWN, GameActivityPointerAxes_getAxisValue(
                        &event->pointers[ptrIdx], AMOTION_EVENT_AXIS_Y) * 0.75,
                        GameActivityPointerAxes_getAxisValue(
                    &event->pointers[ptrIdx], AMOTION_EVENT_AXIS_X) * 0.75);

                break;
            case AMOTION_EVENT_ACTION_MOVE:
                // Process the move action: the new coordinates for all active touch pointers
                // are inside the event->pointers[]. Compare with our internally saved
                // coordinates to find out which pointers are actually moved. Note that there is
                // no index embedded inside event->action for AMOTION_EVENT_ACTION_MOVE (there
                // might be multiple pointers moved at the same time).
                //...
                    io.AddMouseSourceEvent(ImGuiMouseSource_Mouse);
                    io.AddMousePosEvent(GameActivityPointerAxes_getAxisValue(
                        &event->pointers[ptrIdx], AMOTION_EVENT_AXIS_X) * 0.75,
                        GameActivityPointerAxes_getAxisValue(
                    &event->pointers[ptrIdx], AMOTION_EVENT_AXIS_Y) * 0.75);

                GApplication->OnTouchMove(GameActivityPointerAxes_getAxisValue(
                        &event->pointers[ptrIdx], AMOTION_EVENT_AXIS_Y) * 0.75,
                        GameActivityPointerAxes_getAxisValue(
                    &event->pointers[ptrIdx], AMOTION_EVENT_AXIS_X) * 0.75);
               break;
            }
        }
        android_app_clear_motion_events(ib);
    }

    // Process the KeyEvent in a similar way.
    //...

return 0;
}

void android_main(struct android_app* app)
{
    std::cout.rdbuf(new androidbuf);
    
    app->onAppCmd = handle_cmd;

    // Used to poll the events in the main loop
    int events;
    android_poll_source* source;

    // Main loop
    do {
        if (ALooper_pollAll(GApplication != nullptr ? 1 : 0, nullptr,
                            &events, (void**)&source) >= 0) {
            if (source != NULL) source->process(app, source);
                            }
        
        engine_handle_input(app);
        
        // render if vulkan is ready
        if (GApplication != nullptr) {
            GApplication->Tick();
        }
    } while (app->destroyRequested == 0);

    delete std::cout.rdbuf(0);
}
#endif

int main(int argc, const char* argv[]) noexcept
{
    // Runtime Main Routine
    try
    {
        // Handle command line options.
        const Options options(argc, argv);
        // Global GOption, can access from everywhere
        GOption = &options;
        
        // Init environment variables
#if __APPLE__
        setenv("MVK_CONFIG_USE_METAL_ARGUMENT_BUFFERS", "1", 1);
#endif   
        if(options.RenderDoc)
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

        // Windows console color support
#if WIN32 && !defined(__MINGW32__)
        HANDLE hOutput = GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD dwMode;

        GetConsoleMode(hOutput, &dwMode);
        dwMode |= ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING;
        SetConsoleMode(hOutput, dwMode);
#endif
        
        // Start the application.
        GApplication.reset(new NextRendererApplication(options));

        // Application Main Loop
        {
            GApplication->Start();
            while (1)
            {
                if( GApplication->Tick() )
                {
                    break;
                }
            }
            GApplication->End();
        }

        // Shutdown
        GApplication.reset();

        return EXIT_SUCCESS;
    }
    catch (const Options::Help&)
    {
        return EXIT_SUCCESS;
    }
    catch (const std::exception& exception)
    {
        Utilities::Console::Write(Utilities::Severity::Fatal, [&exception]()
        {
            const auto stacktrace = boost::get_error_info<traced>(exception);

            std::cerr << "FATAL: " << exception.what() << std::endl;

            if (stacktrace)
            {
                std::cerr << '\n' << *stacktrace << '\n';
            }
        });
    }
    catch (...)
    {
        Utilities::Console::Write(Utilities::Severity::Fatal, []()
        {
            fmt::print(stderr, "FATAL: caught unhandled exception\n");
        });
    }
    return EXIT_FAILURE;
}
