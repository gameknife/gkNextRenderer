#pragma once

#include <imgui.h>
#include <string>

namespace Utilities::UI
{
    // Shared About dialog for every shipped desktop target. Keeping one implementation
    // means the renderer and the editor cannot drift apart on app name, version or the
    // support information a bug report needs.
    //
    // Call every frame while `open` is true; it opens and drives the modal itself and
    // clears `open` when dismissed.
    void ShowAboutDialog(bool& open);

    // Project links used by the About dialog and the Help menu.
    inline constexpr const char* ProjectHomeUrl = "https://github.com/gameknife/gkNextRenderer";
    inline constexpr const char* ProjectIssuesUrl = "https://github.com/gameknife/gkNextRenderer/issues";
    inline constexpr const char* ProjectDocsUrl = "https://github.com/gameknife/gkNextRenderer/tree/main/docs";
}
