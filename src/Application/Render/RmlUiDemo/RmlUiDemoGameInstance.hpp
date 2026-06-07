#pragma once

#include "Engine/Runtime/GameInstance.hpp"

#include <string>
#include <vector>

namespace RmlUiDemo
{
    struct FDemoModule
    {
        std::string id;
        std::string title;
        std::string description;
    };

    class RmlUiDemoGameInstance final : public NextGameInstanceBase
    {
    public:
        RmlUiDemoGameInstance(Vulkan::WindowConfig& config, Runtime::Config::Options& options, NextEngine* engine);
        ~RmlUiDemoGameInstance() override = default;

        void OnInit() override;
        void OnTick(double deltaSeconds) override;
        void OnDestroy() override;
        bool OnRenderUI() override;
        bool ShouldRenderUiDuringScreenshot() const override { return true; }
        bool OnKey(SDL_Event& event) override;

    private:
        void RequestReload(std::string message = {});
        void SelectModule(size_t index);
        void RenderDocument();
        void BindActions();

        std::string BuildDocument();
        std::string BuildNavigation() const;
        std::string BuildToolbar() const;
        std::string BuildStatePanel() const;
        std::string BuildModal() const;
        std::string ReadAssetText(const std::string& path) const;
        std::string ReadOptionalAssetText(const std::string& path) const;

        const FDemoModule& CurrentModule() const;
        std::string CurrentPagePath() const;
        std::string CurrentPageCssPath() const;

        std::vector<FDemoModule> modules_;
        size_t currentModule_ = 0;
        bool documentDirty_ = true;
        bool modalOpen_ = false;
        bool darkTheme_ = true;
        bool compactMode_ = false;
        int counter_ = 0;
        double elapsedSeconds_ = 0.0;
        std::string statusMessage_;
    };
}
