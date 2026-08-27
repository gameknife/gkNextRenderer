#include "Modules/NextCapture/NextCaptureModule.hpp"

#include "Engine/Runtime/Engine.hpp"
#include "Modules/NextCapture/ScreenShotService.hpp"

namespace Modules::NextCapture
{
    void Install(NextEngine& engine)
    {
        engine.SetScreenShotService(std::make_unique<Runtime::FScreenShotService>(engine));
    }
}
