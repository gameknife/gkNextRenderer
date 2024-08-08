#pragma once

#include "ims_gui.h"

struct Statistics;

namespace Assets
{
    class Scene;    
}

void MainWindowStyle();
void MainWindowGUI(ImStudio::GUI & gui, const Assets::Scene* scene, const Statistics& statistics, ImGuiID id, bool firstRun);
