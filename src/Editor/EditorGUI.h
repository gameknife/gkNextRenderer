#pragma once

// Compatibility shim: old code used Editor::GUI.
// New code should prefer Editor::EditorUiState and free Draw* functions in Editor/EditorUi.hpp.

#include "Editor/Core/EditorUiState.hpp"

namespace Editor
{
    using GUI = EditorUiState;
}
