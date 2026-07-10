#pragma once

#include "Engine/Runtime/GameInstance.hpp"

class FMinimalRenderer final : public NextGameInstanceBase
{
public:
    FMinimalRenderer(Vulkan::WindowConfig& config, Runtime::Config::Options& options, NextEngine* engine);

    void OnInit() override;
    void OnTick(double) override {}
    void OnDestroy() override {}
    bool OnRenderUI() override { return false; }

private:
    std::string sceneName_;
};
