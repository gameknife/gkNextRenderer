#include "Engine/Common/CoreMinimal.hpp"
#include "Modules/LiveCoding/LiveCodingModule.hpp"

#include "Engine/Runtime/Engine.hpp"
#include "Modules/LiveCoding/CppLiveCodingService.hpp"
#include "Modules/LiveCoding/ShaderHotReloader.hpp"

namespace Modules::LiveCoding
{
    void Install(NextEngine& engine, const bool enableCppLiveCoding)
    {
        engine.SetShaderHotReloaderFactory([](NextEngine& owner) -> std::unique_ptr<Runtime::IShaderHotReloader>
        {
            return std::make_unique<ShaderHotReloader>(owner.GetRenderer());
        });

        if (enableCppLiveCoding)
        {
            CppLiveCoding::Startup();
        }
    }

    void BeginFrame()
    {
        CppLiveCoding::BeginFrame();
    }

    bool RequestCppReload()
    {
        return CppLiveCoding::RequestReload();
    }

    bool IsCppLiveCodingAvailable()
    {
        return CppLiveCoding::IsStarted();
    }

    void Shutdown()
    {
        CppLiveCoding::Shutdown();
    }
}
