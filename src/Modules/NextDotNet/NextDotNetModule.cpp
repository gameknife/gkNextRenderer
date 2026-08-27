#include "Modules/NextDotNet/NextDotNetModule.hpp"

#include "Engine/Runtime/Engine.hpp"
#include "Modules/NextDotNet/DotNetRuntime.hpp"

namespace Modules::NextDotNet
{
    void Install(NextEngine& engine, FConfig config)
    {
        engine.SetScriptRuntimeFactory(
            [config = std::move(config)](NextEngine& owner) mutable -> std::unique_ptr<Runtime::IScriptRuntime>
            {
                return std::make_unique<DotNetRuntime>(owner, std::move(config));
            });
    }

    DotNetRuntime* Get(NextEngine& engine)
    {
        return dynamic_cast<DotNetRuntime*>(engine.GetScriptRuntime());
    }

    const DotNetRuntime* Get(const NextEngine& engine)
    {
        return dynamic_cast<const DotNetRuntime*>(engine.GetScriptRuntime());
    }
}
