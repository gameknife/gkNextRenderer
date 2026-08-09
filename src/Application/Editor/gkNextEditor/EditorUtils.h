#if WIN32
#pragma warning( disable : 4005)
#endif

#pragma once

#include "imgui.h"

namespace utils
{

    void           ShowStyleEditorWindow          (bool *child_sty);
    void           ShowColorExportWindow          (bool *child_colexp);
    void           ShowResourcesWindow            (bool *child_resources);
    void           ShowAboutWindow                (bool *child_about);
}
