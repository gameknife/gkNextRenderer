#pragma once

union SDL_Event;

namespace Runtime
{
    // Optional fullscreen UI overlay rendered after the scene each frame
    // (e.g. RmlUi document UI). The implementation lives in Modules/NextRmlUi;
    // the application entry installs a factory via NextEngine::SetUiOverlayFactory
    // and the engine instantiates it once the renderer is ready.
    class IUiOverlay
    {
    public:
        virtual ~IUiOverlay() = default;

        virtual bool HandleEvent(const SDL_Event& event) = 0;
        virtual void BeginFrame() = 0;
        virtual void RenderFrame() = 0;
        virtual bool WantsToCaptureMouse() const = 0;
        virtual bool WantsToCaptureKeyboard() const = 0;
    };
}
