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

private:
#if WITH_QUICKJS
    bool CompileTypeScriptSources();
    void ResetContextAndLoadScript();
    void TickHotReload(double deltaSeconds);
    bool EnsureTscAvailable(const std::filesystem::path& localTsc);

    std::unique_ptr<qjs::Runtime> runtime_;
    std::unique_ptr<qjs::Context> context_;
    std::function<void(double)> tickCallback_;
    double hotReloadElapsed_ = 0.0;
    bool tscChecked_ = false;
    bool tscAvailable_ = false;
#endif
};
