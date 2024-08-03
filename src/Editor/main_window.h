#pragma once

#include "ims_object.h"
#include "ims_buffer.h"
#include "ims_gui.h"

namespace Assets
{
    class Scene;    
}

void MainWindowStyle();
void MainWindowsGUINoDocking(ImStudio::GUI & gui);
void MainWindowGUI(ImStudio::GUI & gui, const Assets::Scene* scene, ImTextureID viewportImage, ImVec2 viewportSize, ImGuiID id, bool firstRun);
