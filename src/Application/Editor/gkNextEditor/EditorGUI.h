#pragma once

// Compatibility shim: old code used Editor::GUI.
// New code should prefer Editor::EditorUiState and free Draw* functions in Application/Editor/gkNextEditor/EditorUi.hpp.

#include "Application/Editor/gkNextEditor/Core/EditorUiState.hpp"

namespace Editor
{
    using GUI = EditorUiState;
}
