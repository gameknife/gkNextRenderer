#pragma once

#include "Common/CoreMinimal.hpp"

namespace qjs
{
    class Context;
    class Runtime;
}

class NextEngine;

class QuickJSEngine final
{
public:
    QuickJSEngine();
    ~QuickJSEngine();

    void Initialize();
    void Tick(double deltaSeconds);
    void RegisterTickCallback(std::function<void(double)> callback);

    // Execute JS code in the current context, returns error string (empty on success)
    std::string Eval(const std::string& code);

    // Register a callback to bind Editor.* helpers into the JS context
    using BindingsCallback = std::function<void(void* jsContext)>;
    void SetEditorBindingsCallback(BindingsCallback callback);

private:
#if WITH_QUICKJS
    bool CompileTypeScriptSources();
    void ResetContextAndLoadScript();
    void TickHotReload(double deltaSeconds);
    bool EnsureTscAvailable(const std::filesystem::path& localTsc);

    std::unique_ptr<qjs::Runtime> runtime_;
    std::unique_ptr<qjs::Context> context_;
    std::function<void(double)> tickCallback_;
    BindingsCallback editorBindingsCallback_;
    double hotReloadElapsed_ = 0.0;
    bool tscChecked_ = false;
    bool tscAvailable_ = false;
#endif
};
