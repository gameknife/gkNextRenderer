#include "Application/Editor/gkNextEditor/Core/RecentScenes.hpp"
#include "Application/Editor/gkNextEditor/Core/EditorUiState.hpp"

#include <spdlog/spdlog.h>
#include <fstream>
#include <filesystem>

namespace Editor
{
    std::string RecentScenesFilePath()
    {
        return "recent_scenes.txt";
    }

    void LoadRecentScenes(EditorUiState& ui)
    {
        ui.recentScenes.clear();
        const std::string path = RecentScenesFilePath();
        std::ifstream file(path);
        if (!file.is_open())
        {
            return;
        }

        std::string line;
        while (std::getline(file, line))
        {
            if (line.empty()) continue;
            std::error_code ec;
            line = std::filesystem::weakly_canonical(line, ec).string();
            ui.recentScenes.push_back(line);
            if (ui.recentScenes.size() >= kRecentScenesCap) break;
        }
    }

    void SaveRecentScenes(const EditorUiState& ui)
    {
        const std::string path = RecentScenesFilePath();
        std::ofstream file(path, std::ios::trunc);
        if (!file.is_open())
        {
            SPDLOG_WARN("Failed to save recent scenes to {}", path);
            return;
        }
        for (const auto& scene : ui.recentScenes)
        {
            file << scene << '\n';
        }
    }

    void PushRecentScene(EditorUiState& ui, std::string path)
    {
        std::error_code ec;
        path = std::filesystem::weakly_canonical(path, ec).string();

        auto it = std::find(ui.recentScenes.begin(), ui.recentScenes.end(), path);
        if (it != ui.recentScenes.end())
        {
            ui.recentScenes.erase(it);
        }
        ui.recentScenes.insert(ui.recentScenes.begin(), path);

        if (ui.recentScenes.size() > kRecentScenesCap)
        {
            ui.recentScenes.resize(kRecentScenesCap);
        }

        SaveRecentScenes(ui);
    }
} // namespace Editor
