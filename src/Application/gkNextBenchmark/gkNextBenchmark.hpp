#pragma once
// ReSharper disable once CppUnusedIncludeDirective
#include "BenchMark.hpp"
#include "Common/CoreMinimal.hpp"
#include "Runtime/Engine.hpp"


class BenchmarkGameInstance : public NextGameInstanceBase
{
public:
    BenchmarkGameInstance(Vulkan::WindowConfig& config, Options& options, NextEngine* engine);
    ~BenchmarkGameInstance() override = default;

    // overrides
    void OnInit() override;
    void OnTick(double deltaSeconds) override;
    void OnDestroy() override {};
    void OnSceneLoaded() override;
    
    bool OnRenderUI() override;

    bool OnKey(int key, int scancode, int action, int mods) override;
    bool OnCursorPosition(double xpos, double ypos) override;
    bool OnMouseButton(int button, int action, int mods) override;
    
    // quick access engine
    NextEngine& GetEngine() { return *engine_; }
private:
    NextEngine* engine_;

    std::unique_ptr<BenchMarker> benchMarker_;
};
