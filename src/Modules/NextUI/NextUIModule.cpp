#include "Modules/NextUI/NextUIModule.hpp"

#include "Engine/Runtime/Engine.hpp"
#include "Modules/NextUI/UserInterface.hpp"

namespace Modules::NextUI
{
    void Install(NextEngine& engine)
    {
        engine.SetUserInterfaceFactory(
            [](NextEngine& owner,
               std::function<void()> preConfig,
               std::function<void()> initialize,
               std::unique_ptr<::NextUI::IMultiViewportBackend> multiViewportBackend)
                -> std::unique_ptr<::NextUI::IUserInterface>
            {
                auto& renderer = owner.GetRenderer();
                return std::make_unique<::NextUI::UserInterface>(
                    &owner,
                    renderer.CommandPool(),
                    renderer.SwapChain(),
                    renderer.DepthBuffer(),
                    owner.GetUserSettings(),
                    std::move(preConfig),
                    std::move(initialize),
                    std::move(multiViewportBackend));
            });
    }
}
