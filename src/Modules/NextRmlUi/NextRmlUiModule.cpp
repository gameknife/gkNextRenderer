#include "Engine/Common/CoreMinimal.hpp"
#include "Modules/NextRmlUi/NextRmlUiModule.hpp"
#include "Modules/NextRmlUi/RmlUiSystem.hpp"
#include "Engine/Runtime/Engine.hpp"

namespace Modules::NextRmlUi
{
    void Install(NextEngine& engine)
    {
        engine.SetUiOverlayFactory([](NextEngine& owner) -> std::unique_ptr<Runtime::IUiOverlay>
        {
            return std::make_unique<NextUI::RmlUiSystem>(owner);
        });
    }

    NextUI::RmlUiSystem* Get(NextEngine& engine)
    {
        // The factory only ever creates RmlUiSystem instances, so the
        // downcast is safe for overlays installed via this module.
        return static_cast<NextUI::RmlUiSystem*>(engine.GetUiOverlay());
    }
}
