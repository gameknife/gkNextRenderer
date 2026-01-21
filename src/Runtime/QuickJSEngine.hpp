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
    void CompileTypeScriptSources();

    std::unique_ptr<qjs::Runtime> runtime_;
    std::unique_ptr<qjs::Context> context_;
    std::function<void(double)> tickCallback_;
#endif
};
