#pragma once

#include "Engine/Runtime/UiOverlay.hpp"

#include <memory>

class NextEngine;

namespace NextUI
{
    class RmlUiSystem;
}

namespace Modules::NextRmlUi
{
    // Installs the RmlUi overlay factory on the engine. Call from the
    // application entry before NextEngine::Start.
    void Install(NextEngine& engine);

    // Typed accessor for applications scripting RmlUi documents directly.
    // Returns nullptr until the engine has created the overlay.
    NextUI::RmlUiSystem* Get(NextEngine& engine);
}
