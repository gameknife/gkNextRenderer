#pragma once

#include "EditorGUI.h"

struct Statistics;

namespace Assets
{
    class Scene;    
}

void MainWindowStyle();
void MainWindowGUI(Editor::GUI & gui, const Assets::Scene* scene, const Statistics& statistics, ImGuiID id, bool firstRun);
