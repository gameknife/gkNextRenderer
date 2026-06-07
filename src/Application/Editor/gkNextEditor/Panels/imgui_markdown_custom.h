#pragma once

#include <string>

struct ImVec4;

namespace Editor
{
    // Render a Markdown string in the current ImGui window.
    //
    // Inline/heading/list/link/emphasis formatting is delegated to imgui_markdown
    // (ThirdParty/imgui_markdown). GitHub-flavored tables and fenced code blocks are
    // handled here, since imgui_markdown supports neither natively. List lines are
    // normalized to the leading-space form imgui_markdown expects.
    void RenderMarkdown(const std::string& text, const ImVec4& baseColor);
}
