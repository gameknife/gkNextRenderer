#include "Modules/NextQuickJS/NextQuickJSModule.hpp"

#include "Engine/Runtime/Engine.hpp"
#include "Modules/NextQuickJS/QuickJSEngine.hpp"

namespace Modules::NextQuickJS
{
    void Install(NextEngine& engine, FConfig config)
    {
        engine.SetScriptRuntimeFactory(
            [config = std::move(config)](NextEngine& owner) mutable -> std::unique_ptr<Runtime::IScriptRuntime>
            {
                return std::make_unique<QuickJSEngine>(owner, std::move(config));
            });
    }

    QuickJSEngine* Get(NextEngine& engine)
    {
        return dynamic_cast<QuickJSEngine*>(engine.GetScriptRuntime());
    }

    const QuickJSEngine* Get(const NextEngine& engine)
    {
        return dynamic_cast<const QuickJSEngine*>(engine.GetScriptRuntime());
    }
}
