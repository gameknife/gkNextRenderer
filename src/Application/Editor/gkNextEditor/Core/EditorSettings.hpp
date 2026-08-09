#pragma once

namespace Editor
{
    struct EditorSettings
    {
        bool hoverHighlight = true;
        bool outlinerAutoScroll = true;
        bool gizmoSnap = false;
        float gizmoSnapTranslate = 1.0f;
        int32_t gizmoDefaultMode = 0;
        int32_t progressiveRenderResumeFrames = 8;
    };
}
