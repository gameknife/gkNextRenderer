#include "Panels/imgui_markdown_custom.h"

#include <imgui.h>

// imgui_markdown is third-party; suppress its warnings so the editor's /WX build
// is not broken by upstream code.
#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#include "ThirdParty/imgui_markdown/imgui_markdown.h"
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include <string>
#include <vector>

namespace Editor
{
    namespace
    {
        // ---- string helpers ----

        std::string Trim(const std::string& s)
        {
            size_t b = s.find_first_not_of(" \t\r\n");
            if (b == std::string::npos)
            {
                return "";
            }
            size_t e = s.find_last_not_of(" \t\r\n");
            return s.substr(b, e - b + 1);
        }

        std::vector<std::string> SplitLines(const std::string& text)
        {
            std::vector<std::string> lines;
            std::string current;
            for (char c : text)
            {
                if (c == '\n')
                {
                    if (!current.empty() && current.back() == '\r')
                    {
                        current.pop_back();
                    }
                    lines.push_back(current);
                    current.clear();
                }
                else
                {
                    current += c;
                }
            }
            lines.push_back(current);
            return lines;
        }

        std::string StripEmphasisMarkers(const std::string& s)
        {
            std::string out;
            for (char c : s)
            {
                if (c != '*' && c != '`')
                {
                    out += c;
                }
            }
            return Trim(out);
        }

        // ---- inline rendering (for table cells) ----

        enum class EInline
        {
            Normal,
            Bold,
            Code
        };

        struct InlineSeg
        {
            EInline type;
            std::string text;
        };

        std::vector<InlineSeg> ParseInline(const std::string& text)
        {
            std::vector<InlineSeg> segs;
            std::string current;
            size_t i = 0;
            while (i < text.size())
            {
                if (i + 1 < text.size() && text[i] == '*' && text[i + 1] == '*')
                {
                    if (!current.empty())
                    {
                        segs.push_back({EInline::Normal, current});
                        current.clear();
                    }
                    i += 2;
                    size_t end = text.find("**", i);
                    if (end == std::string::npos)
                    {
                        current = "**";
                        continue;
                    }
                    segs.push_back({EInline::Bold, text.substr(i, end - i)});
                    i = end + 2;
                    continue;
                }
                if (text[i] == '`')
                {
                    if (!current.empty())
                    {
                        segs.push_back({EInline::Normal, current});
                        current.clear();
                    }
                    i += 1;
                    size_t end = text.find('`', i);
                    if (end == std::string::npos)
                    {
                        current = "`";
                        continue;
                    }
                    segs.push_back({EInline::Code, text.substr(i, end - i)});
                    i = end + 1;
                    continue;
                }
                current += text[i];
                ++i;
            }
            if (!current.empty())
            {
                segs.push_back({EInline::Normal, current});
            }
            return segs;
        }

        void RenderInline(const std::string& text, const ImVec4& baseColor)
        {
            auto segs = ParseInline(text);
            if (segs.empty())
            {
                ImGui::TextUnformatted("");
                return;
            }
            ImGui::PushTextWrapPos(0.0f);
            bool first = true;
            for (const auto& seg : segs)
            {
                if (!first)
                {
                    ImGui::SameLine(0, 0);
                }
                switch (seg.type)
                {
                case EInline::Bold:
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
                    break;
                case EInline::Code:
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.75f, 0.4f, 1.0f));
                    break;
                default:
                    ImGui::PushStyleColor(ImGuiCol_Text, baseColor);
                    break;
                }
                ImGui::TextUnformatted(seg.text.c_str());
                ImGui::PopStyleColor();
                first = false;
            }
            ImGui::PopTextWrapPos();
        }

        // ---- fenced code block ----

        void DrawCodeBlock(const std::string& code, const std::string& lang)
        {
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            const float padding = 6.0f;
            const float width = ImGui::GetContentRegionAvail().x;
            const float labelHeight = lang.empty() ? 0.0f : ImGui::GetTextLineHeight() + 2.0f;
            const ImVec2 textSize = ImGui::CalcTextSize(code.c_str(), nullptr, false, width - padding * 2);
            const float height = textSize.y + padding * 2 + labelHeight;

            const ImVec2 pos = ImGui::GetCursorScreenPos();

            drawList->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height), IM_COL32(25, 30, 40, 230), 4.0f);
            drawList->AddRect(pos, ImVec2(pos.x + width, pos.y + height), IM_COL32(55, 65, 85, 200), 4.0f);

            if (!lang.empty())
            {
                drawList->AddText(ImVec2(pos.x + padding, pos.y + 2.0f), IM_COL32(100, 120, 160, 200), lang.c_str());
            }

            ImGui::SetCursorScreenPos(ImVec2(pos.x + padding, pos.y + padding + labelHeight));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.82f, 0.88f, 0.78f, 1.0f));
            ImGui::PushTextWrapPos(pos.x + width - padding);
            ImGui::TextUnformatted(code.c_str());
            ImGui::PopTextWrapPos();
            ImGui::PopStyleColor();

            ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + height + 4.0f));
        }

        // ---- GFM table ----

        bool IsTableSeparator(const std::string& trimmed)
        {
            if (trimmed.empty() || trimmed.find('-') == std::string::npos)
            {
                return false;
            }
            for (char c : trimmed)
            {
                if (c != '|' && c != '-' && c != ':' && c != ' ' && c != '\t')
                {
                    return false;
                }
            }
            return true;
        }

        std::vector<std::string> SplitCells(const std::string& rowTrimmed)
        {
            std::string s = rowTrimmed;
            if (!s.empty() && s.front() == '|')
            {
                s.erase(s.begin());
            }
            if (!s.empty() && s.back() == '|')
            {
                s.pop_back();
            }
            std::vector<std::string> cells;
            std::string cell;
            bool escape = false;
            for (char c : s)
            {
                if (escape)
                {
                    cell += c;
                    escape = false;
                    continue;
                }
                if (c == '\\')
                {
                    escape = true;
                    continue;
                }
                if (c == '|')
                {
                    cells.push_back(Trim(cell));
                    cell.clear();
                    continue;
                }
                cell += c;
            }
            cells.push_back(Trim(cell));
            return cells;
        }

        void DrawTable(const std::string& block, int id)
        {
            std::vector<std::vector<std::string>> rows;
            for (const auto& line : SplitLines(block))
            {
                const std::string t = Trim(line);
                if (t.empty() || IsTableSeparator(t))
                {
                    continue;
                }
                rows.push_back(SplitCells(t));
            }
            if (rows.empty())
            {
                return;
            }

            size_t ncols = 0;
            for (const auto& r : rows)
            {
                ncols = (r.size() > ncols) ? r.size() : ncols;
            }
            if (ncols == 0)
            {
                return;
            }

            ImGui::PushID(id);
            const ImGuiTableFlags flags =
                ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp;
            if (ImGui::BeginTable("##md_table", static_cast<int>(ncols), flags))
            {
                for (size_t c = 0; c < ncols; ++c)
                {
                    const std::string name = c < rows[0].size() ? StripEmphasisMarkers(rows[0][c]) : std::string();
                    ImGui::TableSetupColumn(name.c_str());
                }
                ImGui::TableHeadersRow();

                for (size_t r = 1; r < rows.size(); ++r)
                {
                    ImGui::TableNextRow();
                    for (size_t c = 0; c < ncols; ++c)
                    {
                        ImGui::TableSetColumnIndex(static_cast<int>(c));
                        if (c < rows[r].size())
                        {
                            RenderInline(rows[r][c], ImVec4(0.85f, 0.85f, 0.85f, 1.0f));
                        }
                    }
                }
                ImGui::EndTable();
            }
            ImGui::PopID();
        }

        // ---- block splitter ----

        enum class EBlock
        {
            Text,
            Code,
            Table
        };

        struct Block
        {
            EBlock type;
            std::string content;
            std::string lang;
        };

        std::vector<Block> SplitBlocks(const std::string& text)
        {
            const std::vector<std::string> lines = SplitLines(text);
            std::vector<Block> blocks;
            std::string textAccum;

            auto flushText = [&]() {
                if (!textAccum.empty())
                {
                    blocks.push_back({EBlock::Text, textAccum, ""});
                    textAccum.clear();
                }
            };

            for (size_t i = 0; i < lines.size();)
            {
                const std::string raw = lines[i];
                const std::string t = Trim(raw);

                // Fenced code block.
                if (t.rfind("```", 0) == 0)
                {
                    flushText();
                    const std::string lang = Trim(t.substr(3));
                    std::string code;
                    ++i;
                    while (i < lines.size() && Trim(lines[i]).rfind("```", 0) != 0)
                    {
                        if (!code.empty())
                        {
                            code += "\n";
                        }
                        code += lines[i];
                        ++i;
                    }
                    if (i < lines.size())
                    {
                        ++i; // skip closing fence
                    }
                    blocks.push_back({EBlock::Code, code, lang});
                    continue;
                }

                // GFM table: a row containing '|' immediately followed by a separator row.
                if (t.find('|') != std::string::npos && i + 1 < lines.size() &&
                    IsTableSeparator(Trim(lines[i + 1])))
                {
                    flushText();
                    std::string tbl;
                    while (i < lines.size())
                    {
                        const std::string lt = Trim(lines[i]);
                        if (lt.empty() || lt.find('|') == std::string::npos)
                        {
                            break;
                        }
                        if (!tbl.empty())
                        {
                            tbl += "\n";
                        }
                        tbl += lines[i];
                        ++i;
                    }
                    blocks.push_back({EBlock::Table, tbl, ""});
                    continue;
                }

                textAccum += raw;
                textAccum += "\n";
                ++i;
            }
            flushText();
            return blocks;
        }

        // ---- list normalization for imgui_markdown ----

        std::string NormalizeListLine(const std::string& line)
        {
            size_t lead = 0;
            while (lead < line.size() && (line[lead] == ' ' || line[lead] == '\t'))
            {
                ++lead;
            }
            const std::string rest = line.substr(lead);

            // Unordered list: -, * or + followed by a space.
            if (rest.size() >= 2 && (rest[0] == '-' || rest[0] == '*' || rest[0] == '+') &&
                (rest[1] == ' ' || rest[1] == '\t'))
            {
                return std::string(lead + 2, ' ') + "* " + Trim(rest.substr(2));
            }

            // Ordered list: digits + '.' + space. imgui_markdown has no ordered list, so
            // render as indented text that keeps the number visible.
            size_t d = 0;
            while (d < rest.size() && rest[d] >= '0' && rest[d] <= '9')
            {
                ++d;
            }
            if (d > 0 && d + 1 < rest.size() && rest[d] == '.' && (rest[d + 1] == ' ' || rest[d + 1] == '\t'))
            {
                return std::string(lead + 2, ' ') + rest.substr(0, d) + ". " + Trim(rest.substr(d + 2));
            }

            return line;
        }

        std::string NormalizeText(const std::string& seg)
        {
            std::string out;
            for (const auto& line : SplitLines(seg))
            {
                out += NormalizeListLine(line);
                out += "\n";
            }
            return out;
        }

        // ---- imgui_markdown config ----

        void LinkCallback(ImGui::MarkdownLinkCallbackData) {}

        ImGui::MarkdownImageData ImageCallback(ImGui::MarkdownLinkCallbackData)
        {
            ImGui::MarkdownImageData data;
            data.isValid = false;
            return data;
        }

        void FormatCallback(const ImGui::MarkdownFormatInfo& info, bool start)
        {
            switch (info.type)
            {
            case ImGui::MarkdownFormatType::HEADING:
            {
                const float scale = info.level == 1 ? 1.3f : info.level == 2 ? 1.16f : 1.05f;
                if (start)
                {
                    ImGui::SetWindowFontScale(scale);
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
                }
                ImGui::defaultMarkdownFormatCallback(info, start);
                if (!start)
                {
                    ImGui::PopStyleColor();
                    ImGui::SetWindowFontScale(1.0f);
                }
                break;
            }
            case ImGui::MarkdownFormatType::EMPHASIS:
                // imgui_markdown only distinguishes single (*) vs strong (**) via level.
                if (start)
                {
                    ImGui::PushStyleColor(ImGuiCol_Text,
                                          info.level > 1 ? ImVec4(1.0f, 1.0f, 1.0f, 1.0f)
                                                         : ImVec4(0.85f, 0.9f, 1.0f, 1.0f));
                }
                else
                {
                    ImGui::PopStyleColor();
                }
                break;
            default:
                ImGui::defaultMarkdownFormatCallback(info, start);
                break;
            }
        }

        ImGui::MarkdownConfig& Config()
        {
            static ImGui::MarkdownConfig config = []() {
                ImGui::MarkdownConfig c;
                c.linkCallback = LinkCallback;
                c.imageCallback = ImageCallback;
                c.formatCallback = FormatCallback;
                c.linkIcon = "";
                c.headingFormats[0] = {nullptr, true};
                c.headingFormats[1] = {nullptr, true};
                c.headingFormats[2] = {nullptr, false};
                c.formatFlags = ImGuiMarkdownFormatFlags_GithubStyle;
                return c;
            }();
            return config;
        }
    } // namespace

    void RenderMarkdown(const std::string& text, const ImVec4& baseColor)
    {
        int tableId = 0;
        for (const auto& block : SplitBlocks(text))
        {
            switch (block.type)
            {
            case EBlock::Code:
                DrawCodeBlock(block.content, block.lang);
                ImGui::Spacing();
                break;
            case EBlock::Table:
                DrawTable(block.content, tableId++);
                ImGui::Spacing();
                break;
            case EBlock::Text:
            {
                const std::string normalized = NormalizeText(block.content);
                if (Trim(normalized).empty())
                {
                    break;
                }
                ImGui::PushStyleColor(ImGuiCol_Text, baseColor);
                ImGui::Markdown(normalized.c_str(), normalized.length(), Config());
                ImGui::PopStyleColor();
                break;
            }
            }
        }
    }
} // namespace Editor
