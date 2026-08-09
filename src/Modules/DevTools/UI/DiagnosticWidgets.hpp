#pragma once

#include <imgui.h>
#include <string>

namespace Runtime::DevToolsUI
{
    const char* GetBoolStatusLabel(bool value);
    void DrawBadge(const char* text, const ImVec4& background, const ImVec4& foreground);
    void DrawSectionHeader(const char* title);
    void DrawValueRow(const char* label, const char* value, const ImVec4& valueColor);
    void DrawValueRow(const char* label,
                      const std::string& value,
                      const ImVec4& valueColor = ImVec4(0.93f, 0.96f, 1.0f, 1.0f));
    void DrawBooleanRow(const char* label, bool enabled);
    void DrawShortcutRow(const char* shortcut, const char* label, bool active);
}
