#pragma once

#include "Common/CoreMinimal.hpp"

#include <string>
#include <vector>

namespace Editor
{
    struct EditorUiState;

    void LoadRecentScenes(EditorUiState& ui);
    void SaveRecentScenes(const EditorUiState& ui);
    void PushRecentScene(EditorUiState& ui, std::string path);
    std::string RecentScenesFilePath();
} // namespace Editor
