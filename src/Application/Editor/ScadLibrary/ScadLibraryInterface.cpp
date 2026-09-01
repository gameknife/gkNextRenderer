#define GLM_ENABLE_EXPERIMENTAL
#include "ScadLibraryInterface.hpp"

#include "AI/Adapters/KitModuleAIAdapter.hpp"
#include "AI/Adapters/RigClipAIAdapter.hpp"
#include "AI/Adapters/SceneObjectsAIAdapter.hpp"
#include "AI/Adapters/SceneSourceAIAdapter.hpp"
#include "AI/Adapters/TerrainProcessAIAdapter.hpp"
#include "AI/FixtureScadAITransport.hpp"
#include "AI/NextAIScadTransport.hpp"
#include "AI/ScadAIController.hpp"
#include "AI/ScadAIPanel.hpp"
#include "Application/Editor/Common/Preview/AssetThumbnailRenderer.hpp"
#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Rendering/VulkanBaseRenderer.hpp"
#include "Engine/Runtime/Components/RenderComponent.hpp"
#include "Modules/NextUI/ImGuiScaling.hpp"
#include "Engine/Runtime/Interface/UserInterface.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/Subsystems/TaskCoordinator.hpp"
#include "Engine/Runtime/Utilities/NextEngineHelper.hpp"
#include "Engine/Utilities/FileHelper.hpp"
#include "Engine/Utilities/Math.hpp"
#include "Engine/Vulkan/SwapChain.hpp"
#include "Modules/NextUI/UI/DesktopUI.hpp"
#include "Modules/ScadLoader/FScadEvaluator.h"
#include "Modules/ScadLoader/FScadLexer.h"
#include "Modules/ScadLoader/FScadParser.h"
#include "Modules/ScadLoader/FScadShared.h"
#include "Modules/ScadLoader/FScadSourceIndex.h"
#include "ThirdParty/ImGuizmo/ImGuizmo.h"
#include "ThirdParty/fontawesome/IconsFontAwesome6.h"

#include <imgui.h>
#include <imgui_freetype.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fmt/format.h>
#include <fmt/ranges.h>
#include <fstream>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/norm.hpp>
#include <glm/gtx/quaternion.hpp>
#include <limits>
#include <map>
#include <numeric>
#include <optional>
#include <regex>
#include <set>
#include <spdlog/spdlog.h>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace ScadLibrary
{
    namespace
    {
        constexpr float kTitleBarHeight = 48.0f;
        constexpr float kBottomBarHeight = 48.0f;
        constexpr float kCollapsedRailWidth = 46.0f;
        constexpr float kKitBrowserHeaderHeight = 56.0f;
        constexpr int kBenchGridColumns = 6;
        constexpr float kBenchGridStep = 14.0f;
        constexpr const char* kScadKitDragDropPayload = "SCAD_LIBRARY_KIT_MODULE";

        struct FScadKitDragPayload
        {
            int32_t kitIndex = -1;
            int32_t moduleIndex = -1;
        };

        struct FKitThumbnailControlResult
        {
            bool clicked = false;
            bool hovered = false;
        };

        std::string KitThumbnailDisplayName(const std::string& name)
        {
            const size_t kitPrefixEnd = name.find('_');
            if (kitPrefixEnd == std::string::npos)
            {
                return name;
            }
            const size_t categoryPrefixEnd = name.find('_', kitPrefixEnd + 1);
            if (categoryPrefixEnd == std::string::npos || categoryPrefixEnd + 1 >= name.size())
            {
                return name;
            }
            return name.substr(categoryPrefixEnd + 1);
        }

        std::array<std::string, 2> MakeKitThumbnailLabel(
            const std::string& name,
            const float maxWidth,
            const float fontScale)
        {
            std::array<std::string, 2> lines;
            size_t sourceOffset = 0;
            for (size_t lineIndex = 0; lineIndex < lines.size() && sourceOffset < name.size(); ++lineIndex)
            {
                std::string& line = lines[lineIndex];
                while (sourceOffset < name.size())
                {
                    const std::string candidate = line + name[sourceOffset];
                    if (!line.empty() && ImGui::CalcTextSize(candidate.c_str()).x * fontScale > maxWidth)
                    {
                        break;
                    }
                    line = candidate;
                    ++sourceOffset;
                }

                if (line.empty())
                {
                    line.assign(1, name[sourceOffset++]);
                }
            }

            if (sourceOffset < name.size())
            {
                std::string& lastLine = lines.back();
                constexpr std::string_view ellipsis = "...";
                while (!lastLine.empty() &&
                       ImGui::CalcTextSize((lastLine + std::string(ellipsis)).c_str()).x * fontScale > maxWidth)
                {
                    lastLine.pop_back();
                }
                lastLine += ellipsis;
            }
            return lines;
        }

        FKitThumbnailControlResult DrawKitThumbnailControl(
            const char* id,
            const ImTextureID textureId,
            const std::string& label,
            const float thumbnailSize,
            const bool selected)
        {
            const ImVec2 thumbnailPos = ImGui::GetCursorScreenPos();
            ImGui::PushStyleColor(ImGuiCol_Button,
                                  selected ? ImVec4(0.16f, 0.31f, 0.50f, 1.0f)
                                           : ImVec4(0.10f, 0.12f, 0.15f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.39f, 0.60f, 1.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
            const bool clicked = textureId != 0
                ? ImGui::ImageButton(id, textureId, ImVec2(thumbnailSize, thumbnailSize))
                : ImGui::Button(ICON_FA_CUBES_STACKED, ImVec2(thumbnailSize, thumbnailSize));
            const bool hovered = ImGui::IsItemHovered();
            ImGui::PopStyleVar(2);
            ImGui::PopStyleColor(2);

            ImDrawList* drawList = ImGui::GetWindowDrawList();
            const ImVec2 thumbnailEnd = thumbnailPos + ImVec2(thumbnailSize, thumbnailSize);

            constexpr float labelFontScale = 0.75f;
            const std::string displayLabel = KitThumbnailDisplayName(label);
            const std::array<std::string, 2> labelLines = MakeKitThumbnailLabel(
                displayLabel, thumbnailSize - 8.0f, labelFontScale);
            const int labelLineCount = labelLines[1].empty() ? 1 : 2;
            const float textLineHeight = ImGui::GetTextLineHeight() * labelFontScale;
            const float labelHeight = textLineHeight * static_cast<float>(labelLineCount) + 6.0f;
            const ImVec2 labelMin(thumbnailPos.x + 1.0f, thumbnailEnd.y - labelHeight - 1.0f);
            drawList->AddRectFilled(labelMin, thumbnailEnd - ImVec2(1.0f, 1.0f), IM_COL32(8, 10, 14, 210));
            for (int lineIndex = 0; lineIndex < labelLineCount; ++lineIndex)
            {
                const ImVec2 textSize = ImGui::CalcTextSize(labelLines[lineIndex].c_str()) * labelFontScale;
                drawList->AddText(
                    ImGui::GetFont(), ImGui::GetFontSize() * labelFontScale,
                    ImVec2(thumbnailPos.x + (thumbnailSize - textSize.x) * 0.5f,
                           labelMin.y + 3.0f + textLineHeight * static_cast<float>(lineIndex)),
                    ImGui::GetColorU32(ImGuiCol_Text), labelLines[lineIndex].c_str());
            }

            // Borders are deliberately emitted last so the label overlay can never hide interaction state.
            drawList->AddRect(thumbnailPos + ImVec2(0.5f, 0.5f), thumbnailEnd - ImVec2(0.5f, 0.5f),
                              IM_COL32(92, 104, 122, 210), 0.0f, 0, 1.0f);
            if (selected || hovered)
            {
                const float stateBorderWidth = selected ? 2.0f : 1.0f;
                const float stateInset = stateBorderWidth * 0.5f;
                drawList->AddRect(
                    thumbnailPos + ImVec2(stateInset, stateInset),
                    thumbnailEnd - ImVec2(stateInset, stateInset),
                    selected ? IM_COL32(91, 173, 255, 255) : IM_COL32(160, 205, 255, 235),
                    0.0f, 0, stateBorderWidth);
            }
            return {.clicked = clicked, .hovered = hovered};
        }

        uint64_t Fnv1a64(std::string_view value)
        {
            uint64_t hash = 1469598103934665603ull;
            for (const unsigned char character : value)
            {
                hash ^= character;
                hash *= 1099511628211ull;
            }
            return hash;
        }

        std::string PreviewFileToken(std::string value)
        {
            for (char& character : value)
            {
                if (!std::isalnum(static_cast<unsigned char>(character)) && character != '_' && character != '-')
                {
                    character = '_';
                }
            }
            return value;
        }

        struct FScadRawArgument
        {
            std::string name;
            std::string source;
            bool named = false;
        };

        struct FScadParameterEditor
        {
            std::string name;
            std::string defaultSource;
            std::string source;
            Assets::Scad::ExprPtr expression;
            bool explicitValue = false;
        };

        std::string TrimScadText(std::string_view text)
        {
            size_t begin = 0;
            while (begin < text.size() && std::isspace(static_cast<unsigned char>(text[begin])))
            {
                ++begin;
            }
            size_t end = text.size();
            while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1])))
            {
                --end;
            }
            return std::string(text.substr(begin, end - begin));
        }

        std::vector<std::string> SplitTopLevelScadArguments(const std::string& source)
        {
            std::vector<std::string> parts;
            if (TrimScadText(source).empty())
            {
                return parts;
            }

            size_t begin = 0;
            int parentheses = 0;
            int brackets = 0;
            int braces = 0;
            bool quoted = false;
            bool escaped = false;
            for (size_t index = 0; index < source.size(); ++index)
            {
                const char character = source[index];
                if (quoted)
                {
                    if (escaped)
                    {
                        escaped = false;
                    }
                    else if (character == '\\')
                    {
                        escaped = true;
                    }
                    else if (character == '"')
                    {
                        quoted = false;
                    }
                    continue;
                }
                if (character == '"')
                {
                    quoted = true;
                    continue;
                }
                switch (character)
                {
                case '(':
                    ++parentheses;
                    break;
                case ')':
                    parentheses = std::max(0, parentheses - 1);
                    break;
                case '[':
                    ++brackets;
                    break;
                case ']':
                    brackets = std::max(0, brackets - 1);
                    break;
                case '{':
                    ++braces;
                    break;
                case '}':
                    braces = std::max(0, braces - 1);
                    break;
                case ',':
                    if (parentheses == 0 && brackets == 0 && braces == 0)
                    {
                        parts.push_back(TrimScadText(std::string_view(source).substr(begin, index - begin)));
                        begin = index + 1;
                    }
                    break;
                default:
                    break;
                }
            }
            parts.push_back(TrimScadText(std::string_view(source).substr(begin)));
            parts.erase(std::remove_if(parts.begin(), parts.end(), [](const std::string& part) { return part.empty(); }),
                        parts.end());
            return parts;
        }

        std::optional<size_t> FindTopLevelScadAssignment(const std::string& source)
        {
            int parentheses = 0;
            int brackets = 0;
            int braces = 0;
            bool quoted = false;
            bool escaped = false;
            for (size_t index = 0; index < source.size(); ++index)
            {
                const char character = source[index];
                if (quoted)
                {
                    if (escaped)
                    {
                        escaped = false;
                    }
                    else if (character == '\\')
                    {
                        escaped = true;
                    }
                    else if (character == '"')
                    {
                        quoted = false;
                    }
                    continue;
                }
                if (character == '"')
                {
                    quoted = true;
                    continue;
                }
                switch (character)
                {
                case '(':
                    ++parentheses;
                    break;
                case ')':
                    parentheses = std::max(0, parentheses - 1);
                    break;
                case '[':
                    ++brackets;
                    break;
                case ']':
                    brackets = std::max(0, brackets - 1);
                    break;
                case '{':
                    ++braces;
                    break;
                case '}':
                    braces = std::max(0, braces - 1);
                    break;
                case '=':
                    if (parentheses == 0 && brackets == 0 && braces == 0 &&
                        (index == 0 || source[index - 1] != '=') &&
                        (index + 1 >= source.size() || source[index + 1] != '='))
                    {
                        return index;
                    }
                    break;
                default:
                    break;
                }
            }
            return std::nullopt;
        }

        bool IsScadIdentifier(const std::string& text)
        {
            if (text.empty())
            {
                return false;
            }
            const auto isFirst = [](unsigned char character)
            { return std::isalpha(character) || character == '_' || character == '$'; };
            const auto isRest = [](unsigned char character)
            { return std::isalnum(character) || character == '_' || character == '$'; };
            if (!isFirst(static_cast<unsigned char>(text.front())))
            {
                return false;
            }
            return std::all_of(text.begin() + 1, text.end(),
                               [&](char character) { return isRest(static_cast<unsigned char>(character)); });
        }

        bool ParseScadExpression(const std::string& source, Assets::Scad::ExprPtr& outExpression)
        {
            if (TrimScadText(source).empty())
            {
                outExpression.reset();
                return false;
            }
            const std::string wrapper = fmt::format(
                "module __scadlibrary_expression_probe__() {{ __scadlibrary_expression__({}); }}", source);
            std::vector<Assets::Scad::Token> tokens;
            std::string error;
            Assets::Scad::Scope scope;
            if (!Assets::Scad::ScadLexer::Tokenize(wrapper, tokens, error) ||
                !Assets::Scad::ScadParser::Parse(tokens, scope, error) || scope.size() != 1 ||
                scope.front() == nullptr || scope.front()->body.size() != 1 ||
                scope.front()->body.front() == nullptr || scope.front()->body.front()->args.size() != 1)
            {
                outExpression.reset();
                return false;
            }
            outExpression = scope.front()->body.front()->args.front().value;
            return outExpression != nullptr;
        }

        bool ParseScadModuleParameters(const FKitModuleInfo& moduleInfo,
                                       std::vector<FScadParameterEditor>& outParameters)
        {
            outParameters.clear();
            for (const std::string& part : SplitTopLevelScadArguments(moduleInfo.params))
            {
                const std::optional<size_t> assignment = FindTopLevelScadAssignment(part);
                const std::string name = TrimScadText(
                    assignment ? std::string_view(part).substr(0, *assignment) : std::string_view(part));
                if (!IsScadIdentifier(name))
                {
                    outParameters.clear();
                    return false;
                }
                FScadParameterEditor parameter;
                parameter.name = name;
                if (assignment)
                {
                    parameter.defaultSource = TrimScadText(std::string_view(part).substr(*assignment + 1));
                }
                outParameters.push_back(std::move(parameter));
            }
            return true;
        }

        std::vector<FScadRawArgument> ParseScadCallArguments(const std::string& source)
        {
            std::vector<FScadRawArgument> arguments;
            for (const std::string& part : SplitTopLevelScadArguments(source))
            {
                const std::optional<size_t> assignment = FindTopLevelScadAssignment(part);
                if (assignment)
                {
                    const std::string name = TrimScadText(std::string_view(part).substr(0, *assignment));
                    if (IsScadIdentifier(name))
                    {
                        arguments.push_back({name, TrimScadText(std::string_view(part).substr(*assignment + 1)), true});
                        continue;
                    }
                }
                arguments.push_back({{}, part, false});
            }
            return arguments;
        }

        std::vector<FScadParameterEditor> MakeScadParameterEditors(const FKitModuleInfo& moduleInfo,
                                                                     const std::string& currentArguments,
                                                                     std::vector<FScadRawArgument>& outUnknown)
        {
            std::vector<FScadParameterEditor> parameters;
            if (!ParseScadModuleParameters(moduleInfo, parameters))
            {
                return parameters;
            }
            const std::vector<FScadRawArgument> arguments = ParseScadCallArguments(currentArguments);
            std::vector<bool> used(arguments.size(), false);
            size_t positionalIndex = 0;
            for (FScadParameterEditor& parameter : parameters)
            {
                for (size_t index = 0; index < arguments.size(); ++index)
                {
                    if (!used[index] && arguments[index].named && arguments[index].name == parameter.name)
                    {
                        parameter.source = arguments[index].source;
                        parameter.explicitValue = true;
                        used[index] = true;
                        break;
                    }
                }
                if (!parameter.explicitValue)
                {
                    while (positionalIndex < arguments.size() &&
                           (used[positionalIndex] || arguments[positionalIndex].named))
                    {
                        ++positionalIndex;
                    }
                    if (positionalIndex < arguments.size())
                    {
                        parameter.source = arguments[positionalIndex].source;
                        parameter.explicitValue = true;
                        used[positionalIndex] = true;
                        ++positionalIndex;
                    }
                    else
                    {
                        parameter.source = parameter.defaultSource;
                    }
                }
                ParseScadExpression(parameter.source, parameter.expression);
            }
            for (size_t index = 0; index < arguments.size(); ++index)
            {
                if (!used[index])
                {
                    outUnknown.push_back(arguments[index]);
                }
            }
            return parameters;
        }

        std::string ScadParameterValueSource(const FScadParameterEditor& parameter)
        {
            if (parameter.expression == nullptr)
            {
                return parameter.source;
            }
            return parameter.source;
        }

        std::string ComposeScadArguments(const std::vector<FScadParameterEditor>& parameters,
                                         const std::vector<FScadRawArgument>& unknown)
        {
            std::string result;
            const auto append = [&](const std::string& value)
            {
                if (value.empty())
                {
                    return;
                }
                if (!result.empty())
                {
                    result += ", ";
                }
                result += value;
            };
            for (const FScadParameterEditor& parameter : parameters)
            {
                if (!parameter.source.empty())
                {
                    append(fmt::format("{} = {}", parameter.name, ScadParameterValueSource(parameter)));
                }
            }
            for (const FScadRawArgument& argument : unknown)
            {
                append(argument.named ? fmt::format("{} = {}", argument.name, argument.source)
                                       : argument.source);
            }
            return result;
        }

        bool IsColorScadParameter(const std::string& name)
        {
            std::string lower;
            lower.reserve(name.size());
            for (const char character : name)
            {
                lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
            }
            return lower == "c" || lower == "color" || lower == "colour" || lower == "skin" ||
                lower.ends_with("color") || lower.ends_with("colour");
        }

        std::string ScadParameterDescription(const std::string& name)
        {
            std::string lower;
            lower.reserve(name.size());
            for (const char character : name)
            {
                lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
            }
            if (lower == "seed" || lower.ends_with("seed"))
            {
                return "随机种子：相同 seed 会产生相同的确定性变体";
            }
            if (IsColorScadParameter(name))
            {
                return "颜色参数：通常为 RGB 或 RGBA 向量";
            }
            if (lower == "plinth" || lower == "dual" || lower == "laptop" || lower == "tablet" ||
                lower == "sticky" || lower == "closed" || lower == "porch" || lower == "randomrotation")
            {
                return "布尔开关参数";
            }
            if (lower == "l" || lower == "w" || lower == "d" || lower == "h" || lower == "t" ||
                lower == "len" || lower == "width" || lower == "height" || lower == "depth" ||
                lower == "radius" || lower == "r")
            {
                return "几何尺寸参数，单位遵循当前 Kit 的 scaleClass";
            }
            return "SCAD 模块参数";
        }

        bool ReadScadNumericLiteral(const Assets::Scad::ExprPtr& expression, double& outValue)
        {
            if (expression == nullptr)
            {
                return false;
            }
            if (expression->kind == Assets::Scad::ExprKind::Number)
            {
                outValue = expression->num;
                return true;
            }
            if (expression->kind == Assets::Scad::ExprKind::Unary && expression->list.size() == 1 &&
                expression->list.front() != nullptr &&
                expression->list.front()->kind == Assets::Scad::ExprKind::Number &&
                (expression->str == "+" || expression->str == "-"))
            {
                outValue = expression->str == "-" ? -expression->list.front()->num : expression->list.front()->num;
                return true;
            }
            return false;
        }

        std::string QuoteScadString(const std::string& value)
        {
            std::string result;
            result.reserve(value.size() + 2);
            result.push_back('"');
            for (const char character : value)
            {
                if (character == '\\' || character == '"')
                {
                    result.push_back('\\');
                }
                result.push_back(character);
            }
            result.push_back('"');
            return result;
        }

        std::string ReadScadModuleComment(const FKitInfo& kit, const FKitModuleInfo& module)
        {
            if (kit.filePath.empty() || module.line <= 1)
            {
                return {};
            }

            static std::unordered_map<std::string, std::string> commentCache;
            const std::string cacheKey = fmt::format("{}#{}", kit.filePath, module.line);
            const auto cached = commentCache.find(cacheKey);
            if (cached != commentCache.end())
            {
                return cached->second;
            }

            std::ifstream input(kit.filePath, std::ios::binary);
            if (!input)
            {
                return {};
            }
            std::vector<std::string> lines;
            std::string line;
            while (std::getline(input, line))
            {
                lines.push_back(std::move(line));
            }
            std::string comment;
            const size_t moduleLine = static_cast<size_t>(module.line - 1);
            if (moduleLine <= lines.size())
            {
                for (size_t index = moduleLine; index > 0;)
                {
                    --index;
                    const std::string trimmed = TrimScadText(lines[index]);
                    if (trimmed.empty())
                    {
                        break;
                    }
                    if (!trimmed.starts_with("//"))
                    {
                        break;
                    }
                    const std::string text = TrimScadText(std::string_view(trimmed).substr(2));
                    if (text.empty() || std::all_of(text.begin(), text.end(), [](unsigned char character)
                                                     { return character == '-' || character == '=' || character == ' '; }))
                    {
                        continue;
                    }
                    comment = comment.empty() ? text : text + "\n" + comment;
                }
            }
            commentCache.emplace(cacheKey, comment);
            return comment;
        }

        // Kit file change watch: how often the gather task runs, and how long a
        // detected change is debounced before the preview reload actually fires
        // (merges consecutive writes from an external agent).
        constexpr double kKitWatchPollIntervalSeconds = 0.5;
        constexpr std::chrono::milliseconds kKitWatchReloadDebounce{300};

        // The folder a scene is filed under. It is a browsing convention only:
        // any of these directories may hold any mix of instances, terrain and
        // free source, and every scene gets every editor it has content for.
        std::optional<EScadSceneFolder> SceneFolderFromRelativePath(const std::filesystem::path& relativePath)
        {
            if (relativePath.empty())
            {
                return std::nullopt;
            }
            const std::string root = relativePath.begin()->string();
            if (root == "evaluated")
            {
                return EScadSceneFolder::Evaluated;
            }
            if (root == "source")
            {
                return EScadSceneFolder::Source;
            }
            if (root == "proc")
            {
                return EScadSceneFolder::Procedural;
            }
            return std::nullopt;
        }

        // Kits and rigs are authored elsewhere; everything else under
        // assets/scad is openable as a scene.
        bool IsSceneAssemblyRelativePath(const std::filesystem::path& relativePath)
        {
            if (relativePath.empty())
            {
                return false;
            }
            const std::string root = relativePath.begin()->string();
            return root != "lib" && root != "characters" && root != "specs";
        }

        const char* SceneFolderLabel(EScadSceneFolder folder)
        {
            switch (folder)
            {
            case EScadSceneFolder::Evaluated:
                return "Evaluated";
            case EScadSceneFolder::Source:
                return "Source";
            case EScadSceneFolder::Procedural:
                return "Proc";
            }
            return "Scene";
        }

        const char* SceneFolderIcon(EScadSceneFolder folder)
        {
            switch (folder)
            {
            case EScadSceneFolder::Evaluated:
                return ICON_FA_CUBE;
            case EScadSceneFolder::Source:
                return ICON_FA_FILE_CODE;
            case EScadSceneFolder::Procedural:
                return ICON_FA_TROWEL_BRICKS;
            }
            return ICON_FA_FILE;
        }

        ImVec4 SceneFolderIconColor(EScadSceneFolder folder)
        {
            switch (folder)
            {
            case EScadSceneFolder::Evaluated:
                return ImVec4(1.0f, 0.68f, 0.25f, 1.0f);
            case EScadSceneFolder::Source:
                return ImVec4(0.36f, 0.70f, 1.0f, 1.0f);
            case EScadSceneFolder::Procedural:
                return ImVec4(0.40f, 0.86f, 0.56f, 1.0f);
            }
            return ImVec4(0.70f, 0.72f, 0.76f, 1.0f);
        }

        ImVec4 SegmentKindColor(EScadSegmentKind kind)
        {
            switch (kind)
            {
            case EScadSegmentKind::Instance:
                return ImVec4(1.0f, 0.68f, 0.25f, 1.0f);
            case EScadSegmentKind::Terrain:
                return ImVec4(0.40f, 0.86f, 0.56f, 1.0f);
            case EScadSegmentKind::TerrainRule:
                return ImVec4(0.52f, 0.84f, 0.72f, 1.0f);
            case EScadSegmentKind::Source:
                return ImVec4(0.36f, 0.70f, 1.0f, 1.0f);
            }
            return ImVec4(0.70f, 0.72f, 0.76f, 1.0f);
        }

        const char* SegmentKindIcon(EScadSegmentKind kind)
        {
            switch (kind)
            {
            case EScadSegmentKind::Instance:
                return ICON_FA_CUBE;
            case EScadSegmentKind::Terrain:
                return ICON_FA_MOUNTAIN_SUN;
            case EScadSegmentKind::TerrainRule:
                return ICON_FA_TROWEL_BRICKS;
            case EScadSegmentKind::Source:
                return ICON_FA_FILE_CODE;
            }
            return ICON_FA_FILE;
        }

        bool FindMatchingParenthesis(const std::string& source, size_t open, size_t& outClose)
        {
            int depth = 0;
            for (size_t index = open; index < source.size(); ++index)
            {
                if (source[index] == '(')
                {
                    ++depth;
                }
                else if (source[index] == ')' && --depth == 0)
                {
                    outClose = index;
                    return true;
                }
            }
            return false;
        }

        std::vector<std::string> SplitScadArguments(std::string_view source)
        {
            std::vector<std::string> arguments;
            size_t begin = 0;
            int depth = 0;
            for (size_t index = 0; index <= source.size(); ++index)
            {
                if (index < source.size() && (source[index] == '(' || source[index] == '[' || source[index] == '{'))
                {
                    ++depth;
                }
                else if (index < source.size() && (source[index] == ')' || source[index] == ']' || source[index] == '}'))
                {
                    --depth;
                }
                if (index == source.size() || (source[index] == ',' && depth == 0))
                {
                    arguments.push_back(TrimScadText(source.substr(begin, index - begin)));
                    begin = index + 1;
                }
            }
            return arguments;
        }

        bool ParseScadNumber(std::string_view source, double& outValue)
        {
            const std::string text = TrimScadText(source);
            char* end = nullptr;
            outValue = std::strtod(text.c_str(), &end);
            return end != text.c_str() && TrimScadText(end).empty();
        }

        struct FLayScatterSource
        {
            int count = 10;
            double x0 = -20.0;
            double x1 = 20.0;
            double y0 = -20.0;
            double y1 = 20.0;
            int seed = 0;
            bool rotate = true;
            std::string prefix;
            std::string suffix;
        };

        bool ParseLayScatterSource(const std::string& source, FLayScatterSource& outValue)
        {
            const size_t name = source.find("lay_scatter");
            const size_t open = name == std::string::npos ? std::string::npos : source.find('(', name + 11);
            size_t close = std::string::npos;
            if (open == std::string::npos || !FindMatchingParenthesis(source, open, close))
            {
                return false;
            }
            outValue.prefix = source.substr(0, name);
            if (TrimScadText(outValue.prefix).starts_with("module"))
            {
                // A module declaration is library API, not a placed layout
                // operator. Only edit call sites in a scene assembly here.
                return false;
            }
            outValue.suffix = source.substr(close + 1);
            const std::array<const char*, 7> ordered = {"n", "x0", "x1", "y0", "y1", "seed", "rot"};
            size_t positional = 0;
            for (const std::string& rawArgument : SplitScadArguments(std::string_view(source).substr(open + 1, close - open - 1)))
            {
                const size_t equals = rawArgument.find('=');
                const std::string key = equals == std::string::npos
                    ? (positional < ordered.size() ? ordered[positional++] : std::string())
                    : TrimScadText(std::string_view(rawArgument).substr(0, equals));
                const std::string value = TrimScadText(std::string_view(rawArgument).substr(
                    equals == std::string::npos ? 0 : equals + 1));
                double number = 0.0;
                if ((key == "n" || key == "x0" || key == "x1" || key == "y0" || key == "y1" || key == "seed") &&
                    !ParseScadNumber(value, number))
                {
                    return false;
                }
                if (key == "n") outValue.count = static_cast<int>(std::lround(number));
                else if (key == "x0") outValue.x0 = number;
                else if (key == "x1") outValue.x1 = number;
                else if (key == "y0") outValue.y0 = number;
                else if (key == "y1") outValue.y1 = number;
                else if (key == "seed") outValue.seed = static_cast<int>(std::lround(number));
                else if (key == "rot")
                {
                    if (value == "true" || value == "1") outValue.rotate = true;
                    else if (value == "false" || value == "0") outValue.rotate = false;
                    else return false;
                }
            }
            return true;
        }

        std::string SerializeLayScatterSource(const FLayScatterSource& value)
        {
            return fmt::format("{}lay_scatter(n = {}, x0 = {:.6g}, x1 = {:.6g}, y0 = {:.6g}, y1 = {:.6g}, seed = {}, rot = {}){}",
                               value.prefix, value.count, value.x0, value.x1, value.y0, value.y1, value.seed,
                               value.rotate ? "true" : "false", value.suffix);
        }

        std::string LowercaseAscii(std::string value)
        {
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character)
            {
                return static_cast<char>(std::tolower(character));
            });
            return value;
        }

        bool HasSuffix(const std::string& value, const std::string& suffix)
        {
            return value.size() >= suffix.size() &&
                value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
        }

        std::string NormalizeSceneCategoryName(std::string value)
        {
            value = LowercaseAscii(std::move(value));
            if (value == "deadly")
            {
                return "brotato3d";
            }
            if (value == "tw")
            {
                return "nexttotalwar";
            }
            return value;
        }

        std::string SceneCategoryKeyFromRelativePath(const std::filesystem::path& relativePath)
        {
            const std::filesystem::path parent = relativePath.parent_path();
            const std::string root = relativePath.empty()
                ? std::string()
                : LowercaseAscii(relativePath.begin()->string());
            const std::string stem = LowercaseAscii(relativePath.stem().string());

            std::string folder;
            auto parentIt = parent.begin();
            if (parentIt != parent.end())
            {
                const auto categoryIt = std::next(parentIt);
                if (categoryIt != parent.end())
                {
                    folder = LowercaseAscii(categoryIt->string());
                }
            }

            if (folder == "generated")
            {
                return "generated/" + root;
            }

            std::string category = folder;
            if (category.empty() && HasSuffix(stem, "_showcase"))
            {
                category = stem.substr(0, stem.size() - std::string("_showcase").size());
            }
            if (category.empty())
            {
                return root.empty() ? "other" : root;
            }

            // The temporary `others` bucket still contains a few named
            // showcase scenes. Promote those names to their real game group.
            if (category == "others" && stem.starts_with("coldwar_"))
            {
                category = "coldwar";
            }
            category = NormalizeSceneCategoryName(std::move(category));
            if (category == "others")
            {
                return "other/others";
            }
            if (root == "evaluated")
            {
                return "evaluated/" + category;
            }
            return "showcase/" + category;
        }

        std::string SceneCategoryDisplayName(std::string category)
        {
            category = NormalizeSceneCategoryName(std::move(category));
            if (category == "brotato3d")
            {
                return "brotato3d";
            }
            if (category == "coldwar")
            {
                return "coldwar";
            }
            if (category == "nexttotalwar")
            {
                return "nexttotalwar";
            }
            std::replace(category.begin(), category.end(), '_', ' ');
            if (!category.empty())
            {
                category[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(category[0])));
            }
            return category;
        }

        std::string SceneCategoryLabelFromKey(const std::string& key)
        {
            if (key == "evaluated")
            {
                return "对象场景 / Evaluated";
            }
            if (key == "source")
            {
                return "普通源码场景 / Source";
            }
            if (key == "proc")
            {
                return "普通过程场景 / Proc";
            }
            if (key.starts_with("showcase/"))
            {
                return SceneCategoryDisplayName(key.substr(std::string("showcase/").size()));
            }
            if (key.starts_with("generated/"))
            {
                return "生成场景 / " + SceneCategoryDisplayName(key.substr(std::string("generated/").size()));
            }
            if (key.starts_with("evaluated/"))
            {
                return "对象场景 / " + SceneCategoryDisplayName(key.substr(std::string("evaluated/").size()));
            }
            if (key.starts_with("other/"))
            {
                return "其它场景 / " + SceneCategoryDisplayName(key.substr(std::string("other/").size()));
            }
            return "其它场景 / " + SceneCategoryDisplayName(key);
        }

        int SceneCategorySortRank(const std::string& key)
        {
            if (key.starts_with("showcase/"))
            {
                return 0;
            }
            if (key == "evaluated")
            {
                return 10;
            }
            if (key.starts_with("evaluated/"))
            {
                return 11;
            }
            if (key.starts_with("generated/"))
            {
                return 20;
            }
            if (key == "source")
            {
                return 30;
            }
            if (key == "proc")
            {
                return 31;
            }
            return 40;
        }

        glm::mat4 SceneObjectWorldMatrix(const FBenchItem& item)
        {
            const glm::dmat4 scadMatrix = glm::translate(glm::dmat4(1.0), glm::dvec3(item.x, item.y, item.z)) *
                Assets::Scad::ScadRotateXYZ(glm::dvec3(item.rotX, item.rotY, item.rotZ)) *
                glm::scale(glm::dmat4(1.0), glm::dvec3(item.scale, item.scaleY, item.scaleZ));
            const glm::dmat4 basis = Assets::Scad::ScadToWorldBasis(1.0);
            return glm::mat4(basis * scadMatrix * glm::inverse(basis));
        }

        void AccumulateNodeBounds(const Assets::Scene& scene, const Assets::Node& node, glm::vec3& minBounds,
                                  glm::vec3& maxBounds, bool& found)
        {
            glm::vec3 center;
            float radius = 0.0f;
            if (scene.GetNodeBounds(node.GetInstanceId(), center, radius))
            {
                const glm::vec3 extent(radius);
                minBounds = glm::min(minBounds, center - extent);
                maxBounds = glm::max(maxBounds, center + extent);
                found = true;
            }

            for (const std::shared_ptr<Assets::Node>& child : node.Children())
            {
                if (child != nullptr)
                {
                    AccumulateNodeBounds(scene, *child, minBounds, maxBounds, found);
                }
            }
        }

        void AccumulateNodeLocalBounds(const Assets::Scene& scene, const Assets::Node& node,
                                       const glm::mat4& rootWorldInverse, glm::vec3& minBounds,
                                       glm::vec3& maxBounds, bool& found)
        {
            if (const auto render = node.GetComponent<Runtime::RenderComponent>())
            {
                if (const Assets::Model* model = scene.GetModel(render->GetModelId()))
                {
                    const glm::vec3 localMin = model->GetLocalAABBMin();
                    const glm::vec3 localMax = model->GetLocalAABBMax();
                    const glm::mat4 nodeToRoot = rootWorldInverse * node.WorldTransform();
                    for (int corner = 0; corner < 8; ++corner)
                    {
                        const glm::vec3 point(
                            (corner & 1) != 0 ? localMax.x : localMin.x,
                            (corner & 2) != 0 ? localMax.y : localMin.y,
                            (corner & 4) != 0 ? localMax.z : localMin.z);
                        const glm::vec3 rootPoint = glm::vec3(nodeToRoot * glm::vec4(point, 1.0f));
                        minBounds = glm::min(minBounds, rootPoint);
                        maxBounds = glm::max(maxBounds, rootPoint);
                        found = true;
                    }
                }
            }

            for (const std::shared_ptr<Assets::Node>& child : node.Children())
            {
                if (child != nullptr)
                {
                    AccumulateNodeLocalBounds(scene, *child, rootWorldInverse, minBounds, maxBounds, found);
                }
            }
        }

        bool GetNodeOrientedBounds(const Assets::Scene& scene, const Assets::Node& root, glm::vec3& localMin,
                                   glm::vec3& localMax)
        {
            localMin = glm::vec3(FLT_MAX);
            localMax = glm::vec3(-FLT_MAX);
            bool found = false;
            AccumulateNodeLocalBounds(scene, root, glm::inverse(root.WorldTransform()), localMin, localMax, found);
            return found;
        }

        bool IntersectOrientedBox(const glm::vec3& rayOrigin, const glm::vec3& rayDirection,
                                  const glm::mat4& worldTransform, const glm::vec3& localMin,
                                  const glm::vec3& localMax, float& outDistance)
        {
            const glm::mat4 worldInverse = glm::inverse(worldTransform);
            const glm::vec3 localOrigin = glm::vec3(worldInverse * glm::vec4(rayOrigin, 1.0f));
            const glm::vec3 localDirection = glm::vec3(worldInverse * glm::vec4(rayDirection, 0.0f));
            float entry = 0.0f;
            float exit = std::numeric_limits<float>::max();
            for (int axis = 0; axis < 3; ++axis)
            {
                if (std::abs(localDirection[axis]) < 1.0e-6f)
                {
                    if (localOrigin[axis] < localMin[axis] || localOrigin[axis] > localMax[axis])
                    {
                        return false;
                    }
                    continue;
                }
                float nearDistance = (localMin[axis] - localOrigin[axis]) / localDirection[axis];
                float farDistance = (localMax[axis] - localOrigin[axis]) / localDirection[axis];
                if (nearDistance > farDistance)
                {
                    std::swap(nearDistance, farDistance);
                }
                entry = std::max(entry, nearDistance);
                exit = std::min(exit, farDistance);
                if (entry > exit)
                {
                    return false;
                }
            }
            outDistance = entry;
            return true;
        }

        void DrawOrientedBoxOverlay(const Assets::UniformBufferObject& ubo, const ImVec2& viewportPos,
                                    const ImVec2& viewportSize, const glm::mat4& worldTransform,
                                    const glm::vec3& localMin, const glm::vec3& localMax, const ImU32 color)
        {
            ImVec2 corners[8];
            bool projected[8]{};
            for (int corner = 0; corner < 8; ++corner)
            {
                const glm::vec3 point(
                    (corner & 1) != 0 ? localMax.x : localMin.x,
                    (corner & 2) != 0 ? localMax.y : localMin.y,
                    (corner & 4) != 0 ? localMax.z : localMin.z);
                const glm::vec4 clip = ubo.ViewProjectionUnJit * worldTransform * glm::vec4(point, 1.0f);
                if (clip.w <= 0.001f)
                {
                    continue;
                }
                const glm::vec3 ndc = glm::vec3(clip) / clip.w;
                if (ndc.z < 0.0f || ndc.z > 1.0f)
                {
                    continue;
                }
                corners[corner] = {
                    viewportPos.x + (ndc.x * 0.5f + 0.5f) * viewportSize.x,
                    viewportPos.y + (ndc.y * 0.5f + 0.5f) * viewportSize.y,
                };
                projected[corner] = true;
            }

            constexpr uint32_t edges[][2] = {
                {0, 1}, {1, 3}, {3, 2}, {2, 0}, {4, 5}, {5, 7},
                {7, 6}, {6, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7},
            };
            ImDrawList* drawList = ImGui::GetForegroundDrawList();
            drawList->PushClipRect(viewportPos, viewportPos + viewportSize, true);
            for (const auto& edge : edges)
            {
                if (projected[edge[0]] && projected[edge[1]])
                {
                    drawList->AddLine(corners[edge[0]], corners[edge[1]], color, 2.0f);
                }
            }
            drawList->PopClipRect();
        }

        std::filesystem::path WorkspaceDir() { return std::filesystem::current_path() / "scad_library"; }

        // Authoring tools must prefer the repository source tree even when the
        // executable is launched with out/build/<preset>/bin as its cwd. A
        // packaged build has no AGENTS.md marker and falls back to the normal
        // platform asset resolver.
        std::filesystem::path AuthoringPath(const std::filesystem::path& relativePath)
        {
            if (relativePath.is_absolute())
            {
                return relativePath;
            }
            std::filesystem::path cursor = std::filesystem::current_path();
            std::error_code ec;
            while (!cursor.empty())
            {
                if (std::filesystem::exists(cursor / "AGENTS.md", ec) &&
                    std::filesystem::exists(cursor / relativePath, ec))
                {
                    return std::filesystem::absolute(cursor / relativePath, ec);
                }
                const std::filesystem::path parent = cursor.parent_path();
                if (parent == cursor)
                {
                    break;
                }
                cursor = parent;
            }
            return Utilities::FileHelper::GetPlatformFilePath(relativePath.string().c_str());
        }

        // The category token reads better with a friendly label where we have one.
        const char* CategoryLabel(const std::string& category)
        {
            if (category == "bldg")
                return "建筑";
            if (category == "block")
                return "街区";
            if (category == "furn")
                return "家具";
            if (category == "prop")
                return "道具";
            if (category == "nature")
                return "植被地景";
            if (category == "veh")
                return "载具";
            if (category == "boat")
                return "船只";
            if (category == "wall")
                return "墙体";
            if (category == "part")
                return "构件";
            if (category == "ground")
                return "地面";
            if (category == "road")
                return "道路";
            if (category == "head")
                return "头部";
            if (category == "hair")
                return "发型";
            if (category == "hat")
                return "帽饰";
            if (category == "torso")
                return "躯干";
            if (category == "arm")
                return "手臂";
            if (category == "leg")
                return "腿部";
            if (category == "acc")
                return "配饰";
            if (category == "char")
                return "角色";
            if (category == "misc")
                return "其他";
            return category.c_str();
        }

        bool PassesFilter(const FKitModuleInfo& moduleInfo, const char* filter)
        {
            if (filter[0] == '\0')
            {
                return true;
            }
            return moduleInfo.name.find(filter) != std::string::npos ||
                moduleInfo.category.find(filter) != std::string::npos;
        }

        bool IsPathWithin(const std::filesystem::path& path, const std::filesystem::path& root)
        {
            std::error_code ec;
            const std::filesystem::path canonicalPath = std::filesystem::weakly_canonical(path, ec);
            if (ec)
            {
                return false;
            }
            const std::filesystem::path canonicalRoot = std::filesystem::weakly_canonical(root, ec);
            if (ec)
            {
                return false;
            }
            const std::filesystem::path relative = canonicalPath.lexically_relative(canonicalRoot);
            return !relative.empty() && *relative.begin() != "..";
        }

        std::string ReadAssemblyTextFile(const std::filesystem::path& path)
        {
            std::ifstream input(path, std::ios::binary);
            if (!input)
            {
                return {};
            }
            return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
        }

        std::vector<std::string> FindKitDependencies(const std::string& source)
        {
            static const std::regex useRegex(R"((?:use|include)\s*<([^>]*kit_[^>]*)>)", std::regex_constants::icase);
            std::vector<std::string> dependencies;
            for (std::sregex_iterator it(source.begin(), source.end(), useRegex), end; it != end; ++it)
            {
                const std::string kitName = std::filesystem::path((*it)[1].str()).stem().string();
                if (std::find(dependencies.begin(), dependencies.end(), kitName) == dependencies.end())
                {
                    dependencies.push_back(kitName);
                }
            }
            return dependencies;
        }

        std::string RewriteScadDependencyPaths(const std::string& source, const std::filesystem::path& sourceDir,
                                               const std::filesystem::path& targetDir, bool absolute)
        {
            static const std::regex useRegex(R"(((?:use|include)\s*<)([^>]+)(>))", std::regex_constants::icase);
            std::string result;
            std::string::const_iterator cursor = source.begin();
            std::smatch match;
            while (std::regex_search(cursor, source.end(), match, useRegex))
            {
                result.append(cursor, match[0].first);
                std::filesystem::path dependencyPath(match[2].str());
                if (!dependencyPath.is_absolute())
                {
                    dependencyPath = sourceDir / dependencyPath;
                }
                dependencyPath = dependencyPath.lexically_normal();
                if (!absolute)
                {
                    const std::filesystem::path relative = dependencyPath.lexically_relative(targetDir);
                    if (!relative.empty())
                    {
                        dependencyPath = relative;
                    }
                }
                result += match[1].str();
                result += dependencyPath.generic_string();
                result += match[3].str();
                cursor = match[0].second;
            }
            result.append(cursor, source.end());
            return result;
        }

        std::string ScadValueSource(const Assets::Scad::Value& value)
        {
            using Value = Assets::Scad::Value;
            switch (value.type)
            {
            case Value::Type::Number:
                return fmt::format("{:.9g}", value.num);
            case Value::Type::Bool:
                return value.boolean ? "true" : "false";
            case Value::Type::Str:
                {
                    std::string escaped;
                    escaped.reserve(value.str.size() + 2);
                    for (const char character : value.str)
                    {
                        if (character == '\\' || character == '"')
                        {
                            escaped.push_back('\\');
                        }
                        escaped.push_back(character);
                    }
                    return fmt::format("\"{}\"", escaped);
                }
            case Value::Type::Vec:
                {
                    std::string result = "[";
                    for (size_t index = 0; index < value.vec.size(); ++index)
                    {
                        if (index > 0)
                        {
                            result += ", ";
                        }
                        result += ScadValueSource(value.vec[index]);
                    }
                    result += "]";
                    return result;
                }
            case Value::Type::Range:
                return fmt::format("[{:.9g}:{:.9g}:{:.9g}]", value.rangeBegin, value.rangeStep, value.rangeEnd);
            default:
                return "undef";
            }
        }

        // ------------------------------------------------------------------ kit file watch

        // A browsable parts library file: kit_*.scad (excluding the combinator
        // rule libraries kit_layout.scad / kit_terrain.scad / kit_road.scad /
        // kit_geo_city.scad),
        // mirroring the filter in KitCatalog::ScanKits.
        bool IsBrowsableKitFile(const std::filesystem::path& path)
        {
            const std::string filename = path.filename().string();
            return path.extension() == ".scad" && filename.rfind("kit_", 0) == 0 &&
                filename != "kit_layout.scad" && filename != "kit_terrain.scad" &&
                filename != "kit_road.scad" && filename != "kit_geo_city.scad";
        }

        struct FKitWatchGatherResult
        {
            std::vector<std::string> changedPaths; // kit files edited or vanished
            bool treeChanged = false;              // kit_*.scad added or removed in the lib dir
        };

        // Runs on a TaskCoordinator worker thread: only touches the copied-in
        // paths/timestamps, never UI or scene state. Detects content edits by
        // comparing last_write_time against the previous snapshot, and kit
        // add/remove by enumerating the lib directory (entry membership, not
        // directory mtime, so it is reliable across platforms).
        FKitWatchGatherResult GatherKitFileChanges(
            const std::vector<std::string>& watchPaths,
            const std::vector<std::pair<std::string, std::filesystem::file_time_type>>& prevStamps,
            const std::string& libDir)
        {
            FKitWatchGatherResult result;
            std::error_code ec;

            std::set<std::string> seenFiles;
            for (const std::string& path : watchPaths)
            {
                const std::filesystem::path filePath(path);
                const std::filesystem::file_time_type stamp = std::filesystem::last_write_time(filePath, ec);
                if (ec)
                {
                    ec.clear();
                    // File vanished or unreadable: treat as a change so the tree is rescanned.
                    result.changedPaths.push_back(path);
                    continue;
                }
                seenFiles.insert(filePath.filename().string());

                bool unchanged = false;
                for (const auto& [prevPath, prevStamp] : prevStamps)
                {
                    if (prevPath == path)
                    {
                        unchanged = prevStamp == stamp;
                        break;
                    }
                }
                if (!unchanged)
                {
                    result.changedPaths.push_back(path);
                }
            }

            std::set<std::string> onDiskFiles;
            if (std::filesystem::is_directory(std::filesystem::path(libDir), ec))
            {
                for (const std::filesystem::directory_entry& entry :
                     std::filesystem::directory_iterator(libDir, ec))
                {
                    if (ec)
                    {
                        ec.clear();
                        continue;
                    }
                    if (!entry.is_regular_file(ec))
                    {
                        ec.clear();
                        continue;
                    }
                    if (IsBrowsableKitFile(entry.path()))
                    {
                        onDiskFiles.insert(entry.path().filename().string());
                    }
                }
            }
            if (onDiskFiles != seenFiles)
            {
                result.treeChanged = true;
            }

            std::sort(result.changedPaths.begin(), result.changedPaths.end());
            result.changedPaths.erase(
                std::unique(result.changedPaths.begin(), result.changedPaths.end()), result.changedPaths.end());
            return result;
        }
    } // namespace

    ScadLibraryInterface::ScadLibraryInterface(NextEngine& engine, std::string startupAssemblyPath) :
        engine_(engine), startupAssemblyPath_(std::move(startupAssemblyPath))
    {
        std::unique_ptr<AI::IScadAITransport> aiTransport = engine_.GetOptions().AgentValidation
            ? std::unique_ptr<AI::IScadAITransport>(std::make_unique<AI::FFixtureScadAITransport>())
            : std::unique_ptr<AI::IScadAITransport>(std::make_unique<AI::FNextAIScadTransport>());
        aiController_ = std::make_unique<AI::FScadAIController>(std::move(aiTransport));
        aiPanel_ = std::make_unique<AI::FScadAIPanel>();
        RescanKits();
        RescanAssemblies();
    }

    ScadLibraryInterface::~ScadLibraryInterface()
    {
        // Drain any in-flight kit watch task before members are torn down: its
        // completion callback runs on the main thread and touches `this`.
        // TaskCoordinator outlives the game instance (destroyed in NextEngine::End).
        Tasks::TaskCoordinator::GetInstance()->WaitForAllTasks();
    }

    void ScadLibraryInterface::Config()
    {
        ImGuiIO& io = ImGui::GetIO();
        imguiIniPath_ = Utilities::FileHelper::GetPlatformFilePath("scadlibrary.ini");
        io.IniFilename = imguiIniPath_.c_str();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigFlags &= ~ImGuiConfigFlags_DockingEnable;
    }

    void ScadLibraryInterface::Init()
    {
        NextUI::Theme::ApplyProfessionalTheme();
        // ScadLibrary uses a deliberately flatter editor treatment than the
        // general-purpose engine UI: restrained rounding, quiet borders and
        // denser controls keep the scene itself visually dominant.
        ImGuiStyle& style = ImGui::GetStyle();
        style.WindowRounding = 0.0f;
        style.ChildRounding = 3.0f;
        style.FrameRounding = 3.0f;
        style.PopupRounding = 4.0f;
        style.ScrollbarRounding = 3.0f;
        style.GrabRounding = 3.0f;
        style.TabRounding = 3.0f;
        style.FrameBorderSize = 0.0f;
        style.ChildBorderSize = 1.0f;
        style.ItemSpacing = ImVec2(7.0f, 6.0f);
        style.FramePadding = ImVec2(8.0f, 5.0f);
        style.Colors[ImGuiCol_WindowBg] = ImVec4(0.075f, 0.083f, 0.094f, 0.985f);
        style.Colors[ImGuiCol_ChildBg] = ImVec4(0.045f, 0.050f, 0.058f, 0.52f);
        style.Colors[ImGuiCol_FrameBg] = ImVec4(0.045f, 0.050f, 0.058f, 0.96f);
        style.Colors[ImGuiCol_Border] = ImVec4(0.20f, 0.22f, 0.25f, 0.72f);
        ImGuiIO& io = ImGui::GetIO();
        // The vcpkg imgui is built with the FreeType feature, so the font atlas must
        // use the FreeType builder (matches ScadStudio/gkNextEditor).
        io.Fonts->SetFontLoader(ImGuiFreeType::GetFontLoader());
        io.Fonts->FontLoaderFlags = ImGuiFreeTypeLoaderFlags_NoHinting;

        const std::string fontPath = Utilities::FileHelper::GetPlatformFilePath("assets/fonts/DroidSansFallback.ttf");
        ImFont* font =
            io.Fonts->AddFontFromFileTTF(fontPath.c_str(), 18.0f, nullptr, io.Fonts->GetGlyphRangesChineseFull());
        if (font == nullptr)
        {
            io.Fonts->AddFontDefault();
        }
    }

    void ScadLibraryInterface::SetWorkspaceMode(EWorkspaceMode mode)
    {
        if (workspaceMode_ == mode)
        {
            return;
        }
        workspaceMode_ = mode;
        benchCollapsed_ = false;

        if (mode == EWorkspaceMode::SceneAssembly)
        {
            rigPreview_.SetActive(false);
            if (document_.HasTerrain() || !Bench().empty() || document_.SourceSegmentCount() > 0)
            {
                ReloadBench(false);
            }
            else if (selectedKit_ >= 0 && !selectedModule_.empty())
            {
                PreviewModule(selectedKit_, selectedModule_);
            }
        }
        else if (mode == EWorkspaceMode::CharacterDesigner)
        {
            designerDirty_ = true;
        }
        else if (mode == EWorkspaceMode::CharacterWorkbench)
        {
            workbenchReloadRequested_ = true;
        }
        else
        {
            rigPreview_.SetActive(false);
            if (kitBrowserSelectedKit_ < 0 && !kits_.empty())
            {
                kitBrowserSelectedKit_ = 0;
            }
            if (kitBrowserSelectedKit_ >= 0 && kitBrowserSelectedKit_ < static_cast<int>(kits_.size()) &&
                !kits_[kitBrowserSelectedKit_].modules.empty())
            {
                kitBrowserSelectedModule_ = 0;
                PreviewModule(kitBrowserSelectedKit_, kits_[kitBrowserSelectedKit_].modules[0].name);
            }
            else
            {
                kitBrowserSelectedModule_ = -1;
            }
        }
    }

    void ScadLibraryInterface::Render()
    {
        // Auto-refresh when an external agent edits a kit_*.scad file. The
        // filesystem probe runs on a worker thread; this only performs the
        // deferred preview reload / tree rescan when it is safe to do so.
        PollKitFileChanges();

        // First frame: open a real kit-based scene when available.
        if (!welcomeLoaded_)
        {
            welcomeLoaded_ = true;
            const bool openedStartup = !startupAssemblyPath_.empty() &&
                std::filesystem::path(startupAssemblyPath_).extension() == ".scad" &&
                OpenAssembly(startupAssemblyPath_);
            if (!openedStartup)
            {
                const auto preferred =
                    std::find_if(assemblies_.begin(), assemblies_.end(), [](const FSceneAssemblyInfo& info)
                                 { return info.relativePath == "assets/scad/evaluated/scene_assembly_example.scad"; });
                if (preferred != assemblies_.end())
                {
                    OpenAssembly(preferred->relativePath);
                }
                else if (!assemblies_.empty())
                {
                    OpenAssembly(assemblies_[0].relativePath);
                }
            }
        }

        DrawTitleBar();
        DrawBottomBar();

        ImGuiViewport* viewport = ImGui::GetMainViewport();
        if (viewport == nullptr)
        {
            return;
        }

        const float panelY = viewport->Pos.y + kTitleBarHeight;
        const float panelHeight = std::max(1.0f, viewport->Size.y - kTitleBarHeight - kBottomBarHeight);
        const bool kitBrowserMode = workspaceMode_ == EWorkspaceMode::KitBrowser;
        if (kitBrowserMode)
        {
            constexpr float kitBrowserGap = 10.0f;
            const float kitBrowserLeftWidth = std::clamp(viewport->Size.x * 0.22f, 280.0f, 360.0f);
            const float kitBrowserContentHeight = std::max(1.0f, panelHeight - kKitBrowserHeaderHeight);
            const float kitBrowserCatalogHeight = std::clamp(kitBrowserContentHeight * 0.42f, 220.0f, 420.0f);
            const ImVec2 kitBrowserViewportPos(viewport->Pos.x,
                                                panelY + kKitBrowserHeaderHeight + kitBrowserCatalogHeight +
                                                    kitBrowserGap);
            const ImVec2 kitBrowserViewportSize(
                kitBrowserLeftWidth,
                std::max(1.0f, kitBrowserContentHeight - kitBrowserCatalogHeight - kitBrowserGap));
            DrawKitBrowserPanel(ImVec2(viewport->Pos.x, panelY), ImVec2(viewport->Size.x, panelHeight));
            viewportPosition_ = {kitBrowserViewportPos.x, kitBrowserViewportPos.y};
            viewportSize_ = {kitBrowserViewportSize.x, kitBrowserViewportSize.y};
            sceneToolbarVisible_ = false;
            DrawViewportAxis(kitBrowserViewportPos, kitBrowserViewportSize);
            const NextUI::Scaling::FViewportRect framebufferViewport =
                NextUI::Scaling::ImGuiToMainFramebufferViewport(kitBrowserViewportPos, kitBrowserViewportSize);
            engine_.GetRenderer().SwapChain().UpdateOutputViewport(
                Utilities::Math::floorToInt(framebufferViewport.Position.x),
                Utilities::Math::floorToInt(framebufferViewport.Position.y),
                Utilities::Math::ceilToInt(framebufferViewport.Size.x),
                Utilities::Math::ceilToInt(framebufferViewport.Size.y));
            return;
        }

        const bool composeMode = workspaceMode_ == EWorkspaceMode::SceneAssembly;
        const bool workbenchMode = workspaceMode_ == EWorkspaceMode::CharacterWorkbench;
        const float leftFullWidth = workbenchMode ? std::clamp(viewport->Size.x * 0.16f, 230.0f, 290.0f)
                                                  : std::clamp(viewport->Size.x * 0.24f, 300.0f, 380.0f);
        const float rightFullWidth = workspaceMode_ == EWorkspaceMode::CharacterWorkbench
            ? std::clamp(viewport->Size.x * 0.28f, 420.0f, 520.0f)
            : (workspaceMode_ == EWorkspaceMode::CharacterDesigner
                   ? std::clamp(viewport->Size.x * 0.31f, 400.0f, 540.0f)
                   : std::clamp(viewport->Size.x * 0.28f, 340.0f, 460.0f));
        const float leftWidth = composeMode ? (browserCollapsed_ ? kCollapsedRailWidth : leftFullWidth)
                                            : (workbenchMode ? leftFullWidth : 0.0f);
        const float rightWidth = benchCollapsed_ ? kCollapsedRailWidth : rightFullWidth;

        if (composeMode)
        {
            DrawBrowserPanel(ImVec2(viewport->Pos.x, panelY), ImVec2(leftWidth, panelHeight));
        }
        else if (workbenchMode && workbenchEverLoaded_ && rigPreview_.HasRig())
        {
            DrawBoneHierarchyPanel(ImVec2(viewport->Pos.x, panelY), ImVec2(leftWidth, panelHeight));
        }
        DrawModePanel(ImVec2(viewport->Pos.x + viewport->Size.x - rightWidth, panelY), ImVec2(rightWidth, panelHeight));
        const bool showTimeline = workspaceMode_ == EWorkspaceMode::CharacterWorkbench && workbenchEditorTab_ == 0 &&
            workbenchEverLoaded_ && rigPreview_.HasRig();
        const float timelineHeight = showTimeline ? std::clamp(viewport->Size.y * 0.38f, 320.0f, 390.0f) : 0.0f;
        viewportPosition_ = {viewport->Pos.x + leftWidth, panelY};
        viewportSize_ = {std::max(1.0f, viewport->Size.x - leftWidth - rightWidth),
                         std::max(1.0f, panelHeight - timelineHeight)};
        sceneToolbarVisible_ = false;
        if (showTimeline)
        {
            DrawAnimationTimelinePanel(
                ImVec2(viewport->Pos.x + leftWidth, panelY + panelHeight - timelineHeight),
                ImVec2(std::max(1.0f, viewport->Size.x - leftWidth - rightWidth), timelineHeight));
            DrawViewportToolbar(ImVec2(viewport->Pos.x + leftWidth, panelY));
            DrawBoneGizmo(ImVec2(viewport->Pos.x + leftWidth, panelY),
                          ImVec2(std::max(1.0f, viewport->Size.x - leftWidth - rightWidth),
                                 std::max(1.0f, panelHeight - timelineHeight)));
        }
        else if (composeMode)
        {
            const ImVec2 sceneViewportPos(viewport->Pos.x + leftWidth, panelY);
            const ImVec2 sceneViewportSize(std::max(1.0f, viewport->Size.x - leftWidth - rightWidth), panelHeight);
            DrawKitDropTarget(sceneViewportPos, sceneViewportSize);
            // Terrain operators are selected from the Structure outliner. The
            // TERR tab edits only base data, while a selected operator keeps
            // its visual handles visible in the viewport.
            const bool structureTerrainSelection =
                assemblyEditorTab_ == 3 && document_.HasTerrain() &&
                selectedSegment_ >= 0 && selectedSegment_ < static_cast<int>(document_.Segments().size()) &&
                (document_.Segments()[selectedSegment_].kind == EScadSegmentKind::Terrain ||
                 document_.Segments()[selectedSegment_].kind == EScadSegmentKind::TerrainRule);
            if (structureTerrainSelection)
            {
                DrawTerrainFeatureToolbar(sceneViewportPos);
                DrawTerrainFeatureOverlay(sceneViewportPos, sceneViewportSize);
            }
            else if (assemblyEditorTab_ == 3 && HasActiveProceduralHandles())
            {
                DrawLayScatterOverlay(sceneViewportPos, sceneViewportSize);
            }
            else
            {
                DrawSceneGizmoToolbar(sceneViewportPos);
                DrawSelectedStructureBounds(sceneViewportPos, sceneViewportSize);
                DrawSceneObjectGizmo(sceneViewportPos, sceneViewportSize);
            }
        }
        DrawViewportAxis(
            ImVec2(viewport->Pos.x + leftWidth, panelY),
            ImVec2(std::max(1.0f, viewport->Size.x - leftWidth - rightWidth),
                   std::max(1.0f, panelHeight - timelineHeight)));

        // Deferred bench reload: wait until the drag/edit is released so the scene
        // is not rebuilt on every mouse-move.
        if (composeMode && autoReload_ && (terrainProcessDirty_ || benchDirty_) && !ImGui::IsAnyItemActive() &&
            !terrainFeatureDragging_ && !terrainRuleDragging_ && !layScatterDragging_)
        {
            ReloadCurrentAssemblyPreview();
        }
        if (workspaceMode_ == EWorkspaceMode::CharacterWorkbench && workbenchReloadRequested_ &&
            !ImGui::IsAnyItemActive())
        {
            ReloadWorkbench();
        }
        else if (workspaceMode_ == EWorkspaceMode::CharacterWorkbench && workbenchEquipmentRebuildRequested_ &&
                 !ImGui::IsAnyItemActive())
        {
            ReloadWorkbenchStage();
        }
        else if (workspaceMode_ == EWorkspaceMode::CharacterDesigner && designerDirty_ && !ImGui::IsAnyItemActive())
        {
            ReloadDesigner();
        }

        const NextUI::Scaling::FViewportRect framebufferViewport =
            NextUI::Scaling::ImGuiToMainFramebufferViewport(
                ImVec2(viewport->Pos.x + leftWidth, viewport->Pos.y + kTitleBarHeight),
                ImVec2(std::max(1.0f, viewport->Size.x - leftWidth - rightWidth),
                       std::max(1.0f, panelHeight - timelineHeight)));
        engine_.GetRenderer().SwapChain().UpdateOutputViewport(
            Utilities::Math::floorToInt(framebufferViewport.Position.x),
            Utilities::Math::floorToInt(framebufferViewport.Position.y),
            Utilities::Math::ceilToInt(framebufferViewport.Size.x),
            Utilities::Math::ceilToInt(framebufferViewport.Size.y));
    }

    bool ScadLibraryInterface::ConsumeFocusSelectedRequest()
    {
        const bool requested = focusSelectedRequested_;
        focusSelectedRequested_ = false;
        return requested;
    }

    bool ScadLibraryInterface::ConsumeFrameAllRequest()
    {
        const bool requested = frameAllRequested_;
        frameAllRequested_ = false;
        return requested;
    }

    bool ScadLibraryInterface::IsViewportPoint(const double x, const double y) const
    {
        const bool insideViewport = x >= viewportPosition_.x && y >= viewportPosition_.y &&
            x < viewportPosition_.x + viewportSize_.x && y < viewportPosition_.y + viewportSize_.y;
        const bool insideSceneToolbar = sceneToolbarVisible_ && x >= sceneToolbarPosition_.x &&
            y >= sceneToolbarPosition_.y && x < sceneToolbarPosition_.x + sceneToolbarSize_.x &&
            y < sceneToolbarPosition_.y + sceneToolbarSize_.y;
        return insideViewport && !insideSceneToolbar;
    }

    void ScadLibraryInterface::DrawTitleBar()
    {
        NextUI::Theme::FAppTitleBarConfig config{};
        config.BrandWindowId = "ScadLibraryBrand";
        config.MenuWindowId = "ScadLibraryMenu";
        config.RightWindowId = "ScadLibraryWindowControls";
        config.AppName = "SCAD Library";
        config.Height = kTitleBarHeight;
        config.RightContentWidth = 0.0f;
        config.DrawMenuBar = [&]() -> float
        {
            float menuRight = ImGui::GetCursorScreenPos().x;
            if (ImGui::BeginMenu("File"))
            {
                if (ImGui::MenuItem("重新扫描场景与 Kit", "F5"))
                {
                    RescanKits();
                    RescanAssemblies();
                }
                if (ImGui::MenuItem("保存场景", "Ctrl+S", false, !openedAssemblyPath_.empty()))
                {
                    SaveAssembly(false);
                }
                if (ImGui::MenuItem("导出新场景", nullptr, false, !Bench().empty()))
                {
                    ExportBench();
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Exit"))
                {
                    engine_.RequestClose();
                }
                ImGui::EndMenu();
            }
            menuRight = std::max(menuRight, ImGui::GetItemRectMax().x);

            if (ImGui::BeginMenu("View"))
            {
                if (workspaceMode_ == EWorkspaceMode::SceneAssembly)
                {
                    bool browserOpen = !browserCollapsed_;
                    if (ImGui::MenuItem("场景与 Kit 浏览器", nullptr, browserOpen))
                    {
                        browserCollapsed_ = !browserCollapsed_;
                    }
                }
                bool inspectorOpen = !benchCollapsed_;
                if (workspaceMode_ != EWorkspaceMode::KitBrowser && ImGui::MenuItem("Inspector", nullptr, inspectorOpen))
                {
                    benchCollapsed_ = !benchCollapsed_;
                }
                ImGui::EndMenu();
            }
            menuRight = std::max(menuRight, ImGui::GetItemRectMax().x);

            return menuRight;
        };
        config.IsMaximized = engine_.IsMaximized();
        config.OnMinimize = [&]() { engine_.RequestMinimize(); };
        config.OnToggleMaximize = [&]() { engine_.ToggleMaximize(); };
        config.OnClose = [&]() { engine_.RequestClose(); };
        NextUI::Theme::DrawAppTitleBar(engine_, config);
    }

    void ScadLibraryInterface::DrawWorkspaceToolbar()
    {
        constexpr float buttonWidth = 144.0f;
        const float buttonHeight = std::max(32.0f, kBottomBarHeight - 8.0f);
        const auto modeButton = [&](const char* label, const char* shortcut, EWorkspaceMode mode)
        {
            if (NextUI::Theme::ToolbarButton(label, shortcut, workspaceMode_ == mode,
                                              ImVec2(buttonWidth, buttonHeight)))
            {
                SetWorkspaceMode(mode);
            }
        };

        modeButton(ICON_FA_PERSON "  动作与装备", "Ctrl+3", EWorkspaceMode::CharacterWorkbench);
        ImGui::SameLine(0.0f, 4.0f);
        modeButton(ICON_FA_CUBES_STACKED "  角色合成", "Ctrl+2", EWorkspaceMode::CharacterDesigner);
        ImGui::SameLine(0.0f, 4.0f);
        modeButton(ICON_FA_CITY "  场景组装", "Ctrl+1", EWorkspaceMode::SceneAssembly);
        ImGui::SameLine(0.0f, 4.0f);
        modeButton(ICON_FA_BOOK_OPEN "  Kit 浏览", "Ctrl+4", EWorkspaceMode::KitBrowser);
    }

    void ScadLibraryInterface::DrawActionToolbar()
    {
        constexpr float buttonWidth = 72.0f;
        constexpr float buttonHeight = 40.0f;
        constexpr float itemGap = 4.0f;
        const auto actionButton = [&](const char* label, const char* tooltip, bool active = false)
        {
            return NextUI::Theme::ToolbarButton(label, tooltip, active, ImVec2(buttonWidth, buttonHeight));
        };

        if (workspaceMode_ == EWorkspaceMode::SceneAssembly)
        {
            ImGui::BeginDisabled(openedAssemblyPath_.empty());
            if (actionButton(ICON_FA_FLOPPY_DISK " 保存", "保存当前场景"))
            {
                SaveAssembly(false);
            }
            ImGui::EndDisabled();
            ImGui::SameLine(0.0f, itemGap);
            ImGui::BeginDisabled(assemblySource_.empty() && Bench().empty());
            if (actionButton(ICON_FA_PLAY " 预览", "预览当前未保存内容"))
            {
                PreviewAssemblySource();
            }
            ImGui::EndDisabled();
        }
        else if (workspaceMode_ == EWorkspaceMode::CharacterDesigner)
        {
            if (actionButton(ICON_FA_ROTATE_RIGHT " 刷新", "刷新角色预览"))
            {
                ReloadDesigner();
            }
            ImGui::SameLine(0.0f, itemGap);
            if (actionButton(ICON_FA_FILE_EXPORT " 导出", "导出当前角色", true))
            {
                ExportCharacter();
            }
        }
        else if (workspaceMode_ == EWorkspaceMode::CharacterWorkbench)
        {
            if (actionButton(ICON_FA_ROTATE_RIGHT " 刷新", "重新载入动作与装备"))
            {
                workbenchReloadRequested_ = true;
            }
        }
        else
        {
            if (actionButton(ICON_FA_ROTATE_RIGHT " 刷新", "重新扫描 Kit 目录"))
            {
                RescanKits();
            }
        }

        if (workspaceMode_ != EWorkspaceMode::KitBrowser)
        {
            ImGui::SameLine(0.0f, itemGap);
            if (NextUI::Theme::ToolbarButton(ICON_FA_WAND_MAGIC_SPARKLES " AI", "打开 AI 创作面板",
                                             inspectorPrimaryTab_ == 1, ImVec2(58.0f, buttonHeight)))
            {
                benchCollapsed_ = false;
                aiOpenRequested_ = true;
            }
        }
        ImGui::SameLine(0.0f, 10.0f);
        ImGui::TextDisabled("FPS %.0f", engine_.GetFrameRate());
    }

    void ScadLibraryInterface::DrawBottomBar()
    {
        NextUI::Theme::FBottomBarConfig config{};
        config.WindowId = "ScadLibraryBottomBar";
        config.Height = kBottomBarHeight;
        config.CenterWidth = 588.0f;
        config.RightWidth = workspaceMode_ == EWorkspaceMode::SceneAssembly ||
                workspaceMode_ == EWorkspaceMode::CharacterDesigner
            ? 300.0f
            : (workspaceMode_ == EWorkspaceMode::CharacterWorkbench ? 220.0f : 160.0f);
        config.DrawLeftContent = [&]()
        {
            if (!statusLine_.empty())
            {
                const ImVec4 color = statusError_ ? NextUI::Theme::Color(NextUI::Theme::EColor::Danger)
                                                  : NextUI::Theme::Color(NextUI::Theme::EColor::TextMuted);
                ImGui::TextColored(color, "%s", statusLine_.c_str());
            }
            else
            {
                if (workspaceMode_ == EWorkspaceMode::SceneAssembly)
                {
                    ImGui::TextDisabled("打开 kit 场景，或从零件库添加对象进行组装");
                }
                else if (workspaceMode_ == EWorkspaceMode::CharacterDesigner)
                {
                    ImGui::TextDisabled("选择部件与颜色，生成可复用角色");
                }
                else if (workspaceMode_ == EWorkspaceMode::CharacterWorkbench)
                {
                    ImGui::TextDisabled("编辑动作关键帧与骨架装备");
                }
                else
                {
                    ImGui::TextDisabled("浏览 Kit、模块分类与几何度量");
                }
            }
        };
        config.DrawCenterContent = [&]() { DrawWorkspaceToolbar(); };
        config.DrawRightContent = [&]() { DrawActionToolbar(); };
        NextUI::Theme::DrawBottomBar(config);
    }

    void ScadLibraryInterface::DrawKitDropTarget(const ImVec2& pos, const ImVec2& size)
    {
        if (ImGui::GetDragDropPayload() == nullptr || size.x <= 0.0f || size.y <= 0.0f)
        {
            return;
        }

        ImGuiViewport* viewport = ImGui::GetMainViewport();
        if (viewport == nullptr)
        {
            return;
        }

        ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(size, ImGuiCond_Always);
        ImGui::SetNextWindowViewport(viewport->ID);
        ImGui::SetNextWindowBgAlpha(0.0f);
        const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollWithMouse |
            ImGuiWindowFlags_NoBringToFrontOnFocus;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        if (ImGui::Begin("##ScadLibraryKitDropTarget", nullptr, flags))
        {
            ImGui::InvisibleButton("##ScadLibraryKitDropTargetButton", size);
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(
                        kScadKitDragDropPayload, ImGuiDragDropFlags_AcceptBeforeDelivery))
                {
                    if (payload->DataSize == sizeof(FScadKitDragPayload))
                    {
                        const auto* data = static_cast<const FScadKitDragPayload*>(payload->Data);
                        if (data->kitIndex >= 0 && data->kitIndex < static_cast<int>(kits_.size()) &&
                            data->moduleIndex >= 0 &&
                            data->moduleIndex < static_cast<int>(kits_[data->kitIndex].modules.size()))
                        {
                            glm::vec3 scadPosition;
                            if (GetKitDropPlacement(scadPosition))
                            {
                                DrawKitDropPreview(kits_[data->kitIndex].modules[data->moduleIndex], scadPosition,
                                                   pos, size);
                            }
                            if (payload->IsDelivery() && PlaceKitFromDrop(data->kitIndex, data->moduleIndex))
                            {
                                // Commit only on mouse release. The preview above stays lightweight while the
                                // pointer moves; the real scene is refreshed exactly once after the drop.
                                ReloadCurrentAssemblyPreview();
                            }
                        }
                    }
                }
                ImGui::EndDragDropTarget();
            }
        }
        ImGui::End();
        ImGui::PopStyleVar(2);
    }

    bool ScadLibraryInterface::GetKitDropPlacement(glm::vec3& outScadPosition) const
    {
        // NextEngine keeps SDL input positions in framebuffer pixels. ImGui is
        // scaled down to logical coordinates for the UI, but the unprojection
        // path consumes framebuffer pixels, so applying UiScale here would
        // shift the ray away from the cursor on high-DPI displays.
        const glm::vec2 mousePosition = glm::vec2(engine_.GetMousePos());
        glm::vec3 rayOrigin;
        glm::vec3 rayDirection;
        Runtime::EngineHelper::GetScreenToWorldRay(mousePosition, rayOrigin, rayDirection);

        glm::vec3 worldPosition(0.0f);
        bool hitSurface = false;
        engine_.RayCast(rayOrigin, rayDirection, [&](Assets::RayCastResult result)
        {
            if (result.Hit)
            {
                worldPosition = glm::vec3(result.HitPoint);
                hitSurface = true;
            }
            return true;
        });

        // An empty scene has no BVH hit yet. Use the SCAD ground plane in that
        // case, which also gives a useful placement target when the view is
        // aimed below the horizon.
        if (!hitSurface && std::abs(rayDirection.y) > 1.0e-5f)
        {
            const float distance = -rayOrigin.y / rayDirection.y;
            if (distance > 0.0f)
            {
                worldPosition = rayOrigin + rayDirection * distance;
                hitSurface = true;
            }
        }

        if (!hitSurface)
        {
            return false;
        }

        // SCAD is Z-up with +Y pointing toward the opposite engine-Z axis.
        outScadPosition = glm::vec3(worldPosition.x, -worldPosition.z, worldPosition.y);
        return true;
    }

    void ScadLibraryInterface::DrawKitDropPreview(const FKitModuleInfo& module, const glm::vec3& scadPosition,
                                                   const ImVec2& viewportPos, const ImVec2& viewportSize) const
    {
        // catalog.json contains extents rather than the complete local bbox. Kit modules conventionally sit on
        // the Z=0 plane, so use their footprint centered on the placement cursor and their measured height.
        const float footprintX = module.hasMetrics ? std::max(module.footprintX, 0.25f) : 1.0f;
        const float footprintY = module.hasMetrics ? std::max(module.footprintY, 0.25f) : 1.0f;
        const float height = module.hasMetrics ? std::max(module.height, 0.25f) : 1.0f;
        const glm::vec3 localMin(-footprintX * 0.5f, -footprintY * 0.5f, 0.0f);
        const glm::vec3 localMax(footprintX * 0.5f, footprintY * 0.5f, height);
        const float scale = engine_.GetUserSettings().ScadToWorldScale;
        const glm::mat4 worldTransform = glm::mat4(Assets::Scad::ScadToWorldBasis(scale)) *
            glm::translate(glm::mat4(1.0f), scadPosition);
        DrawOrientedBoxOverlay(engine_.GetLastUniformBufferObject(), viewportPos, viewportSize, worldTransform,
                               localMin, localMax, IM_COL32(255, 193, 74, 255));
    }

    bool ScadLibraryInterface::PlaceKitFromDrop(const int kitIndex, const int moduleIndex)
    {
        if (openedAssemblyPath_.empty())
        {
            statusLine_ = "请先打开一个场景，再从 Kit 库拖拽模块";
            statusError_ = true;
            return false;
        }
        if (kitIndex < 0 || kitIndex >= static_cast<int>(kits_.size()) || moduleIndex < 0 ||
            moduleIndex >= static_cast<int>(kits_[kitIndex].modules.size()))
        {
            statusLine_ = "无法放置：Kit 模块已不存在，请先刷新资源库";
            statusError_ = true;
            return false;
        }

        glm::vec3 scadPosition;
        if (!GetKitDropPlacement(scadPosition))
        {
            statusLine_ = "无法确定落点：请把 Kit 拖到场景几何体或地面方向";
            statusError_ = true;
            return false;
        }

        AddToBenchAt(kitIndex, kits_[kitIndex].modules[moduleIndex].name, scadPosition);
        return true;
    }

    void ScadLibraryInterface::DrawKitBrowserPanel(const ImVec2& pos, const ImVec2& size)
    {
        constexpr float gap = 10.0f;
        const float leftWidth = std::clamp(size.x * 0.22f, 280.0f, 360.0f);
        const float contentHeight = std::max(1.0f, size.y - kKitBrowserHeaderHeight);
        const float catalogHeight = std::clamp(contentHeight * 0.42f, 220.0f, 420.0f);
        const ImVec2 viewportPos(pos.x, pos.y + kKitBrowserHeaderHeight + catalogHeight + gap);
        const ImVec2 viewportSize(leftWidth, std::max(1.0f, contentHeight - catalogHeight - gap));
        const ImVec2 galleryPos(pos.x + leftWidth + gap, pos.y + kKitBrowserHeaderHeight);
        const ImVec2 gallerySize(std::max(1.0f, size.x - leftWidth - gap), contentHeight);
        const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking |
            ImGuiWindowFlags_NoSavedSettings;

        ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(size.x, kKitBrowserHeaderHeight), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.98f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f, 10.0f));
        if (ImGui::Begin("##ScadLibraryKitBrowserHeader", nullptr, flags))
        {
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(ICON_FA_BOOK_OPEN "  Kit 浏览器");
            ImGui::SameLine();
            ImGui::TextDisabled("%zu 个 Kit · 批量浏览并选择模块预览", kits_.size());
            ImGui::SameLine(ImGui::GetWindowWidth() - 54.0f);
            if (NextUI::Theme::IconButton(ICON_FA_ARROWS_ROTATE "##kit_browser_rescan", "重新扫描 Kit 目录", false,
                                          ImVec2(30.0f, 30.0f)))
            {
                RescanKits();
            }
        }
        ImGui::End();
        ImGui::PopStyleVar(3);

        // The right side is a complete Kit gallery. Selecting any card drives the compact preview in the left rail.
        ImGui::SetNextWindowPos(galleryPos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(gallerySize, ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.98f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 12.0f));
        if (ImGui::Begin("##ScadLibraryKitGallery", nullptr, flags))
        {
            ImGui::TextUnformatted(ICON_FA_IMAGES "  全部 Kit 模块");
            ImGui::SameLine();
            ImGui::TextDisabled("%zu 个 Kit · %zu 个模块", kits_.size(),
                                std::accumulate(kits_.begin(), kits_.end(), size_t{0},
                                                [](const size_t total, const FKitInfo& kit)
                                                {
                                                    return total + kit.modules.size();
                                                }));
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputTextWithHint("##kit_browser_gallery_filter", ICON_FA_MAGNIFYING_GLASS " 搜索 Kit、模块或分类…",
                                     kitBrowserGalleryFilterBuf_, sizeof(kitBrowserGalleryFilterBuf_));
            std::map<std::string, size_t> galleryCategoryCounts;
            for (const FKitInfo& kit : kits_)
            {
                for (const FKitModuleInfo& module : kit.modules)
                {
                    if (!module.category.empty())
                    {
                        ++galleryCategoryCounts[module.category];
                    }
                }
            }
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 2.0f));
            const auto drawCategoryChip = [&](const char* label, const bool selected)
            {
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 999.0f);
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 0.0f));
                ImGui::PushStyleColor(ImGuiCol_Button,
                                      selected ? ImVec4(0.18f, 0.42f, 0.70f, 1.0f) : ImVec4(0.12f, 0.15f, 0.20f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.22f, 0.49f, 0.78f, 1.0f));
                const bool clicked = ImGui::Button(label);
                ImGui::PopStyleColor(2);
                ImGui::PopStyleVar(2);
                return clicked;
            };
            
            const float categoryRowRight = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
            float categoryRowUsedWidth = 0.0f;
            const auto placeCategoryChip = [&](const std::string& label, const bool selected, const auto& onClick)
            {
                constexpr float chipGap = 6.0f;
                const float chipWidth = ImGui::CalcTextSize(label.c_str()).x;
                if (categoryRowUsedWidth > 0.0f)
                {
                    if (categoryRowUsedWidth > categoryRowRight)
                    {
                        ImGui::NewLine();
                        categoryRowUsedWidth = 0.0f;
                        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - ImGui::GetTextLineHeight() * 0.5f);
                    }
                    else
                    {
                        ImGui::SameLine(0.0f, chipGap);
                        categoryRowUsedWidth += chipGap;
                    }
                }
                if (drawCategoryChip(label.c_str(), selected))
                {
                    onClick();
                }
                categoryRowUsedWidth += chipWidth;
            };
            placeCategoryChip("全部##kit_browser_category_all", kitBrowserGalleryCategory_.empty(), [&]()
            {
                kitBrowserGalleryCategory_.clear();
            });
            size_t rareCategoryCount = 0;
            for (const auto& [category, count] : galleryCategoryCounts)
            {
                if (count <= 10)
                {
                    ++rareCategoryCount;
                    continue;
                }
                placeCategoryChip(fmt::format("{}##kit_browser_category_{}", CategoryLabel(category), category),
                                  kitBrowserGalleryCategory_ == category, [&]()
                {
                    kitBrowserGalleryCategory_ = category;
                });
            }
            if (rareCategoryCount > 0)
            {
                placeCategoryChip(fmt::format("更多分类 ({})##kit_browser_category_more", rareCategoryCount),
                                  kitBrowserShowRareCategories_, [&]()
                {
                    kitBrowserShowRareCategories_ = !kitBrowserShowRareCategories_;
                });
            }
            if (kitBrowserShowRareCategories_)
            {
                for (const auto& [category, count] : galleryCategoryCounts)
                {
                    if (count > 10)
                    {
                        continue;
                    }
                    placeCategoryChip(fmt::format("{}##kit_browser_category_{}", CategoryLabel(category), category),
                                      kitBrowserGalleryCategory_ == category, [&]()
                    {
                        kitBrowserGalleryCategory_ = category;
                    });
                }
            }
            ImGui::PopStyleVar();
            ImGui::Separator();

            if (kits_.empty())
            {
                ImGui::TextDisabled("assets/scad/lib 下没有 Kit");
            }
            else
            {
                NextUI::IUserInterface* userInterface = engine_.GetUserInterface();
                Vulkan::AssetThumbnailRenderer* thumbnails = userInterface != nullptr
                    ? &EditorPreview::AssetThumbnails(engine_.GetRenderer())
                    : nullptr;
                constexpr float thumbnailGap = 10.0f;
                constexpr float thumbnailSize = 96.0f;
                const int columnCount = std::max(
                    1, static_cast<int>((ImGui::GetContentRegionAvail().x + thumbnailGap) /
                                         (thumbnailSize + thumbnailGap)));
                const float rowHeight = thumbnailSize + thumbnailGap;

                ImGui::BeginChild("##kit_browser_gallery_scroll", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None);
                uint32_t thumbnailIndexBase = 0;
                bool foundAnyModule = false;
                for (int kitIndex = 0; kitIndex < static_cast<int>(kits_.size()); ++kitIndex)
                {
                    const FKitInfo& kit = kits_[kitIndex];
                    std::vector<int> visibleModuleIndices;
                    visibleModuleIndices.reserve(kit.modules.size());
                    for (int moduleIndex = 0; moduleIndex < static_cast<int>(kit.modules.size()); ++moduleIndex)
                    {
                        const FKitModuleInfo& module = kit.modules[moduleIndex];
                        const bool textMatches = kitBrowserGalleryFilterBuf_[0] == '\0' ||
                            kit.name.find(kitBrowserGalleryFilterBuf_) != std::string::npos ||
                            kit.scaleClass.find(kitBrowserGalleryFilterBuf_) != std::string::npos ||
                            module.name.find(kitBrowserGalleryFilterBuf_) != std::string::npos ||
                            module.category.find(kitBrowserGalleryFilterBuf_) != std::string::npos ||
                            module.params.find(kitBrowserGalleryFilterBuf_) != std::string::npos;
                        const bool categoryMatches = kitBrowserGalleryCategory_.empty() ||
                            module.category == kitBrowserGalleryCategory_;
                        if (textMatches && categoryMatches)
                        {
                            visibleModuleIndices.push_back(moduleIndex);
                        }
                    }
                    if (!visibleModuleIndices.empty())
                    {
                        foundAnyModule = true;
                        const std::string kitHeader = fmt::format(
                            "{}  {} 个模块##kit_browser_gallery_group_{}", kit.name, visibleModuleIndices.size(), kitIndex);
                        if (ImGui::CollapsingHeader(kitHeader.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
                        {
                            const int rowCount =
                                (static_cast<int>(visibleModuleIndices.size()) + columnCount - 1) / columnCount;
                            ImGuiListClipper clipper;
                            clipper.Begin(rowCount, rowHeight);
                            while (clipper.Step())
                            {
                                for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
                                {
                                    for (int column = 0; column < columnCount; ++column)
                                    {
                                        const int visibleIndex = row * columnCount + column;
                                        if (visibleIndex >= static_cast<int>(visibleModuleIndices.size()))
                                        {
                                            break;
                                        }
                                        const int moduleIndex = visibleModuleIndices[visibleIndex];
                                        const FKitModuleInfo& module = kit.modules[moduleIndex];
                                        if (column > 0)
                                        {
                                            ImGui::SameLine(0.0f, thumbnailGap);
                                        }
                                        ImGui::PushID(static_cast<int>(thumbnailIndexBase) + moduleIndex);
                                        ImTextureID textureId = 0;
                                        std::string thumbnailSource;
                                        uint64_t thumbnailHash = 0;
                                        if (thumbnails != nullptr &&
                                            EnsureKitThumbnailSource(kitIndex, moduleIndex, thumbnailSource, thumbnailHash))
                                        {
                                            const uint32_t sampleSlot = thumbnails->RequestScadKitThumbnail(
                                                thumbnailIndexBase + static_cast<uint32_t>(moduleIndex), thumbnailSource,
                                                thumbnailHash);
                                            if (sampleSlot != std::numeric_limits<uint32_t>::max())
                                            {
                                                textureId = userInterface->RequestImTextureIdRaw(sampleSlot);
                                            }
                                        }
                                        const FKitThumbnailControlResult thumbnailControl = DrawKitThumbnailControl(
                                            "##kit_browser_gallery_thumbnail", textureId, module.name, thumbnailSize,
                                            kitBrowserSelectedKit_ == kitIndex && kitBrowserSelectedModule_ == moduleIndex);
                                        if (thumbnailControl.clicked)
                                        {
                                            kitBrowserSelectedKit_ = kitIndex;
                                            kitBrowserSelectedModule_ = moduleIndex;
                                            PreviewModule(kitIndex, module.name);
                                        }
                                        if (thumbnailControl.hovered)
                                        {
                                            ImGui::SetTooltip("%s%s%s", module.name.c_str(),
                                                              module.params.empty() ? "" : "\n参数：",
                                                              module.params.empty() ? "" : module.params.c_str());
                                        }
                                        ImGui::PopID();
                                    }
                                }
                            }
                        }
                    }
                    thumbnailIndexBase += static_cast<uint32_t>(kit.modules.size());
                }
                if (!foundAnyModule)
                {
                    ImGui::TextDisabled("没有匹配的 Kit 模块");
                }
                ImGui::EndChild();
            }
        }
        ImGui::End();
        ImGui::PopStyleVar(3);

        // Kit catalog.
        ImGui::SetNextWindowPos(ImVec2(pos.x, pos.y + kKitBrowserHeaderHeight), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(leftWidth, catalogHeight), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.98f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 12.0f));
        if (ImGui::Begin("##ScadLibraryKitCatalog", nullptr, flags))
        {
            ImGui::TextUnformatted(ICON_FA_LIST "  Kit 目录");
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputTextWithHint("##kit_browser_filter", ICON_FA_MAGNIFYING_GLASS " 搜索 Kit、模块或尺度…",
                                     kitBrowserFilterBuf_, sizeof(kitBrowserFilterBuf_));
            ImGui::TextDisabled("%zu 个 Kit", kits_.size());
            ImGui::Separator();
            std::vector<int> visibleKitIndices;
            for (int kitIndex = 0; kitIndex < static_cast<int>(kits_.size()); ++kitIndex)
            {
                const FKitInfo& kit = kits_[kitIndex];
                bool matches = kitBrowserFilterBuf_[0] == '\0' ||
                    kit.name.find(kitBrowserFilterBuf_) != std::string::npos ||
                    kit.scaleClass.find(kitBrowserFilterBuf_) != std::string::npos;
                if (!matches)
                {
                    matches = std::any_of(kit.modules.begin(), kit.modules.end(), [&](const FKitModuleInfo& module)
                                          {
                                              return module.name.find(kitBrowserFilterBuf_) != std::string::npos ||
                                                  module.category.find(kitBrowserFilterBuf_) != std::string::npos;
                                          });
                }
                if (!matches)
                {
                    continue;
                }
                visibleKitIndices.push_back(kitIndex);
            }

            if (visibleKitIndices.empty())
            {
                ImGui::TextDisabled(kits_.empty() ? "assets/scad/lib 下没有 Kit" : "没有匹配的 Kit");
            }
            else if (ImGui::BeginTable("##kit_browser_catalog_table", 3,
                                       ImGuiTableFlags_Sortable | ImGuiTableFlags_RowBg |
                                           ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_BordersOuter |
                                           ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_Resizable |
                                           ImGuiTableFlags_ScrollY,
                                       ImVec2(0.0f, 0.0f)))
            {
                ImGui::TableSetupColumn("Kit", ImGuiTableColumnFlags_DefaultSort);
                ImGui::TableSetupColumn("模块数");
                ImGui::TableSetupColumn("尺度");
                ImGui::TableHeadersRow();

                const ImGuiTableSortSpecs* sortSpecs = ImGui::TableGetSortSpecs();
                if (sortSpecs != nullptr && sortSpecs->SpecsCount > 0)
                {
                    const ImGuiTableColumnSortSpecs& spec = sortSpecs->Specs[0];
                    std::stable_sort(visibleKitIndices.begin(), visibleKitIndices.end(), [&](int lhs, int rhs)
                    {
                        const FKitInfo& leftKit = kits_[lhs];
                        const FKitInfo& rightKit = kits_[rhs];
                        int result = 0;
                        if (spec.ColumnIndex == 0)
                        {
                            result = leftKit.name.compare(rightKit.name);
                        }
                        else if (spec.ColumnIndex == 1)
                        {
                            result = leftKit.modules.size() < rightKit.modules.size()
                                ? -1
                                : (leftKit.modules.size() > rightKit.modules.size() ? 1 : 0);
                        }
                        else
                        {
                            const std::string leftScale = leftKit.scaleClass.empty() ? "未标注" : leftKit.scaleClass;
                            const std::string rightScale = rightKit.scaleClass.empty() ? "未标注" : rightKit.scaleClass;
                            result = leftScale.compare(rightScale);
                        }
                        if (result == 0)
                        {
                            result = leftKit.name.compare(rightKit.name);
                        }
                        return spec.SortDirection == ImGuiSortDirection_Descending ? result > 0 : result < 0;
                    });
                }

                for (const int kitIndex : visibleKitIndices)
                {
                    const FKitInfo& kit = kits_[kitIndex];
                    ImGui::TableNextRow(ImGuiTableRowFlags_None, 36.0f);
                    ImGui::TableSetColumnIndex(0);
                    ImGui::PushID(kitIndex);
                    if (ImGui::Selectable(kit.name.c_str(), kitBrowserSelectedKit_ == kitIndex,
                                          ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap,
                                          ImVec2(0.0f, 0.0f)))
                    {
                        kitBrowserSelectedKit_ = kitIndex;
                        kitBrowserSelectedModule_ = kit.modules.empty() ? -1 : 0;
                        if (!kit.modules.empty())
                        {
                            PreviewModule(kitIndex, kit.modules[0].name);
                        }
                    }
                    if (ImGui::IsItemHovered())
                    {
                        ImGui::SetTooltip("%s", kit.filePath.c_str());
                    }
                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text("%zu", kit.modules.size());
                    ImGui::TableSetColumnIndex(2);
                    ImGui::TextDisabled("%s", kit.scaleClass.empty() ? "未标注" : kit.scaleClass.c_str());
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
        }
        ImGui::End();
        ImGui::PopStyleVar(3);

        // A small overlay keeps the viewport self-describing and exposes frame-all.
        ImGui::SetNextWindowPos(ImVec2(viewportPos.x + 12.0f, viewportPos.y + 12.0f), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.90f);
        const ImGuiWindowFlags toolbarFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 5.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(5.0f, 5.0f));
        if (ImGui::Begin("##ScadLibraryKitPreviewToolbar", nullptr, toolbarFlags))
        {
            if (NextUI::Theme::ToolbarButton(ICON_FA_ROTATE_LEFT, "回到全览视图", false))
            {
                frameAllRequested_ = true;
            }
            ImGui::SameLine();
            if (kitBrowserSelectedKit_ >= 0 && kitBrowserSelectedKit_ < static_cast<int>(kits_.size()) &&
                kitBrowserSelectedModule_ >= 0 &&
                kitBrowserSelectedModule_ < static_cast<int>(kits_[kitBrowserSelectedKit_].modules.size()))
            {
                ImGui::Text("%s", kits_[kitBrowserSelectedKit_].modules[kitBrowserSelectedModule_].name.c_str());
            }
            else
            {
                ImGui::TextDisabled("选择模块以预览");
            }
        }
        ImGui::End();
        ImGui::PopStyleVar(2);
    }

    void ScadLibraryInterface::DrawBrowserPanel(const ImVec2& pos, const ImVec2& size)
    {
        ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(size, ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.96f);
        const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, browserCollapsed_ ? ImVec2(7.0f, 8.0f) : ImVec2(12.0f, 12.0f));
        if (!ImGui::Begin("##ScadLibraryBrowser", nullptr, flags))
        {
            ImGui::End();
            ImGui::PopStyleVar(3);
            return;
        }
        ImGui::PopStyleVar(3);

        if (browserCollapsed_)
        {
            if (ImGui::Button(ICON_FA_CHEVRON_RIGHT "##expand_browser", ImVec2(30.0f, 30.0f)))
            {
                browserCollapsed_ = false;
            }
            ImGui::End();
            return;
        }

        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(ICON_FA_FOLDER_TREE "  资源库");
        ImGui::SameLine(ImGui::GetWindowWidth() - 82.0f);
        if (NextUI::Theme::IconButton(ICON_FA_ARROWS_ROTATE "##rescan", "重新扫描场景与 Kit", false,
                                      ImVec2(28.0f, 28.0f)))
        {
            RescanKits();
            RescanAssemblies();
        }
        ImGui::SameLine();
        if (NextUI::Theme::IconButton(ICON_FA_CHEVRON_LEFT "##collapse_browser", "收起资源库", false,
                                      ImVec2(28.0f, 28.0f)))
        {
            browserCollapsed_ = true;
        }
        NextUI::Theme::DrawThinSeparator(0.72f);

        if (ImGui::BeginTabBar("##library_tabs", ImGuiTabBarFlags_FittingPolicyResizeDown))
        {
            if (ImGui::BeginTabItem(fmt::format(ICON_FA_CITY "  场景  {}", assemblies_.size()).c_str()))
            {
                ImGui::Spacing();
                ImGui::SetNextItemWidth(-1.0f);
                ImGui::InputTextWithHint("##assembly_filter", ICON_FA_MAGNIFYING_GLASS " 搜索场景路径或 Kit…",
                                         assemblyFilterBuf_, sizeof(assemblyFilterBuf_));
                const size_t terrainCount =
                    std::count_if(assemblies_.begin(), assemblies_.end(),
                                  [](const FSceneAssemblyInfo& scene) { return scene.hasTerrain; });
                const size_t structureCount =
                    std::count_if(assemblies_.begin(), assemblies_.end(),
                                  [](const FSceneAssemblyInfo& scene) { return scene.hasFreeStructure; });
                ImGui::TextDisabled("%zu 个场景  ·  含地形 %zu  ·  含程序结构 %zu", assemblies_.size(), terrainCount,
                                    structureCount);
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("目录只是归档方式。每个场景都能同时拥有实例、地形和源码结构，\n"
                                      "打开后按节点分别编辑，无需整体转换。");
                }
                ImGui::Spacing();
                ImGui::BeginChild("##assembly_files", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None);

                struct FAssemblyGroupView
                {
                    std::string key;
                    std::string label;
                    std::vector<int> indices;
                };
                std::vector<FAssemblyGroupView> groups;
                for (int index = 0; index < static_cast<int>(assemblies_.size()); ++index)
                {
                    const FSceneAssemblyInfo& assembly = assemblies_[index];
                    const std::string dependencies = fmt::format("{}", fmt::join(assembly.kitDependencies, ", "));
                    if (assemblyFilterBuf_[0] != '\0' &&
                        assembly.relativePath.find(assemblyFilterBuf_) == std::string::npos &&
                        dependencies.find(assemblyFilterBuf_) == std::string::npos)
                    {
                        continue;
                    }
                    auto groupIt = std::find_if(groups.begin(), groups.end(), [&](const FAssemblyGroupView& group)
                    {
                        return group.key == assembly.categoryKey;
                    });
                    if (groupIt == groups.end())
                    {
                        groups.push_back({assembly.categoryKey, assembly.categoryLabel, {}});
                        groupIt = std::prev(groups.end());
                    }
                    groupIt->indices.push_back(index);
                }

                std::sort(groups.begin(), groups.end(), [](const FAssemblyGroupView& left,
                                                           const FAssemblyGroupView& right)
                {
                    const int leftRank = SceneCategorySortRank(left.key);
                    const int rightRank = SceneCategorySortRank(right.key);
                    return leftRank != rightRank ? leftRank < rightRank : left.key < right.key;
                });

                for (FAssemblyGroupView& group : groups)
                {
                    std::stable_sort(group.indices.begin(), group.indices.end(), [&](int leftIndex, int rightIndex)
                    {
                        const FSceneAssemblyInfo& left = assemblies_[leftIndex];
                        const FSceneAssemblyInfo& right = assemblies_[rightIndex];
                        if (left.folder != right.folder)
                        {
                            return static_cast<int>(left.folder) < static_cast<int>(right.folder);
                        }
                        return left.relativePath < right.relativePath;
                    });

                    size_t terrainInGroup = 0;
                    size_t structureInGroup = 0;
                    for (const int index : group.indices)
                    {
                        terrainInGroup += assemblies_[index].hasTerrain ? 1 : 0;
                        structureInGroup += assemblies_[index].hasFreeStructure ? 1 : 0;
                    }

                    const std::string groupTitle = fmt::format("{}  {} 个场景##assembly_group_{}",
                                                                group.label, group.indices.size(), group.key);
                    const bool showcaseGroup = group.key.starts_with("showcase/");
                    if (!ImGui::CollapsingHeader(groupTitle.c_str(),
                                                 showcaseGroup ? ImGuiTreeNodeFlags_DefaultOpen : 0))
                    {
                        continue;
                    }
                    std::string compositionSummary;
                    auto appendCompositionCount = [&](const char* label, size_t count)
                    {
                        if (count == 0)
                        {
                            return;
                        }
                        if (!compositionSummary.empty())
                        {
                            compositionSummary += "  ·  ";
                        }
                        compositionSummary += fmt::format("{} {}", label, count);
                    };
                    appendCompositionCount("含地形", terrainInGroup);
                    appendCompositionCount("含程序结构", structureInGroup);
                    ImGui::TextDisabled("%s", compositionSummary.c_str());
                    ImGui::Indent(8.0f);
                    for (const int index : group.indices)
                    {
                        const FSceneAssemblyInfo& assembly = assemblies_[index];
                        const std::string dependencies = fmt::format("{}", fmt::join(assembly.kitDependencies, ", "));
                        constexpr float rowHeight = 29.0f;
                        const ImVec2 rowPosition = ImGui::GetCursorScreenPos();
                        const float rowWidth = ImGui::GetContentRegionAvail().x;
                        const bool rowSelected = index == selectedAssembly_;
                        const bool rowClicked = ImGui::Selectable(fmt::format("##assembly_row_{}", index).c_str(),
                                                                  rowSelected, 0,
                                                                  ImVec2(rowWidth, rowHeight));
                        const bool rowHovered = ImGui::IsItemHovered();

                        ImGui::SetCursorScreenPos(ImVec2(rowPosition.x + 8.0f,
                                                         rowPosition.y +
                                                             std::floor((rowHeight - ImGui::GetTextLineHeight()) * 0.5f)));
                        ImGui::TextColored(SceneFolderIconColor(assembly.folder), "%s",
                                           SceneFolderIcon(assembly.folder));
                        ImGui::SameLine(0.0f, 7.0f);
                        const std::string pureName = std::filesystem::path(assembly.relativePath).stem().string();
                        ImGui::TextUnformatted(pureName.c_str());
                        if (assembly.hasTerrain)
                        {
                            ImGui::SameLine(0.0f, 6.0f);
                            ImGui::TextColored(SegmentKindColor(EScadSegmentKind::Terrain), ICON_FA_MOUNTAIN_SUN);
                        }
                        if (assembly.hasFreeStructure)
                        {
                            ImGui::SameLine(0.0f, 6.0f);
                            ImGui::TextColored(SegmentKindColor(EScadSegmentKind::Source), ICON_FA_CODE_BRANCH);
                        }
                        ImGui::SetCursorScreenPos(ImVec2(rowPosition.x,
                                                         rowPosition.y + rowHeight + ImGui::GetStyle().ItemSpacing.y));

                        if (rowClicked)
                        {
                            selectedAssembly_ = index;
                            OpenAssembly(assembly.relativePath);
                        }
                        if (rowHovered)
                        {
                            ImGui::SetTooltip("类别: %s  ·  目录: %s\n依赖: %s%s%s%s", group.label.c_str(),
                                              SceneFolderLabel(assembly.folder), dependencies.c_str(),
                                              assembly.hasTerrain ? "\n含地形与过程规则" : "",
                                              assembly.hasFreeStructure ? "\n含循环 / module 等程序结构" : "",
                                              assembly.generated ? "\n由规格或工具生成，手改可能被覆盖" : "");
                        }
                    }
                    ImGui::Unindent(8.0f);
                }
                if (groups.empty())
                {
                    ImGui::TextDisabled(assemblies_.empty() ? "未找到引用 kit_*.scad 的场景" : "没有匹配的场景");
                }
                ImGui::EndChild();
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem(fmt::format(ICON_FA_CUBES_STACKED "  Kit  {}", kits_.size()).c_str()))
            {
                ImGui::Spacing();
                ImGui::SetNextItemWidth(-1.0f);
                ImGui::InputTextWithHint("##filter", ICON_FA_MAGNIFYING_GLASS " 搜索 Kit 或模块…", filterBuf_,
                                         sizeof(filterBuf_));
                ImGui::TextUnformatted("缩略图布局");
                ImGui::SameLine();
                for (const int columns : {2, 3, 4})
                {
                    const std::string label = fmt::format("{} 列##kit_thumbnail_columns_{}", columns, columns);
                    if (ImGui::RadioButton(label.c_str(), kitThumbnailColumns_ == columns))
                    {
                        kitThumbnailColumns_ = columns;
                    }
                    if (columns != 4)
                    {
                        ImGui::SameLine();
                    }
                }
                ImGui::Spacing();

                if (kitThumbnailExpanded_.size() != kits_.size())
                {
                    kitThumbnailExpanded_.assign(kits_.size(), false);
                }
                if (kitThumbnailScrollY_ > 0.0f && kitThumbnailStickyKit_ >= 0 &&
                    kitThumbnailStickyKit_ < static_cast<int>(kits_.size()))
                {
                    const FKitInfo& stickyKit = kits_[kitThumbnailStickyKit_];
                    const std::string stickyHeader = fmt::format(
                        "{}  {} 个模块##kit_thumbnail_sticky_header", stickyKit.name, stickyKit.modules.size());
                    ImGui::SetNextItemOpen(kitThumbnailExpanded_[kitThumbnailStickyKit_], ImGuiCond_Always);
                    kitThumbnailExpanded_[kitThumbnailStickyKit_] = ImGui::CollapsingHeader(stickyHeader.c_str());
                }

                ImGui::BeginChild("##kit_preview_library", ImVec2(0, 0), ImGuiChildFlags_None);
                NextUI::IUserInterface* userInterface = engine_.GetUserInterface();
                Vulkan::AssetThumbnailRenderer* thumbnails = nullptr;
                if (userInterface != nullptr)
                {
                    thumbnails = &EditorPreview::AssetThumbnails(engine_.GetRenderer());
                }

                constexpr float cardGap = 8.0f;
                const float availableWidth = std::max(1.0f, ImGui::GetContentRegionAvail().x);
                const int columnCount = std::clamp(kitThumbnailColumns_, 2, 4);
                const float thumbnailSize = std::max(
                    1.0f, (availableWidth - cardGap * static_cast<float>(columnCount - 1)) /
                              static_cast<float>(columnCount));
                const float rowHeight = thumbnailSize + cardGap;
                const float currentScrollY = ImGui::GetScrollY();
                int stickyCandidate = -1;

                uint32_t thumbnailIndexBase = 0;
                for (int kitIndex = 0; kitIndex < static_cast<int>(kits_.size()); ++kitIndex)
                {
                    const FKitInfo& kit = kits_[kitIndex];
                    std::vector<int> visibleModuleIndices;
                    visibleModuleIndices.reserve(kit.modules.size());
                    for (int moduleIndex = 0; moduleIndex < static_cast<int>(kit.modules.size()); ++moduleIndex)
                    {
                        const FKitModuleInfo& module = kit.modules[moduleIndex];
                        const bool matches = filterBuf_[0] == '\0' ||
                            kit.name.find(filterBuf_) != std::string::npos ||
                            PassesFilter(module, filterBuf_) ||
                            module.params.find(filterBuf_) != std::string::npos;
                        if (matches)
                        {
                            visibleModuleIndices.push_back(moduleIndex);
                        }
                    }

                    if (visibleModuleIndices.empty())
                    {
                        thumbnailIndexBase += static_cast<uint32_t>(kit.modules.size());
                        continue;
                    }

                    const std::string kitHeader =
                        fmt::format("{}  {} 个模块##kit_thumbnail_group_{}", kit.name, visibleModuleIndices.size(), kitIndex);
                    if (ImGui::GetCursorPosY() <= currentScrollY + 1.0f)
                    {
                        stickyCandidate = kitIndex;
                    }
                    ImGui::SetNextItemOpen(kitThumbnailExpanded_[kitIndex], ImGuiCond_Always);
                    kitThumbnailExpanded_[kitIndex] = ImGui::CollapsingHeader(kitHeader.c_str());
                    if (!kitThumbnailExpanded_[kitIndex])
                    {
                        thumbnailIndexBase += static_cast<uint32_t>(kit.modules.size());
                        continue;
                    }

                    ImGuiListClipper clipper;
                    const int rowCount = (static_cast<int>(visibleModuleIndices.size()) + columnCount - 1) /
                        columnCount;
                    clipper.Begin(rowCount, rowHeight);
                    while (clipper.Step())
                    {
                        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
                        {
                            for (int column = 0; column < columnCount; ++column)
                            {
                                const int visibleIndex = row * columnCount + column;
                                if (visibleIndex >= static_cast<int>(visibleModuleIndices.size()))
                                {
                                    break;
                                }
                                const int moduleIndex = visibleModuleIndices[visibleIndex];
                                const FKitModuleInfo& module = kit.modules[moduleIndex];
                                const uint32_t thumbnailIndex = thumbnailIndexBase +
                                    static_cast<uint32_t>(moduleIndex);
                                if (column > 0)
                                {
                                    ImGui::SameLine(0.0f, cardGap);
                                }

                                ImGui::PushID(static_cast<int>(thumbnailIndex));
                                ImGui::BeginGroup();
                                const bool selected = selectedKit_ == kitIndex && selectedModule_ == module.name;
                                ImTextureID textureId = 0;
                                std::string thumbnailSource;
                                uint64_t thumbnailHash = 0;
                                if (thumbnails != nullptr &&
                                    EnsureKitThumbnailSource(kitIndex, moduleIndex, thumbnailSource, thumbnailHash))
                                {
                                    const uint32_t sampleSlot = thumbnails->RequestScadKitThumbnail(
                                        thumbnailIndex, thumbnailSource, thumbnailHash);
                                    if (sampleSlot != std::numeric_limits<uint32_t>::max())
                                    {
                                        textureId = userInterface->RequestImTextureIdRaw(sampleSlot);
                                    }
                                }

                                const FKitThumbnailControlResult thumbnailControl = DrawKitThumbnailControl(
                                    "##kit_preview", textureId, module.name, thumbnailSize, selected);
                                if (thumbnailControl.clicked)
                                {
                                    // Selecting a card is deliberately local to the library. The old
                                    // PreviewModule() path remains available from the context menu for
                                    // the standalone kit editor, but browsing must not replace the scene.
                                    selectedKit_ = kitIndex;
                                    selectedModule_ = module.name;
                                }

                                if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
                                {
                                    const FScadKitDragPayload payload{kitIndex, moduleIndex};
                                    ImGui::SetDragDropPayload(kScadKitDragDropPayload, &payload, sizeof(payload));
                                    if (textureId != 0)
                                    {
                                        ImGui::Image(textureId, ImVec2(64.0f, 64.0f));
                                        ImGui::SameLine();
                                    }
                                    ImGui::TextUnformatted(module.name.c_str());
                                    ImGui::TextDisabled("拖到中央视口放置");
                                    ImGui::EndDragDropSource();
                                }

                                if (thumbnailControl.hovered)
                                {
                                    if (module.hasMetrics)
                                    {
                                        ImGui::SetTooltip("%s%s%s\n脚印 %.1f × %.1f  · 高 %.1f\n三角形 %d\n拖到中央视口即可放置",
                                                          module.name.c_str(), module.params.empty() ? "" : "\n参数：",
                                                          module.params.empty() ? "" : module.params.c_str(),
                                                          module.footprintX, module.footprintY, module.height,
                                                          module.triangles);
                                    }
                                    else
                                    {
                                        ImGui::SetTooltip("%s\n拖到中央视口即可放置", module.name.c_str());
                                    }
                                }
                                if (ImGui::BeginPopupContextItem("##kit_module_context"))
                                {
                                    if (ImGui::MenuItem("AI 编辑此模块"))
                                    {
                                        PreviewModule(kitIndex, module.name);
                                        inspectorPrimaryTab_ = 1;
                                        aiOpenRequested_ = true;
                                        benchCollapsed_ = false;
                                    }
                                    ImGui::EndPopup();
                                }
                                ImGui::EndGroup();
                                ImGui::PopID();
                            }
                        }
                    }
                    clipper.End();
                    ImGui::Spacing();
                    thumbnailIndexBase += static_cast<uint32_t>(kit.modules.size());
                }
                if (kits_.empty())
                {
                    ImGui::TextDisabled("assets/scad/lib 下没有 kit_*.scad");
                }
                kitThumbnailScrollY_ = ImGui::GetScrollY();
                if (stickyCandidate >= 0)
                {
                    kitThumbnailStickyKit_ = stickyCandidate;
                }
                ImGui::EndChild();
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem(fmt::format(ICON_FA_CODE_BRANCH "  Outliner  {}", document_.Segments().size()).c_str()))
            {
                DrawStructureOutliner();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        ImGui::End();
    }

    void ScadLibraryInterface::DrawBoneHierarchyPanel(const ImVec2& pos, const ImVec2& size)
    {
        ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(size, ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.98f);
        const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 10.0f));
        if (!ImGui::Begin("##ScadLibraryBoneHierarchy", nullptr, flags))
        {
            ImGui::End();
            ImGui::PopStyleVar(3);
            return;
        }
        ImGui::PopStyleVar(3);

        ImGui::TextUnformatted(ICON_FA_BONE "  骨骼层级");
        ImGui::Separator();
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##bone_filter", ICON_FA_MAGNIFYING_GLASS " 搜索骨骼/节点", boneFilterBuf_,
                                 sizeof(boneFilterBuf_));

        if (workbench_.Clips().empty() || rigPreview_.Asset().bones.empty())
        {
            ImGui::TextDisabled("当前角色没有骨骼。");
            ImGui::End();
            return;
        }

        FEditableRigClip& clip = workbench_.Clips()[workbenchClip_];
        workbenchBone_ = std::clamp(workbenchBone_, 0, static_cast<int>(rigPreview_.Asset().bones.size()) - 1);
        const auto selectBone = [&](int boneIndex)
        {
            workbenchBone_ = boneIndex;
            timelineSelectedChannel_ = -1;
            timelineSelectedKey_ = -1;
            for (int channelIndex = 0; channelIndex < static_cast<int>(clip.channels.size()); ++channelIndex)
            {
                if (clip.channels[channelIndex].bone == boneIndex)
                {
                    timelineSelectedChannel_ = channelIndex;
                    break;
                }
            }
        };
        const auto channelCountForBone = [&](int boneIndex)
        {
            int count = 0;
            for (const FEditableRigChannel& channel : clip.channels)
            {
                count += channel.bone == boneIndex ? 1 : 0;
            }
            return count;
        };

        ImGui::BeginChild("##bone_tree", ImVec2(0.0f, -184.0f), ImGuiChildFlags_None);
        const std::string filter = boneFilterBuf_;
        if (!filter.empty())
        {
            for (int boneIndex = 0; boneIndex < static_cast<int>(rigPreview_.Asset().bones.size()); ++boneIndex)
            {
                const Assets::FRigBone& bone = rigPreview_.Asset().bones[boneIndex];
                if (bone.name.find(filter) == std::string::npos)
                {
                    continue;
                }
                const int channelCount = channelCountForBone(boneIndex);
                const std::string label =
                    channelCount > 0 ? fmt::format("{}  [{}]", bone.name, channelCount) : bone.name;
                if (ImGui::Selectable(label.c_str(), boneIndex == workbenchBone_))
                {
                    selectBone(boneIndex);
                }
            }
        }
        else
        {
            const auto drawBoneTree = [&](auto&& self, int boneIndex) -> void
            {
                const Assets::FRigBone& bone = rigPreview_.Asset().bones[boneIndex];
                ImGuiTreeNodeFlags treeFlags =
                    ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen;
                if (bone.children.empty())
                {
                    treeFlags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
                }
                if (boneIndex == workbenchBone_)
                {
                    treeFlags |= ImGuiTreeNodeFlags_Selected;
                }
                const int channelCount = channelCountForBone(boneIndex);
                const std::string label =
                    channelCount > 0 ? fmt::format("{}  [{}]", bone.name, channelCount) : bone.name;
                const bool open = ImGui::TreeNodeEx(reinterpret_cast<void*>(static_cast<intptr_t>(boneIndex + 1)),
                                                    treeFlags, "%s", label.c_str());
                if (ImGui::IsItemClicked())
                {
                    selectBone(boneIndex);
                }
                if (open && !bone.children.empty())
                {
                    for (int32_t child : bone.children)
                    {
                        if (child >= 0 && child < static_cast<int32_t>(rigPreview_.Asset().bones.size()))
                        {
                            self(self, child);
                        }
                    }
                    ImGui::TreePop();
                }
            };
            for (int boneIndex = 0; boneIndex < static_cast<int>(rigPreview_.Asset().bones.size()); ++boneIndex)
            {
                if (rigPreview_.Asset().bones[boneIndex].parent < 0)
                {
                    drawBoneTree(drawBoneTree, boneIndex);
                }
            }
        }
        ImGui::EndChild();

        const Assets::FRigBone& selectedBone = rigPreview_.Asset().bones[workbenchBone_];
        ImGui::Separator();
        ImGui::TextUnformatted("骨骼信息");
        ImGui::Separator();
        ImGui::TextDisabled("名称");
        ImGui::SameLine(72.0f);
        ImGui::TextUnformatted(selectedBone.name.c_str());
        ImGui::TextDisabled("类型");
        ImGui::SameLine(72.0f);
        ImGui::TextUnformatted("Transform");
        ImGui::TextDisabled("子级数量");
        ImGui::SameLine(72.0f);
        ImGui::Text("%zu", selectedBone.children.size());
        ImGui::TextDisabled("动画轨道");
        ImGui::SameLine(72.0f);
        ImGui::Text("%d", channelCountForBone(workbenchBone_));
        ImGui::End();
    }

    void ScadLibraryInterface::DrawModePanel(const ImVec2& pos, const ImVec2& size)
    {
        ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(size, ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.96f);
        const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, benchCollapsed_ ? ImVec2(7.0f, 8.0f) : ImVec2(12.0f, 12.0f));
        if (!ImGui::Begin("##ScadLibraryBench", nullptr, flags))
        {
            ImGui::End();
            ImGui::PopStyleVar(3);
            return;
        }
        ImGui::PopStyleVar(3);

        if (benchCollapsed_)
        {
            if (ImGui::Button(ICON_FA_CHEVRON_LEFT "##expand_bench", ImVec2(30.0f, 30.0f)))
            {
                benchCollapsed_ = false;
            }
            ImGui::End();
            return;
        }

        ImGui::AlignTextToFramePadding();
        if (workspaceMode_ == EWorkspaceMode::SceneAssembly)
        {
            if (document_.HasTerrain())
            {
                ImGui::Text(ICON_FA_SLIDERS "  场景属性  ·  %zu 对象 / %zu 特征 / %zu 规则", Bench().size(),
                            TerrainProcess().Terrain().features.size(), TerrainProcess().ActiveRuleCount());
            }
            else
            {
                ImGui::Text(ICON_FA_SLIDERS "  场景属性  ·  %zu 对象", Bench().size());
            }
        }
        else if (workspaceMode_ == EWorkspaceMode::CharacterDesigner)
        {
            ImGui::TextUnformatted(ICON_FA_SLIDERS "  角色属性");
        }
        else
        {
            ImGui::TextUnformatted(ICON_FA_SLIDERS "  动作与装备属性");
        }
        ImGui::SameLine(ImGui::GetWindowWidth() - 43.0f);
        if (NextUI::Theme::IconButton(ICON_FA_CHEVRON_RIGHT "##collapse_bench", "收起属性面板", false,
                                      ImVec2(28.0f, 28.0f)))
        {
            benchCollapsed_ = true;
        }
        NextUI::Theme::DrawThinSeparator(0.72f);

        if (ImGui::BeginTabBar("##inspector_primary_tabs"))
        {
            if (ImGui::BeginTabItem("属性"))
            {
                inspectorPrimaryTab_ = 0;
                if (workspaceMode_ == EWorkspaceMode::SceneAssembly)
                {
                    DrawBenchContent();
                }
                else if (workspaceMode_ == EWorkspaceMode::CharacterDesigner)
                {
                    DrawDesignerContent();
                }
                else
                {
                    DrawWorkbenchContent();
                }
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("AI", nullptr,
                                    aiOpenRequested_ ? ImGuiTabItemFlags_SetSelected : 0))
            {
                inspectorPrimaryTab_ = 1;
                DrawAIContent();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
            aiOpenRequested_ = false;
        }
        ImGui::End();
    }

    AI::FScadAIEditTarget ScadLibraryInterface::ResolveAITarget() const
    {
        AI::FScadAIEditTarget target;
        if (workspaceMode_ == EWorkspaceMode::CharacterWorkbench)
        {
            target.kind = AI::EScadAIEditKind::RigClip;
            target.documentKey = workbench_.SourcePath().empty() ? std::string(rigSourceBuf_) : workbench_.SourcePath();
            target.displayName = std::filesystem::path(target.documentKey).filename().string();
            if (!workbench_.Clips().empty())
            {
                const int clipIndex = std::clamp(workbenchClip_, 0, static_cast<int>(workbench_.Clips().size()) - 1);
                target.primaryId = workbench_.Clips()[clipIndex].name;
            }
            if (rigPreview_.HasRig() && workbenchBone_ >= 0 &&
                workbenchBone_ < static_cast<int>(rigPreview_.Asset().bones.size()))
            {
                target.secondaryIds.push_back(rigPreview_.Asset().bones[workbenchBone_].name);
            }
            return target;
        }

        if (workspaceMode_ != EWorkspaceMode::SceneAssembly)
        {
            target.kind = AI::EScadAIEditKind::SceneSource;
            target.documentKey = "unsupported-character-designer";
            target.displayName = "角色组装（暂不在本批范围）";
            return target;
        }

        if (aiKitContextActive_ && selectedKit_ >= 0 && selectedKit_ < static_cast<int>(kits_.size()) &&
            !selectedModule_.empty())
        {
            target.kind = AI::EScadAIEditKind::KitModule;
            target.documentKey = kits_[selectedKit_].filePath;
            target.displayName = kits_[selectedKit_].name;
            target.primaryId = selectedModule_;
            return target;
        }
        // The AI target follows what the user is editing, not what kind of
        // file it is: the same scene offers object, terrain and source editing
        // and each tab hands the model the matching document view.
        if (assemblyEditorTab_ == 2 && document_.HasTerrain())
        {
            target.kind = AI::EScadAIEditKind::TerrainProcess;
            target.documentKey = openedAssemblyPath_.empty() ? "draft:terrain" : openedAssemblyPath_;
            target.displayName = std::filesystem::path(target.documentKey).filename().string();
            target.primaryId = terrainSelectionIsRule_ ? fmt::format("r{}", selectedTerrainRule_)
                                                       : fmt::format("f{}", selectedTerrainFeature_);
            return target;
        }
        target.kind = (assemblyEditorTab_ == 0 && !Bench().empty()) ? AI::EScadAIEditKind::SceneObjects
                                                                    : AI::EScadAIEditKind::SceneSource;
        target.documentKey = openedAssemblyPath_.empty() ? "draft:scene" : openedAssemblyPath_;
        target.displayName = std::filesystem::path(target.documentKey).filename().string();
        if (target.kind == AI::EScadAIEditKind::SceneObjects && selectedBenchItem_ >= 0 &&
            selectedBenchItem_ < static_cast<int>(Bench().size()))
        {
            target.primaryId = fmt::format("o{}", selectedBenchItem_);
        }
        return target;
    }

    nlohmann::json ScadLibraryInterface::CaptureAISnapshot(const AI::FScadAIEditTarget& target) const
    {
        using Kind = AI::EScadAIEditKind;
        if (target.kind == Kind::KitModule)
        {
            const std::string source = aiKitDraftDirty_ && aiKitDraftPath_ == target.documentKey
                ? aiKitDraftSource_
                : ReadAssemblyTextFile(target.documentKey);
            return {{"source", source}, {"module", target.primaryId}};
        }
        if (target.kind == Kind::SceneSource)
        {
            return {{"source", assemblySource_}};
        }
        if (target.kind == Kind::SceneObjects)
        {
            nlohmann::json objects = nlohmann::json::array();
            for (size_t index = 0; index < Bench().size(); ++index)
            {
                const FBenchItem& item = Bench()[index];
                nlohmann::json object{
                    {"id", fmt::format("o{}", index)},
                    {"kitIndex", item.kitIndex},
                    {"module", item.moduleName},
                    {"position", {item.x, item.y, item.z}},
                    {"rotation", {item.rotX, item.rotY, item.rotZ}},
                    {"scale", {item.scale, item.scaleY, item.scaleZ}},
                    {"arguments", std::string(item.args)},
                };
                if (item.hasColor)
                {
                    object["color"] = {item.color[0], item.color[1], item.color[2], item.color[3]};
                }
                objects.push_back(std::move(object));
            }
            nlohmann::json catalog = nlohmann::json::array();
            for (size_t kitIndex = 0; kitIndex < kits_.size(); ++kitIndex)
            {
                for (const FKitModuleInfo& module : kits_[kitIndex].modules)
                {
                    catalog.push_back(
                        {{"kitIndex", static_cast<int>(kitIndex)}, {"kit", kits_[kitIndex].name},
                         {"module", module.name}, {"params", module.params}});
                }
            }
            nlohmann::json selectedObject = nullptr;
            if (selectedBenchItem_ >= 0 && selectedBenchItem_ < static_cast<int>(objects.size()))
            {
                selectedObject = objects[static_cast<size_t>(selectedBenchItem_)];
                const int kitIndex = selectedObject.value("kitIndex", -1);
                if (kitIndex >= 0 && kitIndex < static_cast<int>(kits_.size()))
                {
                    selectedObject["kit"] = kits_[kitIndex].name;
                }
            }
            return {{"objects", std::move(objects)}, {"catalog", std::move(catalog)},
                    {"selectedId", target.primaryId}, {"selectedObject", std::move(selectedObject)},
                    {"selectionScope", target.primaryId.empty() ? "scene" : "selected_instance"},
                    {"fn", fnSegments_}};
        }
        if (target.kind == Kind::TerrainProcess)
        {
            const Assets::Scad::FTerrainSpec& terrain = TerrainProcess().Terrain();
            nlohmann::json terrainJson{
                {"size", {terrain.size.x, terrain.size.y}},
                {"cells", {terrain.cells.x, terrain.cells.y}},
                {"seed", terrain.seed},
                {"baseHeight", terrain.baseHeight},
                {"relief", terrain.relief},
                {"roughness", terrain.roughness},
                {"hasWaterLevel", terrain.hasWaterLevel},
                {"waterLevel", terrain.waterLevel},
                {"palette", terrain.palette},
            };
            nlohmann::json features = nlohmann::json::array();
            for (size_t index = 0; index < terrain.features.size(); ++index)
            {
                const Assets::Scad::FTerrainFeature& feature = terrain.features[index];
                nlohmann::json points = nlohmann::json::array();
                for (const glm::dvec2& point : feature.pts) points.push_back({point.x, point.y});
                features.push_back({
                    {"id", fmt::format("f{}", index)},
                    {"type", FTerrainProcessDocument::FeatureTypeName(feature.type)},
                    {"at", {feature.at.x, feature.at.y}},
                    {"size", {feature.size.x, feature.size.y}},
                    {"rotation", feature.rot},
                    {"radius", feature.radius},
                    {"height", feature.height},
                    {"depth", feature.depth},
                    {"width", feature.width},
                    {"rugged", feature.rugged},
                    {"points", std::move(points)},
                });
            }
            nlohmann::json rules = nlohmann::json::array();
            for (size_t index = 0; index < TerrainProcess().Rules().size(); ++index)
            {
                const FTerrainProcessRule& rule = TerrainProcess().Rules()[index];
                if (rule.removed) continue;
                nlohmann::json points = nlohmann::json::array();
                for (const glm::dvec2& point : rule.points) points.push_back({point.x, point.y});
                rules.push_back({
                    {"id", fmt::format("r{}", index)},
                    {"type", FTerrainProcessDocument::RuleTypeName(rule.type)},
                    {"x", rule.x}, {"y", rule.y}, {"dz", rule.dz},
                    {"sampleX", rule.sampleX}, {"sampleY", rule.sampleY},
                    {"maxTilt", rule.maxTilt}, {"probe", rule.probe},
                    {"points", std::move(points)}, {"step", rule.step}, {"seed", rule.seed},
                    {"offset", rule.offset}, {"count", rule.count},
                    {"region", {rule.region.x, rule.region.y, rule.region.z, rule.region.w}},
                    {"circularRegion", rule.circularRegion},
                    {"regionCenter", {rule.regionCenter.x, rule.regionCenter.y}},
                    {"regionRadius", rule.regionRadius}, {"minHeight", rule.minHeight},
                    {"maxHeight", rule.maxHeight}, {"maxSlope", rule.maxSlope},
                    {"avoidWater", rule.avoidWater}, {"biomes", rule.biomes},
                    {"randomRotation", rule.randomRotation}, {"variants", rule.variants},
                    {"scaleRange", {rule.scaleRange.x, rule.scaleRange.y}}, {"childSource", rule.childSource},
                });
            }
            return {{"terrain", std::move(terrainJson)}, {"features", std::move(features)},
                    {"rules", std::move(rules)}, {"selectedId", target.primaryId}};
        }

        nlohmann::json bones = nlohmann::json::array();
        if (rigPreview_.HasRig())
        {
            for (const Assets::FRigBone& bone : rigPreview_.Asset().bones) bones.push_back(bone.name);
        }
        nlohmann::json clips = nlohmann::json::array();
        for (const FEditableRigClip& clip : workbench_.Clips())
        {
            nlohmann::json channels = nlohmann::json::array();
            for (const FEditableRigChannel& channel : clip.channels)
            {
                if (!rigPreview_.HasRig() || channel.bone < 0 ||
                    channel.bone >= static_cast<int>(rigPreview_.Asset().bones.size()))
                {
                    continue;
                }
                const char* type = channel.type == EEditableRigChannel::Position
                    ? "pos"
                    : (channel.type == EEditableRigChannel::Rotation ? "rot" : "scale");
                nlohmann::json keys = nlohmann::json::array();
                for (const FEditableRigKey& key : channel.keys)
                {
                    keys.push_back({{"time", key.time}, {"value", {key.value.x, key.value.y, key.value.z}}});
                }
                channels.push_back({{"bone", rigPreview_.Asset().bones[channel.bone].name},
                                    {"channel", type}, {"keys", std::move(keys)}});
            }
            clips.push_back({{"id", clip.name}, {"name", clip.name}, {"loop", clip.loop},
                             {"duration", clip.duration}, {"channels", std::move(channels)}});
        }
        return {{"bones", std::move(bones)}, {"clips", std::move(clips)},
                {"selectedClip", target.primaryId}, {"selectedBones", target.secondaryIds}};
    }

    AI::FScadDocumentRevision ScadLibraryInterface::CaptureAIRevision(
        const AI::FScadAIEditTarget& target) const
    {
        const nlohmann::json snapshot = CaptureAISnapshot(target);
        return {aiDocumentGeneration_, AI::HashCanonicalSnapshot(snapshot)};
    }

    void ScadLibraryInterface::DrawAIContent()
    {
        if (workspaceMode_ == EWorkspaceMode::CharacterDesigner)
        {
            ImGui::TextWrapped("本批融合覆盖 Kit、场景、过程场景和角色动作；角色部件组装继续使用属性编辑器。");
            return;
        }
        const AI::FScadAIEditTarget target = ResolveAITarget();
        const AI::FScadDocumentRevision revision = CaptureAIRevision(target);
        const AI::FScadAIControllerSnapshot beforeRefresh = aiController_->Snapshot();
        if (aiCandidatePreviewActive_ &&
            (!(target == aiPreviewTarget_) || !beforeRefresh.proposal ||
             !AI::IsProposalCurrent(*beforeRefresh.proposal, target, revision)))
        {
            EndAIProposalPreview();
        }
        aiController_->RefreshIdentity(target, revision);
        std::string label = fmt::format("{} · {}{}{}", AI::EditKindName(target.kind), target.displayName,
                                        target.primaryId.empty() ? "" : " · ", target.primaryId);
        if (target.kind == AI::EScadAIEditKind::SceneObjects)
        {
            if (selectedBenchItem_ >= 0 && selectedBenchItem_ < static_cast<int>(Bench().size()))
            {
                const FBenchItem& selected = Bench()[selectedBenchItem_];
                const std::string kitName = selected.kitIndex >= 0 &&
                        selected.kitIndex < static_cast<int>(kits_.size())
                    ? kits_[selected.kitIndex].name
                    : fmt::format("Kit {}", selected.kitIndex);
                label += fmt::format(" · 已选实例 {}/{}", kitName, selected.moduleName);
            }
            else
            {
                label += " · 未选择实例（AI 将编辑整体场景）";
            }
        }
        const AI::FScadAIControllerSnapshot controllerSnapshot = aiController_->Snapshot();
        const bool canApply = controllerSnapshot.proposal &&
            AI::IsProposalCurrent(*controllerSnapshot.proposal, target, revision) &&
            !(target.kind == AI::EScadAIEditKind::KitModule &&
              (selectedKit_ < 0 || selectedModule_.empty()));
        AI::FScadAIPanelActions actions;
        actions.submit = [this](const std::string& instruction) { SubmitAIRequest(instruction); };
        actions.preview = [this]() { PreviewAIProposal(); };
        actions.compareOriginal = [this]() { PreviewAIOriginal(); };
        actions.apply = [this]() { ApplyAIProposal(); };
        actions.reject = [this]() { RejectAIProposal(); };
        actions.undo = [this]() { UndoLastAIEdit(); };
        actions.regenerate = [this]() { SubmitAIRequest(aiLastInstruction_); };
        aiPanel_->Draw(label, *aiController_, canApply, aiHasUndo_, aiCandidatePreviewActive_, actions);
        if (aiKitDraftDirty_)
        {
            ImGui::Separator();
            ImGui::TextWrapped("Kit 草稿尚未保存：%s", aiKitDraftPath_.c_str());
            if (ImGui::Button("保存 Kit 草稿"))
            {
                SaveAIKitDraft();
            }
        }
        if (ImGui::CollapsingHeader("ScadStudio 旧会话迁移"))
        {
            if (ImGui::Button("扫描旧会话"))
            {
                aiLegacySessions_ = AI::FScadStudioSessionImporter::Scan(
                    std::filesystem::current_path() / "scad_studio", aiLegacyImportWarnings_);
            }
            for (const std::string& warning : aiLegacyImportWarnings_)
            {
                ImGui::TextWrapped("%s", warning.c_str());
            }
            for (size_t index = 0; index < aiLegacySessions_.size(); ++index)
            {
                const AI::FScadStudioImportCandidate& candidate = aiLegacySessions_[index];
                ImGui::PushID(static_cast<int>(index));
                ImGui::TextWrapped("%s · %zu file(s)", candidate.title.c_str(), candidate.fileCount);
                ImGui::SameLine();
                if (ImGui::SmallButton("导入为草稿"))
                {
                    openedAssemblyPath_.clear();
                    RefreshAssemblyWatchBaseline();
                    ReparseDocument(candidate.source, {});
                    assemblySourceDirty_ = true;
                    aiKitContextActive_ = false;
                    assemblyEditorTab_ = 1;
                    const std::string safeId = candidate.id.empty() ? "legacy" : candidate.id;
                    const std::string suggested = fmt::format("assets/scad/source/imported_{}.scad", safeId);
                    std::snprintf(assemblyPathBuf_, sizeof(assemblyPathBuf_), "%s", suggested.c_str());
                    ++aiDocumentGeneration_;
                    aiController_->Reset();
                    statusLine_ = fmt::format("已将旧会话“{}”导入为未保存草稿", candidate.title);
                    statusError_ = false;
                }
                for (const std::string& warning : candidate.warnings)
                {
                    ImGui::TextDisabled("%s", warning.c_str());
                }
                ImGui::PopID();
            }
            if (aiLegacySessions_.empty())
            {
                ImGui::TextDisabled("未扫描到可导入会话；旧数据保持不变。");
            }
        }
    }

    void ScadLibraryInterface::SubmitAIRequest(const std::string& instruction)
    {
        EndAIProposalPreview();
        const AI::FScadAIEditTarget target = ResolveAITarget();
        if (workspaceMode_ == EWorkspaceMode::CharacterDesigner ||
            (target.kind == AI::EScadAIEditKind::KitModule &&
             (selectedKit_ < 0 || selectedKit_ >= static_cast<int>(kits_.size()) || selectedModule_.empty())))
        {
            return;
        }
        const nlohmann::json snapshot = CaptureAISnapshot(target);
        const AI::FScadDocumentRevision revision{aiDocumentGeneration_, AI::HashCanonicalSnapshot(snapshot)};
        AI::FScadAIRequestEnvelope request;
        AI::FScadAIArtifactValidator validator;
        switch (target.kind)
        {
        case AI::EScadAIEditKind::KitModule:
        {
            const std::string source = snapshot.value("source", "");
            request = AI::FKitModuleAIAdapter::BuildRequest(target, revision, source, instruction);
            const std::string module = target.primaryId;
            validator = [source, module](std::string_view response)
            { return AI::FKitModuleAIAdapter::Validate(source, module, response); };
            break;
        }
        case AI::EScadAIEditKind::SceneSource:
        {
            const std::string source = snapshot.value("source", "");
            request = AI::FSceneSourceAIAdapter::BuildRequest(target, revision, source, instruction);
            validator = [source](std::string_view response)
            { return AI::FSceneSourceAIAdapter::Validate(source, response); };
            break;
        }
        case AI::EScadAIEditKind::SceneObjects:
            request = AI::FSceneObjectsAIAdapter::BuildRequest(target, revision, snapshot, instruction);
            validator = [snapshot](std::string_view response)
            { return AI::FSceneObjectsAIAdapter::Validate(snapshot, response); };
            break;
        case AI::EScadAIEditKind::TerrainProcess:
            request = AI::FTerrainProcessAIAdapter::BuildRequest(target, revision, snapshot, instruction);
            validator = [snapshot](std::string_view response)
            { return AI::FTerrainProcessAIAdapter::Validate(snapshot, response); };
            break;
        case AI::EScadAIEditKind::RigClip:
            request = AI::FRigClipAIAdapter::BuildRequest(target, revision, snapshot, instruction);
            validator = [snapshot](std::string_view response)
            { return AI::FRigClipAIAdapter::Validate(snapshot, response); };
            break;
        }
        if (aiController_->Submit(std::move(request), std::move(validator)))
        {
            aiLastInstruction_ = instruction;
        }
    }

    bool ScadLibraryInterface::ApplyAISnapshot(const AI::FScadAIEditTarget& target,
                                               const nlohmann::json& snapshot, bool markDirty)
    {
        using Kind = AI::EScadAIEditKind;
        try
        {
            if (target.kind == Kind::KitModule)
            {
                aiKitDraftPath_ = target.documentKey;
                aiKitDraftModule_ = target.primaryId;
                aiKitDraftSource_ = snapshot.at("kitSource").get<std::string>();
                aiKitDraftDirty_ = markDirty;
                return true;
            }
            if (target.kind == Kind::SceneSource)
            {
                // Rewriting the whole file re-derives every editor's view of
                // it: instances, terrain and source come back classified.
                ReparseDocument(snapshot.at("source").get<std::string>(), openedAssemblyPath_);
                if (markDirty)
                {
                    assemblySourceDirty_ = true;
                }
                return true;
            }
            if (target.kind == Kind::SceneObjects)
            {
                std::vector<FBenchItem> candidate;
                for (const auto& object : snapshot.at("objects"))
                {
                    FBenchItem item;
                    item.kitIndex = object.at("kitIndex").get<int>();
                    item.moduleName = object.at("module").get<std::string>();
                    item.x = object.at("position").at(0).get<float>();
                    item.y = object.at("position").at(1).get<float>();
                    item.z = object.at("position").at(2).get<float>();
                    item.rotX = object.at("rotation").at(0).get<float>();
                    item.rotY = object.at("rotation").at(1).get<float>();
                    item.rotZ = object.at("rotation").at(2).get<float>();
                    item.scale = object.at("scale").at(0).get<float>();
                    item.scaleY = object.at("scale").at(1).get<float>();
                    item.scaleZ = object.at("scale").at(2).get<float>();
                    const std::string arguments = object.value("arguments", "");
                    std::snprintf(item.args, sizeof(item.args), "%s", arguments.c_str());
                    if (object.contains("color"))
                    {
                        item.hasColor = true;
                        for (size_t component = 0; component < 4; ++component)
                        {
                            item.color[component] = object["color"][component].get<float>();
                        }
                    }
                    candidate.push_back(std::move(item));
                }
                // Objects the model dropped are spliced out of the file; the
                // ones it kept or moved are rewritten in place, and everything
                // else in the scene (terrain, loops, comments) is untouched.
                for (FBenchItem& existing : Bench())
                {
                    existing.removed = true;
                }
                for (size_t index = 0; index < candidate.size(); ++index)
                {
                    if (index < Bench().size())
                    {
                        FBenchItem& target = Bench()[index];
                        const size_t begin = target.sourceBegin;
                        const size_t end = target.sourceEnd;
                        const size_t insertAt = target.insertAt;
                        const int segment = target.segmentIndex;
                        const int origin = target.originSegment;
                        target = candidate[index];
                        target.sourceBegin = begin;
                        target.sourceEnd = end;
                        target.insertAt = insertAt;
                        target.segmentIndex = segment;
                        target.originSegment = origin;
                        target.removed = false;
                    }
                    else
                    {
                        document_.AddInstance(candidate[index]);
                    }
                }
                selectedBenchItem_ = Bench().empty() ? -1 : std::clamp(
                    selectedBenchItem_, 0, static_cast<int>(Bench().size()) - 1);
                if (markDirty)
                {
                    benchDirty_ = true;
                    assemblySourceDirty_ = true;
                }
                return true;
            }
            if (target.kind == Kind::TerrainProcess)
            {
                Assets::Scad::FTerrainSpec& terrain = TerrainProcess().Terrain();
                const auto& terrainJson = snapshot.at("terrain");
                terrain.size = {terrainJson.at("size").at(0).get<double>(),
                                terrainJson.at("size").at(1).get<double>()};
                terrain.cells = {terrainJson.at("cells").at(0).get<int>(),
                                 terrainJson.at("cells").at(1).get<int>()};
                terrain.seed = terrainJson.at("seed").get<uint64_t>();
                terrain.baseHeight = terrainJson.at("baseHeight").get<double>();
                terrain.relief = terrainJson.at("relief").get<double>();
                terrain.roughness = terrainJson.at("roughness").get<double>();
                terrain.hasWaterLevel = terrainJson.at("hasWaterLevel").get<bool>();
                terrain.waterLevel = terrainJson.at("waterLevel").get<double>();
                terrain.palette = terrainJson.at("palette").get<std::string>();

                const auto featureType = [](const std::string& name)
                {
                    using Type = Assets::Scad::FTerrainFeature::EType;
                    if (name == "ridge") return Type::Ridge;
                    if (name == "plateau") return Type::Plateau;
                    if (name == "lake") return Type::Lake;
                    if (name == "river") return Type::River;
                    if (name == "road") return Type::Road;
                    if (name == "pad") return Type::Pad;
                    return Type::Mountain;
                };
                terrain.features.clear();
                for (const auto& featureJson : snapshot.at("features"))
                {
                    Assets::Scad::FTerrainFeature feature;
                    feature.type = featureType(featureJson.at("type").get<std::string>());
                    feature.at = {featureJson.at("at").at(0).get<double>(),
                                  featureJson.at("at").at(1).get<double>()};
                    feature.size = {featureJson.at("size").at(0).get<double>(),
                                    featureJson.at("size").at(1).get<double>()};
                    feature.rot = featureJson.at("rotation").get<double>();
                    feature.radius = featureJson.at("radius").get<double>();
                    feature.height = featureJson.at("height").get<double>();
                    feature.depth = featureJson.at("depth").get<double>();
                    feature.width = featureJson.at("width").get<double>();
                    feature.rugged = featureJson.at("rugged").get<double>();
                    for (const auto& point : featureJson.at("points"))
                    {
                        feature.pts.emplace_back(point.at(0).get<double>(), point.at(1).get<double>());
                    }
                    terrain.features.push_back(std::move(feature));
                }

                const auto ruleType = [](const std::string& name)
                {
                    if (name == "terrain_height_anchor") return ETerrainProcessRuleType::HeightAnchor;
                    if (name == "ter_place_tilt") return ETerrainProcessRuleType::PlaceTilt;
                    if (name == "ter_snap") return ETerrainProcessRuleType::Snap;
                    if (name == "ter_along") return ETerrainProcessRuleType::Along;
                    if (name == "ter_scatter") return ETerrainProcessRuleType::Scatter;
                    return ETerrainProcessRuleType::Place;
                };
                std::vector<FTerrainProcessRule> rules = TerrainProcess().Rules();
                for (FTerrainProcessRule& existing : rules)
                {
                    existing.removed = true;
                }
                for (const auto& ruleJson : snapshot.at("rules"))
                {
                    FTerrainProcessRule rule;
                    rule.type = ruleType(ruleJson.at("type").get<std::string>());
                    rule.x = ruleJson.at("x").get<double>();
                    rule.y = ruleJson.at("y").get<double>();
                    rule.dz = ruleJson.at("dz").get<double>();
                    rule.sampleX = ruleJson.at("sampleX").get<double>();
                    rule.sampleY = ruleJson.at("sampleY").get<double>();
                    rule.maxTilt = ruleJson.at("maxTilt").get<double>();
                    rule.probe = ruleJson.at("probe").get<double>();
                    for (const auto& point : ruleJson.at("points"))
                    {
                        rule.points.emplace_back(point.at(0).get<double>(), point.at(1).get<double>());
                    }
                    rule.step = ruleJson.at("step").get<double>();
                    rule.seed = ruleJson.at("seed").get<int>();
                    rule.offset = ruleJson.at("offset").get<double>();
                    rule.count = ruleJson.at("count").get<int>();
                    rule.region = {ruleJson.at("region").at(0).get<double>(),
                                   ruleJson.at("region").at(1).get<double>(),
                                   ruleJson.at("region").at(2).get<double>(),
                                   ruleJson.at("region").at(3).get<double>()};
                    rule.circularRegion = ruleJson.at("circularRegion").get<bool>();
                    rule.regionCenter = {ruleJson.at("regionCenter").at(0).get<double>(),
                                         ruleJson.at("regionCenter").at(1).get<double>()};
                    rule.regionRadius = ruleJson.at("regionRadius").get<double>();
                    rule.minHeight = ruleJson.at("minHeight").get<double>();
                    rule.maxHeight = ruleJson.at("maxHeight").get<double>();
                    rule.maxSlope = ruleJson.at("maxSlope").get<double>();
                    rule.avoidWater = ruleJson.at("avoidWater").get<double>();
                    rule.biomes = ruleJson.at("biomes").get<std::vector<std::string>>();
                    rule.randomRotation = ruleJson.at("randomRotation").get<bool>();
                    rule.variants = ruleJson.value("variants", 0);
                    if (ruleJson.contains("scaleRange") && ruleJson.at("scaleRange").is_array() &&
                        ruleJson.at("scaleRange").size() >= 2)
                    {
                        rule.scaleRange = {ruleJson.at("scaleRange").at(0).get<double>(),
                                           ruleJson.at("scaleRange").at(1).get<double>()};
                    }
                    rule.childSource = ruleJson.at("childSource").get<std::string>();
                    const std::string id = ruleJson.value("id", "");
                    size_t existingIndex = std::string::npos;
                    if (id.size() > 1 && id.front() == 'r' &&
                        std::all_of(id.begin() + 1, id.end(), [](const char character)
                                    { return std::isdigit(static_cast<unsigned char>(character)); }))
                    {
                        existingIndex = static_cast<size_t>(std::stoull(id.substr(1)));
                    }
                    if (existingIndex < TerrainProcess().Rules().size())
                    {
                        rule.sourceBegin = TerrainProcess().Rules()[existingIndex].sourceBegin;
                        rule.sourceEnd = TerrainProcess().Rules()[existingIndex].sourceEnd;
                        rule.removed = false;
                        rules[existingIndex] = std::move(rule);
                    }
                    else
                    {
                        rule.removed = false;
                        rules.push_back(std::move(rule));
                    }
                }
                TerrainProcess().Rules() = std::move(rules);
                selectedTerrainFeature_ = terrain.features.empty() ? 0 : std::clamp(
                    selectedTerrainFeature_, 0, static_cast<int>(terrain.features.size()) - 1);
                selectedTerrainRule_ = TerrainProcess().Rules().empty() ? 0 : std::clamp(
                    selectedTerrainRule_, 0, static_cast<int>(TerrainProcess().Rules().size()) - 1);
                if (markDirty)
                {
                    MarkTerrainProcessDirty();
                    assemblySourceDirty_ = true;
                }
                return true;
            }

            if (!rigPreview_.HasRig())
            {
                return false;
            }
            std::vector<FEditableRigClip> clips;
            for (const auto& clipJson : snapshot.at("clips"))
            {
                FEditableRigClip clip;
                clip.name = clipJson.at("name").get<std::string>();
                clip.loop = clipJson.at("loop").get<bool>();
                clip.duration = clipJson.value("duration", 0.0f);
                for (const auto& channelJson : clipJson.at("channels"))
                {
                    const std::string boneName = channelJson.at("bone").get<std::string>();
                    const int bone = rigPreview_.Asset().FindBone(boneName);
                    if (bone < 0)
                    {
                        return false;
                    }
                    FEditableRigChannel channel;
                    channel.bone = bone;
                    const std::string type = channelJson.at("channel").get<std::string>();
                    channel.type = type == "pos" ? EEditableRigChannel::Position
                        : (type == "rot" ? EEditableRigChannel::Rotation : EEditableRigChannel::Scale);
                    for (const auto& keyJson : channelJson.at("keys"))
                    {
                        const auto& value = keyJson.at("value");
                        channel.keys.push_back(
                            {keyJson.at("time").get<float>(),
                             glm::vec3(value.at(0).get<float>(), value.at(1).get<float>(), value.at(2).get<float>())});
                    }
                    clip.channels.push_back(std::move(channel));
                }
                clips.push_back(std::move(clip));
            }
            workbench_.Clips() = std::move(clips);
            workbenchClip_ = workbench_.Clips().empty() ? 0 : std::clamp(
                workbenchClip_, 0, static_cast<int>(workbench_.Clips().size()) - 1);
            if (markDirty)
            {
                workbench_.CommitRigEdit();
                std::string error;
                if (!workbench_.ApplyToAsset(rigPreview_.MutableAsset(), error))
                {
                    statusLine_ = error;
                    statusError_ = true;
                    return false;
                }
                if (!workbench_.Clips().empty())
                {
                    rigPreview_.PlayClip(workbench_.Clips()[workbenchClip_].name);
                }
            }
            return true;
        }
        catch (const std::exception& exception)
        {
            statusLine_ = fmt::format("AI candidate 应用失败: {}", exception.what());
            statusError_ = true;
            return false;
        }
    }

    void ScadLibraryInterface::ApplyAIProposal()
    {
        const AI::FScadAIControllerSnapshot snapshot = aiController_->Snapshot();
        if (!snapshot.proposal || snapshot.proposal->state != AI::EScadAIProposalState::Ready)
        {
            return;
        }
        const AI::FScadAIEditTarget currentTarget = ResolveAITarget();
        const AI::FScadDocumentRevision currentRevision = CaptureAIRevision(currentTarget);
        if (!AI::IsProposalCurrent(*snapshot.proposal, currentTarget, currentRevision))
        {
            aiController_->RefreshIdentity(currentTarget, currentRevision);
            return;
        }
        aiCandidatePreviewActive_ = false;
        aiPreviewTarget_ = {};
        aiUndoTarget_ = currentTarget;
        aiUndoSnapshot_ = CaptureAISnapshot(currentTarget);
        if (!ApplyAISnapshot(currentTarget, snapshot.proposal->candidate, true))
        {
            aiUndoSnapshot_.clear();
            return;
        }
        aiHasUndo_ = true;
        ++aiDocumentGeneration_;
        aiController_->MarkApplied();
        statusLine_ = fmt::format("AI 提案已应用到 {} 的未保存草稿", currentTarget.displayName);
        statusError_ = false;
    }

    void ScadLibraryInterface::UndoLastAIEdit()
    {
        EndAIProposalPreview();
        if (!aiHasUndo_)
        {
            return;
        }
        if (ApplyAISnapshot(aiUndoTarget_, aiUndoSnapshot_, true))
        {
            ++aiDocumentGeneration_;
            aiHasUndo_ = false;
            aiUndoSnapshot_.clear();
            aiController_->Reset();
            statusLine_ = "已撤销上次 AI 修改";
            statusError_ = false;
        }
    }

    void ScadLibraryInterface::PreviewAIProposal()
    {
        const AI::FScadAIControllerSnapshot controllerSnapshot = aiController_->Snapshot();
        if (!controllerSnapshot.proposal ||
            controllerSnapshot.proposal->state != AI::EScadAIProposalState::Ready)
        {
            return;
        }
        const AI::FScadAIProposal& proposal = *controllerSnapshot.proposal;
        const AI::FScadAIEditTarget currentTarget = ResolveAITarget();
        if (!AI::IsProposalCurrent(proposal, currentTarget, CaptureAIRevision(currentTarget)))
        {
            return;
        }
        if (RenderAISnapshotPreview(proposal.target, proposal.candidate))
        {
            aiCandidatePreviewActive_ = true;
            aiPreviewTarget_ = proposal.target;
            statusLine_ = "Viewport 正在显示 AI 提案；可点击“对比原案”切回";
            statusError_ = false;
        }
    }

    void ScadLibraryInterface::PreviewAIOriginal()
    {
        const AI::FScadAIControllerSnapshot controllerSnapshot = aiController_->Snapshot();
        if (!controllerSnapshot.proposal ||
            controllerSnapshot.proposal->state != AI::EScadAIProposalState::Ready)
        {
            return;
        }
        const AI::FScadAIProposal& proposal = *controllerSnapshot.proposal;
        const AI::FScadAIEditTarget currentTarget = ResolveAITarget();
        if (!AI::IsProposalCurrent(proposal, currentTarget, CaptureAIRevision(currentTarget)))
        {
            return;
        }
        if (RenderAISnapshotPreview(proposal.target, CaptureAISnapshot(proposal.target)))
        {
            aiCandidatePreviewActive_ = false;
            aiPreviewTarget_ = {};
            statusLine_ = "Viewport 已切回原案；可再次点击“预览候选”进行 A/B 对比";
            statusError_ = false;
        }
    }

    void ScadLibraryInterface::RejectAIProposal()
    {
        EndAIProposalPreview();
        aiController_->Reject();
    }

    void ScadLibraryInterface::EndAIProposalPreview()
    {
        if (!aiCandidatePreviewActive_)
        {
            return;
        }
        RenderAISnapshotPreview(aiPreviewTarget_, CaptureAISnapshot(aiPreviewTarget_));
        aiCandidatePreviewActive_ = false;
        aiPreviewTarget_ = {};
    }

    bool ScadLibraryInterface::RenderAISnapshotPreview(const AI::FScadAIEditTarget& target,
                                                        const nlohmann::json& previewSnapshot)
    {
        using Kind = AI::EScadAIEditKind;
        if (target.kind == Kind::KitModule)
        {
            const std::string source = previewSnapshot.value(
                "kitSource", previewSnapshot.value("source", ""));
            std::string kitPath;
            const std::string rewrittenSource = RewriteScadDependencyPaths(
                source, std::filesystem::path(target.documentKey).parent_path(), WorkspaceDir(), true);
            if (source.empty() || !WriteWorkspaceFile("ai_compare_kit.scad", rewrittenSource, kitPath))
            {
                return false;
            }
            std::replace(kitPath.begin(), kitPath.end(), '\\', '/');
            const std::string wrapper = fmt::format(
                "// ScadLibrary AI Kit A/B preview\n$fn = {};\nuse <{}>\n{}();\n",
                fnSegments_, kitPath, target.primaryId);
            preserveCameraOnNextSceneLoad_ = true;
            if (WriteAndLoad("ai_kit_preview.scad", wrapper))
            {
                return true;
            }
            preserveCameraOnNextSceneLoad_ = false;
            return false;
        }
        if (target.kind == Kind::SceneSource)
        {
            std::string source = previewSnapshot.value("source", "");
            if (!openedAssemblyPath_.empty())
            {
                source = RewriteScadDependencyPaths(
                    source, std::filesystem::path(openedAssemblyPath_).parent_path(), WorkspaceDir(), true);
            }
            return !source.empty() && WriteAndLoad("ai_scene_source_preview.scad", source);
        }
        if (target.kind == Kind::SceneObjects)
        {
            const nlohmann::json liveSnapshot = CaptureAISnapshot(target);
            if (!ApplyAISnapshot(target, previewSnapshot, false))
            {
                return false;
            }
            const std::string source = BuildAssemblyPreviewSource();
            ApplyAISnapshot(target, liveSnapshot, false);
            return WriteAndLoad("ai_scene_objects_preview.scad", source);
        }
        if (target.kind == Kind::TerrainProcess)
        {
            const nlohmann::json liveSnapshot = CaptureAISnapshot(target);
            if (!ApplyAISnapshot(target, previewSnapshot, false))
            {
                return false;
            }
            const std::string source = BuildAssemblyPreviewSource();
            ApplyAISnapshot(target, liveSnapshot, false);
            preserveCameraOnNextSceneLoad_ = true;
            return WriteAndLoad("ai_terrain_preview.scad", RewriteScadDependencyPaths(
                source, std::filesystem::path(openedAssemblyPath_).parent_path(), WorkspaceDir(), true));
        }

        const nlohmann::json liveSnapshot = CaptureAISnapshot(target);
        if (!ApplyAISnapshot(target, previewSnapshot, false))
        {
            return false;
        }
        std::string error;
        const bool applied = workbench_.ApplyToAsset(rigPreview_.MutableAsset(), error);
        if (applied)
        {
            const auto& clips = previewSnapshot.at("clips");
            if (!clips.empty())
            {
                const auto selected = std::find_if(clips.begin(), clips.end(), [&target](const auto& clip)
                { return clip.value("id", clip.value("name", "")) == target.primaryId; });
                const auto& clip = selected == clips.end() ? clips.back() : *selected;
                rigPreview_.PlayClip(clip.at("name").get<std::string>());
            }
        }
        ApplyAISnapshot(target, liveSnapshot, false);
        if (!applied)
        {
            statusLine_ = error;
            statusError_ = true;
        }
        return applied;
    }

    void ScadLibraryInterface::SaveAIKitDraft()
    {
        if (!aiKitDraftDirty_ || aiKitDraftPath_.empty() || aiKitDraftSource_.empty())
        {
            return;
        }
        const std::filesystem::path libRoot = AuthoringPath("assets/scad/lib");
        const std::filesystem::path kitPath = std::filesystem::path(aiKitDraftPath_).lexically_normal();
        if (!IsPathWithin(kitPath, libRoot) || kitPath.extension() != ".scad")
        {
            statusLine_ = "Kit 草稿路径不在 assets/scad/lib";
            statusError_ = true;
            return;
        }
        Assets::Scad::FScadSourceIndex sourceIndex;
        std::string error;
        if (!Assets::Scad::BuildScadSourceIndex(aiKitDraftSource_, sourceIndex, error))
        {
            statusLine_ = fmt::format("Kit 保存前解析失败: {}", error);
            statusError_ = true;
            return;
        }

        const std::filesystem::path catalogPath = libRoot / "catalog.json";
        const std::filesystem::path validationPath =
            kitPath.parent_path() / (".ai_validate_" + kitPath.filename().string());
        {
            std::ofstream output(validationPath, std::ios::binary | std::ios::trunc);
            if (!output)
            {
                statusLine_ = "无法写入 Kit 校验文件";
                statusError_ = true;
                return;
            }
            output << aiKitDraftSource_;
        }
        Assets::Scad::ScadProgram candidateProgram;
        const bool programLoaded = Assets::Scad::LoadScadProgram(validationPath.string(), candidateProgram, error);
        Assets::Scad::EvalResult evaluation;
        bool evaluated = false;
        if (programLoaded)
        {
            const std::string callSource = fmt::format("$fn = {};\n{}();\n", fnSegments_, aiKitDraftModule_);
            std::vector<Assets::Scad::Token> tokens;
            Assets::Scad::Scope scope;
            Assets::ScadLoadOptions options;
            evaluated = Assets::Scad::ScadLexer::Tokenize(callSource, tokens, error) &&
                Assets::Scad::ScadParser::Parse(tokens, scope, error) &&
                Assets::Scad::ScadEvaluator::Evaluate(scope, candidateProgram.modules,
                                                       candidateProgram.functions, options, evaluation, error) &&
                evaluation.triangleCount > 0;
        }
        std::error_code validationCleanupError;
        std::filesystem::remove(validationPath, validationCleanupError);
        if (!programLoaded)
        {
            statusLine_ = fmt::format("Kit 依赖闭包校验失败: {}", error);
            statusError_ = true;
            return;
        }
        nlohmann::json catalog;
        try
        {
            std::ifstream input(catalogPath, std::ios::binary);
            catalog = nlohmann::json::parse(input);
            auto kit = std::find_if(catalog.at("kits").begin(), catalog.at("kits").end(), [&](const auto& item)
            { return item.value("file", "") == kitPath.filename().string(); });
            if (kit == catalog.at("kits").end())
            {
                throw std::runtime_error("catalog 中找不到目标 Kit");
            }
            for (auto& module : (*kit).at("modules"))
            {
                const std::string name = module.value("name", "");
                const auto* span = sourceIndex.Find(Assets::Scad::EScadDefinitionKind::Module, name);
                if (!span)
                {
                    throw std::runtime_error("候选 Kit 的公开 module 集合与 catalog 不一致");
                }
                module["line"] = span->line;
                const std::string signature = aiKitDraftSource_.substr(
                    span->signatureBegin, span->signatureEnd - span->signatureBegin);
                const size_t open = signature.find('(');
                const size_t close = signature.rfind(')');
                module["params"] = open != std::string::npos && close > open
                    ? signature.substr(open + 1, close - open - 1)
                    : "";
                if (name == aiKitDraftModule_)
                {
                    module.erase("error");
                    module.erase("metricsStatus");
                    if (evaluated)
                    {
                        glm::dvec3 minBounds(1e30);
                        glm::dvec3 maxBounds(-1e30);
                        for (const auto& [color, bucket] : evaluation.buckets)
                        {
                            (void)color;
                            for (const glm::dvec3& point : bucket.tris)
                            {
                                minBounds = glm::min(minBounds, point);
                                maxBounds = glm::max(maxBounds, point);
                            }
                        }
                        const auto round2 = [](double value)
                        { return std::round(value * 100.0) / 100.0; };
                        module["footprint"] = {round2(maxBounds.x - minBounds.x),
                                               round2(maxBounds.y - minBounds.y)};
                        module["height"] = round2(maxBounds.z);
                        module["zMin"] = round2(minBounds.z);
                        module["center"] = {round2((minBounds.x + maxBounds.x) * 0.5),
                                            round2((minBounds.y + maxBounds.y) * 0.5)};
                        module["triangles"] = evaluation.triangleCount;
                        module["colors"] = evaluation.buckets.size();
                        module["warnings"] = evaluation.warningCount;
                        module["ok"] = true;
                    }
                    else
                    {
                        module["ok"] = false;
                        if (!error.empty()) module["error"] = error;
                    }
                }
            }
        }
        catch (const std::exception& exception)
        {
            statusLine_ = fmt::format("Kit catalog 更新准备失败: {}", exception.what());
            statusError_ = true;
            return;
        }

        const std::filesystem::path kitTemporary = kitPath.string() + ".ai.tmp";
        const std::filesystem::path catalogTemporary = catalogPath.string() + ".ai.tmp";
        const std::filesystem::path kitBackup = kitPath.string() + ".ai.bak";
        const std::filesystem::path catalogBackup = catalogPath.string() + ".ai.bak";
        {
            std::ofstream output(kitTemporary, std::ios::binary | std::ios::trunc);
            if (!output)
            {
                statusLine_ = "无法写入 Kit 临时文件";
                statusError_ = true;
                return;
            }
            output << aiKitDraftSource_;
        }
        {
            std::ofstream output(catalogTemporary, std::ios::binary | std::ios::trunc);
            if (!output)
            {
                std::error_code cleanupError;
                std::filesystem::remove(kitTemporary, cleanupError);
                statusLine_ = "无法写入 catalog 临时文件";
                statusError_ = true;
                return;
            }
            output << catalog.dump(2) << '\n';
        }
        std::error_code ec;
        std::filesystem::remove(kitBackup, ec);
        ec.clear();
        std::filesystem::remove(catalogBackup, ec);
        ec.clear();
        bool kitBackedUp = false;
        bool catalogBackedUp = false;
        bool kitInstalled = false;
        bool catalogInstalled = false;
        std::filesystem::rename(kitPath, kitBackup, ec);
        kitBackedUp = !ec;
        if (!ec)
        {
            std::filesystem::rename(catalogPath, catalogBackup, ec);
            catalogBackedUp = !ec;
        }
        if (!ec)
        {
            std::filesystem::rename(kitTemporary, kitPath, ec);
            kitInstalled = !ec;
        }
        if (!ec)
        {
            std::filesystem::rename(catalogTemporary, catalogPath, ec);
            catalogInstalled = !ec;
        }
        if (ec)
        {
            std::error_code rollbackError;
            if (kitInstalled)
                std::filesystem::remove(kitPath, rollbackError);
            if (catalogInstalled)
                std::filesystem::remove(catalogPath, rollbackError);
            if (kitBackedUp && std::filesystem::exists(kitBackup, rollbackError))
                std::filesystem::rename(kitBackup, kitPath, rollbackError);
            if (catalogBackedUp && std::filesystem::exists(catalogBackup, rollbackError))
                std::filesystem::rename(catalogBackup, catalogPath, rollbackError);
            std::filesystem::remove(kitTemporary, rollbackError);
            std::filesystem::remove(catalogTemporary, rollbackError);
            statusLine_ = fmt::format("Kit/catalog 原子保存失败，已尝试恢复: {}", ec.message());
            statusError_ = true;
            return;
        }
        std::filesystem::remove(kitBackup, ec);
        std::filesystem::remove(catalogBackup, ec);
        aiKitDraftDirty_ = false;
        ++aiDocumentGeneration_;
        RescanKits();
        statusLine_ = "Kit 与重建后的 catalog entry 已原子保存";
        statusError_ = false;
    }

    void ScadLibraryInterface::DrawTerrainProcessContent()
    {
        bool changed = false;
        Assets::Scad::FTerrainSpec& terrain = TerrainProcess().Terrain();

        ImGui::Checkbox("自动刷新", &autoReload_);
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_ROTATE_RIGHT " 刷新预览"))
        {
            ReloadTerrainProcess();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("这里只编辑 TERR 基础参数；Features 与 ter_* 算子在“结构”Outliner 中编辑");

        for (const std::string& warning : terrainProcessWarnings_)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, NextUI::Theme::Color(NextUI::Theme::EColor::Warning));
            ImGui::TextWrapped("%s", warning.c_str());
            ImGui::PopStyleColor();
        }

        const auto editPoint = [&](const char* label, glm::dvec2& point, float speed = 0.5f)
        {
            double value[2] = {point.x, point.y};
            if (ImGui::DragScalarN(label, ImGuiDataType_Double, value, 2, speed))
            {
                point = glm::dvec2(value[0], value[1]);
                return true;
            }
            return false;
        };
        const auto editNumber = [](const char* label, double& value, float speed = 0.1f, const char* format = "%.3f")
        { return ImGui::DragScalar(label, ImGuiDataType_Double, &value, speed, nullptr, nullptr, format); };
        const auto editPoints = [&](std::vector<glm::dvec2>& points)
        {
            bool pointsChanged = false;
            int removePoint = -1;
            for (size_t pointIndex = 0; pointIndex < points.size(); ++pointIndex)
            {
                ImGui::PushID(static_cast<int>(pointIndex));
                ImGui::SetNextItemWidth(-34.0f);
                pointsChanged |= editPoint("##point", points[pointIndex]);
                ImGui::SameLine();
                if (ImGui::SmallButton(ICON_FA_XMARK))
                {
                    removePoint = static_cast<int>(pointIndex);
                }
                ImGui::PopID();
            }
            if (removePoint >= 0 && points.size() > 2)
            {
                points.erase(points.begin() + removePoint);
                pointsChanged = true;
            }
            if (ImGui::SmallButton(ICON_FA_PLUS " 添加折点"))
            {
                points.push_back(points.empty() ? glm::dvec2(0.0) : points.back() + glm::dvec2(5.0, 0.0));
                pointsChanged = true;
            }
            return pointsChanged;
        };

        if (ImGui::CollapsingHeader("地形画布", ImGuiTreeNodeFlags_DefaultOpen))
        {
            changed |= editPoint("尺寸 XY", terrain.size, 1.0f);
            changed |= ImGui::DragInt2("网格 cells", &terrain.cells.x, 1.0f, 4, 256);
            changed |= ImGui::InputScalar("Seed", ImGuiDataType_U64, &terrain.seed);
            changed |= editNumber("基准高度", terrain.baseHeight);
            changed |= editNumber("基础起伏", terrain.relief);
            changed |= editNumber("噪声细节", terrain.roughness, 0.01f);
            changed |= ImGui::InputText("调色板", &terrain.palette);
            if (ImGui::Checkbox("全局水位", &terrain.hasWaterLevel))
            {
                changed = true;
            }
            if (terrain.hasWaterLevel)
            {
                changed |= editNumber("水位高度", terrain.waterLevel);
            }
        }

        // Operators now live in the Structure outliner, alongside the rest of
        // the scene graph. This tab deliberately keeps only TERR base data.
        if (false && ImGui::CollapsingHeader("Terrain Features（按顺序作用）", ImGuiTreeNodeFlags_DefaultOpen))
        {
            int removeFeature = -1;
            int moveFeature = -1;
            int moveDirection = 0;
            for (size_t featureIndex = 0; featureIndex < terrain.features.size(); ++featureIndex)
            {
                Assets::Scad::FTerrainFeature& feature = terrain.features[featureIndex];
                ImGui::PushID(static_cast<int>(featureIndex));
                const std::string label =
                    fmt::format("{:02}  {}", featureIndex + 1, FTerrainProcessDocument::FeatureTypeName(feature.type));
                const bool selected =
                    !terrainSelectionIsRule_ && selectedTerrainFeature_ == static_cast<int>(featureIndex);
                ImGuiTreeNodeFlags featureFlags = ImGuiTreeNodeFlags_AllowOverlap;
                if (selected)
                {
                    featureFlags |= ImGuiTreeNodeFlags_Selected;
                }
                ImGui::SetNextItemOpen(selected, ImGuiCond_Always);
                const bool open = ImGui::TreeNodeEx(label.c_str(), featureFlags);
                if (ImGui::IsItemClicked())
                {
                    selectedTerrainFeature_ = static_cast<int>(featureIndex);
                    terrainSelectionIsRule_ = false;
                }
                if (selected && scrollToSelectedTerrainItem_)
                {
                    ImGui::SetScrollHereY(0.35f);
                    scrollToSelectedTerrainItem_ = false;
                }
                ImGui::SameLine(ImGui::GetContentRegionAvail().x - 76.0f);
                if (ImGui::SmallButton(ICON_FA_ANGLE_UP) && featureIndex > 0)
                {
                    moveFeature = static_cast<int>(featureIndex);
                    moveDirection = -1;
                }
                ImGui::SameLine();
                if (ImGui::SmallButton(ICON_FA_ANGLE_DOWN) && featureIndex + 1 < terrain.features.size())
                {
                    moveFeature = static_cast<int>(featureIndex);
                    moveDirection = 1;
                }
                ImGui::SameLine();
                if (ImGui::SmallButton(ICON_FA_XMARK))
                {
                    removeFeature = static_cast<int>(featureIndex);
                }
                if (open)
                {
                    using EType = Assets::Scad::FTerrainFeature::EType;
                    switch (feature.type)
                    {
                    case EType::Mountain:
                        changed |= editPoint("中心", feature.at);
                        changed |= editNumber("半径", feature.radius);
                        changed |= editNumber("高度", feature.height);
                        changed |= editNumber("扰动强度", feature.rugged, 0.01f);
                        break;
                    case EType::Ridge:
                        ImGui::TextDisabled("山脊折线");
                        changed |= editPoints(feature.pts);
                        changed |= editNumber("宽度", feature.width);
                        changed |= editNumber("高度", feature.height);
                        break;
                    case EType::Plateau:
                        changed |= editPoint("中心", feature.at);
                        changed |= editNumber("半径", feature.radius);
                        changed |= editNumber("高度", feature.height);
                        break;
                    case EType::Lake:
                        changed |= editPoint("中心", feature.at);
                        changed |= editNumber("半径", feature.radius);
                        changed |= editNumber("深度", feature.depth);
                        break;
                    case EType::River:
                        ImGui::TextDisabled("河流折线（上游 → 下游）");
                        changed |= editPoints(feature.pts);
                        changed |= editNumber("宽度", feature.width);
                        changed |= editNumber("深度", feature.depth);
                        break;
                    case EType::Road:
                        ImGui::TextDisabled("道路折线");
                        changed |= editPoints(feature.pts);
                        changed |= editNumber("宽度", feature.width);
                        break;
                    case EType::Pad:
                        changed |= editPoint("中心", feature.at);
                        changed |= editPoint("尺寸", feature.size);
                        changed |= editNumber("旋转", feature.rot, 1.0f, "%.1f°");
                        break;
                    case EType::Hmap:
                        // Sampled heightfield (generated from real elevation
                        // data). Not structurally editable here; the source page
                        // owns it. Only the cheap scalars are exposed.
                        ImGui::TextDisabled("采样高度场%s",
                                            feature.path.empty() ? "（内联）" : "");
                        if (!feature.path.empty())
                        {
                            ImGui::TextWrapped("%s", feature.path.c_str());
                        }
                        if (feature.grid)
                        {
                            ImGui::TextDisabled("%d x %d 采样", feature.grid->cols, feature.grid->rows);
                        }
                        changed |= editNumber("高度缩放", feature.zScale);
                        changed |= editNumber("高度偏移", feature.zBias);
                        break;
                    }
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }
            if (moveFeature >= 0)
            {
                std::swap(terrain.features[moveFeature], terrain.features[moveFeature + moveDirection]);
                if (selectedTerrainFeature_ == moveFeature)
                {
                    selectedTerrainFeature_ += moveDirection;
                }
                else if (selectedTerrainFeature_ == moveFeature + moveDirection)
                {
                    selectedTerrainFeature_ = moveFeature;
                }
                changed = true;
            }
            if (removeFeature >= 0)
            {
                terrain.features.erase(terrain.features.begin() + removeFeature);
                if (selectedTerrainFeature_ > removeFeature)
                {
                    --selectedTerrainFeature_;
                }
                else if (selectedTerrainFeature_ == removeFeature)
                {
                    selectedTerrainFeature_ = std::min(removeFeature, static_cast<int>(terrain.features.size()) - 1);
                }
                changed = true;
            }

            if (ImGui::Button(ICON_FA_PLUS " 添加 Feature"))
            {
                ImGui::OpenPopup("##add_terrain_feature");
            }
            if (ImGui::BeginPopup("##add_terrain_feature"))
            {
                using EType = Assets::Scad::FTerrainFeature::EType;
                const auto addFeature = [&](const char* label, EType type)
                {
                    if (!ImGui::MenuItem(label))
                    {
                        return;
                    }
                    Assets::Scad::FTerrainFeature feature;
                    feature.type = type;
                    feature.radius = 20.0;
                    feature.width = 6.0;
                    feature.height = 8.0;
                    feature.depth = 2.0;
                    feature.rugged = 0.5;
                    feature.size = glm::dvec2(24.0, 18.0);
                    if (type == EType::Ridge || type == EType::River || type == EType::Road)
                    {
                        feature.pts = {{-10.0, 0.0}, {10.0, 0.0}};
                    }
                    terrain.features.push_back(std::move(feature));
                    selectedTerrainFeature_ = static_cast<int>(terrain.features.size()) - 1;
                    terrainSelectionIsRule_ = false;
                    scrollToSelectedTerrainItem_ = true;
                    changed = true;
                };
                addFeature("山峰 mountain", EType::Mountain);
                addFeature("山脊 ridge", EType::Ridge);
                addFeature("台地 plateau", EType::Plateau);
                addFeature("湖泊 lake", EType::Lake);
                addFeature("河流 river", EType::River);
                addFeature("道路 road", EType::Road);
                addFeature("基座 pad", EType::Pad);
                ImGui::EndPopup();
            }
        }

        if (false && ImGui::CollapsingHeader("贴地过程规则", ImGuiTreeNodeFlags_DefaultOpen))
        {
            int removeRule = -1;
            int duplicateRule = -1;
            std::vector<FTerrainProcessRule>& rules = TerrainProcess().Rules();
            for (size_t ruleIndex = 0; ruleIndex < rules.size(); ++ruleIndex)
            {
                FTerrainProcessRule& rule = rules[ruleIndex];
                if (rule.removed)
                {
                    continue;
                }
                ImGui::PushID(static_cast<int>(ruleIndex));
                const std::string label =
                    fmt::format("{:02}  {}", ruleIndex + 1, FTerrainProcessDocument::RuleTypeName(rule.type));
                const bool selected = terrainSelectionIsRule_ && selectedTerrainRule_ == static_cast<int>(ruleIndex);
                ImGuiTreeNodeFlags ruleFlags = ImGuiTreeNodeFlags_AllowOverlap;
                if (selected)
                {
                    ruleFlags |= ImGuiTreeNodeFlags_Selected;
                }
                ImGui::SetNextItemOpen(selected, ImGuiCond_Always);
                const bool open = ImGui::TreeNodeEx(label.c_str(), ruleFlags);
                if (ImGui::IsItemClicked())
                {
                    selectedTerrainRule_ = static_cast<int>(ruleIndex);
                    terrainSelectionIsRule_ = true;
                }
                if (selected && scrollToSelectedTerrainItem_)
                {
                    ImGui::SetScrollHereY(0.35f);
                    scrollToSelectedTerrainItem_ = false;
                }
                ImGui::SameLine(ImGui::GetContentRegionAvail().x - 52.0f);
                if (ImGui::SmallButton(ICON_FA_COPY))
                {
                    duplicateRule = static_cast<int>(ruleIndex);
                }
                ImGui::SameLine();
                if (ImGui::SmallButton(ICON_FA_XMARK))
                {
                    removeRule = static_cast<int>(ruleIndex);
                }
                if (open)
                {
                    if (rule.type == ETerrainProcessRuleType::HeightAnchor)
                    {
                        glm::dvec2 position(rule.x, rule.y);
                        if (editPoint("摆放位置 XY", position))
                        {
                            rule.x = position.x;
                            rule.y = position.y;
                            changed = true;
                        }
                        glm::dvec2 samplePoint(rule.sampleX, rule.sampleY);
                        if (editPoint("高度取样 XY", samplePoint))
                        {
                            rule.sampleX = samplePoint.x;
                            rule.sampleY = samplePoint.y;
                            changed = true;
                        }
                        changed |= editNumber("离地 dz", rule.dz);
                    }
                    else if (rule.type == ETerrainProcessRuleType::Place ||
                             rule.type == ETerrainProcessRuleType::PlaceTilt ||
                             rule.type == ETerrainProcessRuleType::Snap)
                    {
                        glm::dvec2 position(rule.x, rule.y);
                        if (editPoint(rule.type == ETerrainProcessRuleType::Snap ? "外层 at" : "位置 XY", position))
                        {
                            rule.x = position.x;
                            rule.y = position.y;
                            changed = true;
                        }
                        changed |= editNumber("离地 dz", rule.dz);
                    }
                    if (rule.type == ETerrainProcessRuleType::PlaceTilt)
                    {
                        changed |= editNumber("最大倾角", rule.maxTilt, 0.5f, "%.1f°");
                        changed |= editNumber("探针距离", rule.probe);
                    }
                    else if (rule.type == ETerrainProcessRuleType::Along)
                    {
                        ImGui::TextDisabled("沿线折点");
                        changed |= editPoints(rule.points);
                        changed |= editNumber("步距", rule.step);
                        changed |= ImGui::InputInt("Seed", &rule.seed);
                        changed |= editNumber("起始偏移", rule.offset);
                        changed |= editNumber("离地 dz", rule.dz);
                    }
                    else if (rule.type == ETerrainProcessRuleType::Scatter)
                    {
                        changed |= ImGui::InputInt("Seed", &rule.seed);
                        changed |= ImGui::DragInt("数量", &rule.count, 1.0f, 0, 100000);
                        if (ImGui::Checkbox("圆形区域", &rule.circularRegion))
                        {
                            changed = true;
                        }
                        if (rule.circularRegion)
                        {
                            changed |= editPoint("圆心 XY", rule.regionCenter);
                            changed |= editNumber("半径", rule.regionRadius, 0.5f);
                        }
                        else
                        {
                            double region[4] = {rule.region.x, rule.region.y, rule.region.z, rule.region.w};
                            if (ImGui::DragScalarN("区域 x0/y0/x1/y1", ImGuiDataType_Double, region, 4, 0.5f))
                            {
                                rule.region = glm::dvec4(region[0], region[1], region[2], region[3]);
                                changed = true;
                            }
                        }
                        changed |= editNumber("最低高度", rule.minHeight);
                        changed |= editNumber("最高高度", rule.maxHeight);
                        changed |= editNumber("最大坡度", rule.maxSlope, 0.5f, "%.1f°");
                        changed |= editNumber("避水距离", rule.avoidWater);
                        std::string biomeText = fmt::format("{}", fmt::join(rule.biomes, ", "));
                        if (ImGui::InputTextWithHint("Biome 白名单", "grass, grass_dark", &biomeText))
                        {
                            rule.biomes.clear();
                            std::istringstream input(biomeText);
                            std::string biome;
                            while (std::getline(input, biome, ','))
                            {
                                biome.erase(biome.begin(),
                                            std::find_if_not(biome.begin(), biome.end(),
                                                             [](unsigned char c) { return std::isspace(c); }));
                                biome.erase(std::find_if_not(biome.rbegin(), biome.rend(),
                                                             [](unsigned char c) { return std::isspace(c); })
                                                .base(),
                                            biome.end());
                                if (!biome.empty())
                                {
                                    rule.biomes.push_back(std::move(biome));
                                }
                            }
                            changed = true;
                        }
                        if (ImGui::Checkbox("随机旋转", &rule.randomRotation))
                        {
                            changed = true;
                        }
                        changed |= ImGui::DragInt("Mesh 变体数", &rule.variants, 1.0f, 0, 100000);
                        ImGui::SetItemTooltip("0 保持逐实例几何；大于 0 时限制模块产生的 mesh 变体数量");
                        double scaleRange[2] = {rule.scaleRange.x, rule.scaleRange.y};
                        if (ImGui::DragScalarN("实例缩放 min/max", ImGuiDataType_Double, scaleRange, 2, 0.01f,
                                               nullptr, nullptr, "%.2f"))
                        {
                            rule.scaleRange = {scaleRange[0], scaleRange[1]};
                            changed = true;
                        }
                        changed |= editNumber("离地 dz", rule.dz);
                    }

                    ImGui::TextDisabled("Child SCAD（模块调用、rotate 链或代码块）");
                    if (ImGui::InputTextMultiline("##terrain_rule_child", &rule.childSource, ImVec2(-1.0f, 76.0f),
                                                  ImGuiInputTextFlags_AllowTabInput))
                    {
                        changed = true;
                    }
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }
            if (duplicateRule >= 0)
            {
                TerrainProcess().DuplicateRule(static_cast<size_t>(duplicateRule));
                selectedTerrainRule_ = static_cast<int>(TerrainProcess().Rules().size()) - 1;
                terrainSelectionIsRule_ = true;
                scrollToSelectedTerrainItem_ = true;
                changed = true;
            }
            if (removeRule >= 0)
            {
                TerrainProcess().RemoveRule(static_cast<size_t>(removeRule));
                selectedTerrainRule_ = std::clamp(selectedTerrainRule_, 0,
                                                  std::max(0, static_cast<int>(TerrainProcess().Rules().size()) - 1));
                changed = true;
            }

            if (ImGui::Button(ICON_FA_PLUS " 添加贴地规则"))
            {
                ImGui::OpenPopup("##add_terrain_rule");
            }
            if (ImGui::BeginPopup("##add_terrain_rule"))
            {
                const auto addRule = [&](const char* label, ETerrainProcessRuleType type)
                {
                    if (ImGui::MenuItem(label))
                    {
                        TerrainProcess().AddRule(type);
                        selectedTerrainRule_ = static_cast<int>(TerrainProcess().Rules().size()) - 1;
                        terrainSelectionIsRule_ = true;
                        scrollToSelectedTerrainItem_ = true;
                        changed = true;
                    }
                };
                addRule("高度锚点 gk_terrain_height", ETerrainProcessRuleType::HeightAnchor);
                addRule("单件贴地 ter_place", ETerrainProcessRuleType::Place);
                addRule("随坡贴地 ter_place_tilt", ETerrainProcessRuleType::PlaceTilt);
                addRule("布局贴地 ter_snap", ETerrainProcessRuleType::Snap);
                addRule("沿线放置 ter_along", ETerrainProcessRuleType::Along);
                addRule("过滤散布 ter_scatter", ETerrainProcessRuleType::Scatter);
                ImGui::EndPopup();
            }
            ImGui::SameLine();
            ImGui::TextDisabled("也可从左侧 Kit 浏览器“+”直接创建 ter_place");
        }

        if (changed)
        {
            terrain.size.x = std::max(1.0, terrain.size.x);
            terrain.size.y = std::max(1.0, terrain.size.y);
            terrain.cells.x = std::clamp(terrain.cells.x, 4, 256);
            terrain.cells.y = std::clamp(terrain.cells.y, 4, 256);
            terrain.relief = std::max(0.0, terrain.relief);
            terrain.roughness = std::clamp(terrain.roughness, 0.0, 1.0);
            for (Assets::Scad::FTerrainFeature& feature : terrain.features)
            {
                feature.radius = std::max(0.1, feature.radius);
                feature.width = std::max(0.1, feature.width);
                feature.depth = std::max(0.0, feature.depth);
                feature.rugged = std::clamp(feature.rugged, 0.0, 1.0);
                feature.size.x = std::max(0.1, feature.size.x);
                feature.size.y = std::max(0.1, feature.size.y);
            }
            for (FTerrainProcessRule& rule : TerrainProcess().Rules())
            {
                rule.step = std::max(0.01, rule.step);
                rule.probe = std::max(0.01, rule.probe);
                rule.maxTilt = std::clamp(rule.maxTilt, 0.0, 90.0);
                rule.count = std::max(0, rule.count);
                rule.regionRadius = std::max(0.1, rule.regionRadius);
                rule.variants = std::max(0, rule.variants);
                rule.scaleRange.x = std::max(0.01, rule.scaleRange.x);
                rule.scaleRange.y = std::max(0.01, rule.scaleRange.y);
                if (rule.scaleRange.x > rule.scaleRange.y)
                {
                    std::swap(rule.scaleRange.x, rule.scaleRange.y);
                }
                if (rule.minHeight > rule.maxHeight)
                {
                    std::swap(rule.minHeight, rule.maxHeight);
                }
                rule.maxSlope = std::clamp(rule.maxSlope, 0.0, 90.0);
            }
            MarkTerrainProcessDirty();
        }
    }

    bool ScadLibraryInterface::DrawTerrainFeatureDetails(int featureIndex)
    {
        Assets::Scad::FTerrainSpec& terrain = TerrainProcess().Terrain();
        if (featureIndex < 0 || featureIndex >= static_cast<int>(terrain.features.size()))
        {
            return false;
        }
        Assets::Scad::FTerrainFeature& feature = terrain.features[featureIndex];
        const auto editPoint = [](const char* label, glm::dvec2& point)
        {
            double value[2] = {point.x, point.y};
            if (!ImGui::DragScalarN(label, ImGuiDataType_Double, value, 2, 0.5f))
            {
                return false;
            }
            point = {value[0], value[1]};
            return true;
        };
        const auto editNumber = [](const char* label, double& value, float speed = 0.1f, const char* format = "%.3f")
        { return ImGui::DragScalar(label, ImGuiDataType_Double, &value, speed, nullptr, nullptr, format); };
        const auto editPoints = [&editPoint](std::vector<glm::dvec2>& points)
        {
            bool changed = false;
            for (size_t pointIndex = 0; pointIndex < points.size(); ++pointIndex)
            {
                ImGui::PushID(static_cast<int>(pointIndex));
                changed |= editPoint("折点 XY", points[pointIndex]);
                ImGui::PopID();
            }
            if (ImGui::SmallButton(ICON_FA_PLUS " 添加折点"))
            {
                points.push_back(points.empty() ? glm::dvec2(0.0) : points.back() + glm::dvec2(5.0, 0.0));
                changed = true;
            }
            return changed;
        };

        ImGui::Text("Feature %d · %s", featureIndex + 1, FTerrainProcessDocument::FeatureTypeName(feature.type));
        ImGui::Separator();
        using EType = Assets::Scad::FTerrainFeature::EType;
        bool changed = false;
        switch (feature.type)
        {
        case EType::Mountain:
            changed |= editPoint("中心 XY", feature.at);
            changed |= editNumber("半径", feature.radius);
            changed |= editNumber("高度", feature.height);
            changed |= editNumber("扰动强度", feature.rugged, 0.01f);
            break;
        case EType::Ridge:
            changed |= editPoints(feature.pts);
            changed |= editNumber("宽度", feature.width);
            changed |= editNumber("高度", feature.height);
            break;
        case EType::Plateau:
            changed |= editPoint("中心 XY", feature.at);
            changed |= editNumber("半径", feature.radius);
            changed |= editNumber("高度", feature.height);
            break;
        case EType::Lake:
            changed |= editPoint("中心 XY", feature.at);
            changed |= editNumber("半径", feature.radius);
            changed |= editNumber("深度", feature.depth);
            break;
        case EType::River:
            changed |= editPoints(feature.pts);
            changed |= editNumber("宽度", feature.width);
            changed |= editNumber("深度", feature.depth);
            break;
        case EType::Road:
            changed |= editPoints(feature.pts);
            changed |= editNumber("宽度", feature.width);
            break;
        case EType::Pad:
            changed |= editPoint("中心 XY", feature.at);
            changed |= editPoint("尺寸 XY", feature.size);
            changed |= editNumber("旋转", feature.rot, 1.0f, "%.1f°");
            break;
        case EType::Hmap:
            ImGui::TextDisabled("采样高度场%s", feature.path.empty() ? "（内联）" : "");
            if (!feature.path.empty())
            {
                ImGui::TextWrapped("%s", feature.path.c_str());
            }
            changed |= editNumber("高度缩放", feature.zScale);
            changed |= editNumber("高度偏移", feature.zBias);
            break;
        }
        return changed;
    }

    bool ScadLibraryInterface::DrawTerrainRuleDetails(int ruleIndex)
    {
        std::vector<FTerrainProcessRule>& rules = TerrainProcess().Rules();
        if (ruleIndex < 0 || ruleIndex >= static_cast<int>(rules.size()) || rules[ruleIndex].removed)
        {
            return false;
        }
        FTerrainProcessRule& rule = rules[ruleIndex];
        const auto editPoint = [](const char* label, double& x, double& y)
        {
            double value[2] = {x, y};
            if (!ImGui::DragScalarN(label, ImGuiDataType_Double, value, 2, 0.5f))
            {
                return false;
            }
            x = value[0];
            y = value[1];
            return true;
        };
        const auto editNumber = [](const char* label, double& value, float speed = 0.1f, const char* format = "%.3f")
        { return ImGui::DragScalar(label, ImGuiDataType_Double, &value, speed, nullptr, nullptr, format); };

        ImGui::Text("过程算子 %d · %s", ruleIndex + 1, FTerrainProcessDocument::RuleTypeName(rule.type));
        ImGui::Separator();
        bool changed = false;
        if (rule.type == ETerrainProcessRuleType::HeightAnchor)
        {
            changed |= editPoint("摆放位置 XY", rule.x, rule.y);
            changed |= editPoint("高度取样 XY", rule.sampleX, rule.sampleY);
            changed |= editNumber("离地 dz", rule.dz);
        }
        else if (rule.type == ETerrainProcessRuleType::Place || rule.type == ETerrainProcessRuleType::PlaceTilt ||
                 rule.type == ETerrainProcessRuleType::Snap)
        {
            changed |= editPoint(rule.type == ETerrainProcessRuleType::Snap ? "外层 at" : "位置 XY", rule.x, rule.y);
            changed |= editNumber("离地 dz", rule.dz);
        }
        if (rule.type == ETerrainProcessRuleType::PlaceTilt)
        {
            changed |= editNumber("最大倾角", rule.maxTilt, 0.5f, "%.1f°");
            changed |= editNumber("探针距离", rule.probe);
        }
        else if (rule.type == ETerrainProcessRuleType::Along)
        {
            for (size_t pointIndex = 0; pointIndex < rule.points.size(); ++pointIndex)
            {
                ImGui::PushID(static_cast<int>(pointIndex));
                changed |= editPoint("沿线折点 XY", rule.points[pointIndex].x, rule.points[pointIndex].y);
                ImGui::PopID();
            }
            changed |= editNumber("步距", rule.step);
            changed |= ImGui::InputInt("Seed", &rule.seed);
            changed |= editNumber("起始偏移", rule.offset);
            changed |= editNumber("离地 dz", rule.dz);
        }
        else if (rule.type == ETerrainProcessRuleType::Scatter)
        {
            changed |= ImGui::InputInt("Seed", &rule.seed);
            changed |= ImGui::DragInt("数量", &rule.count, 1.0f, 0, 100000);
            if (ImGui::Checkbox("圆形区域", &rule.circularRegion))
            {
                changed = true;
            }
            if (rule.circularRegion)
            {
                changed |= editPoint("圆心 XY", rule.regionCenter.x, rule.regionCenter.y);
                changed |= editNumber("半径", rule.regionRadius, 0.5f);
            }
            else
            {
                double region[4] = {rule.region.x, rule.region.y, rule.region.z, rule.region.w};
                if (ImGui::DragScalarN("区域 x0/y0/x1/y1", ImGuiDataType_Double, region, 4, 0.5f))
                {
                    rule.region = {region[0], region[1], region[2], region[3]};
                    changed = true;
                }
            }
            changed |= editNumber("最低高度", rule.minHeight);
            changed |= editNumber("最高高度", rule.maxHeight);
            changed |= editNumber("最大坡度", rule.maxSlope, 0.5f, "%.1f°");
            changed |= editNumber("避水距离", rule.avoidWater);
            changed |= ImGui::Checkbox("随机旋转", &rule.randomRotation);
            changed |= ImGui::DragInt("Mesh 变体数", &rule.variants, 1.0f, 0, 100000);
            double scaleRange[2] = {rule.scaleRange.x, rule.scaleRange.y};
            if (ImGui::DragScalarN("实例缩放 min/max", ImGuiDataType_Double, scaleRange, 2, 0.01f))
            {
                rule.scaleRange = {scaleRange[0], scaleRange[1]};
                changed = true;
            }
            changed |= editNumber("离地 dz", rule.dz);
        }
        ImGui::TextDisabled("Child SCAD（模块调用、rotate 链或代码块）");
        changed |= ImGui::InputTextMultiline("##terrain_rule_child", &rule.childSource, ImVec2(-1.0f, 88.0f),
                                              ImGuiInputTextFlags_AllowTabInput);
        return changed;
    }

    bool ScadLibraryInterface::DrawLayScatterDetails(size_t segmentIndex)
    {
        FLayScatterSource scatter;
        if (!ParseLayScatterSource(document_.GetSegmentSource(segmentIndex), scatter))
        {
            ImGui::TextDisabled("lay_scatter 参数含变量或复杂表达式，保留源码编辑以避免改变语义。");
            return false;
        }

        ImGui::TextDisabled("布局散布 · children 保持不变");
        bool changed = false;
        changed |= ImGui::DragInt("数量", &scatter.count, 1.0f, 0, 100000);
        double region[4] = {scatter.x0, scatter.y0, scatter.x1, scatter.y1};
        if (ImGui::DragScalarN("区域 x0/y0/x1/y1", ImGuiDataType_Double, region, 0.5f))
        {
            scatter.x0 = region[0];
            scatter.y0 = region[1];
            scatter.x1 = region[2];
            scatter.y1 = region[3];
            changed = true;
        }
        changed |= ImGui::InputInt("Seed", &scatter.seed);
        changed |= ImGui::Checkbox("随机朝向", &scatter.rotate);
        if (!changed)
        {
            return false;
        }
        scatter.count = std::max(0, scatter.count);
        if (scatter.x0 > scatter.x1)
        {
            std::swap(scatter.x0, scatter.x1);
        }
        if (scatter.y0 > scatter.y1)
        {
            std::swap(scatter.y0, scatter.y1);
        }
        return document_.ReplaceSegmentSource(segmentIndex, SerializeLayScatterSource(scatter));
    }

    bool ScadLibraryInterface::DrawBenchItemParameters(FBenchItem& benchItem)
    {
        if (!benchItem.catalogModuleName.empty())
        {
            ImGui::Text("本地模块：%s", benchItem.moduleName.c_str());
            ImGui::TextDisabled("底层 Kit：%s", benchItem.catalogModuleName.c_str());
            ImGui::TextWrapped("该无参包装模块的固定参数保留在 module 定义中；这里可编辑它的放置变换，"
                               "不会把 Kit 参数错误写进包装调用。");
            return false;
        }

        const FKitModuleInfo* moduleInfo = nullptr;
        if (benchItem.kitIndex >= 0 && benchItem.kitIndex < static_cast<int>(kits_.size()))
        {
            const FKitInfo& kit = kits_[benchItem.kitIndex];
            const auto found = std::find_if(kit.modules.begin(), kit.modules.end(),
                                            [&](const FKitModuleInfo& module)
                                            { return module.name == benchItem.CatalogModuleName(); });
            if (found != kit.modules.end())
            {
                moduleInfo = &*found;
            }
        }

        std::vector<FScadParameterEditor> parameters;
        if (moduleInfo == nullptr || !ParseScadModuleParameters(*moduleInfo, parameters))
        {
            const bool changed = ImGui::InputTextWithHint("原始参数##scad_raw_args",
                                                          "SCAD 参数，例如 seed = 3",
                                                          benchItem.args, sizeof(benchItem.args));
            ImGui::SetItemTooltip("模块签名无法结构化解析，保留原始 SCAD 参数编辑\n%s",
                                  moduleInfo == nullptr ? "未找到模块签名" : moduleInfo->params.c_str());
            return changed;
        }

        std::vector<FScadRawArgument> unknown;
        parameters = MakeScadParameterEditors(*moduleInfo, benchItem.args, unknown);
        const std::string moduleComment =
            ReadScadModuleComment(kits_[benchItem.kitIndex], *moduleInfo);
        if (parameters.empty())
        {
            if (!unknown.empty())
            {
                std::string rawUnknown;
                for (const FScadRawArgument& argument : unknown)
                {
                    if (!rawUnknown.empty())
                    {
                        rawUnknown += ", ";
                    }
                    rawUnknown += argument.named ? fmt::format("{} = {}", argument.name, argument.source)
                                                  : argument.source;
                }
                if (ImGui::InputTextWithHint("原始参数##scad_raw_args", "SCAD 参数", &rawUnknown))
                {
                    if (rawUnknown.size() < sizeof(benchItem.args))
                    {
                        std::snprintf(benchItem.args, sizeof(benchItem.args), "%s", rawUnknown.c_str());
                        return true;
                    }
                    statusLine_ = "参数文本过长，最多支持 511 个字符";
                    statusError_ = true;
                }
            }
            else
            {
                ImGui::TextDisabled("该模块没有可编辑参数");
            }
            return false;
        }

        bool changed = false;
        for (size_t index = 0; index < parameters.size(); ++index)
        {
            FScadParameterEditor& parameter = parameters[index];
            const std::string label = fmt::format("{}##scad_parameter_{}", parameter.name, index);
            const Assets::Scad::ExprPtr& expression = parameter.expression;
            const auto setTooltip = [&](const char* type)
            {
                const std::string defaultValue = parameter.defaultSource.empty() ? "无默认值（必填）"
                                                                                   : parameter.defaultSource;
                const std::string tooltip = fmt::format("{}\n类型: {}\n默认值: {}",
                                                         ScadParameterDescription(parameter.name), type,
                                                         defaultValue);
                if (moduleComment.empty())
                {
                    ImGui::SetItemTooltip("%s", tooltip.c_str());
                }
                else
                {
                    ImGui::SetItemTooltip("%s\n\n模块注释:\n%s", tooltip.c_str(), moduleComment.c_str());
                }
            };

            double number = 0.0;
            if (ReadScadNumericLiteral(expression, number))
            {
                if (ImGui::DragScalar(label.c_str(), ImGuiDataType_Double, &number, 0.01f, nullptr, nullptr,
                                      "%.4g"))
                {
                    parameter.source = fmt::format("{:.9g}", number);
                    changed = true;
                }
                setTooltip("数值");
                continue;
            }

            if (expression != nullptr && expression->kind == Assets::Scad::ExprKind::Bool)
            {
                bool value = expression->boolean;
                if (ImGui::Checkbox(label.c_str(), &value))
                {
                    parameter.source = value ? "true" : "false";
                    changed = true;
                }
                setTooltip("布尔");
                continue;
            }

            if (expression != nullptr && expression->kind == Assets::Scad::ExprKind::Str)
            {
                std::string value = expression->str;
                if (ImGui::InputText(label.c_str(), &value))
                {
                    parameter.source = QuoteScadString(value);
                    changed = true;
                }
                setTooltip("字符串");
                continue;
            }

            bool numericVector = expression != nullptr && expression->kind == Assets::Scad::ExprKind::VectorLit &&
                expression->list.size() >= 1 && expression->list.size() <= 4;
            std::vector<double> vectorValues;
            if (numericVector)
            {
                vectorValues.reserve(expression->list.size());
                for (const Assets::Scad::ExprPtr& element : expression->list)
                {
                    double value = 0.0;
                    if (!ReadScadNumericLiteral(element, value))
                    {
                        numericVector = false;
                        break;
                    }
                    vectorValues.push_back(value);
                }
            }
            if (numericVector)
            {
                bool vectorChanged = false;
                if (IsColorScadParameter(parameter.name) &&
                    (vectorValues.size() == 3 || vectorValues.size() == 4))
                {
                    float color[4] = {static_cast<float>(vectorValues[0]), static_cast<float>(vectorValues[1]),
                                      static_cast<float>(vectorValues[2]),
                                      vectorValues.size() == 4 ? static_cast<float>(vectorValues[3]) : 1.0f};
                    vectorChanged = vectorValues.size() == 3
                        ? ImGui::ColorEdit3(label.c_str(), color)
                        : ImGui::ColorEdit4(label.c_str(), color);
                    if (vectorChanged)
                    {
                        parameter.source = vectorValues.size() == 3
                            ? fmt::format("[{:.5f}, {:.5f}, {:.5f}]", color[0], color[1], color[2])
                            : fmt::format("[{:.5f}, {:.5f}, {:.5f}, {:.5f}]", color[0], color[1], color[2],
                                          color[3]);
                    }
                    setTooltip(vectorValues.size() == 3 ? "RGB 颜色" : "RGBA 颜色");
                }
                else if (vectorValues.size() == 1)
                {
                    vectorChanged = ImGui::DragScalar(label.c_str(), ImGuiDataType_Double, vectorValues.data(),
                                                      0.01f, nullptr, nullptr, "%.4g");
                    if (vectorChanged)
                    {
                        parameter.source = fmt::format("[{:.9g}]", vectorValues[0]);
                    }
                    setTooltip("数值向量");
                }
                else
                {
                    vectorChanged = ImGui::DragScalarN(label.c_str(), ImGuiDataType_Double, vectorValues.data(),
                                                       static_cast<int>(vectorValues.size()), 0.01f, nullptr, nullptr,
                                                       "%.4g");
                    if (vectorChanged)
                    {
                        parameter.source = "[";
                        for (size_t component = 0; component < vectorValues.size(); ++component)
                        {
                            if (component > 0)
                            {
                                parameter.source += ", ";
                            }
                            parameter.source += fmt::format("{:.9g}", vectorValues[component]);
                        }
                        parameter.source += "]";
                    }
                    setTooltip(fmt::format("{}D 数值向量", vectorValues.size()).c_str());
                }
                changed |= vectorChanged;
                continue;
            }

            std::string expressionText = parameter.source;
            if (ImGui::InputTextWithHint(label.c_str(), "SCAD 表达式", &expressionText))
            {
                parameter.source = std::move(expressionText);
                changed = true;
            }
            setTooltip("表达式");
        }

        if (!unknown.empty())
        {
            ImGui::TextDisabled("未在模块签名中识别的参数");
            std::string unknownText;
            for (const FScadRawArgument& argument : unknown)
            {
                if (!unknownText.empty())
                {
                    unknownText += ", ";
                }
                unknownText += argument.named ? fmt::format("{} = {}", argument.name, argument.source)
                                              : argument.source;
            }
            if (ImGui::InputTextWithHint("高级参数##scad_unknown_args", "保留的 SCAD 参数", &unknownText))
            {
                if (unknownText.size() < sizeof(benchItem.args))
                {
                    const std::string composed = ComposeScadArguments(parameters, ParseScadCallArguments(unknownText));
                    if (composed.size() < sizeof(benchItem.args))
                    {
                        std::snprintf(benchItem.args, sizeof(benchItem.args), "%s", composed.c_str());
                        return true;
                    }
                }
                statusLine_ = "参数文本过长，最多支持 511 个字符";
                statusError_ = true;
            }
        }

        if (!changed)
        {
            return false;
        }
        const std::string composed = ComposeScadArguments(parameters, unknown);
        if (composed.size() >= sizeof(benchItem.args))
        {
            statusLine_ = "参数文本过长，最多支持 511 个字符";
            statusError_ = true;
            return false;
        }
        std::snprintf(benchItem.args, sizeof(benchItem.args), "%s", composed.c_str());
        return true;
    }

    void ScadLibraryInterface::DrawSceneVariableProperties()
    {
        const std::vector<FScadSceneVariable>& variables = document_.SceneVariables();
        if (variables.empty())
        {
            return;
        }

        ImGui::TextDisabled("场景变量与常量 · %zu 项", variables.size());
        ImGui::TextWrapped("这里的数值会作用于整个场景。编辑表达式变量时，将把该赋值改为数值常量。"
                           "引用它的 translate、过程规则和模块会一起重载。");
        for (size_t index = 0; index < variables.size(); ++index)
        {
            const FScadSceneVariable& variable = variables[index];
            double value = variable.value;
            ImGui::PushID(static_cast<int>(index));
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(variable.name.c_str());
            ImGui::SameLine(92.0f);
            ImGui::SetNextItemWidth(-1.0f);
            bool changed = false;
            if (variable.name == "$fn")
            {
                int segments = std::max(3, static_cast<int>(std::lround(value)));
                changed = ImGui::DragInt("##scene_variable", &segments, 1.0f, 3, 256);
                value = static_cast<double>(segments);
            }
            else
            {
                changed = ImGui::DragScalar("##scene_variable", ImGuiDataType_Double, &value, 0.05f, nullptr,
                                            nullptr, "%.4f");
            }
            if (changed)
            {
                if (document_.SetSceneVariableNumber(index, value))
                {
                    documentVariables_[variable.name] = Assets::Scad::Value::MakeNumber(value);
                    assemblySourceDirty_ = true;
                    benchDirty_ = true;
                    terrainProcessDirty_ = document_.HasTerrain();
                    sourceStructureBoundsDirty_ = true;
                    statusLine_ = fmt::format("已更新场景变量 {} = {:.4g}", variable.name, value);
                    statusError_ = false;
                }
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("第 %d 行原表达式：%s", variable.line, variable.expression.c_str());
            }
            ImGui::PopID();
        }
        ImGui::Separator();
    }

    void ScadLibraryInterface::DrawBenchContent()
    {
        ImGui::Spacing();
        ImGui::TextDisabled("资源文件");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##assembly_path", "assets/scad/evaluated/my_scene.scad", assemblyPathBuf_,
                                 sizeof(assemblyPathBuf_));
        if (ImGui::Button(ICON_FA_FOLDER_OPEN " 打开"))
        {
            OpenAssembly(assemblyPathBuf_);
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(assemblySource_.empty() && Bench().empty());
        if (ImGui::Button(ICON_FA_COPY " 另存为"))
        {
            SaveAssembly(true);
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextDisabled("保存与预览位于顶部工具栏");

        if (!openedAssemblyPath_.empty())
        {
            ImGui::TextDisabled("%s%s", openedAssemblyPath_.c_str(), assemblySourceDirty_ ? "  *" : "");
        }
        if (!openedAssemblyKits_.empty())
        {
            ImGui::TextDisabled("依赖: %s", fmt::format("{}", fmt::join(openedAssemblyKits_, ", ")).c_str());
        }
        if (openedAssemblyPath_.find("/generated/") != std::string::npos ||
            openedAssemblyPath_.find("\\generated\\") != std::string::npos)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, NextUI::Theme::Color(NextUI::Theme::EColor::Warning));
            ImGui::TextWrapped("generated/ 文件可能由 specs/ 重新生成；保存的手工修改可能被覆盖。");
            ImGui::PopStyleColor();
        }
        // Composition, not classification: the same file can offer all of these
        // at once and gains any of them without being converted.
        ImGui::TextDisabled("节点: %zu 实例  ·  %zu 源码结构%s", Bench().size(), document_.SourceSegmentCount(),
                            document_.HasTerrain() ? "  ·  含地形" : "");
        for (const std::string& warning : terrainProcessWarnings_)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, NextUI::Theme::Color(NextUI::Theme::EColor::Warning));
            ImGui::TextWrapped("%s", warning.c_str());
            ImGui::PopStyleColor();
        }
        ImGui::Separator();
        DrawSceneVariableProperties();

        if (ImGui::BeginTabBar("##assembly_editor_tabs"))
        {
            if (document_.HasTerrain() &&
                ImGui::BeginTabItem(fmt::format("过程 ({} + {})", TerrainProcess().Terrain().features.size(),
                                                TerrainProcess().ActiveRuleCount())
                                        .c_str()))
            {
                assemblyEditorTab_ = 2;
                DrawTerrainProcessContent();
                ImGui::EndTabItem();
            }

            {
                const std::string objectTabLabel = fmt::format("对象 ({})", Bench().size());
                if (false && ImGui::BeginTabItem(objectTabLabel.c_str()))
                {
                    assemblyEditorTab_ = 0;
                    ImGui::Checkbox("自动刷新", &autoReload_);
                    ImGui::SameLine();
                    ImGui::TextDisabled("$fn 等全局设置是源码语句，在“结构”或“源码”页编辑");
                    if (document_.SourceSegmentCount() > 0)
                    {
                        ImGui::TextDisabled("该场景还有 %zu 个源码结构；在“结构”页可以逐个关闭或展开为实例。",
                                            document_.SourceSegmentCount());
                    }

                    if (ImGui::Button(ICON_FA_ROTATE_RIGHT " 刷新对象"))
                    {
                        ReloadBench();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button(ICON_FA_TRASH " 清空") && !Bench().empty())
                    {
                        Bench().clear();
                        selectedBenchItem_ = -1;
                        engine_.GetScene().ClearSelection();
                        engine_.GetShowFlags().ShowEdge = false;
                        benchDirty_ = true;
                    }
                    ImGui::Separator();
                    ImGui::SetNextItemWidth(-1.0f);
                    ImGui::InputTextWithHint("##object_filter", "搜索对象模块或参数…", objectFilterBuf_,
                                             sizeof(objectFilterBuf_));

                    int removeIndex = -1;
                    int duplicateIndex = -1;
                    ImGui::BeginChild("##bench_list", ImVec2(0, -62.0f), ImGuiChildFlags_None);
                    for (size_t i = 0; i < Bench().size(); ++i)
                    {
                        FBenchItem& benchItem = Bench()[i];
                        if (objectFilterBuf_[0] != '\0' &&
                            benchItem.moduleName.find(objectFilterBuf_) == std::string::npos &&
                            std::string_view(benchItem.args).find(objectFilterBuf_) == std::string_view::npos)
                        {
                            continue;
                        }
                        ImGui::PushID(static_cast<int>(i));
                        ImGuiTreeNodeFlags objectFlags =
                            ImGuiTreeNodeFlags_AllowOverlap | ImGuiTreeNodeFlags_FramePadding;
                        if (Bench().size() <= 64)
                        {
                            objectFlags |= ImGuiTreeNodeFlags_DefaultOpen;
                        }
                        if (selectedBenchItem_ == static_cast<int>(i))
                        {
                            objectFlags |= ImGuiTreeNodeFlags_Selected;
                            ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.10f, 0.34f, 0.68f, 0.82f));
                            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.14f, 0.42f, 0.80f, 0.92f));
                            ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.16f, 0.48f, 0.92f, 1.0f));
                        }
                        const std::string objectLabel = benchItem.sourceLine > 0
                            ? fmt::format("{} #{}  ·  L{}", benchItem.moduleName, i, benchItem.sourceLine)
                            : fmt::format("{} #{}", benchItem.moduleName, i);
                        const bool open = ImGui::TreeNodeEx(objectLabel.c_str(), objectFlags);
                        if (selectedBenchItem_ == static_cast<int>(i))
                        {
                            ImGui::PopStyleColor(3);
                            if (scrollToSelectedBenchItem_)
                            {
                                ImGui::SetScrollHereY(0.35f);
                                scrollToSelectedBenchItem_ = false;
                            }
                        }
                        if (ImGui::IsItemClicked())
                        {
                            selectedBenchItem_ = static_cast<int>(i);
                            if (Assets::Node* selectedNode =
                                    ResolveSceneObjectNode(benchItem, SceneObjectWorldMatrix(benchItem)))
                            {
                                engine_.GetScene().SetSelectedId(selectedNode->GetInstanceId());
                                engine_.GetShowFlags().ShowEdge = false;
                            }
                        }
                        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 52.0f);
                        if (ImGui::SmallButton(ICON_FA_COPY "##dup"))
                        {
                            duplicateIndex = static_cast<int>(i);
                        }
                        ImGui::SameLine();
                        if (ImGui::SmallButton(ICON_FA_XMARK "##del"))
                        {
                            removeIndex = static_cast<int>(i);
                        }
                        if (open)
                        {
                            float position[3] = {benchItem.x, benchItem.y, benchItem.z};
                            if (ImGui::DragFloat3("位置", position, 0.5f))
                            {
                                benchItem.x = position[0];
                                benchItem.y = position[1];
                                benchItem.z = position[2];
                                benchDirty_ = true;
                            }
                            float rotation[3] = {benchItem.rotX, benchItem.rotY, benchItem.rotZ};
                            if (ImGui::DragFloat3("旋转", rotation, 1.0f, -360.0f, 360.0f, "%.1f°"))
                            {
                                benchItem.rotX = rotation[0];
                                benchItem.rotY = rotation[1];
                                benchItem.rotZ = rotation[2];
                                benchDirty_ = true;
                            }
                            float scale[3] = {benchItem.scale, benchItem.scaleY, benchItem.scaleZ};
                            if (ImGui::DragFloat3("缩放", scale, 0.02f, 0.001f, 100.0f, "%.3f"))
                            {
                                benchItem.scale = scale[0];
                                benchItem.scaleY = scale[1];
                                benchItem.scaleZ = scale[2];
                                benchDirty_ = true;
                            }
                            if (benchItem.hasColor && ImGui::ColorEdit4("颜色", benchItem.color))
                            {
                                benchDirty_ = true;
                            }
                            if (DrawBenchItemParameters(benchItem))
                            {
                                benchDirty_ = true;
                            }
                            ImGui::TreePop();
                        }
                        ImGui::PopID();
                    }
                    if (Bench().empty())
                    {
                        ImGui::TextDisabled("从左侧 Kit 零件库点 \"+\" 添加模块，");
                        ImGui::TextDisabled("在这里调整位置、角度和参数。");
                    }
                    ImGui::EndChild();

                    if (removeIndex >= 0)
                    {
                        Bench().erase(Bench().begin() + removeIndex);
                        if (selectedBenchItem_ == removeIndex)
                        {
                            selectedBenchItem_ = -1;
                        }
                        else if (selectedBenchItem_ > removeIndex)
                        {
                            --selectedBenchItem_;
                        }
                        benchDirty_ = true;
                    }
                    if (duplicateIndex >= 0)
                    {
                        FBenchItem copy = Bench()[duplicateIndex];
                        copy.x += kBenchGridStep * 0.5f;
                        copy.y += kBenchGridStep * 0.5f;
                        copy.runtimeNodeId = std::numeric_limits<uint32_t>::max();
                        Bench().push_back(copy);
                        selectedBenchItem_ = static_cast<int>(Bench().size()) - 1;
                        benchDirty_ = true;
                    }

                    ImGui::Separator();
                    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 116.0f);
                    ImGui::InputTextWithHint("##export_name", "新场景文件名", exportNameBuf_, sizeof(exportNameBuf_));
                    ImGui::SameLine();
                    if (ImGui::Button(ICON_FA_FILE_EXPORT " 导出场景") && !Bench().empty())
                    {
                        ExportBench();
                    }
                    if (ImGui::IsItemHovered())
                    {
                        ImGui::SetTooltip("写入 assets/scad/evaluated/<名>.scad");
                    }
                    ImGui::EndTabItem();
                }
            }

            if (ImGui::BeginTabItem("详情", nullptr,
                                    structureInspectorRequested_ ? ImGuiTabItemFlags_SetSelected : 0))
            {
                assemblyEditorTab_ = 3;
                DrawStructureContent();
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("源码"))
            {
                assemblyEditorTab_ = 1;
                if (sourceBufferDirty_)
                {
                    ImGui::PushStyleColor(ImGuiCol_Text, NextUI::Theme::Color(NextUI::Theme::EColor::Warning));
                    ImGui::TextWrapped("源码已改动。预览或保存会重新解析，对象与地形面板随后刷新。");
                    ImGui::PopStyleColor();
                    ImGui::SameLine();
                    if (ImGui::SmallButton(ICON_FA_ROTATE_RIGHT " 立即解析"))
                    {
                        ReparseDocument(assemblySource_, openedAssemblyPath_);
                        assemblySourceDirty_ = true;
                    }
                }
                else
                {
                    ImGui::TextDisabled("支持完整 SCAD；预览不会先覆盖源文件。");
                }
                if (ImGui::InputTextMultiline("##assembly_source", &assemblySource_, ImVec2(-1.0f, -1.0f),
                                              ImGuiInputTextFlags_AllowTabInput))
                {
                    // Reparsing per keystroke would re-evaluate the whole
                    // use/include closure; the buffer is reconciled on preview,
                    // save, or the explicit button above.
                    assemblySourceDirty_ = true;
                    sourceBufferDirty_ = true;
                }
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
            structureInspectorRequested_ = false;
        }

        if (benchDirty_)
        {
            assemblySourceDirty_ = true;
        }
    }

    void ScadLibraryInterface::DrawStructureOutliner()
    {
        ImGui::TextDisabled("场景结构 · 选择节点后在右侧查看详情");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##left_outliner_filter", ICON_FA_MAGNIFYING_GLASS " 搜索节点", segmentFilterBuf_,
                                 sizeof(segmentFilterBuf_));
        ImGui::Separator();

        const auto selectSegment = [this](int segmentIndex)
        {
            if (segmentIndex < 0 || segmentIndex >= static_cast<int>(document_.Segments().size()))
            {
                return;
            }
            const FScadSceneSegment& segment = document_.Segments()[segmentIndex];
            selectedSegment_ = segmentIndex;
            scrollToSelectedSegment_ = true;
            structureInspectorRequested_ = true;
            assemblyEditorTab_ = 3;
            if (segment.kind == EScadSegmentKind::Instance && segment.instanceIndex >= 0)
            {
                ClearSelectedStructureBounds();
                selectedBenchItem_ = segment.instanceIndex;
                FBenchItem& item = Bench()[selectedBenchItem_];
                ResolveSceneObjectNode(item, SceneObjectWorldMatrix(item));
                engine_.GetScene().ClearSelection();
                engine_.GetShowFlags().ShowEdge = false;
            }
            else if (segment.kind == EScadSegmentKind::TerrainRule && segment.ruleIndex >= 0)
            {
                ClearEditableSceneSelection();
                ClearSelectedStructureBounds();
                selectedTerrainRule_ = segment.ruleIndex;
                terrainSelectionIsRule_ = true;
            }
            else if (segment.kind == EScadSegmentKind::Terrain)
            {
                ClearEditableSceneSelection();
                ClearSelectedStructureBounds();
                terrainSelectionIsRule_ = false;
                selectedTerrainFeature_ = -1;
            }
            else
            {
                ClearEditableSceneSelection();
                if (segment.name == "lay_scatter")
                {
                    ClearSelectedStructureBounds();
                }
                else
                {
                    UpdateSelectedStructureBounds();
                }
            }
        };

        ImGui::BeginChild("##left_structure_tree", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None);
        const ImGuiTableFlags tableFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
            ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp;
        if (ImGui::BeginTable("##structure_table", 4, tableFlags, ImVec2(0.0f, 0.0f)))
        {
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 28.0f);
            ImGui::TableSetupColumn("名称", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("类型", ImGuiTableColumnFlags_WidthFixed, 54.0f);
            ImGui::TableSetupColumn("行", ImGuiTableColumnFlags_WidthFixed, 42.0f);
            ImGui::TableHeadersRow();

            for (size_t segmentIndex = 0; segmentIndex < document_.Segments().size(); ++segmentIndex)
            {
                const FScadSceneSegment& segment = document_.Segments()[segmentIndex];
                const FBenchItem* instance =
                    segment.kind == EScadSegmentKind::Instance && segment.instanceIndex >= 0 &&
                        segment.instanceIndex < static_cast<int>(Bench().size())
                    ? &Bench()[segment.instanceIndex]
                    : nullptr;
                const FKitModuleInfo* moduleInfo = nullptr;
                if (instance != nullptr && instance->kitIndex >= 0 && instance->kitIndex < static_cast<int>(kits_.size()))
                {
                    const std::vector<FKitModuleInfo>& modules = kits_[instance->kitIndex].modules;
                    const auto module = std::find_if(modules.begin(), modules.end(), [&](const FKitModuleInfo& candidate)
                                                     { return candidate.name == instance->CatalogModuleName(); });
                    if (module != modules.end())
                    {
                        moduleInfo = &*module;
                    }
                }
                const std::string& displayName = instance != nullptr
                    ? instance->moduleName
                    : (segment.name.empty() ? segment.label : segment.name);
                const bool filterMiss = segmentFilterBuf_[0] != '\0' &&
                    displayName.find(segmentFilterBuf_) == std::string::npos &&
                    segment.label.find(segmentFilterBuf_) == std::string::npos &&
                    (moduleInfo == nullptr || moduleInfo->category.find(segmentFilterBuf_) == std::string::npos);
                if (segment.kind == EScadSegmentKind::TerrainRule || filterMiss)
                {
                    continue;
                }

                const bool layoutGenerator = segment.kind == EScadSegmentKind::Source && segment.name == "lay_scatter";
                const bool procedural = segment.kind == EScadSegmentKind::Terrain || layoutGenerator;
                const char* structureType = instance != nullptr ? "可编辑" : (procedural ? "过程" : "源码");
                const char* icon = instance != nullptr ? ICON_FA_CUBE
                    : (layoutGenerator ? ICON_FA_WAND_MAGIC_SPARKLES : SegmentKindIcon(segment.kind));
                const ImVec4 iconColor = segment.disabled
                    ? ImVec4(0.52f, 0.53f, 0.56f, 1.0f)
                    : (procedural ? SegmentKindColor(EScadSegmentKind::TerrainRule) : SegmentKindColor(segment.kind));

                ImGui::PushID(static_cast<int>(segmentIndex));
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextColored(iconColor, "%s", icon);
                ImGui::TableSetColumnIndex(1);
                if (ImGui::Selectable(fmt::format("{}##segment_{}", displayName, segmentIndex).c_str(),
                                      selectedSegment_ == static_cast<int>(segmentIndex)))
                {
                    selectSegment(static_cast<int>(segmentIndex));
                }
                if (selectedSegment_ == static_cast<int>(segmentIndex) && scrollToSelectedSegment_)
                {
                    // Selection from the viewport may target a row outside the
                    // current view, but clicking an already visible Outliner
                    // row must not move the user's scroll position.
                    if (!ImGui::IsItemVisible())
                    {
                        ImGui::SetScrollHereY(0.35f);
                    }
                    scrollToSelectedSegment_ = false;
                }
                if (instance != nullptr && ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("%s%s%s\n%s", instance->moduleName.c_str(), instance->catalogModuleName.empty()
                                                                 ? ""
                                                                 : "  → ",
                                      instance->catalogModuleName.c_str(), moduleInfo == nullptr
                                          ? "可编辑实例"
                                          : fmt::format("{} · {}", CategoryLabel(moduleInfo->category), moduleInfo->params).c_str());
                }
                ImGui::TableSetColumnIndex(2);
                ImGui::TextDisabled("%s", structureType);
                ImGui::TableSetColumnIndex(3);
                ImGui::TextDisabled("%s", segment.line > 0 ? fmt::format("L{}", segment.line).c_str() : "新增");

                // The terrain assignment (for example `TERR = [...]`) and the
                // gk_terrain(TERR) call are both top-level Terrain segments,
                // but only the call owns the feature/rule children. Expanding
                // both would render the same operators twice.
                if (segment.kind == EScadSegmentKind::Terrain && segment.name == "gk_terrain")
                {
                    const Assets::Scad::FTerrainSpec& terrain = TerrainProcess().Terrain();
                    for (size_t featureIndex = 0; featureIndex < terrain.features.size(); ++featureIndex)
                    {
                        ImGui::PushID(fmt::format("feature_{}", featureIndex).c_str());
                        const bool selected = selectedSegment_ == static_cast<int>(segmentIndex) &&
                            !terrainSelectionIsRule_ && selectedTerrainFeature_ == static_cast<int>(featureIndex);
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::TextColored(SegmentKindColor(EScadSegmentKind::TerrainRule), "%s", ICON_FA_MOUNTAIN_SUN);
                        ImGui::TableSetColumnIndex(1);
                        ImGui::Indent(14.0f);
                        const bool clicked = ImGui::Selectable(
                            fmt::format("{}##terrain_feature_{}", FTerrainProcessDocument::FeatureTypeName(terrain.features[featureIndex].type),
                                        featureIndex).c_str(), selected);
                        ImGui::Unindent(14.0f);
                        if (clicked)
                        {
                            ClearEditableSceneSelection();
                            ClearSelectedStructureBounds();
                            selectedSegment_ = static_cast<int>(segmentIndex);
                            selectedTerrainFeature_ = static_cast<int>(featureIndex);
                            terrainSelectionIsRule_ = false;
                            scrollToSelectedSegment_ = true;
                            structureInspectorRequested_ = true;
                            assemblyEditorTab_ = 3;
                        }
                        ImGui::TableSetColumnIndex(2);
                        ImGui::TextDisabled("特征");
                        ImGui::TableSetColumnIndex(3);
                        ImGui::TextDisabled("—");
                        ImGui::PopID();
                    }
                    const std::vector<FTerrainProcessRule>& rules = TerrainProcess().Rules();
                    for (size_t ruleIndex = 0; ruleIndex < rules.size(); ++ruleIndex)
                    {
                        if (rules[ruleIndex].removed)
                        {
                            continue;
                        }
                        ImGui::PushID(fmt::format("rule_{}", ruleIndex).c_str());
                        const bool selected = terrainSelectionIsRule_ && selectedTerrainRule_ == static_cast<int>(ruleIndex);
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::TextColored(SegmentKindColor(EScadSegmentKind::TerrainRule), "%s", ICON_FA_WAND_MAGIC_SPARKLES);
                        ImGui::TableSetColumnIndex(1);
                        ImGui::Indent(14.0f);
                        const bool clicked = ImGui::Selectable(
                            fmt::format("{}##terrain_rule_{}", FTerrainProcessDocument::RuleTypeName(rules[ruleIndex].type), ruleIndex).c_str(),
                            selected);
                        ImGui::Unindent(14.0f);
                        if (clicked)
                        {
                            const auto found = std::find_if(document_.Segments().begin(), document_.Segments().end(),
                                                            [ruleIndex](const FScadSceneSegment& candidate)
                                                            {
                                                                return candidate.kind == EScadSegmentKind::TerrainRule &&
                                                                    candidate.ruleIndex == static_cast<int>(ruleIndex);
                                                            });
                            ClearEditableSceneSelection();
                            ClearSelectedStructureBounds();
                            selectedSegment_ = found == document_.Segments().end()
                                ? static_cast<int>(segmentIndex)
                                : static_cast<int>(std::distance(document_.Segments().begin(), found));
                            selectedTerrainRule_ = static_cast<int>(ruleIndex);
                            terrainSelectionIsRule_ = true;
                            scrollToSelectedSegment_ = true;
                            structureInspectorRequested_ = true;
                            assemblyEditorTab_ = 3;
                        }
                        ImGui::TableSetColumnIndex(2);
                        ImGui::TextDisabled("规则");
                        ImGui::TableSetColumnIndex(3);
                        ImGui::TextDisabled("—");
                        ImGui::PopID();
                    }
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        if (document_.Segments().empty())
        {
            ImGui::TextDisabled("打开场景后显示结构。");
        }
        ImGui::EndChild();
    }

    void ScadLibraryInterface::DrawStructureContent()
    {
        ImGui::TextDisabled("结构节点详情");
        ImGui::Separator();

        int explodeRequest = -1;
        int collapseRequest = -1;
        int toggleRequest = -1;
        int removeInstanceRequest = -1;
        int duplicateInstanceRequest = -1;
        bool toggleTo = false;

        // The tree is hosted in the left resource sidebar. Keep this former
        // inline implementation disabled while the right panel is the
        // Inspector only.
        if (false)
        {
        const float outlinerWidth = std::max(220.0f, ImGui::GetContentRegionAvail().x * 0.43f);
        ImGui::BeginChild("##structure_outliner", ImVec2(outlinerWidth, 0.0f), ImGuiChildFlags_Borders);
        for (size_t index = 0; index < document_.Segments().size(); ++index)
        {
            const FScadSceneSegment& segment = document_.Segments()[index];
            // Process rules are children of the TERR node below, rather than
            // a second flat list entry.
            if (segment.kind == EScadSegmentKind::TerrainRule)
            {
                continue;
            }
            if (segmentFilterBuf_[0] != '\0' && segment.name.find(segmentFilterBuf_) == std::string::npos &&
                segment.label.find(segmentFilterBuf_) == std::string::npos)
            {
                continue;
            }
            ImGui::PushID(static_cast<int>(index));

            const bool selected = selectedSegment_ == static_cast<int>(index);
            const std::string lineLabel = segment.line > 0 ? fmt::format("L{}", segment.line) : "新增";
            const std::string rowLabel = fmt::format("{}  {}  ·  {}##segment_{}", SegmentKindIcon(segment.kind),
                                                     segment.name.empty() ? segment.label : segment.name, lineLabel,
                                                     index);
            ImGui::PushStyleColor(ImGuiCol_Text, segment.disabled
                                                     ? ImVec4(0.52f, 0.53f, 0.56f, 1.0f)
                                                     : SegmentKindColor(segment.kind));
            if (ImGui::Selectable(rowLabel.c_str(), selected))
            {
                selectedSegment_ = static_cast<int>(index);
                if (segment.kind == EScadSegmentKind::Instance && segment.instanceIndex >= 0)
                {
                    ClearSelectedStructureBounds();
                    selectedBenchItem_ = segment.instanceIndex;
                    scrollToSelectedBenchItem_ = true;
                    FBenchItem& item = Bench()[selectedBenchItem_];
                    ResolveSceneObjectNode(item, SceneObjectWorldMatrix(item));
                    engine_.GetScene().ClearSelection();
                    engine_.GetShowFlags().ShowEdge = false;
                }
                else if (segment.kind == EScadSegmentKind::TerrainRule && segment.ruleIndex >= 0)
                {
                    ClearEditableSceneSelection();
                    ClearSelectedStructureBounds();
                    terrainSelectionIsRule_ = true;
                    selectedTerrainRule_ = segment.ruleIndex;
                    scrollToSelectedTerrainItem_ = true;
                }
                else if (segment.kind == EScadSegmentKind::Terrain)
                {
                    ClearEditableSceneSelection();
                    ClearSelectedStructureBounds();
                    terrainSelectionIsRule_ = false;
                    selectedTerrainFeature_ = -1;
                }
                else
                {
                    // Source constructs such as for()/if()/module cannot be
                    // transformed as one object. Never leave the previous
                    // instance's gizmo, OBB or outline on screen.
                    ClearEditableSceneSelection();
                    UpdateSelectedStructureBounds();
                }
            }
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered())
            {
                if (segment.line > 0)
                {
                    ImGui::SetTooltip("%s  ·  第 %d-%d 行%s\n%s", ScadSegmentKindLabel(segment.kind), segment.line,
                                      segment.endLine, segment.disabled ? "  ·  已关闭" : "", segment.label.c_str());
                }
                else
                {
                    ImGui::SetTooltip("%s  ·  尚未保存的新节点%s\n%s", ScadSegmentKindLabel(segment.kind),
                                      segment.disabled ? "  ·  已关闭" : "", segment.label.c_str());
                }
            }

            if (segment.kind == EScadSegmentKind::Terrain && segment.name == "gk_terrain")
            {
                ImGui::Indent(18.0f);
                Assets::Scad::FTerrainSpec& terrain = TerrainProcess().Terrain();
                for (size_t featureIndex = 0; featureIndex < terrain.features.size(); ++featureIndex)
                {
                    ImGui::PushID(fmt::format("terrain_feature_{}", featureIndex).c_str());
                    const bool featureSelected = selectedSegment_ == static_cast<int>(index) && !terrainSelectionIsRule_ &&
                                                 selectedTerrainFeature_ == static_cast<int>(featureIndex);
                    const std::string featureLabel = fmt::format("{}  {}", ICON_FA_MOUNTAIN_SUN,
                                                                 FTerrainProcessDocument::FeatureTypeName(
                                                                     terrain.features[featureIndex].type));
                    if (ImGui::Selectable(featureLabel.c_str(), featureSelected))
                    {
                        ClearEditableSceneSelection();
                        ClearSelectedStructureBounds();
                        selectedSegment_ = static_cast<int>(index);
                        selectedTerrainFeature_ = static_cast<int>(featureIndex);
                        terrainSelectionIsRule_ = false;
                    }
                    ImGui::PopID();
                }
                const std::vector<FTerrainProcessRule>& rules = TerrainProcess().Rules();
                for (size_t ruleIndex = 0; ruleIndex < rules.size(); ++ruleIndex)
                {
                    if (rules[ruleIndex].removed)
                    {
                        continue;
                    }
                    const auto segmentIt = std::find_if(
                        document_.Segments().begin(), document_.Segments().end(), [ruleIndex](const FScadSceneSegment& candidate)
                        { return candidate.kind == EScadSegmentKind::TerrainRule && candidate.ruleIndex == static_cast<int>(ruleIndex); });
                    const int ruleSegmentIndex = segmentIt == document_.Segments().end()
                        ? static_cast<int>(index)
                        : static_cast<int>(std::distance(document_.Segments().begin(), segmentIt));
                    ImGui::PushID(fmt::format("terrain_rule_{}", ruleIndex).c_str());
                    const bool ruleSelected = terrainSelectionIsRule_ && selectedTerrainRule_ == static_cast<int>(ruleIndex);
                    const std::string ruleLabel = fmt::format("{}  {}", ICON_FA_WAND_MAGIC_SPARKLES,
                                                               FTerrainProcessDocument::RuleTypeName(rules[ruleIndex].type));
                    if (ImGui::Selectable(ruleLabel.c_str(), ruleSelected))
                    {
                        ClearEditableSceneSelection();
                        ClearSelectedStructureBounds();
                        selectedSegment_ = ruleSegmentIndex;
                        selectedTerrainRule_ = static_cast<int>(ruleIndex);
                        terrainSelectionIsRule_ = true;
                    }
                    ImGui::PopID();
                }
                ImGui::Unindent(18.0f);
            }

            if (false && selected)
            {
                if (scrollToSelectedSegment_)
                {
                    ImGui::SetScrollHereY(0.35f);
                    scrollToSelectedSegment_ = false;
                }
                ImGui::Indent(12.0f);
                ImGui::TextDisabled("%s", segment.label.c_str());
                if (segment.kind == EScadSegmentKind::Instance && segment.instanceIndex >= 0 &&
                    segment.instanceIndex < static_cast<int>(Bench().size()))
                {
                    FBenchItem& item = Bench()[segment.instanceIndex];
                    float position[3] = {item.x, item.y, item.z};
                    if (ImGui::DragFloat3("位置", position, 0.5f))
                    {
                        item.x = position[0];
                        item.y = position[1];
                        item.z = position[2];
                        benchDirty_ = true;
                    }
                    float rotation[3] = {item.rotX, item.rotY, item.rotZ};
                    if (ImGui::DragFloat3("旋转", rotation, 1.0f, -360.0f, 360.0f, "%.1f°"))
                    {
                        item.rotX = rotation[0];
                        item.rotY = rotation[1];
                        item.rotZ = rotation[2];
                        benchDirty_ = true;
                    }
                    float scale[3] = {item.scale, item.scaleY, item.scaleZ};
                    if (ImGui::DragFloat3("缩放", scale, 0.02f, 0.001f, 100.0f, "%.3f"))
                    {
                        item.scale = scale[0];
                        item.scaleY = scale[1];
                        item.scaleZ = scale[2];
                        benchDirty_ = true;
                    }
                    if (item.hasColor && ImGui::ColorEdit4("颜色", item.color))
                    {
                        benchDirty_ = true;
                    }
                    if (DrawBenchItemParameters(item))
                    {
                        benchDirty_ = true;
                    }
                    if (ImGui::SmallButton(ICON_FA_COPY " 复制节点"))
                    {
                        duplicateInstanceRequest = segment.instanceIndex;
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton(ICON_FA_TRASH " 删除节点"))
                    {
                        removeInstanceRequest = segment.instanceIndex;
                    }
                    ImGui::Separator();
                }
                if (document_.IsSwitchable(index))
                {
                    if (segment.disabled)
                    {
                        if (ImGui::SmallButton(ICON_FA_EYE " 启用"))
                        {
                            toggleRequest = static_cast<int>(index);
                            toggleTo = false;
                        }
                    }
                    else if (ImGui::SmallButton(ICON_FA_EYE_SLASH " 关闭"))
                    {
                        toggleRequest = static_cast<int>(index);
                        toggleTo = true;
                    }
                    if (segment.kind == EScadSegmentKind::Source)
                    {
                        ImGui::SameLine();
                        if (segment.explodedInstances > 0)
                        {
                            if (ImGui::SmallButton(ICON_FA_ARROWS_ROTATE " 撤销展开"))
                            {
                                collapseRequest = static_cast<int>(index);
                            }
                        }
                        else if (!segment.disabled && ImGui::SmallButton(ICON_FA_CODE_BRANCH " 关闭并展开为实例"))
                        {
                            explodeRequest = static_cast<int>(index);
                        }
                        if (ImGui::IsItemHovered())
                        {
                            ImGui::SetTooltip("求值这一条结构，把它产生的 Kit 实例写进同一个文件，"
                                              "并用 OpenSCAD 的 * 修饰符关闭原语句。\n"
                                              "其余源码结构不受影响。");
                        }
                    }
                }
                else
                {
                    ImGui::TextDisabled("该节点由所属编辑器管理（地形面板或 module 定义），不能在这里开关。");
                }
                ImGui::Unindent(12.0f);
            }
            ImGui::PopID();
        }
        if (document_.Segments().empty())
        {
            ImGui::TextDisabled("打开一个场景后，这里会列出它的全部顶层节点。");
        }
        ImGui::EndChild();
        }

        ImGui::BeginChild("##structure_inspector", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
        if (selectedSegment_ < 0 || selectedSegment_ >= static_cast<int>(document_.Segments().size()))
        {
            ImGui::TextDisabled("选择左侧 Outliner 中的节点以查看和编辑细节。");
        }
        else
        {
            const FScadSceneSegment& selectedSegment = document_.Segments()[selectedSegment_];
            ImGui::Text("%s", selectedSegment.kind == EScadSegmentKind::Source && selectedSegment.name == "lay_scatter"
                                  ? "布局过程算子"
                                  : ScadSegmentKindLabel(selectedSegment.kind));
            ImGui::TextDisabled("%s", selectedSegment.label.c_str());
            ImGui::Separator();

            if (selectedSegment.kind == EScadSegmentKind::Instance && selectedSegment.instanceIndex >= 0 &&
                selectedSegment.instanceIndex < static_cast<int>(Bench().size()))
            {
                FBenchItem& item = Bench()[selectedSegment.instanceIndex];
                float position[3] = {item.x, item.y, item.z};
                if (ImGui::DragFloat3("位置", position, 0.5f))
                {
                    item.x = position[0];
                    item.y = position[1];
                    item.z = position[2];
                    benchDirty_ = true;
                }
                float rotation[3] = {item.rotX, item.rotY, item.rotZ};
                if (ImGui::DragFloat3("旋转", rotation, 1.0f, -360.0f, 360.0f, "%.1f°"))
                {
                    item.rotX = rotation[0];
                    item.rotY = rotation[1];
                    item.rotZ = rotation[2];
                    benchDirty_ = true;
                }
                float scale[3] = {item.scale, item.scaleY, item.scaleZ};
                if (ImGui::DragFloat3("缩放", scale, 0.02f, 0.001f, 100.0f, "%.3f"))
                {
                    item.scale = scale[0];
                    item.scaleY = scale[1];
                    item.scaleZ = scale[2];
                    benchDirty_ = true;
                }
                if (item.hasColor && ImGui::ColorEdit4("颜色", item.color))
                {
                    benchDirty_ = true;
                }
                if (DrawBenchItemParameters(item))
                {
                    benchDirty_ = true;
                }
                if (ImGui::Button(ICON_FA_COPY " 复制节点"))
                {
                    duplicateInstanceRequest = selectedSegment.instanceIndex;
                }
                ImGui::SameLine();
                if (ImGui::Button(ICON_FA_TRASH " 删除节点"))
                {
                    removeInstanceRequest = selectedSegment.instanceIndex;
                }
            }
            else if ((selectedSegment.kind == EScadSegmentKind::TerrainRule && selectedSegment.ruleIndex >= 0) ||
                     (selectedSegment.kind == EScadSegmentKind::Terrain && terrainSelectionIsRule_))
            {
                const int terrainRuleIndex = selectedSegment.kind == EScadSegmentKind::TerrainRule
                    ? selectedSegment.ruleIndex
                    : selectedTerrainRule_;
                if (DrawTerrainRuleDetails(terrainRuleIndex))
                {
                    MarkTerrainProcessDirty();
                }
                if (ImGui::Button(ICON_FA_COPY " 复制过程算子"))
                {
                    TerrainProcess().DuplicateRule(static_cast<size_t>(terrainRuleIndex));
                    selectedTerrainRule_ = static_cast<int>(TerrainProcess().Rules().size()) - 1;
                    terrainSelectionIsRule_ = true;
                    MarkTerrainProcessDirty();
                }
                ImGui::SameLine();
                if (ImGui::Button(ICON_FA_TRASH " 删除过程算子"))
                {
                    TerrainProcess().RemoveRule(static_cast<size_t>(terrainRuleIndex));
                    terrainSelectionIsRule_ = false;
                    selectedSegment_ = -1;
                    MarkTerrainProcessDirty();
                }
            }
            else if (selectedSegment.kind == EScadSegmentKind::Terrain)
            {
                Assets::Scad::FTerrainSpec& terrain = TerrainProcess().Terrain();
                if (!terrainSelectionIsRule_ && selectedTerrainFeature_ >= 0 &&
                    selectedTerrainFeature_ < static_cast<int>(terrain.features.size()))
                {
                    if (DrawTerrainFeatureDetails(selectedTerrainFeature_))
                    {
                        MarkTerrainProcessDirty();
                    }
                    if (ImGui::Button(ICON_FA_TRASH " 删除 Feature"))
                    {
                        terrain.features.erase(terrain.features.begin() + selectedTerrainFeature_);
                        selectedTerrainFeature_ = -1;
                        MarkTerrainProcessDirty();
                    }
                }
                else
                {
                    ImGui::TextDisabled("TERR 基础信息在“过程”页编辑；这里管理其 Features 与 ter_* 算子。");
                }
                ImGui::Separator();
                if (ImGui::Button(ICON_FA_PLUS " 添加山峰 Feature"))
                {
                    Assets::Scad::FTerrainFeature feature;
                    feature.type = Assets::Scad::FTerrainFeature::EType::Mountain;
                    feature.radius = 20.0;
                    feature.height = 8.0;
                    terrain.features.push_back(std::move(feature));
                    selectedTerrainFeature_ = static_cast<int>(terrain.features.size()) - 1;
                    terrainSelectionIsRule_ = false;
                    MarkTerrainProcessDirty();
                }
                ImGui::SameLine();
                if (ImGui::Button(ICON_FA_PLUS " 添加 ter_place"))
                {
                    TerrainProcess().AddRule(ETerrainProcessRuleType::Place);
                    selectedTerrainRule_ = static_cast<int>(TerrainProcess().Rules().size()) - 1;
                    terrainSelectionIsRule_ = true;
                    MarkTerrainProcessDirty();
                }
            }
            else
            {
                if (selectedSegment.name == "lay_scatter")
                {
                    if (DrawLayScatterDetails(static_cast<size_t>(selectedSegment_)))
                    {
                        assemblySourceDirty_ = true;
                        benchDirty_ = true;
                        sourceStructureBoundsDirty_ = true;
                    }
                    ImGui::Separator();
                }
                if (selectedStructureBoundsValid_)
                {
                    ImGui::TextDisabled("视口中显示该结构的 AABB（不可移动）。");
                }
                else
                {
                    ImGui::TextDisabled("该源码节点不直接产生可编辑几何。");
                }
            }

            if (document_.IsSwitchable(static_cast<size_t>(selectedSegment_)))
            {
                ImGui::Separator();
                if (selectedSegment.disabled)
                {
                    if (ImGui::Button(ICON_FA_EYE " 启用"))
                    {
                        toggleRequest = selectedSegment_;
                        toggleTo = false;
                    }
                }
                else if (ImGui::Button(ICON_FA_EYE_SLASH " 关闭"))
                {
                    toggleRequest = selectedSegment_;
                    toggleTo = true;
                }
                if (selectedSegment.kind == EScadSegmentKind::Source)
                {
                    ImGui::SameLine();
                    if (selectedSegment.explodedInstances > 0)
                    {
                        if (ImGui::Button(ICON_FA_ARROWS_ROTATE " 撤销展开"))
                        {
                            collapseRequest = selectedSegment_;
                        }
                    }
                    else if (!selectedSegment.disabled && ImGui::Button(ICON_FA_CODE_BRANCH " 关闭并展开"))
                    {
                        explodeRequest = selectedSegment_;
                    }
                }
            }
        }
        ImGui::EndChild();

        if (removeInstanceRequest >= 0)
        {
            document_.RemoveInstance(removeInstanceRequest);
            ClearEditableSceneSelection();
            selectedSegment_ = -1;
            assemblySourceDirty_ = true;
            benchDirty_ = true;
        }
        if (duplicateInstanceRequest >= 0 && duplicateInstanceRequest < static_cast<int>(Bench().size()))
        {
            FBenchItem copy = Bench()[duplicateInstanceRequest];
            copy.x += kBenchGridStep * 0.5f;
            copy.y += kBenchGridStep * 0.5f;
            copy.runtimeNodeId = std::numeric_limits<uint32_t>::max();
            selectedBenchItem_ = document_.AddInstance(std::move(copy));
            selectedSegment_ = Bench()[selectedBenchItem_].segmentIndex;
            scrollToSelectedSegment_ = true;
            assemblySourceDirty_ = true;
            benchDirty_ = true;
        }

        if (toggleRequest >= 0 && document_.SetSegmentDisabled(static_cast<size_t>(toggleRequest), toggleTo))
        {
            sourceStructureBoundsDirty_ = true;
            assemblySourceDirty_ = true;
            statusLine_ = toggleTo ? "已关闭该结构（写回时加 * 修饰符）" : "已重新启用该结构";
            statusError_ = false;
            ReloadCurrentAssemblyPreview();
        }
        if (collapseRequest >= 0 && document_.CollapseSegment(static_cast<size_t>(collapseRequest)))
        {
            selectedBenchItem_ = -1;
            sourceStructureBoundsDirty_ = true;
            assemblySourceDirty_ = true;
            statusLine_ = "已撤销展开，恢复原结构";
            statusError_ = false;
            ReloadCurrentAssemblyPreview();
        }
        if (explodeRequest >= 0)
        {
            selectedSegment_ = explodeRequest;
            ExplodeSelectedSegment();
        }
    }

    void ScadLibraryInterface::RescanKits()
    {
        const std::string libDir = AuthoringPath("assets/scad/lib").string();
        bool fromCatalog = false;
        kits_ = LoadKits(libDir, fromCatalog);
        kitThumbnailSources_.clear();
        kitBrowserSelectedModule_ = -1;
        if (kits_.empty())
        {
            kitBrowserSelectedKit_ = -1;
        }
        else if (kitBrowserSelectedKit_ < 0 || kitBrowserSelectedKit_ >= static_cast<int>(kits_.size()))
        {
            kitBrowserSelectedKit_ = 0;
        }
        size_t moduleCount = 0;
        for (const FKitInfo& kit : kits_)
        {
            moduleCount += kit.modules.size();
        }
        statusLine_ = fmt::format("{} {} 个 kit / {} 个模块", fromCatalog ? "catalog.json:" : "文本扫描:", kits_.size(),
                                  moduleCount);
        statusError_ = kits_.empty();
        SPDLOG_INFO("[ScadLibrary] kits loaded from {}: {} kits / {} modules",
                    fromCatalog ? "catalog.json" : "text scan", kits_.size(), moduleCount);
        // Bench items keep kit indices; a rescan can reorder them, so re-anchor by path.
        for (FBenchItem& benchItem : Bench())
        {
            if (benchItem.kitIndex >= static_cast<int>(kits_.size()))
            {
                benchItem.kitIndex = -1;
            }
        }
        Bench().erase(std::remove_if(Bench().begin(), Bench().end(), [](const FBenchItem& b) { return b.kitIndex < 0; }),
                     Bench().end());

        // Character designer feeds from the kit_char parts library.
        kitCharIndex_ = -1;
        for (size_t k = 0; k < kits_.size(); ++k)
        {
            if (kits_[k].name == "kit_char")
            {
                kitCharIndex_ = static_cast<int>(k);
                break;
            }
        }
        designer_.SetKit(kitCharIndex_ >= 0 ? &kits_[kitCharIndex_] : nullptr);
        designerEverLoaded_ = false;

        // The watch compares against this snapshot; keep it in sync so manual
        // rescans (F5 / AI save) do not re-trigger the auto-reload path.
        RefreshKitWatchBaseline();
    }

    void ScadLibraryInterface::RefreshKitWatchBaseline()
    {
        kitWatchStamps_.clear();
        kitWatchStamps_.reserve(kits_.size());
        std::error_code ec;
        for (const FKitInfo& kit : kits_)
        {
            const std::filesystem::file_time_type stamp =
                std::filesystem::last_write_time(std::filesystem::path(kit.filePath), ec);
            if (!ec)
            {
                kitWatchStamps_.emplace_back(kit.filePath, stamp);
            }
            ec.clear();
        }
    }

    void ScadLibraryInterface::RefreshAssemblyWatchBaseline()
    {
        assemblyWatchPath_ = openedAssemblyPath_;
        assemblyWatchChanged_ = false;
        assemblyWatchStampValid_ = false;
        if (assemblyWatchPath_.empty())
        {
            return;
        }

        std::error_code ec;
        assemblyWatchStamp_ = std::filesystem::last_write_time(assemblyWatchPath_, ec);
        assemblyWatchStampValid_ = !ec;
    }

    void ScadLibraryInterface::TickKitFileWatch(double deltaSeconds)
    {
        kitWatchElapsed_ += deltaSeconds;
        if (kitWatchElapsed_ < kKitWatchPollIntervalSeconds)
        {
            return;
        }
        kitWatchElapsed_ = 0.0;

        // The currently opened assembly is authored outside ScadLibrary quite
        // often (for example by ScadStudio or an editor). Check its timestamp
        // on the main thread alongside the kit worker poll. The file metadata
        // query is tiny, while the actual scene parse/rebuild remains deferred
        // to Render() where it is safe to touch UI and engine state.
        if (!assemblyWatchPath_.empty())
        {
            std::error_code ec;
            const std::filesystem::file_time_type stamp =
                std::filesystem::last_write_time(assemblyWatchPath_, ec);
            if (ec)
            {
                if (assemblyWatchStampValid_)
                {
                    assemblyWatchStampValid_ = false;
                    assemblyWatchChanged_ = true;
                    kitWatchPending_ = true;
                    kitWatchReloadAt_ = std::chrono::steady_clock::now() + kKitWatchReloadDebounce;
                }
            }
            else if (!assemblyWatchStampValid_ || stamp != assemblyWatchStamp_)
            {
                assemblyWatchStamp_ = stamp;
                assemblyWatchStampValid_ = true;
                assemblyWatchChanged_ = true;
                kitWatchPending_ = true;
                kitWatchReloadAt_ = std::chrono::steady_clock::now() + kKitWatchReloadDebounce;
            }
        }

        // Do not submit another gather while a detected change is waiting for
        // the debounced rescan. Its snapshot would use the old baseline and
        // report the same change again after RescanKits updates the stamps.
        if (kitWatchTaskInFlight_ || kitWatchPending_)
        {
            return;
        }

        struct FKitWatchTaskContext
        {
            FKitWatchGatherResult result;
        };
        auto context = std::make_shared<FKitWatchTaskContext>();

        std::vector<std::string> watchPaths;
        watchPaths.reserve(kits_.size());
        for (const FKitInfo& kit : kits_)
        {
            watchPaths.push_back(kit.filePath);
        }
        const std::vector<std::pair<std::string, std::filesystem::file_time_type>> prevStamps = kitWatchStamps_;
        const std::string libDir = AuthoringPath("assets/scad/lib").string();

        kitWatchTaskInFlight_ = true;
        Tasks::TaskCoordinator::GetInstance()->AddTask(
            [context, watchPaths = std::move(watchPaths), prevStamps = std::move(prevStamps), libDir](Tasks::ResTask& task)
            {
                (void)task;
                context->result = GatherKitFileChanges(watchPaths, prevStamps, libDir);
            },
            [this, context](Tasks::ResTask& task)
            {
                (void)task;
                FinishKitFileChanges(std::move(context->result.changedPaths), context->result.treeChanged);
            },
            0,
            "ScadLibrary kit watch");
    }

    void ScadLibraryInterface::FinishKitFileChanges(std::vector<std::string> changedPaths, bool treeChanged)
    {
        kitWatchTaskInFlight_ = false;
        if (changedPaths.empty() && !treeChanged)
        {
            return;
        }

        // Resolve the "preview kit changed" flag on the main thread while the
        // current kit indices are still valid (the tree has not been rescanned yet).
        kitWatchChangedPreviewKit_ = false;
        if (selectedKit_ >= 0 && selectedKit_ < static_cast<int>(kits_.size()) && !selectedModule_.empty())
        {
            const std::string& previewPath = kits_[selectedKit_].filePath;
            kitWatchChangedPreviewKit_ =
                std::find(changedPaths.begin(), changedPaths.end(), previewPath) != changedPaths.end();
        }

        // In Scene Assembly the visible preview is usually bench.scad (or a
        // generated terrain/source preview), not preview.scad. Track whether
        // any kit used by that current assembly changed so the visible scene
        // can be rebuilt as well.
        kitWatchChangedAssembly_ = false;
        if (workspaceMode_ == EWorkspaceMode::SceneAssembly && !modulePreviewActive_)
        {
            const auto usesKitPath = [&](const std::string& changedPath)
            {
                const std::string changedKitName = std::filesystem::path(changedPath).stem().string();
                if (std::find(openedAssemblyKits_.begin(), openedAssemblyKits_.end(), changedKitName) !=
                    openedAssemblyKits_.end())
                {
                    return true;
                }
                for (const FBenchItem& benchItem : Bench())
                {
                    if (benchItem.kitIndex >= 0 && benchItem.kitIndex < static_cast<int>(kits_.size()) &&
                        kits_[benchItem.kitIndex].filePath == changedPath)
                    {
                        return true;
                    }
                }
                return false;
            };
            kitWatchChangedAssembly_ =
                std::any_of(changedPaths.begin(), changedPaths.end(), usesKitPath);
        }

        kitWatchFilesChanged_ = true;
        kitWatchPending_ = true;
        kitWatchReloadAt_ = std::chrono::steady_clock::now() + kKitWatchReloadDebounce;
    }

    void ScadLibraryInterface::PollKitFileChanges()
    {
        const bool assemblyPreviewVisible = workspaceMode_ == EWorkspaceMode::SceneAssembly && !modulePreviewActive_;
        const bool modulePreviewVisible = modulePreviewActive_;
        if (!kitWatchPending_ &&
            !(assemblyPreviewVisible && (assemblyWatchChanged_ || kitWatchChangedAssembly_)) &&
            !(modulePreviewVisible && kitWatchChangedPreviewKit_))
        {
            return;
        }
        // Same deferred-reload guard as bench/designer: never rebuild the scene
        // while the user is dragging a widget.
        if (ImGui::IsAnyItemActive())
        {
            return;
        }
        if (std::chrono::steady_clock::now() < kitWatchReloadAt_)
        {
            return;
        }
        kitWatchPending_ = false;

        const bool kitFilesChanged = kitWatchFilesChanged_;
        const bool previewChanged = kitWatchChangedPreviewKit_;
        const bool assemblyKitChanged = kitWatchChangedAssembly_;
        const bool assemblyFileChanged = assemblyWatchChanged_;
        kitWatchFilesChanged_ = false;
        kitWatchChangedPreviewKit_ = false;
        kitWatchChangedAssembly_ = false;

        bool refreshedAssembly = false;
        if (assemblyPreviewVisible && assemblyFileChanged && !openedAssemblyPath_.empty())
        {
            // Re-read the source into the editor model first. This covers
            // direct edits to the currently opened scene, while the preserve
            // flag keeps the camera controller untouched when the engine
            // receives the replacement scene.
            refreshedAssembly = OpenAssembly(openedAssemblyPath_, true);
            assemblyWatchChanged_ = false;
        }
        else if (assemblyPreviewVisible && assemblyKitChanged)
        {
            ReloadCurrentAssemblyPreview();
            refreshedAssembly = true;
        }

        // Reload the preview first while the old kit indices are still valid,
        // keeping the camera (and the AI editing context) intact.
        if (!refreshedAssembly && modulePreviewVisible && previewChanged &&
            selectedKit_ >= 0 && selectedKit_ < static_cast<int>(kits_.size()) &&
            !selectedModule_.empty())
        {
            preserveCameraOnNextSceneLoad_ = true;
            WriteAndLoad("preview.scad", BuildModulePreviewSource(selectedKit_, selectedModule_));
        }

        // RescanKits resets statusLine_, so report the auto-refresh afterwards.
        if (kitFilesChanged)
        {
            RescanKits();
        }

        SPDLOG_INFO("[ScadLibrary] source file(s) changed externally, auto-refreshed (assembly reload: {}, "
                    "module reload: {})",
                    refreshedAssembly, !refreshedAssembly && modulePreviewVisible && previewChanged);

        if (refreshedAssembly)
        {
            statusLine_ = assemblyFileChanged ? "当前场景文件已变化，已自动刷新预览" : "场景依赖 Kit 已变化，已自动刷新预览";
        }
        else if (previewChanged && modulePreviewVisible)
        {
            statusLine_ = fmt::format("kit 文件已变化，已自动刷新预览 {}", selectedModule_);
        }
        else if (kitFilesChanged)
        {
            statusLine_ = "kit 文件已变化，资源库已自动刷新";
        }
        statusError_ = false;
    }

    void ScadLibraryInterface::RescanAssemblies()
    {
        const std::filesystem::path scadRoot = AuthoringPath("assets/scad");
        assemblies_.clear();
        std::error_code ec;
        for (std::filesystem::recursive_directory_iterator
                 it(scadRoot, std::filesystem::directory_options::skip_permission_denied, ec),
             end;
             it != end; it.increment(ec))
        {
            if (ec)
            {
                ec.clear();
                continue;
            }
            const std::filesystem::path relative = it->path().lexically_relative(scadRoot);
            const std::optional<EScadSceneFolder> folder = SceneFolderFromRelativePath(relative);
            if (it->is_directory())
            {
                if (!folder && std::distance(relative.begin(), relative.end()) == 1)
                {
                    it.disable_recursion_pending();
                }
                continue;
            }
            if (!folder || !it->is_regular_file() || it->path().extension() != ".scad")
            {
                continue;
            }
            const std::string source = ReadAssemblyTextFile(it->path());
            std::vector<std::string> dependencies = FindKitDependencies(source);
            FSceneAssemblyInfo info;
            info.relativePath = (std::filesystem::path("assets/scad") / relative).generic_string();
            info.absolutePath = std::filesystem::absolute(it->path(), ec).string();
            info.kitDependencies = std::move(dependencies);
            info.folder = *folder;
            // Text-only hints for the browser badge. The authoritative
            // per-statement classification happens when the scene is opened.
            info.hasTerrain = source.find("gk_terrain") != std::string::npos;
            info.hasProcRules = source.find("ter_place") != std::string::npos ||
                source.find("ter_snap") != std::string::npos || source.find("ter_along") != std::string::npos ||
                source.find("ter_scatter") != std::string::npos;
            info.hasFreeStructure = source.find("module ") != std::string::npos ||
                source.find("for (") != std::string::npos || source.find("for(") != std::string::npos;
            const auto secondPart = std::next(relative.begin());
            info.generated = secondPart != relative.end() && secondPart->string() == "generated";
            info.categoryKey = SceneCategoryKeyFromRelativePath(relative);
            info.categoryLabel = SceneCategoryLabelFromKey(info.categoryKey);
            assemblies_.push_back(std::move(info));
        }
        std::sort(assemblies_.begin(), assemblies_.end(),
                  [](const FSceneAssemblyInfo& a, const FSceneAssemblyInfo& b)
                  {
                      if (a.folder != b.folder)
                      {
                          return static_cast<int>(a.folder) < static_cast<int>(b.folder);
                      }
                      if (a.generated != b.generated)
                      {
                          return !a.generated;
                      }
                      return a.relativePath < b.relativePath;
                  });
        selectedAssembly_ = -1;
        for (int index = 0; index < static_cast<int>(assemblies_.size()); ++index)
        {
            if (!openedAssemblyPath_.empty() &&
                std::filesystem::path(assemblies_[index].absolutePath) == std::filesystem::path(openedAssemblyPath_))
            {
                selectedAssembly_ = index;
                break;
            }
        }
    }

    bool ScadLibraryInterface::EnsureKitThumbnailSource(
        const int kitIndex,
        const int moduleIndex,
        std::string& outPath,
        uint64_t& outHash)
    {
        if (kitIndex < 0 || kitIndex >= static_cast<int>(kits_.size()) || moduleIndex < 0 ||
            moduleIndex >= static_cast<int>(kits_[kitIndex].modules.size()))
        {
            return false;
        }

        const FKitInfo& kit = kits_[kitIndex];
        const FKitModuleInfo& module = kit.modules[moduleIndex];
        const std::string key = kit.filePath + "#" + module.name;
        const std::string source = BuildModulePreviewSource(kitIndex, module.name);
        const uint64_t sourceHash = Fnv1a64(source);
        auto cached = kitThumbnailSources_.find(key);
        if (cached == kitThumbnailSources_.end() || cached->second.sourceHash != sourceHash ||
            cached->second.path.empty())
        {
            const std::string fileName = fmt::format("kit_thumbnail_{}_{}.scad",
                                                      PreviewFileToken(kit.name), PreviewFileToken(module.name));
            std::string absolutePath;
            if (!WriteWorkspaceFile(fileName, source, absolutePath))
            {
                return false;
            }
            cached = kitThumbnailSources_.insert_or_assign(
                key, FKitThumbnailSource{std::move(absolutePath), sourceHash}).first;
        }

        std::error_code ec;
        const auto stamp = std::filesystem::last_write_time(kit.filePath, ec);
        const std::string stampText = ec ? "missing" : fmt::format("{}", stamp.time_since_epoch().count());
        outPath = cached->second.path;
        // The kit file timestamp is part of the thumbnail identity. A changed
        // kit therefore invalidates the rendered image even though the tiny
        // wrapper source itself is unchanged.
        outHash = Fnv1a64(fmt::format("{}|{}|{}", sourceHash, kit.filePath, stampText));
        return true;
    }

    void ScadLibraryInterface::PreviewModule(int kitIndex, const std::string& moduleName)
    {
        if (kitIndex < 0 || kitIndex >= static_cast<int>(kits_.size()))
        {
            return;
        }
        // A module preview replaces the assembly scene. Do not leave an old
        // assembly item selected, otherwise the gizmo/focus code can resolve
        // that stale item against the newly loaded preview scene.
        ClearEditableSceneSelection();
        selectedKit_ = kitIndex;
        selectedModule_ = moduleName;
        aiKitContextActive_ = true;
        aiController_->Reset();
        rigPreview_.SetActive(false);
        modulePreviewActive_ = true;

        if (WriteAndLoad("preview.scad", BuildModulePreviewSource(kitIndex, moduleName)))
        {
            statusLine_ = fmt::format("预览 {}", moduleName);
            statusError_ = false;
        }
    }

    std::string ScadLibraryInterface::BuildModulePreviewSource(int kitIndex, const std::string& moduleName) const
    {
        std::string source;
        source += "// ScadLibrary module preview\n";
        source += fmt::format("$fn = {};\n", fnSegments_);
        std::string usePath = kits_[kitIndex].filePath;
        std::replace(usePath.begin(), usePath.end(), '\\', '/');
        source += fmt::format("use <{}>\n\n", usePath);
        source += fmt::format("{}();\n", moduleName);
        return source;
    }

    void ScadLibraryInterface::AddToBench(int kitIndex, const std::string& moduleName)
    {
        if (kitIndex < 0 || kitIndex >= static_cast<int>(kits_.size()))
        {
            return;
        }
        aiKitContextActive_ = false;

        // Row cursor: advance by the module's catalog footprint so city blocks
        // and hand-scale props both land side by side without overlap.
        float step = kBenchGridStep;
        for (const FKitModuleInfo& moduleInfo : kits_[kitIndex].modules)
        {
            if (moduleInfo.name == moduleName && moduleInfo.hasMetrics)
            {
                step = std::clamp(std::max(moduleInfo.footprintX, moduleInfo.footprintY) + 3.0f, 4.0f, 80.0f);
                break;
            }
        }
        if (Bench().empty())
        {
            benchCursorX_ = 0.0f;
            benchCursorY_ = 0.0f;
            benchRowDepth_ = 0.0f;
            benchColCount_ = 0;
        }
        if (benchColCount_ >= kBenchGridColumns)
        {
            benchCursorX_ = 0.0f;
            benchCursorY_ += benchRowDepth_;
            benchRowDepth_ = 0.0f;
            benchColCount_ = 0;
        }
        const glm::vec3 placement(benchCursorX_, benchCursorY_, 0.0f);
        benchCursorX_ += step;
        benchRowDepth_ = std::max(benchRowDepth_, step);
        benchColCount_++;

        AddToBenchAt(kitIndex, moduleName, placement);
    }

    void ScadLibraryInterface::AddToBenchAt(
        const int kitIndex,
        const std::string& moduleName,
        const glm::vec3& scadPosition)
    {
        if (kitIndex < 0 || kitIndex >= static_cast<int>(kits_.size()) ||
            !std::any_of(kits_[kitIndex].modules.begin(), kits_[kitIndex].modules.end(),
                         [&](const FKitModuleInfo& module) { return module.name == moduleName; }))
        {
            statusLine_ = "无法放置：Kit 模块已不存在，请先刷新资源库";
            statusError_ = true;
            return;
        }

        aiKitContextActive_ = false;
        FBenchItem benchItem;
        benchItem.kitIndex = kitIndex;
        benchItem.moduleName = moduleName;
        benchItem.x = scadPosition.x;
        benchItem.y = scadPosition.y;
        benchItem.z = scadPosition.z;
        selectedBenchItem_ = document_.AddInstance(std::move(benchItem));
        selectedSegment_ = Bench()[selectedBenchItem_].segmentIndex;
        scrollToSelectedSegment_ = true;
        assemblyEditorTab_ = 3;
        benchDirty_ = true;
        statusLine_ = fmt::format("已放置 {} · 可继续拖拽调整位置", moduleName);
        statusError_ = false;
    }

    int ScadLibraryInterface::FindKitIndex(const std::string& moduleName) const
    {
        const auto hasModule = [&](int candidate)
        {
            return std::any_of(kits_[candidate].modules.begin(), kits_[candidate].modules.end(),
                               [&](const FKitModuleInfo& module) { return module.name == moduleName; });
        };
        // Prefer a kit the scene already depends on, so a module name shared by
        // two kits resolves to the one the file actually uses.
        for (const std::string& dependency : openedAssemblyKits_)
        {
            for (int candidate = 0; candidate < static_cast<int>(kits_.size()); ++candidate)
            {
                if (kits_[candidate].name == dependency && hasModule(candidate))
                {
                    return candidate;
                }
            }
        }
        for (int candidate = 0; candidate < static_cast<int>(kits_.size()); ++candidate)
        {
            if (hasModule(candidate))
            {
                return candidate;
            }
        }
        return -1;
    }

    bool ScadLibraryInterface::IsKitModuleName(const std::string& moduleName) const
    {
        return FindKitIndex(moduleName) >= 0;
    }

    std::vector<std::string> ScadLibraryInterface::RequiredKitUsePaths(const std::filesystem::path& outputPath) const
    {
        std::vector<std::string> paths;
        for (const FBenchItem& item : Bench())
        {
            if (item.removed || item.kitIndex < 0 || item.kitIndex >= static_cast<int>(kits_.size()))
            {
                continue;
            }
            // Kits the file already declares are skipped by name. Matching on
            // the written path alone would miss them, because the file spells
            // them relative to itself and this list may be absolute.
            if (std::find(openedAssemblyKits_.begin(), openedAssemblyKits_.end(), kits_[item.kitIndex].name) !=
                openedAssemblyKits_.end())
            {
                continue;
            }
            std::filesystem::path usePath = kits_[item.kitIndex].filePath;
            if (!outputPath.empty())
            {
                const std::filesystem::path relative = usePath.lexically_relative(outputPath.parent_path());
                if (!relative.empty())
                {
                    usePath = relative;
                }
            }
            const std::string generic = usePath.generic_string();
            if (std::find(paths.begin(), paths.end(), generic) == paths.end())
            {
                paths.push_back(generic);
            }
        }
        return paths;
    }

    bool ScadLibraryInterface::ReparseDocument(const std::string& source, const std::string& documentPath,
                                               bool reevaluate)
    {
        terrainProcessWarnings_.clear();

        Assets::Scad::FScadSourceIndex index;
        std::string error;
        if (!Assets::Scad::BuildScadSourceIndex(source, index, error))
        {
            statusLine_ = fmt::format("SCAD 解析失败: {}", error);
            statusError_ = true;
            return false;
        }

        // The statement index addresses this file's bytes; the evaluated
        // top-level scope comes from the full use/include closure so terrain
        // specs and variables defined in a kit still resolve.
        std::map<std::string, Assets::Scad::Value> variables = documentVariables_;
        if (reevaluate && !documentPath.empty())
        {
            Assets::Scad::ScadProgram program;
            std::string programError;
            if (Assets::Scad::LoadScadProgram(documentPath, program, programError))
            {
                Assets::ScadLoadOptions options;
                Assets::Scad::SceneEvalResult evaluated;
                if (Assets::Scad::ScadEvaluator::EvaluateScene(program.mainTopLevel, program.modules, program.functions,
                                                               options, evaluated, programError))
                {
                    variables = std::move(evaluated.topLevelVariables);
                }
                else
                {
                    SPDLOG_DEBUG("[ScadLibrary] evaluation for {} failed: {}", documentPath, programError);
                }
            }
            else
            {
                SPDLOG_DEBUG("[ScadLibrary] program load for {} failed: {}", documentPath, programError);
            }
        }

        documentVariables_ = variables;
        openedAssemblyKits_ = FindKitDependencies(source);
        std::vector<std::string> warnings;
        document_.Parse(source, index, variables,
                        [this](const std::string& moduleName) { return IsKitModuleName(moduleName); }, warnings);
        terrainProcessWarnings_ = std::move(warnings);

        for (FBenchItem& item : document_.Instances())
        {
            item.kitIndex = FindKitIndex(item.CatalogModuleName());
            item.evaluated = true;
        }
        assemblySource_ = document_.Source();
        sourceBufferDirty_ = false;
        terrainProcessDirty_ = false;
        benchDirty_ = false;
        selectedBenchItem_ = -1;
        selectedSegment_ = -1;
        ClearSelectedStructureBounds();
        sourceStructureBounds_.clear();
        sourceStructureBoundsDirty_ = true;
        terrainFeatureOverlayCacheKey_.clear();
        terrainFeatureOverlayData_.reset();
        terrainFeatureDragging_ = false;
        selectedTerrainFeature_ = std::clamp(selectedTerrainFeature_, 0,
                                             std::max(0, static_cast<int>(TerrainProcess().Terrain().features.size()) - 1));
        selectedTerrainRule_ =
            std::clamp(selectedTerrainRule_, 0, std::max(0, static_cast<int>(TerrainProcess().Rules().size()) - 1));
        return true;
    }

    std::vector<FBenchItem> ScadLibraryInterface::EvaluateSegmentInstances(size_t segmentIndex,
                                                                          std::string& outError) const
    {
        std::vector<FBenchItem> produced;
        if (segmentIndex >= document_.Segments().size())
        {
            outError = "无效的节点索引";
            return produced;
        }
        const FScadSceneSegment& segment = document_.Segments()[segmentIndex];

        // Evaluate the whole file: a structure normally depends on variables
        // and modules declared before it, so a fragment cannot be evaluated on
        // its own. The nodes are then attributed back by call-site line.
        std::string workspacePath;
        const std::string previewSource = BuildAssemblyPreviewSource();
        if (!const_cast<ScadLibraryInterface*>(this)->WriteWorkspaceFile("segment_eval.scad", previewSource,
                                                                        workspacePath))
        {
            outError = "无法写入临时求值文件";
            return produced;
        }

        Assets::Scad::ScadProgram program;
        std::string error;
        if (!Assets::Scad::LoadScadProgram(workspacePath, program, error))
        {
            outError = fmt::format("求值失败: {}", error);
            return produced;
        }
        Assets::ScadLoadOptions options;
        Assets::Scad::SceneEvalResult result;
        if (!Assets::Scad::ScadEvaluator::EvaluateScene(program.mainTopLevel, program.modules, program.functions,
                                                        options, result, error))
        {
            outError = fmt::format("求值失败: {}", error);
            return produced;
        }

        constexpr size_t maxEditableObjects = 5000;
        const auto collect = [&](auto&& self, const Assets::Scad::SceneNode& node, const glm::dmat4& parent) -> void
        {
            if (produced.size() >= maxEditableObjects)
            {
                return;
            }
            const glm::dmat4 world = parent * node.localTransform;
            const int kitIndex = FindKitIndex(node.name);
            const bool layoutContainer = node.name.starts_with("lay_");
            if (kitIndex >= 0 && !layoutContainer)
            {
                glm::dvec3 scale(1.0);
                glm::dquat orientation(1.0, 0.0, 0.0, 0.0);
                glm::dvec3 translation(0.0);
                glm::dvec3 skew(0.0);
                glm::dvec4 perspective(0.0);
                if (!glm::decompose(world, scale, orientation, translation, skew, perspective))
                {
                    return;
                }
                const glm::dvec3 euler = glm::degrees(glm::eulerAngles(orientation));

                FBenchItem item;
                item.kitIndex = kitIndex;
                item.moduleName = node.name;
                item.x = static_cast<float>(translation.x);
                item.y = static_cast<float>(translation.y);
                item.z = static_cast<float>(translation.z);
                item.rotX = static_cast<float>(euler.x);
                item.rotY = static_cast<float>(euler.y);
                item.rotZ = static_cast<float>(euler.z);
                item.scale = static_cast<float>(scale.x);
                item.scaleY = static_cast<float>(scale.y);
                item.scaleZ = static_cast<float>(scale.z);
                item.hasColor = node.hasCallColor;
                item.color[0] = static_cast<float>(node.callColor.r);
                item.color[1] = static_cast<float>(node.callColor.g);
                item.color[2] = static_cast<float>(node.callColor.b);
                item.color[3] = static_cast<float>(node.callColor.a);
                item.evaluated = true;
                item.sourceLine = node.sourceLine;
                item.runtimeNodeId = static_cast<uint32_t>(node.instanceId);

                std::string arguments;
                for (const auto& [name, value] : node.parameters)
                {
                    if (!arguments.empty())
                    {
                        arguments += ", ";
                    }
                    arguments += fmt::format("{} = {}", name, ScadValueSource(value));
                }
                std::snprintf(item.args, sizeof(item.args), "%s", arguments.c_str());
                produced.push_back(std::move(item));
                return;
            }

            for (const Assets::Scad::SceneNode& child : node.children)
            {
                self(self, child, world);
            }
        };

        for (const Assets::Scad::SceneNode& root : result.roots)
        {
            // Every node a top-level statement creates has its call site inside
            // that statement's own line range, so the roots partition cleanly.
            if (root.sourceLine < segment.line || root.sourceLine > segment.endLine)
            {
                continue;
            }
            collect(collect, root, glm::dmat4(1.0));
        }

        if (produced.empty())
        {
            outError = "该结构没有产生可编辑的 Kit 实例（只有图元或未知模块时无法展开）";
        }
        return produced;
    }

    bool ScadLibraryInterface::ComputeSegmentWorldBounds(size_t segmentIndex, glm::vec3& outMin,
                                                          glm::vec3& outMax)
    {
        if (segmentIndex >= document_.Segments().size())
        {
            return false;
        }
        if (!RefreshSourceStructureBounds() || segmentIndex >= sourceStructureBounds_.size() ||
            !sourceStructureBounds_[segmentIndex].valid)
        {
            return false;
        }

        outMin = sourceStructureBounds_[segmentIndex].min;
        outMax = sourceStructureBounds_[segmentIndex].max;
        return true;
    }

    bool ScadLibraryInterface::RefreshSourceStructureBounds()
    {
        if (!sourceStructureBoundsDirty_ && sourceStructureBounds_.size() == document_.Segments().size())
        {
            return true;
        }

        std::string workspacePath;
        if (!WriteWorkspaceFile("segment_bounds.scad", BuildAssemblyPreviewSource(), workspacePath))
        {
            return false;
        }

        Assets::Scad::ScadProgram program;
        std::string error;
        if (!Assets::Scad::LoadScadProgram(workspacePath, program, error))
        {
            return false;
        }
        Assets::ScadLoadOptions options;
        Assets::Scad::SceneEvalResult result;
        if (!Assets::Scad::ScadEvaluator::EvaluateScene(program.mainTopLevel, program.modules, program.functions,
                                                        options, result, error))
        {
            return false;
        }

        sourceStructureBounds_.assign(document_.Segments().size(), {});
        std::vector<glm::dvec3> minBounds(document_.Segments().size(),
                                          glm::dvec3(std::numeric_limits<double>::max()));
        std::vector<glm::dvec3> maxBounds(document_.Segments().size(),
                                          glm::dvec3(-std::numeric_limits<double>::max()));
        const glm::dmat4 scadToWorld = Assets::Scad::ScadToWorldBasis(1.0);
        const auto accumulate = [&](auto&& self, const Assets::Scad::SceneNode& node, const glm::dmat4& parent,
                                    const size_t sourceSegmentIndex) -> void
        {
            const glm::dmat4 world = parent * node.localTransform;
            for (const Assets::Scad::SceneMeshBucket& mesh : node.meshes)
            {
                for (const glm::dvec3& vertex : mesh.tris)
                {
                    const glm::dvec3 point = glm::dvec3(scadToWorld * world * glm::dvec4(vertex, 1.0));
                    minBounds[sourceSegmentIndex] = glm::min(minBounds[sourceSegmentIndex], point);
                    maxBounds[sourceSegmentIndex] = glm::max(maxBounds[sourceSegmentIndex], point);
                    sourceStructureBounds_[sourceSegmentIndex].valid = true;
                }
            }
            for (const Assets::Scad::SceneNode& child : node.children)
            {
                self(self, child, world, sourceSegmentIndex);
            }
        };

        for (const Assets::Scad::SceneNode& root : result.roots)
        {
            for (size_t sourceSegmentIndex = 0; sourceSegmentIndex < document_.Segments().size(); ++sourceSegmentIndex)
            {
                const FScadSceneSegment& segment = document_.Segments()[sourceSegmentIndex];
                if (segment.kind == EScadSegmentKind::Source && root.sourceLine >= segment.line &&
                    root.sourceLine <= segment.endLine)
                {
                    accumulate(accumulate, root, glm::dmat4(1.0), sourceSegmentIndex);
                    break;
                }
            }
        }
        for (size_t sourceSegmentIndex = 0; sourceSegmentIndex < sourceStructureBounds_.size(); ++sourceSegmentIndex)
        {
            if (sourceStructureBounds_[sourceSegmentIndex].valid)
            {
                sourceStructureBounds_[sourceSegmentIndex].min = glm::vec3(minBounds[sourceSegmentIndex]);
                sourceStructureBounds_[sourceSegmentIndex].max = glm::vec3(maxBounds[sourceSegmentIndex]);
            }
        }
        sourceStructureBoundsDirty_ = false;
        return true;
    }

    void ScadLibraryInterface::ExplodeSelectedSegment()
    {
        if (selectedSegment_ < 0 || selectedSegment_ >= static_cast<int>(document_.Segments().size()))
        {
            statusLine_ = "请先在“结构”页选择一个源码节点";
            statusError_ = true;
            return;
        }
        if (sourceBufferDirty_ && !ReparseDocument(assemblySource_, openedAssemblyPath_))
        {
            return;
        }
        const size_t segmentIndex = static_cast<size_t>(selectedSegment_);

        std::string error;
        std::vector<FBenchItem> produced = EvaluateSegmentInstances(segmentIndex, error);
        if (produced.empty())
        {
            statusLine_ = error;
            statusError_ = true;
            return;
        }
        if (!document_.ExplodeSegment(segmentIndex, std::move(produced), error))
        {
            statusLine_ = error;
            statusError_ = true;
            return;
        }
        assemblySourceDirty_ = true;
        benchDirty_ = true;
        assemblyEditorTab_ = 0;
        statusLine_ = fmt::format("已关闭 {} 并展开为 {} 个可编辑实例",
                                  document_.Segments()[segmentIndex].name,
                                  document_.Segments()[segmentIndex].explodedInstances);
        statusError_ = false;
        ReloadCurrentAssemblyPreview();
    }

    bool ScadLibraryInterface::OpenAssembly(const std::string& path, bool preserveCamera)
    {
        const std::filesystem::path scadRoot = AuthoringPath("assets/scad");
        std::filesystem::path sourcePath(path);
        if (!sourcePath.is_absolute())
        {
            const std::string generic = sourcePath.generic_string();
            constexpr std::string_view prefix = "assets/scad/";
            sourcePath = generic.starts_with(prefix) ? scadRoot / generic.substr(prefix.size()) : scadRoot / sourcePath;
        }
        std::error_code ec;
        sourcePath = std::filesystem::weakly_canonical(sourcePath, ec);
        if (ec || !IsPathWithin(sourcePath, scadRoot) || sourcePath.extension() != ".scad" ||
            !std::filesystem::is_regular_file(sourcePath, ec))
        {
            statusLine_ = fmt::format("无法打开场景: {}", path);
            statusError_ = true;
            return false;
        }
        const std::filesystem::path scadRelative = sourcePath.lexically_relative(scadRoot);
        if (!IsSceneAssemblyRelativePath(scadRelative))
        {
            statusLine_ = "assets/scad/lib 与 characters 下的文件是零件库与角色资产，不作为场景打开";
            statusError_ = true;
            return false;
        }
        const std::string source = ReadAssemblyTextFile(sourcePath);
        if (source.empty())
        {
            statusLine_ = fmt::format("场景为空或读取失败: {}", sourcePath.string());
            statusError_ = true;
            return false;
        }

        static const std::regex fnRegex(R"(\$fn\s*=\s*(\d+)\s*;)");
        std::smatch fnMatch;
        if (std::regex_search(source, fnMatch, fnRegex))
        {
            fnSegments_ = std::clamp(std::stoi(fnMatch[1].str()), 3, 128);
        }

        rigPreview_.SetActive(false);
        modulePreviewActive_ = false;
        openedAssemblyPath_ = sourcePath.string();
        documentVariables_.clear();
        aiKitContextActive_ = false;
        aiController_->Reset();
        benchCursorX_ = 0.0f;
        benchCursorY_ = 0.0f;
        benchRowDepth_ = 0.0f;
        benchColCount_ = 0;

        if (!ReparseDocument(source, openedAssemblyPath_))
        {
            return false;
        }
        assemblySourceDirty_ = false;

        const std::filesystem::path repoRoot = scadRoot.parent_path().parent_path();
        const std::string relativePath = sourcePath.lexically_relative(repoRoot).generic_string();
        std::snprintf(assemblyPathBuf_, sizeof(assemblyPathBuf_), "%s", relativePath.c_str());
        for (int index = 0; index < static_cast<int>(assemblies_.size()); ++index)
        {
            if (std::filesystem::path(assemblies_[index].absolutePath) == sourcePath)
            {
                selectedAssembly_ = index;
                break;
            }
        }
        RefreshAssemblyWatchBaseline();
        preserveCameraOnNextSceneLoad_ = preserveCamera;
        engine_.RequestLoadScene({.filename = openedAssemblyPath_});
        statusLine_ = fmt::format("已打开 {} · {} 个 Kit · {} 实例 / {} 源码结构{}", relativePath,
                                  openedAssemblyKits_.size(), Bench().size(), document_.SourceSegmentCount(),
                                  document_.HasTerrain() ? " / 含地形" : "");
        statusError_ = false;
        return true;
    }

    std::string ScadLibraryInterface::BuildAssemblyPreviewSource() const
    {
        // Preview and save go through the same writer: whatever the object,
        // terrain and structure editors changed is spliced into the opened
        // file, and everything they did not touch keeps its bytes.
        FScadSceneWriteOptions options;
        options.requiredUsePaths = RequiredKitUsePaths(std::filesystem::path(openedAssemblyPath_));
        const std::string source = document_.BuildSource(options);
        if (openedAssemblyPath_.empty())
        {
            return source;
        }
        return RewriteScadDependencyPaths(source, std::filesystem::path(openedAssemblyPath_).parent_path(), {}, true);
    }

    void ScadLibraryInterface::PreviewAssemblySource()
    {
        rigPreview_.SetActive(false);
        modulePreviewActive_ = false;
        if (sourceBufferDirty_ && !ReparseDocument(assemblySource_, openedAssemblyPath_))
        {
            return;
        }
        // Previewing edited assembly data is an in-place reload; keep the view.
        preserveCameraOnNextSceneLoad_ = true;
        if (WriteAndLoad("assembly_preview.scad", BuildAssemblyPreviewSource()))
        {
            statusLine_ = fmt::format("预览 {} 个实例 / {} 个地形特征 / {} 条过程规则", Bench().size(),
                                      TerrainProcess().Terrain().features.size(), TerrainProcess().ActiveRuleCount());
            statusError_ = false;
            return;
        }
        preserveCameraOnNextSceneLoad_ = false;
    }

    void ScadLibraryInterface::SaveAssembly(bool saveAs, bool reloadScene)
    {
        if (sourceBufferDirty_ && !ReparseDocument(assemblySource_, openedAssemblyPath_))
        {
            return;
        }
        const std::filesystem::path scadRoot = AuthoringPath("assets/scad");
        std::filesystem::path targetPath =
            saveAs ? std::filesystem::path(assemblyPathBuf_) : std::filesystem::path(openedAssemblyPath_);
        if (targetPath.empty())
        {
            statusLine_ = "请先输入场景保存路径";
            statusError_ = true;
            return;
        }
        if (!targetPath.is_absolute())
        {
            const std::string generic = targetPath.generic_string();
            constexpr std::string_view prefix = "assets/scad/";
            targetPath = generic.starts_with(prefix) ? scadRoot / generic.substr(prefix.size()) : scadRoot / targetPath;
        }
        if (targetPath.extension().empty())
        {
            targetPath += ".scad";
        }
        targetPath = targetPath.lexically_normal();
        const std::filesystem::path targetRelative = targetPath.lexically_relative(scadRoot);
        // Any scene directory accepts any scene: the editors a file gets are
        // decided per statement, not by which folder it sits in.
        if (!IsPathWithin(targetPath, scadRoot) || targetPath.extension() != ".scad" ||
            !IsSceneAssemblyRelativePath(targetRelative))
        {
            statusLine_ = "场景只能保存到 assets/scad 下的场景目录（不含 lib 与 characters）";
            statusError_ = true;
            return;
        }

        std::error_code ec;
        std::filesystem::create_directories(targetPath.parent_path(), ec);
        FScadSceneWriteOptions options;
        options.requiredUsePaths = RequiredKitUsePaths(targetPath);
        std::string source = document_.BuildSource(options);
        if (saveAs && !openedAssemblyPath_.empty() &&
            std::filesystem::path(openedAssemblyPath_).parent_path() != targetPath.parent_path())
        {
            source = RewriteScadDependencyPaths(source, std::filesystem::path(openedAssemblyPath_).parent_path(),
                                                targetPath.parent_path(), false);
        }
        std::ofstream output(targetPath, std::ios::binary | std::ios::trunc);
        if (!output)
        {
            statusLine_ = fmt::format("保存失败: {}", targetPath.string());
            statusError_ = true;
            return;
        }
        output << source;
        output.close();

        openedAssemblyPath_ = std::filesystem::absolute(targetPath, ec).string();
        aiKitContextActive_ = false;
        modulePreviewActive_ = false;
        // Re-index the file we just wrote so every span the editors hold points
        // at the bytes on disk again. The write came from the editors' own
        // state, so the evaluated bindings are still valid and re-evaluating
        // the use/include closure here would stall every gizmo release.
        ReparseDocument(source, openedAssemblyPath_, false);
        assemblySourceDirty_ = false;
        RefreshAssemblyWatchBaseline();
        const std::filesystem::path repoRoot = scadRoot.parent_path().parent_path();
        const std::string relativePath = targetPath.lexically_relative(repoRoot).generic_string();
        std::snprintf(assemblyPathBuf_, sizeof(assemblyPathBuf_), "%s", relativePath.c_str());
        if (reloadScene)
        {
            // Saving and reloading the current assembly must not move the
            // user's camera.
            preserveCameraOnNextSceneLoad_ = true;
            engine_.RequestLoadScene({.filename = openedAssemblyPath_});
        }
        RescanAssemblies();
        statusLine_ = reloadScene ? fmt::format("已保存并重载 {}", relativePath)
                                  : fmt::format("已实时更新并写回 {}", relativePath);
        statusError_ = false;
        SPDLOG_INFO("[ScadLibrary] saved scene assembly -> {}", targetPath.string());
    }

    void ScadLibraryInterface::MarkTerrainProcessDirty()
    {
        terrainProcessDirty_ = true;
        assemblySourceDirty_ = true;
    }

    void ScadLibraryInterface::ReloadTerrainProcess()
    {
        if (!document_.HasTerrain())
        {
            return;
        }
        terrainProcessDirty_ = false;
        ReloadCurrentAssemblyPreview();
    }

    void ScadLibraryInterface::ReloadCurrentAssemblyPreview()
    {
        // One preview path for every editor: the whole document is written to
        // the workspace and loaded, so instances, terrain rules and untouched
        // source all appear together.
        benchDirty_ = false;
        terrainProcessDirty_ = false;
        PreviewAssemblySource();
    }

    void ScadLibraryInterface::ReloadBench(bool preserveCamera)
    {
        benchDirty_ = false;
        rigPreview_.SetActive(false);
        modulePreviewActive_ = false;
        preserveCameraOnNextSceneLoad_ = preserveCamera;
        if (WriteAndLoad("bench.scad", BuildAssemblyPreviewSource()))
        {
            statusLine_ = fmt::format("场景 {} 个实例已重载", Bench().size());
            statusError_ = false;
            return;
        }
        preserveCameraOnNextSceneLoad_ = false;
    }

    void ScadLibraryInterface::ExportBench()
    {
        std::string name(exportNameBuf_);
        if (name.empty())
        {
            name = "my_scene";
        }
        std::string cleanName;
        for (const char c : name)
        {
            if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-')
            {
                cleanName.push_back(c);
            }
        }
        if (cleanName.empty())
        {
            cleanName = "my_scene";
        }
        const std::filesystem::path sceneDir = AuthoringPath("assets/scad") / "evaluated";
        std::error_code ec;
        std::filesystem::create_directories(sceneDir, ec);
        const std::filesystem::path outPath = sceneDir / (cleanName + ".scad");
        std::ofstream out(outPath, std::ios::binary | std::ios::trunc);
        if (!out)
        {
            statusLine_ = fmt::format("导出失败: {}", outPath.string());
            statusError_ = true;
            return;
        }
        FScadSceneWriteOptions exportOptions;
        exportOptions.requiredUsePaths = RequiredKitUsePaths(outPath);
        std::string exported = document_.BuildSource(exportOptions);
        if (!openedAssemblyPath_.empty() &&
            std::filesystem::path(openedAssemblyPath_).parent_path() != outPath.parent_path())
        {
            exported = RewriteScadDependencyPaths(exported, std::filesystem::path(openedAssemblyPath_).parent_path(),
                                                  outPath.parent_path(), false);
        }
        out << exported;
        out.close();
        statusLine_ = fmt::format("已导出 {}", outPath.string());
        statusError_ = false;
        RescanAssemblies();
        OpenAssembly(outPath.string());
        SPDLOG_INFO("[ScadLibrary] exported scene assembly -> {}", outPath.string());
    }

    bool ScadLibraryInterface::WriteWorkspaceFile(const std::string& fileName, const std::string& source,
                                                  std::string& outAbsPath)
    {
        std::error_code ec;
        std::filesystem::create_directories(WorkspaceDir(), ec);
        const std::filesystem::path path = WorkspaceDir() / fileName;
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out)
        {
            statusLine_ = fmt::format("写入失败: {}", path.string());
            statusError_ = true;
            return false;
        }
        out << source;
        out.close();
        outAbsPath = std::filesystem::absolute(path, ec).string();
        return true;
    }

    bool ScadLibraryInterface::WriteAndLoad(const std::string& fileName, const std::string& source)
    {
        std::string absPath;
        if (!WriteWorkspaceFile(fileName, source, absPath))
        {
            return false;
        }
        engine_.RequestLoadScene({.filename = absPath});
        return true;
    }

    // ------------------------------------------------------------- character designer

    std::string ScadLibraryInterface::KitCharUsePath(bool relative) const
    {
        if (relative)
        {
            return "../lib/kit_char.scad";
        }
        if (kitCharIndex_ < 0)
        {
            return "";
        }
        std::string usePath = kits_[kitCharIndex_].filePath;
        std::replace(usePath.begin(), usePath.end(), '\\', '/');
        return usePath;
    }

    void ScadLibraryInterface::ReloadDesigner()
    {
        designerDirty_ = false;
        if (!designer_.HasKit())
        {
            statusLine_ = "assets/scad/lib 下没有可用的 kit_char.scad";
            statusError_ = true;
            return;
        }

        const std::string source = designer_.BuildSource(KitCharUsePath(false));
        std::string characterPath;
        if (!WriteWorkspaceFile("character_preview.scad", source, characterPath))
        {
            return;
        }

        std::string error;
        std::vector<std::string> warnings;
        if (!rigPreview_.LoadRig(characterPath, error, &warnings))
        {
            // Rig 解析失败：退回静态场景加载，至少能看到几何和报错。
            rigPreview_.SetActive(false);
            engine_.RequestLoadScene({.filename = characterPath});
            statusLine_ = fmt::format("rig 解析失败: {}", error);
            statusError_ = true;
            return;
        }

        std::string equipmentError;
        rigPreview_.SetEquipment({}, equipmentError);
        rigPreview_.PlayClip("idle");
        rigPreview_.SetPaused(false);
        rigPreview_.SetTint(glm::vec3(designerTint_[0], designerTint_[1], designerTint_[2]));
        rigPreview_.SetActive(true);

        // 舞台场景只有地板；角色由 rig 实例化（避免与静态 bind 网格重影）。
        std::string stage;
        stage += "// rig preview stage\n";
        stage += "color([0.50, 0.51, 0.53]) translate([0, 0, -0.15]) cube([3.5, 3.5, 0.3], center = true);\n";
        stage += "color([0.62, 0.63, 0.65]) translate([0, 0, 0.001]) cube([0.9, 0.9, 0.002], center = true);\n";
        if (!WriteAndLoad("rigstage.scad", stage))
        {
            return;
        }

        // DroidSansFallback 缺"骼"字形，用"骨架"。
        statusLine_ = fmt::format("角色预览: 骨架 {} / 部件 {} / 动画 {}{}", rigPreview_.Asset().bones.size(),
                                  rigPreview_.Asset().parts.size(), rigPreview_.Asset().clips.size(),
                                  warnings.empty() ? "" : fmt::format("，{} 条 warning", warnings.size()));
        statusError_ = !warnings.empty();
    }

    void ScadLibraryInterface::ExportCharacter()
    {
        std::string clean;
        for (const char c : std::string(characterNameBuf_))
        {
            if (std::isalnum(static_cast<unsigned char>(c)) || c == '_')
            {
                clean.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
            }
        }
        if (clean.empty())
        {
            clean = "my_character";
        }
        const std::filesystem::path charDir = AuthoringPath("assets/scad") / "characters";
        std::error_code ec;
        std::filesystem::create_directories(charDir, ec);
        const std::filesystem::path outPath = charDir / (clean + ".scad");
        std::ofstream out(outPath, std::ios::binary | std::ios::trunc);
        if (!out)
        {
            statusLine_ = fmt::format("导出失败: {}", outPath.string());
            statusError_ = true;
            return;
        }
        out << designer_.BuildSource(KitCharUsePath(true));
        statusLine_ = fmt::format("已导出 {}", outPath.string());
        statusError_ = false;
        SPDLOG_INFO("[ScadLibrary] exported character -> {}", outPath.string());
    }

    void ScadLibraryInterface::DrawDesignerContent()
    {
        if (!designer_.HasKit())
        {
            ImGui::Spacing();
            ImGui::TextDisabled("assets/scad/lib 下没有可用的 kit_char.scad");
            ImGui::TextDisabled("（需要 head/torso/arm/leg 分类的部件）");
            return;
        }
        if (!designerEverLoaded_)
        {
            designerEverLoaded_ = true;
            designerDirty_ = true;
        }

        ImGui::Spacing();
        auto slotCombo = [&](const char* label, const std::vector<FKitModuleInfo>& options, int& index, bool optional)
        {
            const char* current =
                index >= 0 && index < static_cast<int>(options.size()) ? options[index].name.c_str() : "（无）";
            if (ImGui::BeginCombo(label, current))
            {
                if (optional && ImGui::Selectable("（无）", index < 0) && index != -1)
                {
                    index = -1;
                    designerDirty_ = true;
                }
                for (int i = 0; i < static_cast<int>(options.size()); ++i)
                {
                    if (ImGui::Selectable(options[i].name.c_str(), index == i) && index != i)
                    {
                        index = i;
                        designerDirty_ = true;
                    }
                }
                ImGui::EndCombo();
            }
        };

        slotCombo("头型", designer_.Heads(), designer_.head, false);
        slotCombo("发型", designer_.Hairs(), designer_.hair, true);
        slotCombo("帽饰", designer_.Hats(), designer_.hat, true);
        // DroidSansFallback 缺"躯"字形，用"上身"。
        slotCombo("上身", designer_.Torsos(), designer_.torso, false);
        slotCombo("手臂", designer_.Arms(), designer_.arm, false);
        slotCombo("腿部", designer_.Legs(), designer_.leg, false);

        if (!designer_.Accessories().empty())
        {
            ImGui::Separator();
            ImGui::TextDisabled("配饰");
            for (size_t i = 0; i < designer_.Accessories().size(); ++i)
            {
                bool enabled = i < designer_.accEnabled.size() && designer_.accEnabled[i] != 0;
                if (ImGui::Checkbox(designer_.Accessories()[i].name.c_str(), &enabled))
                {
                    designer_.accEnabled[i] = enabled ? 1 : 0;
                    designerDirty_ = true;
                }
            }
        }

        ImGui::Separator();
        if (ImGui::ColorEdit3("肤色", designer_.skinColor))
        {
            designerDirty_ = true;
        }
        if (ImGui::ColorEdit3("发色", designer_.hairColor))
        {
            designerDirty_ = true;
        }
        if (ImGui::ColorEdit3("主色", designerTint_))
        {
            // 运行时 tint（品红占位部位），改材质即可，无需重载。
            rigPreview_.SetTint(glm::vec3(designerTint_[0], designerTint_[1], designerTint_[2]));
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("运行时换色（ch_TINT() 品红占位部位）；导出文件保留占位");
        }

        ImGui::Separator();
        if (rigPreview_.HasRig() && rigPreview_.Active())
        {
            const std::string& current = rigPreview_.CurrentClip();
            if (ImGui::BeginCombo("动画", current.empty() ? "绑定姿态" : current.c_str()))
            {
                if (ImGui::Selectable("绑定姿态", current.empty()))
                {
                    rigPreview_.PlayClip("");
                }
                for (const Assets::FRigClip& clip : rigPreview_.Asset().clips)
                {
                    if (ImGui::Selectable(clip.name.c_str(), current == clip.name))
                    {
                        rigPreview_.PlayClip(clip.name);
                    }
                }
                ImGui::EndCombo();
            }
        }
        if (ImGui::Button(ICON_FA_ROTATE_RIGHT " 刷新预览"))
        {
            ReloadDesigner();
        }

        ImGui::Separator();
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 96.0f);
        ImGui::InputTextWithHint("##char_name", "角色文件名", characterNameBuf_, sizeof(characterNameBuf_));
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_FILE_EXPORT " 导出##char"))
        {
            ExportCharacter();
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("写入 assets/scad/characters/<名>.scad（use ../lib/kit_char.scad，游戏可直接加载）");
        }
    }

    // ---------------------------------------------------- rig action/equipment workbench

    void ScadLibraryInterface::ReloadWorkbench()
    {
        workbenchReloadRequested_ = false;
        workbenchEquipmentRebuildRequested_ = false;

        const std::filesystem::path sourcePath = AuthoringPath(rigSourceBuf_);

        std::string error;
        std::vector<std::string> warnings;
        if (!rigPreview_.LoadRig(sourcePath.string(), error, &warnings))
        {
            statusLine_ = fmt::format("角色载入失败: {}", error);
            statusError_ = true;
            rigPreview_.SetActive(false);
            return;
        }
        if (!workbench_.CaptureRig(sourcePath.string(), rigPreview_.Asset(), error))
        {
            statusLine_ = fmt::format("动作编辑器初始化失败: {}", error);
            statusError_ = true;
            return;
        }

        if (!workbench_.LoadEquipment(workbench_.EquipmentPath(), error))
        {
            std::string coldwarPath;
            for (const FKitInfo& kit : kits_)
            {
                if (kit.name == "kit_coldwar")
                {
                    coldwarPath = kit.filePath;
                    break;
                }
            }
            workbench_.SetDefaultEquipment(coldwarPath);
        }

        workbenchClip_ = 0;
        for (size_t index = 0; index < workbench_.Clips().size(); ++index)
        {
            if (workbench_.Clips()[index].name == "stand_idle")
            {
                workbenchClip_ = static_cast<int>(index);
                break;
            }
        }
        workbenchBone_ = 0;
        timelineSelectedChannel_ = -1;
        timelineSelectedKey_ = -1;
        timelineDraggingKey_ = false;
        timelineVisibleDuration_ = 0.0f;
        if (!workbench_.Clips().empty() && !workbench_.Clips()[workbenchClip_].channels.empty())
        {
            timelineSelectedChannel_ = 0;
            workbenchBone_ = std::clamp(workbench_.Clips()[workbenchClip_].channels.front().bone, 0,
                                        static_cast<int>(rigPreview_.Asset().bones.size()) - 1);
        }
        rigPreview_.SetActive(true);
        rigPreview_.SetPaused(false);
        rigPreview_.SetPlaySpeed(1.0f);
        if (!workbench_.Clips().empty())
        {
            rigPreview_.PlayClip(workbench_.Clips()[workbenchClip_].name);
            rigPreview_.SetCurrentTime(0.0f);
        }
        ReloadWorkbenchStage();
        workbenchEverLoaded_ = true;
        statusLine_ = fmt::format("角色工作室: {} 骨架 / {} 动作 / {} 装备{}", rigPreview_.Asset().bones.size(),
                                  workbench_.Clips().size(), workbench_.Equipment().size(),
                                  warnings.empty() ? "" : fmt::format("，{} 条 warning", warnings.size()));
        statusError_ = !warnings.empty();
    }

    void ScadLibraryInterface::ReloadWorkbenchStage()
    {
        workbenchEquipmentRebuildRequested_ = false;
        std::string equipmentError;
        const bool equipmentOk = rigPreview_.SetEquipment(workbench_.Equipment(), equipmentError);
        rigPreview_.SetActive(true);

        std::string stage;
        stage += "// ScadLibrary rig workbench stage\n";
        stage += "color([0.48, 0.49, 0.51]) translate([0, 0, -0.15]) cube([4.5, 4.5, 0.3], center = true);\n";
        stage += "color([0.62, 0.63, 0.65]) translate([0, 0, 0.001]) cube([1.0, 1.0, 0.002], center = true);\n";
        if (!WriteAndLoad("rig_workbench_stage.scad", stage))
        {
            return;
        }
        if (!equipmentOk)
        {
            statusLine_ = fmt::format("部分装备预览失败: {}", equipmentError);
            statusError_ = true;
        }
    }

    void ScadLibraryInterface::ApplyWorkbenchRigEdit()
    {
        workbench_.CommitRigEdit();
        std::string error;
        if (!workbench_.ApplyToAsset(rigPreview_.MutableAsset(), error))
        {
            statusLine_ = fmt::format("动作应用失败: {}", error);
            statusError_ = true;
            return;
        }
        rigPreview_.SetCurrentTime(std::min(rigPreview_.CurrentTime(), rigPreview_.CurrentDuration()));
    }

    void ScadLibraryInterface::DrawWorkbenchContent()
    {
        ImGui::Spacing();
        ImGui::SetNextItemWidth(-88.0f);
        ImGui::InputTextWithHint("##rig_source", "ScadRig .scad 路径", rigSourceBuf_, sizeof(rigSourceBuf_));
        ImGui::SameLine();
        if (ImGui::Button("打开", ImVec2(80.0f, 0.0f)))
        {
            workbenchReloadRequested_ = true;
        }

        if (!workbenchEverLoaded_ || !rigPreview_.HasRig() || workspaceMode_ != EWorkspaceMode::CharacterWorkbench)
        {
            ImGui::TextDisabled("正在载入角色工作室…");
            return;
        }

        ImGui::TextDisabled("%s", workbench_.SourcePath().c_str());
        if (ImGui::Button(workbench_.RigDirty() ? "保存动作 *" : "保存动作"))
        {
            std::string error;
            if (workbench_.SaveRig(error))
            {
                statusLine_ = fmt::format("动作已保存到 {}", workbench_.SourcePath());
                statusError_ = false;
            }
            else
            {
                statusLine_ = fmt::format("动作保存失败: {}", error);
                statusError_ = true;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button(workbench_.EquipmentDirty() ? "保存装备 *" : "保存装备"))
        {
            std::string error;
            if (workbench_.SaveEquipment(error))
            {
                statusLine_ = fmt::format("装备已保存到 {}", workbench_.EquipmentPath());
                statusError_ = false;
            }
            else
            {
                statusLine_ = fmt::format("装备保存失败: {}", error);
                statusError_ = true;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("从磁盘重载"))
        {
            workbenchReloadRequested_ = true;
        }

        if (!workbench_.Clips().empty())
        {
            workbenchClip_ = std::clamp(workbenchClip_, 0, static_cast<int>(workbench_.Clips().size()) - 1);
            FEditableRigClip& clip = workbench_.Clips()[workbenchClip_];
            if (ImGui::BeginCombo("动作", clip.name.c_str()))
            {
                for (int index = 0; index < static_cast<int>(workbench_.Clips().size()); ++index)
                {
                    const bool selected = index == workbenchClip_;
                    if (ImGui::Selectable(workbench_.Clips()[index].name.c_str(), selected))
                    {
                        workbenchClip_ = index;
                        timelineSelectedChannel_ = -1;
                        timelineSelectedKey_ = -1;
                        timelineDraggingKey_ = false;
                        timelineVisibleDuration_ = 0.0f;
                        if (!workbench_.Clips()[index].channels.empty())
                        {
                            timelineSelectedChannel_ = 0;
                            workbenchBone_ = std::clamp(workbench_.Clips()[index].channels.front().bone, 0,
                                                        static_cast<int>(rigPreview_.Asset().bones.size()) - 1);
                        }
                        rigPreview_.PlayClip(workbench_.Clips()[index].name);
                    }
                }
                ImGui::EndCombo();
            }

            if (ImGui::Button(rigPreview_.Paused() ? "播放" : "暂停"))
            {
                rigPreview_.SetPaused(!rigPreview_.Paused());
            }
            ImGui::SameLine();
            if (ImGui::Button("回到开头"))
            {
                rigPreview_.SetPaused(true);
                rigPreview_.SetCurrentTime(0.0f);
            }
            ImGui::SameLine();
            float speed = rigPreview_.PlaySpeed();
            ImGui::SetNextItemWidth(100.0f);
            if (ImGui::DragFloat("速度", &speed, 0.05f, -3.0f, 3.0f, "%.2fx"))
            {
                rigPreview_.SetPlaySpeed(speed);
            }
            float time = rigPreview_.CurrentTime();
            if (ImGui::SliderFloat("时间", &time, 0.0f, std::max(rigPreview_.CurrentDuration(), 0.001f), "%.3f s"))
            {
                rigPreview_.SetPaused(true);
                rigPreview_.SetCurrentTime(time);
            }
        }

        ImGui::Separator();
        if (ImGui::BeginTabBar("##workbench_modes"))
        {
            if (ImGui::BeginTabItem("动作修改"))
            {
                workbenchEditorTab_ = 0;
                ImGui::TextDisabled("关键帧编辑器已停靠在视口底部。");
                if (!workbench_.Clips().empty())
                {
                    const FEditableRigClip& activeClip = workbench_.Clips()[workbenchClip_];
                    ImGui::Spacing();
                    ImGui::Text("%s", activeClip.name.c_str());
                    ImGui::TextDisabled("%.3f 秒 / %zu 条轨道", activeClip.duration, activeClip.channels.size());
                    ImGui::TextDisabled("左侧骨骼树选择骨骼，右侧时间轴编辑轨道。");
                }
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem(fmt::format("装备 ({})", workbench_.Equipment().size()).c_str()))
            {
                workbenchEditorTab_ = 1;
                DrawEquipmentEditor();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }

    void ScadLibraryInterface::DrawViewportToolbar(const ImVec2& viewportPos)
    {
        ImGui::SetNextWindowPos(ImVec2(viewportPos.x + 12.0f, viewportPos.y + 12.0f), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.94f);
        const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking |
            ImGuiWindowFlags_AlwaysAutoResize;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 5.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(5.0f, 5.0f));
        if (ImGui::Begin("##ScadLibraryViewportToolbar", nullptr, flags))
        {
            if (NextUI::Theme::ToolbarButton(ICON_FA_ARROWS_UP_DOWN_LEFT_RIGHT, "移动骨骼", boneGizmoOperation_ == 0))
            {
                boneGizmoOperation_ = 0;
            }
            ImGui::SameLine();
            if (NextUI::Theme::ToolbarButton(ICON_FA_ROTATE, "旋转骨骼", boneGizmoOperation_ == 1))
            {
                boneGizmoOperation_ = 1;
            }
            ImGui::SameLine();
            if (NextUI::Theme::ToolbarButton(ICON_FA_EXPAND, "缩放骨骼", boneGizmoOperation_ == 2))
            {
                boneGizmoOperation_ = 2;
            }
            NextUI::Theme::DrawVerticalSeparator(20.0f, 8.0f, 0.65f);
            ImGui::TextDisabled("本地  ·  透视");
        }
        ImGui::End();
        ImGui::PopStyleVar(2);
    }

    void ScadLibraryInterface::UpsertGizmoKey(EEditableRigChannel type, const glm::vec3& value)
    {
        if (workbench_.Clips().empty() || workbenchClip_ < 0 ||
            workbenchClip_ >= static_cast<int>(workbench_.Clips().size()))
        {
            return;
        }

        FEditableRigClip& clip = workbench_.Clips()[workbenchClip_];
        int channelIndex = -1;
        for (int index = 0; index < static_cast<int>(clip.channels.size()); ++index)
        {
            if (clip.channels[index].bone == workbenchBone_ && clip.channels[index].type == type)
            {
                channelIndex = index;
                break;
            }
        }
        if (channelIndex < 0)
        {
            FEditableRigChannel channel;
            channel.bone = workbenchBone_;
            channel.type = type;
            clip.channels.push_back(std::move(channel));
            channelIndex = static_cast<int>(clip.channels.size()) - 1;
        }

        FEditableRigChannel& channel = clip.channels[channelIndex];
        const float currentTime = rigPreview_.CurrentTime();
        int keyIndex = -1;
        for (int index = 0; index < static_cast<int>(channel.keys.size()); ++index)
        {
            if (std::abs(channel.keys[index].time - currentTime) <= 0.0005f)
            {
                keyIndex = index;
                break;
            }
        }
        if (keyIndex < 0)
        {
            channel.keys.push_back({currentTime, value});
            keyIndex = static_cast<int>(channel.keys.size()) - 1;
        }
        else
        {
            channel.keys[keyIndex].value = value;
        }

        timelineSelectedChannel_ = channelIndex;
        timelineSelectedKey_ = keyIndex;
        ApplyWorkbenchRigEdit();

        const std::vector<FEditableRigKey>& sortedKeys = clip.channels[channelIndex].keys;
        float nearest = std::numeric_limits<float>::max();
        for (int index = 0; index < static_cast<int>(sortedKeys.size()); ++index)
        {
            const float distance = std::abs(sortedKeys[index].time - currentTime);
            if (distance < nearest)
            {
                nearest = distance;
                timelineSelectedKey_ = index;
            }
        }
    }

    void ScadLibraryInterface::DrawSceneGizmoToolbar(const ImVec2& viewportPos)
    {
        ImGui::SetNextWindowPos(ImVec2(viewportPos.x + 12.0f, viewportPos.y + 12.0f), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.94f);
        const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 5.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(5.0f, 5.0f));
        if (ImGui::Begin("##SceneObjectGizmoToolbar", nullptr, flags))
        {
            sceneToolbarVisible_ = true;
            sceneToolbarPosition_ = {ImGui::GetWindowPos().x, ImGui::GetWindowPos().y};
            sceneToolbarSize_ = {ImGui::GetWindowSize().x, ImGui::GetWindowSize().y};
            if (NextUI::Theme::ToolbarButton(ICON_FA_ROTATE_LEFT, "回到全览视图", false))
            {
                frameAllRequested_ = true;
            }
            ImGui::SameLine();

            glm::vec3 selectedCenter;
            float selectedRadius = 0.0f;
            const bool hasSelection = GetSelectedSceneObjectBounds(selectedCenter, selectedRadius);
            ImGui::BeginDisabled(!hasSelection);
            if (NextUI::Theme::ToolbarButton(ICON_FA_CROSSHAIRS, "聚焦选中 Kit（F）", false))
            {
                focusSelectedRequested_ = true;
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            NextUI::Theme::DrawVerticalSeparator(20.0f, 8.0f, 0.65f);
            ImGui::SameLine();

            if (NextUI::Theme::ToolbarButton(ICON_FA_ARROWS_UP_DOWN_LEFT_RIGHT, "移动对象", sceneGizmoOperation_ == 0))
            {
                sceneGizmoOperation_ = 0;
            }
            ImGui::SameLine();
            if (NextUI::Theme::ToolbarButton(ICON_FA_ROTATE, "旋转对象", sceneGizmoOperation_ == 1))
            {
                sceneGizmoOperation_ = 1;
            }
            NextUI::Theme::DrawVerticalSeparator(20.0f, 8.0f, 0.65f);
            if (selectedBenchItem_ >= 0 && selectedBenchItem_ < static_cast<int>(Bench().size()))
            {
                ImGui::Text("%s", Bench()[selectedBenchItem_].moduleName.c_str());
                NextUI::Theme::DrawTooltip("松开鼠标后写回 SCAD");
            }
            else
            {
                ImGui::TextDisabled("选择右侧对象以编辑");
            }
        }
        ImGui::End();
        ImGui::PopStyleVar(2);
    }

    void ScadLibraryInterface::ClearEditableSceneSelection()
    {
        selectedBenchItem_ = -1;
        sceneGizmoWasUsing_ = false;
        sceneGizmoAwaitingPickRelease_ = false;
        engine_.GetScene().ClearSelection();
        engine_.GetShowFlags().ShowEdge = false;
    }

    void ScadLibraryInterface::ClearSelectedStructureBounds()
    {
        selectedStructureBoundsSegment_ = -1;
        selectedStructureBoundsValid_ = false;
    }

    void ScadLibraryInterface::UpdateSelectedStructureBounds()
    {
        ClearSelectedStructureBounds();
        if (selectedSegment_ < 0 || selectedSegment_ >= static_cast<int>(document_.Segments().size()) ||
            document_.Segments()[selectedSegment_].kind != EScadSegmentKind::Source ||
            document_.Segments()[selectedSegment_].name == "lay_scatter")
        {
            return;
        }

        glm::vec3 minBounds;
        glm::vec3 maxBounds;
        if (ComputeSegmentWorldBounds(static_cast<size_t>(selectedSegment_), minBounds, maxBounds))
        {
            selectedStructureBoundsSegment_ = selectedSegment_;
            selectedStructureBoundsMin_ = minBounds;
            selectedStructureBoundsMax_ = maxBounds;
            selectedStructureBoundsValid_ = true;
        }
    }

    void ScadLibraryInterface::DrawSelectedStructureBounds(const ImVec2& viewportPos, const ImVec2& viewportSize) const
    {
        if (!selectedStructureBoundsValid_ || selectedStructureBoundsSegment_ != selectedSegment_)
        {
            return;
        }
        DrawOrientedBoxOverlay(engine_.GetLastUniformBufferObject(), viewportPos, viewportSize, glm::mat4(1.0f),
                               selectedStructureBoundsMin_, selectedStructureBoundsMax_, IM_COL32(88, 200, 255, 255));
    }

    void ScadLibraryInterface::DrawViewportAxis(const ImVec2& viewportPos, const ImVec2& viewportSize)
    {
        if (viewportSize.x < 120.0f || viewportSize.y < 120.0f)
        {
            return;
        }

        struct FAxis
        {
            const char* label;
            glm::vec3 scadDirection;
            ImU32 color;
            glm::vec3 cameraDirection{};
        };
        std::array<FAxis, 3> axes{{
            {"X", {1.0f, 0.0f, 0.0f}, IM_COL32(238, 83, 83, 255)},
            {"Y", {0.0f, 1.0f, 0.0f}, IM_COL32(94, 201, 112, 255)},
            {"Z", {0.0f, 0.0f, 1.0f}, IM_COL32(80, 148, 255, 255)},
        }};

        const glm::mat3 scadToWorld = glm::mat3(Assets::Scad::ScadToWorldBasis(1.0));
        const glm::mat3 viewRotation = glm::mat3(engine_.GetLastUniformBufferObject().ModelView);
        for (FAxis& axis : axes)
        {
            axis.cameraDirection = viewRotation * scadToWorld * axis.scadDirection;
        }
        std::sort(axes.begin(), axes.end(), [](const FAxis& lhs, const FAxis& rhs)
        { return lhs.cameraDirection.z < rhs.cameraDirection.z; });

        ImDrawList* drawList = ImGui::GetForegroundDrawList();
        const ImVec2 origin(viewportPos.x + 62.0f, viewportPos.y + viewportSize.y - 62.0f);
        drawList->PushClipRect(viewportPos, viewportPos + viewportSize, true);
        drawList->AddCircleFilled(origin, 45.0f, IM_COL32(18, 20, 24, 184), 32);
        drawList->AddCircle(origin, 45.0f, IM_COL32(255, 255, 255, 42), 32, 1.0f);
        drawList->AddText(ImVec2(origin.x - 36.0f, origin.y + 28.0f), IM_COL32(210, 214, 222, 190),
                          "SCAD");

        for (const FAxis& axis : axes)
        {
            glm::vec2 screen(axis.cameraDirection.x, -axis.cameraDirection.y);
            const float visibility = glm::length(screen);
            if (visibility > 1.0e-4f)
            {
                screen /= visibility;
            }
            else
            {
                screen = {0.0f, -1.0f};
            }
            const float length = 12.0f + 25.0f * std::clamp(visibility, 0.0f, 1.0f);
            const ImVec2 end(origin.x + screen.x * length, origin.y + screen.y * length);
            const ImVec2 perpendicular(-screen.y, screen.x);
            const ImVec2 arrowBase(end.x - screen.x * 7.0f, end.y - screen.y * 7.0f);
            drawList->AddLine(origin, end, axis.color, 3.0f);
            drawList->AddTriangleFilled(
                end, ImVec2(arrowBase.x + perpendicular.x * 4.0f, arrowBase.y + perpendicular.y * 4.0f),
                ImVec2(arrowBase.x - perpendicular.x * 4.0f, arrowBase.y - perpendicular.y * 4.0f),
                axis.color);
            drawList->AddText(ImVec2(end.x + screen.x * 4.0f - 3.0f, end.y + screen.y * 4.0f - 7.0f),
                              axis.color, axis.label);
        }
        drawList->AddCircleFilled(origin, 3.0f, IM_COL32(230, 233, 240, 255));
        drawList->PopClipRect();
    }

    bool ScadLibraryInterface::TerrainFeatureConsumesMouse(double x, double y) const
    {
        if (!HasActiveProceduralHandles() || !showTerrainFeatureOverlay_)
        {
            return false;
        }
        if (terrainFeatureDragging_ || terrainRuleDragging_ || layScatterDragging_)
        {
            return true;
        }

        // Engine mouse events use framebuffer pixels, while the overlay handles
        // are stored in ImGui's logical coordinate space.
        const ImVec2 uiMouse = NextUI::Scaling::MainFramebufferToImGuiPoint(
            ImVec2(static_cast<float>(x), static_cast<float>(y)));
        const glm::vec2 mouse(uiMouse.x, uiMouse.y);
        for (const FTerrainFeatureHandle& handle : terrainFeatureHandles_)
        {
            if (glm::distance2(mouse, handle.screen) <= 144.0f)
            {
                return true;
            }
        }
        for (const FTerrainRuleHandle& handle : terrainRuleHandles_)
        {
            if (glm::distance2(mouse, handle.screen) <= 144.0f)
            {
                return true;
            }
        }
        for (const FLayScatterHandle& handle : layScatterHandles_)
        {
            if (glm::distance2(mouse, handle.screen) <= 144.0f)
            {
                return true;
            }
        }
        return false;
    }

    bool ScadLibraryInterface::HasActiveProceduralHandles() const
    {
        if (workspaceMode_ != EWorkspaceMode::SceneAssembly || selectedSegment_ < 0 ||
            selectedSegment_ >= static_cast<int>(document_.Segments().size()))
        {
            return false;
        }
        const FScadSceneSegment& segment = document_.Segments()[selectedSegment_];
        return segment.kind == EScadSegmentKind::Terrain || segment.kind == EScadSegmentKind::TerrainRule ||
            (segment.kind == EScadSegmentKind::Source && segment.name == "lay_scatter");
    }

    void ScadLibraryInterface::DrawLayScatterOverlay(const ImVec2& viewportPos, const ImVec2& viewportSize)
    {
        layScatterHandles_.clear();
        if (!showTerrainFeatureOverlay_ || !HasActiveProceduralHandles() || selectedSegment_ < 0 ||
            document_.Segments()[selectedSegment_].name != "lay_scatter")
        {
            layScatterDragging_ = false;
            return;
        }

        FLayScatterSource scatter;
        if (!ParseLayScatterSource(document_.GetSegmentSource(static_cast<size_t>(selectedSegment_)), scatter))
        {
            return;
        }

        const Assets::UniformBufferObject& ubo = engine_.GetLastUniformBufferObject();
        const auto project = [&](const glm::dvec2& point, ImVec2& outScreen, float& outWorldHeight)
        {
            const glm::vec3 world = Assets::Scad::ScadToWorldPos(glm::dvec3(point.x, point.y, 0.35), 1.0);
            const glm::vec4 clip = ubo.ViewProjectionUnJit * glm::vec4(world, 1.0f);
            if (clip.w <= 0.001f)
            {
                return false;
            }
            const glm::vec3 ndc = glm::vec3(clip) / clip.w;
            if (ndc.z < 0.0f || ndc.z > 1.0f)
            {
                return false;
            }
            outScreen = {viewportPos.x + (ndc.x * 0.5f + 0.5f) * viewportSize.x,
                         viewportPos.y + (ndc.y * 0.5f + 0.5f) * viewportSize.y};
            outWorldHeight = world.y;
            return true;
        };

        const std::array<glm::dvec2, 4> corners = {
            glm::dvec2(scatter.x0, scatter.y0), glm::dvec2(scatter.x1, scatter.y0),
            glm::dvec2(scatter.x1, scatter.y1), glm::dvec2(scatter.x0, scatter.y1),
        };
        std::array<ImVec2, 4> screen{};
        std::array<float, 4> worldHeight{};
        std::array<bool, 4> visible{};
        for (int corner = 0; corner < 4; ++corner)
        {
            visible[corner] = project(corners[corner], screen[corner], worldHeight[corner]);
            if (visible[corner])
            {
                layScatterHandles_.push_back(
                    {corner, glm::vec2(screen[corner].x, screen[corner].y), worldHeight[corner]});
            }
        }

        ImDrawList* drawList = ImGui::GetForegroundDrawList();
        drawList->PushClipRect(viewportPos, viewportPos + viewportSize, true);
        constexpr ImU32 color = IM_COL32(255, 191, 64, 255);
        for (int corner = 0; corner < 4; ++corner)
        {
            const int next = (corner + 1) % 4;
            if (visible[corner] && visible[next])
            {
                drawList->AddLine(screen[corner], screen[next], color, 2.2f);
            }
        }
        const glm::dvec2 center((scatter.x0 + scatter.x1) * 0.5, (scatter.y0 + scatter.y1) * 0.5);
        ImVec2 centerScreen;
        float unusedHeight = 0.0f;
        if (project(center, centerScreen, unusedHeight))
        {
            drawList->AddText(ImVec2(centerScreen.x + 8.0f, centerScreen.y - 18.0f), color,
                              fmt::format("lay_scatter  n={}  seed={}", scatter.count, scatter.seed).c_str());
        }

        const glm::vec2 mouse(ImGui::GetIO().MousePos.x, ImGui::GetIO().MousePos.y);
        int hovered = -1;
        float nearest = 144.0f;
        for (int index = 0; index < static_cast<int>(layScatterHandles_.size()); ++index)
        {
            const float distance = glm::distance2(mouse, layScatterHandles_[index].screen);
            if (distance <= nearest)
            {
                hovered = index;
                nearest = distance;
            }
        }
        for (int index = 0; index < static_cast<int>(layScatterHandles_.size()); ++index)
        {
            const FLayScatterHandle& handle = layScatterHandles_[index];
            const bool isHovered = index == hovered;
            const float radius = isHovered ? 7.0f : 5.0f;
            const ImVec2 handleScreen(handle.screen.x, handle.screen.y);
            drawList->AddRectFilled(ImVec2(handleScreen.x - radius, handleScreen.y - radius),
                                    ImVec2(handleScreen.x + radius, handleScreen.y + radius),
                                    isHovered ? IM_COL32(255, 255, 255, 255) : color, 1.5f);
            drawList->AddRect(ImVec2(handleScreen.x - radius, handleScreen.y - radius),
                              ImVec2(handleScreen.x + radius, handleScreen.y + radius), IM_COL32(12, 18, 26, 230));
        }

        const bool mouseInside = mouse.x >= viewportPos.x && mouse.y >= viewportPos.y &&
            mouse.x < viewportPos.x + viewportSize.x && mouse.y < viewportPos.y + viewportSize.y;
        if (hovered >= 0 || layScatterDragging_)
        {
            ImGui::GetIO().WantCaptureMouse = true;
        }
        if (!layScatterDragging_ && hovered >= 0 && mouseInside && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            const FLayScatterHandle& handle = layScatterHandles_[hovered];
            layScatterDragCorner_ = handle.corner;
            layScatterDragPlaneHeight_ = handle.worldHeight;
            layScatterDragging_ = true;
        }
        if (layScatterDragging_ && ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            const ImVec2 framebufferMouse = NextUI::Scaling::ImGuiToMainFramebufferPoint(
                ImVec2(mouse.x, mouse.y));
            glm::vec3 rayOrigin;
            glm::vec3 rayDirection;
            Runtime::EngineHelper::GetScreenToWorldRay(
                glm::vec2(framebufferMouse.x, framebufferMouse.y), rayOrigin, rayDirection);
            if (std::abs(rayDirection.y) > 1.0e-5f)
            {
                const float distance = (layScatterDragPlaneHeight_ - rayOrigin.y) / rayDirection.y;
                if (distance > 0.0f)
                {
                    const glm::vec3 hit = rayOrigin + rayDirection * distance;
                    const glm::dvec2 point(hit.x, -hit.z);
                    if (layScatterDragCorner_ == 0 || layScatterDragCorner_ == 3)
                        scatter.x0 = point.x;
                    else
                        scatter.x1 = point.x;
                    if (layScatterDragCorner_ == 0 || layScatterDragCorner_ == 1)
                        scatter.y0 = point.y;
                    else
                        scatter.y1 = point.y;
                    if (scatter.x0 > scatter.x1) std::swap(scatter.x0, scatter.x1);
                    if (scatter.y0 > scatter.y1) std::swap(scatter.y0, scatter.y1);
                    if (document_.ReplaceSegmentSource(static_cast<size_t>(selectedSegment_),
                                                       SerializeLayScatterSource(scatter)))
                    {
                        assemblySourceDirty_ = true;
                        benchDirty_ = true;
                        sourceStructureBoundsDirty_ = true;
                    }
                }
            }
        }
        if (layScatterDragging_ && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
        {
            layScatterDragging_ = false;
            layScatterDragCorner_ = -1;
        }
        drawList->PopClipRect();
    }

    bool ScadLibraryInterface::ConsumePreserveCameraOnNextSceneLoad()
    {
        const bool preserve = preserveCameraOnNextSceneLoad_;
        preserveCameraOnNextSceneLoad_ = false;
        return preserve;
    }

    void ScadLibraryInterface::DrawTerrainFeatureToolbar(const ImVec2& viewportPos)
    {
        ImGui::SetNextWindowPos(ImVec2(viewportPos.x + 12.0f, viewportPos.y + 12.0f), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.88f);
        const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 6.0f));
        if (ImGui::Begin("##TerrainFeatureToolbar", nullptr, flags))
        {
            ImGui::Checkbox("Feature 轮廓", &showTerrainFeatureOverlay_);
            ImGui::SameLine();
            const std::vector<Assets::Scad::FTerrainFeature>& features = TerrainProcess().Terrain().features;
            const std::vector<FTerrainProcessRule>& rules = TerrainProcess().Rules();
            if (terrainSelectionIsRule_ && selectedTerrainRule_ >= 0 &&
                selectedTerrainRule_ < static_cast<int>(rules.size()))
            {
                ImGui::TextColored(ImVec4(0.31f, 1.0f, 0.59f, 1.0f), "■ #%02d %s  ·  再次拖动方形手柄编辑",
                                   selectedTerrainRule_ + 1,
                                   FTerrainProcessDocument::RuleTypeName(rules[selectedTerrainRule_].type));
                ImGui::SameLine();
                ImGui::TextDisabled("· Shift 拖动复制");
            }
            else if (!terrainSelectionIsRule_ && selectedTerrainFeature_ >= 0 &&
                     selectedTerrainFeature_ < static_cast<int>(features.size()))
            {
                const Assets::Scad::FTerrainFeature& feature = features[selectedTerrainFeature_];
                ImGui::TextDisabled("#%02d %s  ·  再次拖动圆点编辑  ·  Shift 拖动复制", selectedTerrainFeature_ + 1,
                                    FTerrainProcessDocument::FeatureTypeName(feature.type));
            }
            else
            {
                ImGui::TextDisabled("从右侧 Feature 列表或视口圆点选择");
            }
        }
        ImGui::End();
        ImGui::PopStyleVar();
    }

    void ScadLibraryInterface::RefreshTerrainFeatureOverlayCache()
    {
        const Assets::Scad::FTerrainSpec& terrain = TerrainProcess().Terrain();
        const std::string cacheKey = Assets::Scad::ScadTerrain::SpecCacheKey(terrain);
        if (terrainFeatureOverlayData_ && terrainFeatureOverlayCacheKey_ == cacheKey)
        {
            return;
        }
        if (terrainFeatureOverlayData_ && (terrainFeatureDragging_ || ImGui::IsAnyItemActive()))
        {
            return;
        }

        terrainFeatureOverlayData_ = Assets::Scad::ScadTerrain::Build(terrain);
        terrainFeatureOverlayCacheKey_ = cacheKey;
    }

    void ScadLibraryInterface::DrawTerrainFeatureOverlay(const ImVec2& viewportPos, const ImVec2& viewportSize)
    {
        terrainFeatureHandles_.clear();
        terrainRuleHandles_.clear();
        layScatterHandles_.clear();
        if (!showTerrainFeatureOverlay_ || viewportSize.x <= 1.0f || viewportSize.y <= 1.0f)
        {
            terrainFeatureDragging_ = false;
            terrainRuleDragging_ = false;
            return;
        }

        Assets::Scad::FTerrainSpec& terrain = TerrainProcess().Terrain();
        if (terrain.features.empty())
        {
            selectedTerrainFeature_ = -1;
            terrainFeatureDragging_ = false;
            terrainRuleDragging_ = false;
            return;
        }
        selectedTerrainFeature_ = std::clamp(selectedTerrainFeature_, 0, static_cast<int>(terrain.features.size()) - 1);
        RefreshTerrainFeatureOverlayCache();

        const Assets::UniformBufferObject& ubo = engine_.GetLastUniformBufferObject();
        ImDrawList* drawList = ImGui::GetForegroundDrawList();
        drawList->PushClipRect(viewportPos, ImVec2(viewportPos.x + viewportSize.x, viewportPos.y + viewportSize.y),
                               true);

        const auto projectWorld = [&](const glm::vec3& world, ImVec2& screen)
        {
            const glm::vec4 clip = ubo.ViewProjectionUnJit * glm::vec4(world, 1.0f);
            if (clip.w <= 0.001f)
            {
                return false;
            }
            const glm::vec3 ndc = glm::vec3(clip) / clip.w;
            if (ndc.z < 0.0f || ndc.z > 1.0f || std::abs(ndc.x) > 1.2f || std::abs(ndc.y) > 1.2f)
            {
                return false;
            }
            screen.x = viewportPos.x + (ndc.x * 0.5f + 0.5f) * viewportSize.x;
            screen.y = viewportPos.y + (ndc.y * 0.5f + 0.5f) * viewportSize.y;
            return true;
        };
        const auto surfaceHeight = [&](const glm::dvec2& point)
        {
            return terrainFeatureOverlayData_ ? terrainFeatureOverlayData_->HeightAt(point.x, point.y)
                                              : terrain.baseHeight;
        };
        const auto worldAt = [&](const glm::dvec2& point, double lift = 0.28)
        { return Assets::Scad::ScadToWorldPos(glm::dvec3(point.x, point.y, surfaceHeight(point) + lift), 1.0); };
        const auto colorForFeature = [](Assets::Scad::FTerrainFeature::EType type, int alpha)
        {
            using EType = Assets::Scad::FTerrainFeature::EType;
            switch (type)
            {
            case EType::Mountain:
                return IM_COL32(255, 132, 48, alpha);
            case EType::Ridge:
                return IM_COL32(255, 184, 72, alpha);
            case EType::Plateau:
                return IM_COL32(190, 116, 255, alpha);
            case EType::Lake:
                return IM_COL32(66, 205, 255, alpha);
            case EType::River:
                return IM_COL32(44, 139, 255, alpha);
            case EType::Road:
                return IM_COL32(255, 218, 84, alpha);
            case EType::Pad:
                return IM_COL32(255, 108, 196, alpha);
            case EType::Hmap:
                return IM_COL32(150, 160, 170, alpha);
            }
            return IM_COL32(255, 255, 255, alpha);
        };
        const auto drawLabel = [&](const ImVec2& position, ImU32 color, const std::string& text)
        {
            const ImVec2 labelPos(position.x + 8.0f, position.y - 18.0f);
            drawList->AddText(ImVec2(labelPos.x + 1.0f, labelPos.y + 1.0f), IM_COL32(0, 0, 0, 220), text.c_str());
            drawList->AddText(labelPos, color, text.c_str());
        };
        const auto drawTerrainSegment = [&](const glm::dvec2& from, const glm::dvec2& to, ImU32 color, float thickness)
        {
            const int subdivisions = std::clamp(static_cast<int>(std::ceil(glm::distance(from, to) / 3.0)), 1, 64);
            ImVec2 previous;
            bool hasPrevious = false;
            for (int step = 0; step <= subdivisions; ++step)
            {
                const double t = static_cast<double>(step) / subdivisions;
                const glm::dvec2 point = glm::mix(from, to, t);
                ImVec2 screen;
                if (projectWorld(worldAt(point), screen))
                {
                    if (hasPrevious)
                    {
                        drawList->AddLine(previous, screen, color, thickness);
                    }
                    previous = screen;
                    hasPrevious = true;
                }
                else
                {
                    hasPrevious = false;
                }
            }
        };
        const auto drawTerrainPolyline =
            [&](const std::vector<glm::dvec2>& points, bool closed, ImU32 color, float thickness)
        {
            if (points.size() < 2)
            {
                return;
            }
            const size_t segmentCount = closed ? points.size() : points.size() - 1;
            for (size_t segment = 0; segment < segmentCount; ++segment)
            {
                drawTerrainSegment(points[segment], points[(segment + 1) % points.size()], color, thickness);
            }
        };
        const auto circlePoints = [](const glm::dvec2& center, double radius)
        {
            std::vector<glm::dvec2> points;
            constexpr int segmentCount = 48;
            points.reserve(segmentCount);
            for (int segment = 0; segment < segmentCount; ++segment)
            {
                const double angle = glm::two_pi<double>() * static_cast<double>(segment) / segmentCount;
                points.emplace_back(center + radius * glm::dvec2(std::cos(angle), std::sin(angle)));
            }
            return points;
        };
        const auto addHandle = [&](int featureIndex, int pointIndex, const glm::dvec2& point)
        {
            ImVec2 screen;
            const glm::vec3 world = worldAt(point, 0.42);
            if (projectWorld(world, screen))
            {
                terrainFeatureHandles_.push_back({featureIndex, pointIndex, glm::vec2(screen.x, screen.y), world.y});
            }
        };
        const auto addFeatureWorldHandle = [&](int featureIndex, int pointIndex, const glm::vec3& world)
        {
            ImVec2 screen;
            if (projectWorld(world, screen))
            {
                terrainFeatureHandles_.push_back({featureIndex, pointIndex, glm::vec2(screen.x, screen.y), world.y});
            }
        };
        const auto drawFeatureVerticalRuler =
            [&](int featureIndex, const glm::dvec2& point, double value, const char* prefix, ImU32 color)
        {
            const double base = surfaceHeight(point) + 0.45;
            const double rulerHeight = std::max(0.1, value);
            const glm::vec3 rulerBottom = Assets::Scad::ScadToWorldPos(glm::dvec3(point.x, point.y, base), 1.0);
            const glm::vec3 rulerTop =
                Assets::Scad::ScadToWorldPos(glm::dvec3(point.x, point.y, base + rulerHeight), 1.0);
            ImVec2 bottomScreen;
            ImVec2 topScreen;
            if (projectWorld(rulerBottom, bottomScreen) && projectWorld(rulerTop, topScreen))
            {
                drawList->AddLine(bottomScreen, topScreen, color, 2.0f);
                drawList->AddCircleFilled(topScreen, 4.5f, color);
                drawLabel(topScreen, color, fmt::format("{}={:.1f}", prefix, value));
                addFeatureWorldHandle(featureIndex, -3, rulerTop);
            }
        };
        const auto drawWidthBand =
            [&](const std::vector<glm::dvec2>& points, double width, ImU32 lineColor, ImU32 fillColor, float thickness)
        {
            if (points.size() < 2)
            {
                return;
            }
            for (size_t segment = 0; segment + 1 < points.size(); ++segment)
            {
                const glm::dvec2 delta = points[segment + 1] - points[segment];
                const double length = glm::length(delta);
                if (length <= 1e-6)
                {
                    continue;
                }
                const glm::dvec2 side(-delta.y / length * width * 0.5, delta.x / length * width * 0.5);
                const glm::dvec2 corners[] = {
                    points[segment] + side,
                    points[segment + 1] + side,
                    points[segment + 1] - side,
                    points[segment] - side,
                };
                ImVec2 projected[4];
                bool allVisible = true;
                for (int corner = 0; corner < 4; ++corner)
                {
                    allVisible &= projectWorld(worldAt(corners[corner], 0.18), projected[corner]);
                }
                if (allVisible)
                {
                    drawList->AddConvexPolyFilled(projected, 4, fillColor);
                }
                drawTerrainSegment(corners[0], corners[1], lineColor, thickness);
                drawTerrainSegment(corners[3], corners[2], lineColor, thickness);
            }
            drawTerrainPolyline(points, false, lineColor, thickness + 0.5f);
        };

        using EType = Assets::Scad::FTerrainFeature::EType;
        for (int featureIndex = 0; featureIndex < static_cast<int>(terrain.features.size()); ++featureIndex)
        {
            Assets::Scad::FTerrainFeature& feature = terrain.features[featureIndex];
            const bool selected = !terrainSelectionIsRule_ && featureIndex == selectedTerrainFeature_;
            const ImU32 lineColor = colorForFeature(feature.type, selected ? 255 : 112);
            const ImU32 fillColor = colorForFeature(feature.type, selected ? 46 : 18);
            const float thickness = selected ? 2.4f : 1.25f;

            if (feature.type == EType::Mountain || feature.type == EType::Plateau || feature.type == EType::Lake)
            {
                const std::vector<glm::dvec2> outline = circlePoints(feature.at, feature.radius);
                drawTerrainPolyline(outline, true, lineColor, thickness);
                drawTerrainSegment(feature.at, feature.at + glm::dvec2(feature.radius, 0.0), lineColor, thickness);
                addHandle(featureIndex, -1, feature.at);
                if (selected)
                {
                    addHandle(featureIndex, -2, feature.at + glm::dvec2(feature.radius, 0.0));
                    ImVec2 centerScreen;
                    if (projectWorld(worldAt(feature.at, 0.45), centerScreen))
                    {
                        if (feature.type == EType::Mountain)
                        {
                            drawLabel(centerScreen, lineColor,
                                      fmt::format("山峰  r={:.1f}  h={:.1f}  rugged={:.2f}", feature.radius,
                                                  feature.height, feature.rugged));
                        }
                        else if (feature.type == EType::Plateau)
                        {
                            drawLabel(centerScreen, lineColor,
                                      fmt::format("台地  r={:.1f}  h={:.1f}", feature.radius, feature.height));
                        }
                        else
                        {
                            drawLabel(centerScreen, lineColor,
                                      fmt::format("湖泊  r={:.1f}  depth={:.1f}", feature.radius, feature.depth));
                        }
                    }
                }
                if (selected && (feature.type == EType::Mountain || feature.type == EType::Plateau))
                {
                    drawFeatureVerticalRuler(featureIndex, feature.at, feature.height, "h", lineColor);
                }
                else if (selected && feature.type == EType::Lake)
                {
                    drawFeatureVerticalRuler(featureIndex, feature.at, feature.depth, "depth", lineColor);
                }
            }
            else if (feature.type == EType::Ridge || feature.type == EType::River || feature.type == EType::Road)
            {
                drawWidthBand(feature.pts, feature.width, lineColor, fillColor, thickness);
                for (int pointIndex = 0; pointIndex < static_cast<int>(feature.pts.size()); ++pointIndex)
                {
                    addHandle(featureIndex, pointIndex, feature.pts[pointIndex]);
                }
                if (selected && !feature.pts.empty())
                {
                    ImVec2 firstScreen;
                    if (projectWorld(worldAt(feature.pts.front(), 0.45), firstScreen))
                    {
                        if (feature.type == EType::Ridge)
                        {
                            drawLabel(firstScreen, lineColor,
                                      fmt::format("山脊  width={:.1f}  h={:.1f}", feature.width, feature.height));
                        }
                        else if (feature.type == EType::River)
                        {
                            drawLabel(firstScreen, lineColor,
                                      fmt::format("河流  width={:.1f}  depth={:.1f}", feature.width, feature.depth));
                        }
                        else
                        {
                            drawLabel(firstScreen, lineColor, fmt::format("道路  width={:.1f}", feature.width));
                        }
                    }
                }
                if (selected && feature.pts.size() >= 2)
                {
                    const glm::dvec2 delta = feature.pts[1] - feature.pts[0];
                    const double length = glm::length(delta);
                    if (length > 1e-6)
                    {
                        const glm::dvec2 side(-delta.y / length, delta.x / length);
                        const glm::dvec2 midpoint = (feature.pts[0] + feature.pts[1]) * 0.5;
                        const glm::dvec2 widthPoint = midpoint + side * feature.width * 0.5;
                        drawTerrainSegment(midpoint, widthPoint, lineColor, 2.0f);
                        addHandle(featureIndex, -4, widthPoint);
                        ImVec2 widthScreen;
                        if (projectWorld(worldAt(widthPoint, 0.45), widthScreen))
                        {
                            drawLabel(widthScreen, lineColor, fmt::format("width={:.1f}", feature.width));
                        }
                    }
                    if (feature.type == EType::Ridge)
                    {
                        drawFeatureVerticalRuler(featureIndex, feature.pts.front(), feature.height, "h", lineColor);
                    }
                    else if (feature.type == EType::River)
                    {
                        drawFeatureVerticalRuler(featureIndex, feature.pts.front(), feature.depth, "depth", lineColor);
                    }
                }
            }
            else if (feature.type == EType::Pad)
            {
                const double angle = glm::radians(feature.rot);
                const glm::dvec2 axisX(std::cos(angle), std::sin(angle));
                const glm::dvec2 axisY(-axisX.y, axisX.x);
                const glm::dvec2 halfX = axisX * feature.size.x * 0.5;
                const glm::dvec2 halfY = axisY * feature.size.y * 0.5;
                const std::vector<glm::dvec2> outline = {
                    feature.at - halfX - halfY,
                    feature.at + halfX - halfY,
                    feature.at + halfX + halfY,
                    feature.at - halfX + halfY,
                };
                drawTerrainPolyline(outline, true, lineColor, thickness);
                addHandle(featureIndex, -1, feature.at);
                if (selected)
                {
                    ImVec2 centerScreen;
                    if (projectWorld(worldAt(feature.at, 0.45), centerScreen))
                    {
                        drawLabel(centerScreen, lineColor,
                                  fmt::format("基座  {:.1f} × {:.1f}  rot={:.1f}°", feature.size.x, feature.size.y,
                                              feature.rot));
                    }
                }
            }
        }

        const auto addRuleHandle = [&](int ruleIndex, int pointIndex, const glm::dvec2& point)
        {
            ImVec2 screen;
            const glm::vec3 world = worldAt(point, 0.65);
            if (projectWorld(world, screen))
            {
                terrainRuleHandles_.push_back({ruleIndex, pointIndex, glm::vec2(screen.x, screen.y), world.y});
            }
        };
        const auto addRuleWorldHandle = [&](int ruleIndex, int pointIndex, const glm::vec3& world)
        {
            ImVec2 screen;
            if (projectWorld(world, screen))
            {
                terrainRuleHandles_.push_back({ruleIndex, pointIndex, glm::vec2(screen.x, screen.y), world.y});
            }
        };
        const auto drawRuleMarker = [&](const glm::dvec2& point, ImU32 color, float radius)
        {
            ImVec2 screen;
            if (projectWorld(worldAt(point, 0.62), screen))
            {
                drawList->AddCircleFilled(screen, radius, IM_COL32(15, 22, 28, 210));
                drawList->AddCircle(screen, radius, color, 0, 2.0f);
                drawList->AddLine(ImVec2(screen.x - radius - 3.0f, screen.y),
                                  ImVec2(screen.x + radius + 3.0f, screen.y), color, 1.4f);
                drawList->AddLine(ImVec2(screen.x, screen.y - radius - 3.0f),
                                  ImVec2(screen.x, screen.y + radius + 3.0f), color, 1.4f);
            }
        };
        const auto childSummary = [](const std::string& child)
        {
            std::string summary = child;
            summary.erase(std::remove(summary.begin(), summary.end(), '\n'), summary.end());
            if (summary.size() > 38)
            {
                summary.resize(38);
                summary += "…";
            }
            return summary;
        };
        const auto positiveMod = [](int64_t value, int64_t divisor)
        {
            const int64_t remainder = value % divisor;
            return remainder < 0 ? remainder + divisor : remainder;
        };
        const auto layHash = [&](int64_t value)
        {
            const auto square = [&](int64_t x)
            {
                x = positiveMod(x, 65521);
                return positiveMod(x * x + x * 587 + 41, 65521);
            };
            return square(square(positiveMod(value, 65521)) + 13);
        };
        const auto layRandF = [&](int64_t seed, int64_t index)
        {
            const int64_t value =
                positiveMod(layHash(seed + index * 131) * 65521 + layHash(seed * 3 + index * 977 + 5), 9973);
            return static_cast<double>(value) / 9972.0;
        };
        const auto biomeId = [](std::string_view name)
        {
            constexpr std::string_view names[] = {"grass",     "grass_dark", "dry_grass", "sand", "rock",
                                                  "rock_high", "snow",       "bed",       "road", "pad"};
            for (int index = 0; index < static_cast<int>(std::size(names)); ++index)
            {
                if (name == names[index])
                {
                    return index;
                }
            }
            return -1;
        };
        const auto scatterAccepts = [&](const FTerrainProcessRule& rule, const glm::dvec2& point)
        {
            if (!terrainFeatureOverlayData_)
            {
                return true;
            }
            double height = 0.0;
            double slope = 0.0;
            bool water = false;
            uint8_t biome = 0;
            terrainFeatureOverlayData_->InfoAt(point.x, point.y, height, slope, water, biome);
            const uint8_t pointBiome = biome;
            if (height < rule.minHeight || height > rule.maxHeight || slope > rule.maxSlope)
            {
                return false;
            }
            if (rule.avoidWater >= 0.0)
            {
                if (water)
                {
                    return false;
                }
                if (rule.avoidWater > 0.0)
                {
                    for (const glm::dvec2& offset :
                         {glm::dvec2(rule.avoidWater, 0.0), glm::dvec2(-rule.avoidWater, 0.0),
                          glm::dvec2(0.0, rule.avoidWater), glm::dvec2(0.0, -rule.avoidWater)})
                    {
                        bool nearbyWater = false;
                        terrainFeatureOverlayData_->InfoAt(point.x + offset.x, point.y + offset.y, height, slope,
                                                           nearbyWater, biome);
                        if (nearbyWater)
                        {
                            return false;
                        }
                    }
                }
            }
            if (rule.biomes.empty())
            {
                return true;
            }
            return std::any_of(rule.biomes.begin(), rule.biomes.end(),
                               [&](const std::string& name) { return biomeId(name) == pointBiome; });
        };

        std::vector<FTerrainProcessRule>& rules = TerrainProcess().Rules();
        selectedTerrainRule_ = std::clamp(selectedTerrainRule_, 0, std::max(0, static_cast<int>(rules.size()) - 1));
        for (int ruleIndex = 0; ruleIndex < static_cast<int>(rules.size()); ++ruleIndex)
        {
            FTerrainProcessRule& rule = rules[ruleIndex];
            if (rule.removed)
            {
                continue;
            }
            const bool selected = terrainSelectionIsRule_ && ruleIndex == selectedTerrainRule_;
            const ImU32 color = selected ? IM_COL32(80, 255, 150, 255) : IM_COL32(80, 255, 150, 105);
            const ImU32 sampleColor = selected ? IM_COL32(255, 255, 255, 245) : IM_COL32(190, 255, 215, 130);
            const float thickness = selected ? 2.2f : 1.1f;
            const glm::dvec2 position(rule.x, rule.y);

            if (rule.type == ETerrainProcessRuleType::HeightAnchor)
            {
                const glm::dvec2 sample(rule.sampleX, rule.sampleY);
                drawTerrainSegment(position, sample, color, thickness);
                drawRuleMarker(position, color, selected ? 7.0f : 5.0f);
                drawRuleMarker(sample, IM_COL32(255, 255, 255, selected ? 255 : 120), selected ? 6.0f : 4.0f);
                addRuleHandle(ruleIndex, -1, position);
                addRuleHandle(ruleIndex, -2, sample);
            }
            else if (rule.type == ETerrainProcessRuleType::Place || rule.type == ETerrainProcessRuleType::PlaceTilt ||
                     rule.type == ETerrainProcessRuleType::Snap)
            {
                drawRuleMarker(position, color, selected ? 7.0f : 5.0f);
                addRuleHandle(ruleIndex, -1, position);
                if (rule.type == ETerrainProcessRuleType::PlaceTilt)
                {
                    drawTerrainPolyline(circlePoints(position, rule.probe), true, color, thickness);
                }
            }
            else if (rule.type == ETerrainProcessRuleType::Along)
            {
                drawTerrainPolyline(rule.points, false, color, thickness);
                for (int pointIndex = 0; pointIndex < static_cast<int>(rule.points.size()); ++pointIndex)
                {
                    addRuleHandle(ruleIndex, pointIndex, rule.points[pointIndex]);
                }
                int sampleCount = 0;
                for (size_t segment = 0; selected && segment + 1 < rule.points.size() && sampleCount < 1000; ++segment)
                {
                    const glm::dvec2 delta = rule.points[segment + 1] - rule.points[segment];
                    const double length = glm::length(delta);
                    if (length <= 1e-6 || rule.step <= 0.0)
                    {
                        continue;
                    }
                    const int count = static_cast<int>(std::floor((length - rule.offset) / rule.step));
                    for (int sample = 0; sample <= count && sampleCount < 1000; ++sample, ++sampleCount)
                    {
                        const glm::dvec2 point =
                            rule.points[segment] + delta * ((rule.offset + sample * rule.step) / length);
                        ImVec2 screen;
                        if (projectWorld(worldAt(point, 0.72), screen))
                        {
                            drawList->AddNgon(screen, 4.0f, sampleColor, 4, 1.7f);
                        }
                    }
                }
            }
            else if (rule.type == ETerrainProcessRuleType::Scatter)
            {
                const glm::dvec2 regionCenter = rule.circularRegion
                    ? rule.regionCenter
                    : glm::dvec2((rule.region.x + rule.region.z) * 0.5, (rule.region.y + rule.region.w) * 0.5);
                if (rule.circularRegion)
                {
                    drawTerrainPolyline(circlePoints(rule.regionCenter, rule.regionRadius), true, color, thickness);
                    drawTerrainSegment(rule.regionCenter, rule.regionCenter + glm::dvec2(rule.regionRadius, 0.0), color,
                                       thickness);
                    addRuleHandle(ruleIndex, -3, rule.regionCenter);
                    addRuleHandle(ruleIndex, -4, rule.regionCenter + glm::dvec2(rule.regionRadius, 0.0));
                }
                else
                {
                    const std::vector<glm::dvec2> region = {{rule.region.x, rule.region.y},
                                                            {rule.region.z, rule.region.y},
                                                            {rule.region.z, rule.region.w},
                                                            {rule.region.x, rule.region.w}};
                    drawTerrainPolyline(region, true, color, thickness);
                    addRuleHandle(ruleIndex, -3, region[0]);
                    addRuleHandle(ruleIndex, -4, region[2]);
                }
                if (selected)
                {
                    constexpr double itemsPerWorldUnit = 10.0;
                    const double base = surfaceHeight(regionCenter) + 0.65;
                    const double rulerHeight =
                        std::clamp(static_cast<double>(rule.count) / itemsPerWorldUnit, 2.0, 60.0);
                    const glm::vec3 rulerBottom =
                        Assets::Scad::ScadToWorldPos(glm::dvec3(regionCenter.x, regionCenter.y, base), 1.0);
                    const glm::vec3 rulerTop = Assets::Scad::ScadToWorldPos(
                        glm::dvec3(regionCenter.x, regionCenter.y, base + rulerHeight), 1.0);
                    ImVec2 bottomScreen;
                    ImVec2 topScreen;
                    if (projectWorld(rulerBottom, bottomScreen) && projectWorld(rulerTop, topScreen))
                    {
                        drawList->AddLine(bottomScreen, topScreen, color, 2.0f);
                        const float halfSize = 4.5f;
                        drawList->AddRectFilled(ImVec2(topScreen.x - halfSize, topScreen.y - halfSize),
                                                ImVec2(topScreen.x + halfSize, topScreen.y + halfSize), color, 1.5f);
                        drawLabel(topScreen, color, fmt::format("count={}", rule.count));
                        addRuleWorldHandle(ruleIndex, -5, rulerTop);
                    }
                }
                int accepted = 0;
                const int candidateCount = selected ? std::min(std::max(0, rule.count) * 4, 20000) : 0;
                for (int candidate = 0; candidate < candidateCount && accepted < std::min(rule.count, 1000);
                     ++candidate)
                {
                    const int64_t candidateSeed = static_cast<int64_t>(rule.seed) * 8191 + candidate * 131;
                    glm::dvec2 point;
                    if (rule.circularRegion)
                    {
                        const double radius = std::sqrt(layRandF(candidateSeed, 1)) * rule.regionRadius;
                        const double angle = glm::two_pi<double>() * layRandF(candidateSeed, 2);
                        point = rule.regionCenter + radius * glm::dvec2(std::cos(angle), std::sin(angle));
                    }
                    else
                    {
                        point = {rule.region.x + (rule.region.z - rule.region.x) * layRandF(candidateSeed, 1),
                                 rule.region.y + (rule.region.w - rule.region.y) * layRandF(candidateSeed, 2)};
                    }
                    if (!scatterAccepts(rule, point))
                    {
                        continue;
                    }
                    ++accepted;
                    ImVec2 screen;
                    if (projectWorld(worldAt(point, 0.7), screen))
                    {
                        drawList->AddCircleFilled(screen, selected ? 3.2f : 2.2f, sampleColor);
                    }
                }
            }

            if (selected)
            {
                glm::dvec2 labelPoint = position;
                if (rule.type == ETerrainProcessRuleType::Along && !rule.points.empty())
                    labelPoint = rule.points.front();
                if (rule.type == ETerrainProcessRuleType::Scatter)
                    labelPoint = rule.circularRegion ? rule.regionCenter : glm::dvec2(rule.region.x, rule.region.y);
                ImVec2 screen;
                if (projectWorld(worldAt(labelPoint, 0.8), screen))
                {
                    drawLabel(screen, color,
                              fmt::format("{}  {}", FTerrainProcessDocument::RuleTypeName(rule.type),
                                          childSummary(rule.childSource)));
                }
            }
        }

        const glm::vec2 mouse(ImGui::GetIO().MousePos.x, ImGui::GetIO().MousePos.y);
        const ImVec2 framebufferMousePoint = NextUI::Scaling::ImGuiToMainFramebufferPoint(
            ImVec2(mouse.x, mouse.y));
        const glm::vec2 framebufferMouse(framebufferMousePoint.x, framebufferMousePoint.y);
        int hoveredHandle = -1;
        float nearestDistance = 144.0f;
        for (int handleIndex = 0; handleIndex < static_cast<int>(terrainFeatureHandles_.size()); ++handleIndex)
        {
            const FTerrainFeatureHandle& handle = terrainFeatureHandles_[handleIndex];
            const float distance = glm::distance2(mouse, handle.screen);
            if (distance <= nearestDistance)
            {
                hoveredHandle = handleIndex;
                nearestDistance = distance;
            }
        }

        for (int handleIndex = 0; handleIndex < static_cast<int>(terrainFeatureHandles_.size()); ++handleIndex)
        {
            const FTerrainFeatureHandle& handle = terrainFeatureHandles_[handleIndex];
            const bool selected = !terrainSelectionIsRule_ && handle.featureIndex == selectedTerrainFeature_;
            const bool hovered = handleIndex == hoveredHandle;
            const Assets::Scad::FTerrainFeature& feature = terrain.features[handle.featureIndex];
            const ImU32 color = colorForFeature(feature.type, selected ? 255 : 150);
            const ImVec2 screen(handle.screen.x, handle.screen.y);
            drawList->AddCircleFilled(screen, hovered ? 7.0f : (selected ? 5.5f : 3.5f),
                                      hovered ? IM_COL32(255, 255, 255, 255) : color);
            drawList->AddCircle(screen, hovered ? 7.0f : (selected ? 5.5f : 3.5f), IM_COL32(12, 18, 26, 230), 0, 1.5f);
        }
        int hoveredRuleHandle = -1;
        nearestDistance = 144.0f;
        for (int handleIndex = 0; handleIndex < static_cast<int>(terrainRuleHandles_.size()); ++handleIndex)
        {
            const float distance = glm::distance2(mouse, terrainRuleHandles_[handleIndex].screen);
            if (distance <= nearestDistance)
            {
                hoveredRuleHandle = handleIndex;
                nearestDistance = distance;
            }
        }
        for (int handleIndex = 0; handleIndex < static_cast<int>(terrainRuleHandles_.size()); ++handleIndex)
        {
            const FTerrainRuleHandle& handle = terrainRuleHandles_[handleIndex];
            const bool selected = terrainSelectionIsRule_ && handle.ruleIndex == selectedTerrainRule_;
            const bool hovered = handleIndex == hoveredRuleHandle;
            const ImVec2 screen(handle.screen.x, handle.screen.y);
            const float radius = hovered ? 7.0f : (selected ? 5.5f : 3.5f);
            drawList->AddRectFilled(
                ImVec2(screen.x - radius, screen.y - radius), ImVec2(screen.x + radius, screen.y + radius),
                hovered ? IM_COL32(255, 255, 255, 255) : IM_COL32(80, 255, 150, selected ? 255 : 150), 1.5f);
            drawList->AddRect(ImVec2(screen.x - radius, screen.y - radius),
                              ImVec2(screen.x + radius, screen.y + radius), IM_COL32(12, 18, 26, 230), 1.5f, 0, 1.5f);
        }

        const bool mouseInside = mouse.x >= viewportPos.x && mouse.y >= viewportPos.y &&
            mouse.x < viewportPos.x + viewportSize.x && mouse.y < viewportPos.y + viewportSize.y;
        if (hoveredHandle >= 0 || hoveredRuleHandle >= 0 || terrainFeatureDragging_ || terrainRuleDragging_)
        {
            ImGui::GetIO().WantCaptureMouse = true;
        }
        if (!terrainFeatureDragging_ && !terrainRuleDragging_ && hoveredRuleHandle >= 0 && mouseInside &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            const FTerrainRuleHandle& handle = terrainRuleHandles_[hoveredRuleHandle];
            const bool alreadySelected = terrainSelectionIsRule_ && selectedTerrainRule_ == handle.ruleIndex;
            selectedTerrainRule_ = handle.ruleIndex;
            terrainSelectionIsRule_ = true;
            scrollToSelectedTerrainItem_ = true;
            if (alreadySelected)
            {
                terrainRuleDragPoint_ = handle.pointIndex;
                terrainRuleDragPlaneHeight_ = handle.worldHeight;
                terrainDragStartMouse_ = mouse;
                terrainDragCopyRequested_ =
                    ImGui::IsKeyDown(ImGuiKey_LeftShift) || ImGui::IsKeyDown(ImGuiKey_RightShift);
                terrainDragCopied_ = false;
                if (handle.pointIndex == -5)
                {
                    terrainDragStartValue_ = rules[handle.ruleIndex].count;
                }
                terrainRuleDragging_ = true;
            }
        }
        if (!terrainFeatureDragging_ && hoveredRuleHandle < 0 && hoveredHandle >= 0 && mouseInside &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            const FTerrainFeatureHandle& handle = terrainFeatureHandles_[hoveredHandle];
            const bool alreadySelected = !terrainSelectionIsRule_ && selectedTerrainFeature_ == handle.featureIndex;
            selectedTerrainFeature_ = handle.featureIndex;
            terrainSelectionIsRule_ = false;
            scrollToSelectedTerrainItem_ = true;
            if (alreadySelected)
            {
                terrainFeatureDragPoint_ = handle.pointIndex;
                terrainFeatureDragPlaneHeight_ = handle.worldHeight;
                terrainDragStartMouse_ = mouse;
                terrainDragCopyRequested_ =
                    ImGui::IsKeyDown(ImGuiKey_LeftShift) || ImGui::IsKeyDown(ImGuiKey_RightShift);
                terrainDragCopied_ = false;
                if (handle.pointIndex == -3)
                {
                    const Assets::Scad::FTerrainFeature& feature = terrain.features[handle.featureIndex];
                    terrainDragStartValue_ =
                        feature.type == EType::Lake || feature.type == EType::River ? feature.depth : feature.height;
                }
                terrainFeatureDragging_ = true;
            }
        }

        if (terrainFeatureDragging_)
        {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left) && glm::distance2(mouse, terrainDragStartMouse_) > 4.0f)
            {
                if (terrainDragCopyRequested_ && !terrainDragCopied_ && selectedTerrainFeature_ >= 0 &&
                    selectedTerrainFeature_ < static_cast<int>(terrain.features.size()))
                {
                    terrain.features.push_back(terrain.features[selectedTerrainFeature_]);
                    selectedTerrainFeature_ = static_cast<int>(terrain.features.size()) - 1;
                    scrollToSelectedTerrainItem_ = true;
                    terrainDragCopied_ = true;
                    MarkTerrainProcessDirty();
                }
                if (selectedTerrainFeature_ >= 0 &&
                    selectedTerrainFeature_ < static_cast<int>(terrain.features.size()) &&
                    terrainFeatureDragPoint_ == -3)
                {
                    Assets::Scad::FTerrainFeature& feature = terrain.features[selectedTerrainFeature_];
                    const double value =
                        std::max(0.0, terrainDragStartValue_ + (terrainDragStartMouse_.y - mouse.y) * 0.2);
                    if (feature.type == EType::Lake || feature.type == EType::River)
                    {
                        feature.depth = value;
                    }
                    else
                    {
                        feature.height = value;
                    }
                    MarkTerrainProcessDirty();
                }
                else
                {
                    glm::vec3 rayOrigin;
                    glm::vec3 rayDirection;
                    Runtime::EngineHelper::GetScreenToWorldRay(framebufferMouse, rayOrigin, rayDirection);
                    if (std::abs(rayDirection.y) > 1e-5f)
                    {
                        const float distance = (terrainFeatureDragPlaneHeight_ - rayOrigin.y) / rayDirection.y;
                        if (distance > 0.0f && selectedTerrainFeature_ >= 0 &&
                            selectedTerrainFeature_ < static_cast<int>(terrain.features.size()))
                        {
                            const glm::vec3 hit = rayOrigin + rayDirection * distance;
                            const glm::dvec2 scadPoint(hit.x, -hit.z);
                            Assets::Scad::FTerrainFeature& feature = terrain.features[selectedTerrainFeature_];
                            if (terrainFeatureDragPoint_ == -2)
                            {
                                feature.radius = std::max(0.1, glm::distance(feature.at, scadPoint));
                            }
                            else if (terrainFeatureDragPoint_ == -1)
                            {
                                feature.at = scadPoint;
                            }
                            else if (terrainFeatureDragPoint_ == -4 && feature.pts.size() >= 2)
                            {
                                const glm::dvec2 delta = feature.pts[1] - feature.pts[0];
                                const double length = glm::length(delta);
                                if (length > 1e-6)
                                {
                                    const glm::dvec2 side(-delta.y / length, delta.x / length);
                                    const glm::dvec2 midpoint = (feature.pts[0] + feature.pts[1]) * 0.5;
                                    feature.width = std::max(0.1, 2.0 * std::abs(glm::dot(scadPoint - midpoint, side)));
                                }
                            }
                            else if (terrainFeatureDragPoint_ >= 0 &&
                                     terrainFeatureDragPoint_ < static_cast<int>(feature.pts.size()))
                            {
                                feature.pts[terrainFeatureDragPoint_] = scadPoint;
                            }
                            MarkTerrainProcessDirty();
                        }
                    }
                }
            }
            if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
            {
                terrainFeatureDragging_ = false;
                terrainDragCopyRequested_ = false;
                terrainDragCopied_ = false;
            }
        }
        if (terrainRuleDragging_)
        {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left) && glm::distance2(mouse, terrainDragStartMouse_) > 4.0f)
            {
                if (terrainDragCopyRequested_ && !terrainDragCopied_ && selectedTerrainRule_ >= 0 &&
                    selectedTerrainRule_ < static_cast<int>(rules.size()))
                {
                    TerrainProcess().DuplicateRule(static_cast<size_t>(selectedTerrainRule_), false);
                    selectedTerrainRule_ = static_cast<int>(rules.size()) - 1;
                    scrollToSelectedTerrainItem_ = true;
                    terrainDragCopied_ = true;
                    MarkTerrainProcessDirty();
                }
                if (selectedTerrainRule_ >= 0 && selectedTerrainRule_ < static_cast<int>(rules.size()) &&
                    terrainRuleDragPoint_ == -5)
                {
                    FTerrainProcessRule& rule = rules[selectedTerrainRule_];
                    rule.count = std::clamp(static_cast<int>(std::lround(terrainDragStartValue_ +
                                                                         (terrainDragStartMouse_.y - mouse.y) * 2.0)),
                                            0, 100000);
                    MarkTerrainProcessDirty();
                }
                else
                {
                    glm::vec3 rayOrigin;
                    glm::vec3 rayDirection;
                    Runtime::EngineHelper::GetScreenToWorldRay(framebufferMouse, rayOrigin, rayDirection);
                    if (std::abs(rayDirection.y) > 1e-5f)
                    {
                        const float distance = (terrainRuleDragPlaneHeight_ - rayOrigin.y) / rayDirection.y;
                        if (distance > 0.0f && selectedTerrainRule_ >= 0 &&
                            selectedTerrainRule_ < static_cast<int>(rules.size()))
                        {
                            const glm::vec3 hit = rayOrigin + rayDirection * distance;
                            const glm::dvec2 point(hit.x, -hit.z);
                            FTerrainProcessRule& rule = rules[selectedTerrainRule_];
                            if (terrainRuleDragPoint_ == -1)
                            {
                                rule.x = point.x;
                                rule.y = point.y;
                            }
                            else if (terrainRuleDragPoint_ == -2)
                            {
                                rule.sampleX = point.x;
                                rule.sampleY = point.y;
                            }
                            else if (terrainRuleDragPoint_ == -3)
                            {
                                if (rule.circularRegion)
                                    rule.regionCenter = point;
                                else
                                {
                                    rule.region.x = point.x;
                                    rule.region.y = point.y;
                                }
                            }
                            else if (terrainRuleDragPoint_ == -4)
                            {
                                if (rule.circularRegion)
                                    rule.regionRadius = std::max(0.1, glm::distance(rule.regionCenter, point));
                                else
                                {
                                    rule.region.z = point.x;
                                    rule.region.w = point.y;
                                }
                            }
                            else if (terrainRuleDragPoint_ >= 0 &&
                                     terrainRuleDragPoint_ < static_cast<int>(rule.points.size()))
                            {
                                rule.points[terrainRuleDragPoint_] = point;
                            }
                            MarkTerrainProcessDirty();
                        }
                    }
                }
            }
            if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
            {
                terrainRuleDragging_ = false;
                terrainDragCopyRequested_ = false;
                terrainDragCopied_ = false;
            }
        }

        drawList->PopClipRect();
    }

    void ScadLibraryInterface::CommitSceneGizmoEdit()
    {
        engine_.GetScene().MarkDirty();
        benchDirty_ = false;
        assemblySourceDirty_ = true;
        // Releasing the gizmo writes the moved instance back into the file it
        // came from. Only that statement is rewritten, so a scene that also
        // holds loops, a terrain or comments keeps them byte for byte.
        if (!openedAssemblyPath_.empty())
        {
            SaveAssembly(false, false);
            return;
        }

        if (std::string_view(assemblyPathBuf_).empty())
        {
            std::snprintf(assemblyPathBuf_, sizeof(assemblyPathBuf_), "%s", "assets/scad/evaluated/my_scene.scad");
        }
        SaveAssembly(true, false);
    }

    bool ScadLibraryInterface::GetSelectedSceneObjectBounds(glm::vec3& center, float& radius)
    {
        // A standalone module preview has no FBenchItem. Its only editable
        // selection is the module shown in the preview scene, so use that
        // scene's bounds as the focus target.
        if (modulePreviewActive_ &&
            (workspaceMode_ == EWorkspaceMode::SceneAssembly || workspaceMode_ == EWorkspaceMode::KitBrowser))
        {
            const Assets::Scene& scene = engine_.GetScene();
            const glm::vec3 minBounds = scene.GetSceneAABBMin();
            const glm::vec3 maxBounds = scene.GetSceneAABBMax();
            if (!glm::all(glm::lessThan(minBounds, maxBounds)))
            {
                return false;
            }
            center = (minBounds + maxBounds) * 0.5f;
            radius = std::max(glm::length(maxBounds - minBounds) * 0.5f, 0.5f);
            return true;
        }

        // Source/module rows in the structure Outliner are represented by a
        // cached world-space OBB rather than an FBenchItem. The overlay already
        // uses these bounds; use the same selection for F so both paths focus
        // the object the user sees selected.
        if (workspaceMode_ == EWorkspaceMode::SceneAssembly && selectedStructureBoundsValid_ &&
            selectedStructureBoundsSegment_ == selectedSegment_)
        {
            center = (selectedStructureBoundsMin_ + selectedStructureBoundsMax_) * 0.5f;
            radius = std::max(glm::length(selectedStructureBoundsMax_ - selectedStructureBoundsMin_) * 0.5f, 0.05f);
            return true;
        }

        if (workspaceMode_ != EWorkspaceMode::SceneAssembly || selectedBenchItem_ < 0 ||
            selectedBenchItem_ >= static_cast<int>(Bench().size()))
        {
            return false;
        }

        FBenchItem& item = Bench()[selectedBenchItem_];
        Assets::Node* selectedNode = ResolveSceneObjectNode(item, SceneObjectWorldMatrix(item));
        if (selectedNode == nullptr)
        {
            // The document selection is authoritative in ScadLibrary: the
            // engine selection is intentionally cleared to suppress its edge
            // outline. A temporarily unresolved runtime node should still let
            // F frame the selected instance at its authored transform.
            center = glm::vec3(SceneObjectWorldMatrix(item)[3]);
            radius = 0.5f;
            return true;
        }

        glm::vec3 localMin;
        glm::vec3 localMax;
        if (!GetNodeOrientedBounds(engine_.GetScene(), *selectedNode, localMin, localMax))
        {
            center = glm::vec3(selectedNode->WorldTransform()[3]);
            radius = 0.5f;
            return true;
        }

        const glm::mat4& world = selectedNode->WorldTransform();
        center = glm::vec3(world * glm::vec4((localMin + localMax) * 0.5f, 1.0f));
        radius = 0.001f;
        for (int corner = 0; corner < 8; ++corner)
        {
            const glm::vec3 localPoint((corner & 1) != 0 ? localMax.x : localMin.x,
                                       (corner & 2) != 0 ? localMax.y : localMin.y,
                                       (corner & 4) != 0 ? localMax.z : localMin.z);
            const glm::vec3 worldPoint = glm::vec3(world * glm::vec4(localPoint, 1.0f));
            radius = std::max(radius, glm::length(worldPoint - center));
        }
        return true;
    }

    bool ScadLibraryInterface::SelectSceneObjectFromViewport(const glm::vec3& rayOrigin, const glm::vec3& rayDirection)
    {
        if (workspaceMode_ != EWorkspaceMode::SceneAssembly || Bench().empty())
        {
            return false;
        }

        Assets::Scene& scene = engine_.GetScene();
        int hitIndex = -1;
        float nearestDistance = std::numeric_limits<float>::max();
        for (int index = 0; index < static_cast<int>(Bench().size()); ++index)
        {
            FBenchItem& item = Bench()[index];
            Assets::Node* node = ResolveSceneObjectNode(item, SceneObjectWorldMatrix(item));
            if (node == nullptr)
            {
                continue;
            }

            glm::vec3 localMin;
            glm::vec3 localMax;
            if (!GetNodeOrientedBounds(scene, *node, localMin, localMax))
            {
                continue;
            }

            float distance = 0.0f;
            if (IntersectOrientedBox(rayOrigin, rayDirection, node->WorldTransform(), localMin, localMax, distance) &&
                distance < nearestDistance)
            {
                hitIndex = index;
                nearestDistance = distance;
            }
        }

        if (hitIndex < 0)
        {
            ClearEditableSceneSelection();
            ClearSelectedStructureBounds();
            selectedSegment_ = -1;
            statusLine_ = "未命中可编辑结构节点的 OBB";
            statusError_ = false;
            return false;
        }

        FBenchItem& selected = Bench()[hitIndex];
        ClearSelectedStructureBounds();
        selectedBenchItem_ = hitIndex;
        selectedSegment_ = selected.segmentIndex;
        scrollToSelectedSegment_ = selectedSegment_ >= 0;
        sceneGizmoAwaitingPickRelease_ = true;
        scene.ClearSelection();
        engine_.GetShowFlags().ShowEdge = false;
        statusLine_ = fmt::format("已从视口选择结构节点 {}", selected.moduleName);
        statusError_ = false;
        return true;
    }

    Assets::Node* ScadLibraryInterface::ResolveSceneObjectNode(FBenchItem& item, const glm::mat4& expectedWorld)
    {
        Assets::Scene& scene = engine_.GetScene();
        if (item.runtimeNodeId != std::numeric_limits<uint32_t>::max())
        {
            Assets::Node* resolved = scene.GetNodeByInstanceId(item.runtimeNodeId);
            if (resolved != nullptr && resolved->GetName() == item.moduleName)
            {
                return resolved;
            }
            item.runtimeNodeId = std::numeric_limits<uint32_t>::max();
        }

        Assets::Node* nearest = nullptr;
        float nearestDistance = std::numeric_limits<float>::max();
        for (const std::shared_ptr<Assets::Node>& candidate : scene.Nodes())
        {
            if (candidate == nullptr || candidate->GetName() != item.moduleName)
            {
                continue;
            }

            float distance = 0.0f;
            const glm::mat4& candidateWorld = candidate->WorldTransform();
            for (int column = 0; column < 4; ++column)
            {
                const glm::vec4 delta = candidateWorld[column] - expectedWorld[column];
                distance += glm::dot(delta, delta);
            }
            if (distance < nearestDistance)
            {
                nearestDistance = distance;
                nearest = candidate.get();
            }
        }
        if (nearest != nullptr)
        {
            item.runtimeNodeId = nearest->GetInstanceId();
        }
        return nearest;
    }

    void ScadLibraryInterface::ApplySceneObjectTransform(FBenchItem& item, const glm::mat4& worldMatrix)
    {
        Assets::Node* node = ResolveSceneObjectNode(item, worldMatrix);
        if (node == nullptr)
        {
            return;
        }

        glm::mat4 parentWorld(1.0f);
        if (Assets::Node* parent = node->GetParent())
        {
            parentWorld = parent->WorldTransform();
        }
        const glm::mat4 localMatrix = glm::inverse(parentWorld) * worldMatrix;

        glm::vec3 scale(1.0f);
        glm::quat rotation(1.0f, 0.0f, 0.0f, 0.0f);
        glm::vec3 translation(0.0f);
        glm::vec3 skew(0.0f);
        glm::vec4 perspective(0.0f);
        if (!glm::decompose(localMatrix, scale, rotation, translation, skew, perspective))
        {
            return;
        }

        node->SetTranslation(translation);
        node->SetRotation(glm::normalize(rotation));
        node->SetScale(scale);
        engine_.GetScene().MarkTransformDirty();
    }

    void ScadLibraryInterface::DrawSceneObjectGizmo(const ImVec2& viewportPos, const ImVec2& viewportSize)
    {
        if (viewportSize.x <= 1.0f || viewportSize.y <= 1.0f || selectedBenchItem_ < 0 ||
            selectedBenchItem_ >= static_cast<int>(Bench().size()))
        {
            sceneGizmoWasUsing_ = false;
            return;
        }

        FBenchItem& item = Bench()[selectedBenchItem_];
        glm::mat4 worldMatrix = SceneObjectWorldMatrix(item);
        Assets::Node* selectedNode = ResolveSceneObjectNode(item, worldMatrix);
        if (selectedNode != nullptr)
        {
            glm::vec3 localMin(FLT_MAX);
            glm::vec3 localMax(-FLT_MAX);
            bool foundBounds = false;
            AccumulateNodeLocalBounds(engine_.GetScene(), *selectedNode,
                                      glm::inverse(selectedNode->WorldTransform()), localMin, localMax, foundBounds);
            if (foundBounds)
            {
                DrawOrientedBoxOverlay(engine_.GetLastUniformBufferObject(), viewportPos, viewportSize,
                                       selectedNode->WorldTransform(), localMin, localMax,
                                       IM_COL32(255, 184, 41, 255));
            }
        }
        if (sceneGizmoAwaitingPickRelease_)
        {
            if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
            {
                sceneGizmoAwaitingPickRelease_ = false;
            }
            sceneGizmoWasUsing_ = false;
            return;
        }

        const Assets::UniformBufferObject& ubo = engine_.GetLastUniformBufferObject();
        const glm::mat4& view = ubo.ModelView;
        glm::mat4 projection = ubo.ProjectionUnJit;
        projection[1][1] *= -1.0f;

        constexpr ImGuizmo::OPERATION operations[] = {ImGuizmo::TRANSLATE, ImGuizmo::ROTATE};
        sceneGizmoOperation_ = std::clamp(sceneGizmoOperation_, 0, 1);
        ImGuizmo::SetAlternativeWindow(nullptr);
        ImGuizmo::SetOrthographic(false);
        ImGuizmo::BeginFrame();
        ImGuizmo::SetDrawlist(ImGui::GetForegroundDrawList());
        ImGuizmo::SetRect(viewportPos.x, viewportPos.y, viewportSize.x, viewportSize.y);
        ImGuizmo::GetStyle().Colors[ImGuizmo::COLOR::SELECTION] = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
        ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(projection), operations[sceneGizmoOperation_],
                             ImGuizmo::LOCAL, glm::value_ptr(worldMatrix));

        const bool usingNow = ImGuizmo::IsUsing();
        const bool interacting = ImGuizmo::IsOver() || usingNow;
        ImGui::GetIO().WantCaptureMouse = ImGui::GetIO().WantCaptureMouse || interacting;
        if (usingNow)
        {
            const glm::dmat4 basis = Assets::Scad::ScadToWorldBasis(1.0);
            const glm::dmat4 editedScad = glm::inverse(basis) * glm::dmat4(worldMatrix) * basis;
            glm::dvec3 scale(1.0);
            glm::dquat orientation(1.0, 0.0, 0.0, 0.0);
            glm::dvec3 translation(0.0);
            glm::dvec3 skew(0.0);
            glm::dvec4 perspective(0.0);
            if (glm::decompose(editedScad, scale, orientation, translation, skew, perspective))
            {
                orientation = glm::normalize(orientation);
                double angleZ = 0.0;
                double angleY = 0.0;
                double angleX = 0.0;
                glm::extractEulerAngleZYX(glm::toMat4(orientation), angleZ, angleY, angleX);
                item.x = static_cast<float>(translation.x);
                item.y = static_cast<float>(translation.y);
                item.z = static_cast<float>(translation.z);
                item.rotX = static_cast<float>(glm::degrees(angleX));
                item.rotY = static_cast<float>(glm::degrees(angleY));
                item.rotZ = static_cast<float>(glm::degrees(angleZ));
                item.scale = static_cast<float>(scale.x);
                item.scaleY = static_cast<float>(scale.y);
                item.scaleZ = static_cast<float>(scale.z);
                ApplySceneObjectTransform(item, worldMatrix);
                sceneGizmoWasUsing_ = true;
            }
            return;
        }

        if (sceneGizmoWasUsing_)
        {
            sceneGizmoWasUsing_ = false;
            CommitSceneGizmoEdit();
        }
    }

    void ScadLibraryInterface::DrawBoneGizmo(const ImVec2& viewportPos, const ImVec2& viewportSize)
    {
        if (viewportSize.x <= 1.0f || viewportSize.y <= 1.0f || workbenchBone_ < 0 ||
            workbenchBone_ >= static_cast<int>(rigPreview_.Asset().bones.size()))
        {
            return;
        }
        Assets::Node* boneNode = rigPreview_.BoneNode(workbenchBone_);
        if (boneNode == nullptr)
        {
            return;
        }

        const Assets::UniformBufferObject& ubo = engine_.GetLastUniformBufferObject();
        const glm::mat4& view = ubo.ModelView;
        glm::mat4 projection = ubo.ProjectionUnJit;
        projection[1][1] *= -1.0f;
        glm::mat4 worldMatrix = boneNode->WorldTransform();

        constexpr ImGuizmo::OPERATION operations[] = {ImGuizmo::TRANSLATE, ImGuizmo::ROTATE, ImGuizmo::SCALE};
        boneGizmoOperation_ = std::clamp(boneGizmoOperation_, 0, 2);
        ImGuizmo::SetAlternativeWindow(nullptr);
        ImGuizmo::SetOrthographic(false);
        ImGuizmo::BeginFrame();
        ImGuizmo::SetDrawlist(ImGui::GetForegroundDrawList());
        ImGuizmo::SetRect(viewportPos.x, viewportPos.y, viewportSize.x, viewportSize.y);
        ImGuizmo::GetStyle().Colors[ImGuizmo::COLOR::SELECTION] = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
        ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(projection), operations[boneGizmoOperation_],
                             ImGuizmo::LOCAL, glm::value_ptr(worldMatrix));

        const bool interacting = ImGuizmo::IsOver() || ImGuizmo::IsUsing();
        ImGui::GetIO().WantCaptureMouse = ImGui::GetIO().WantCaptureMouse || interacting;
        if (!ImGuizmo::IsUsing())
        {
            return;
        }

        rigPreview_.SetPaused(true);
        glm::mat4 parentWorld(1.0f);
        if (Assets::Node* parent = boneNode->GetParent())
        {
            parentWorld = parent->WorldTransform();
        }
        const glm::mat4 localMatrix = glm::inverse(parentWorld) * worldMatrix;
        const Assets::FRigBone& bone = rigPreview_.Asset().bones[workbenchBone_];
        const glm::mat4 bindMatrix = glm::translate(glm::mat4(1.0f), bone.bindT) * glm::mat4_cast(bone.bindR) *
            glm::scale(glm::mat4(1.0f), bone.bindS);
        const glm::mat4 offsetMatrix = glm::inverse(bindMatrix) * localMatrix;

        glm::vec3 scale{};
        glm::quat rotation{};
        glm::vec3 translation{};
        glm::vec3 skew{};
        glm::vec4 perspective{};
        if (!glm::decompose(offsetMatrix, scale, rotation, translation, skew, perspective))
        {
            return;
        }
        rotation = glm::normalize(rotation);
        if (boneGizmoOperation_ == 0)
        {
            UpsertGizmoKey(EEditableRigChannel::Position, FCharacterWorkbench::EnginePositionToScad(translation));
        }
        else if (boneGizmoOperation_ == 1)
        {
            UpsertGizmoKey(EEditableRigChannel::Rotation, FCharacterWorkbench::EngineRotationToScad(rotation));
        }
        else
        {
            UpsertGizmoKey(EEditableRigChannel::Scale, FCharacterWorkbench::EngineScaleToScad(scale));
        }
    }

    void ScadLibraryInterface::DrawAnimationTimelinePanel(const ImVec2& pos, const ImVec2& size)
    {
        ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(size, ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.98f);
        const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
        if (!ImGui::Begin("##ScadLibraryAnimationTimeline", nullptr, flags))
        {
            ImGui::End();
            ImGui::PopStyleVar(2);
            return;
        }

        if (workbench_.Clips().empty() || rigPreview_.Asset().bones.empty())
        {
            ImGui::TextDisabled("当前角色没有可编辑动作或骨架。");
            ImGui::End();
            ImGui::PopStyleVar(2);
            return;
        }

        FEditableRigClip& clip = workbench_.Clips()[workbenchClip_];
        bool edited = false;

        const auto sampleChannel = [](const FEditableRigChannel& channel, float time)
        {
            const glm::vec3 defaultValue =
                channel.type == EEditableRigChannel::Scale ? glm::vec3(1.0f) : glm::vec3(0.0f);
            if (channel.keys.empty())
            {
                return defaultValue;
            }
            if (time <= channel.keys.front().time)
            {
                return channel.keys.front().value;
            }
            if (time >= channel.keys.back().time)
            {
                return channel.keys.back().value;
            }
            for (size_t index = 1; index < channel.keys.size(); ++index)
            {
                if (time <= channel.keys[index].time)
                {
                    const FEditableRigKey& lhs = channel.keys[index - 1];
                    const FEditableRigKey& rhs = channel.keys[index];
                    const float alpha = (time - lhs.time) / std::max(rhs.time - lhs.time, 0.0001f);
                    return glm::mix(lhs.value, rhs.value, alpha);
                }
            }
            return channel.keys.back().value;
        };

        ImGui::TextUnformatted(ICON_FA_FILM " 动作时间轴");
        ImGui::SameLine();
        ImGui::TextDisabled("%s  ·  %.3f 秒  ·  %zu 条轨道", clip.name.c_str(), clip.duration, clip.channels.size());
        ImGui::SameLine();
        ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowWidth() - 244.0f));
        if (ImGui::SmallButton(boneGizmoOperation_ == 0 ? "[移动]" : "移动"))
        {
            boneGizmoOperation_ = 0;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton(boneGizmoOperation_ == 1 ? "[旋转]" : "旋转"))
        {
            boneGizmoOperation_ = 1;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton(boneGizmoOperation_ == 2 ? "[缩放]" : "缩放"))
        {
            boneGizmoOperation_ = 2;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("拖动 Gizmo 自动 K 帧");
        ImGui::Separator();

        workbenchBone_ = std::clamp(workbenchBone_, 0, static_cast<int>(rigPreview_.Asset().bones.size()) - 1);
        ImGui::BeginChild("##timeline_editor", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None);

        if (ImGui::SmallButton(ICON_FA_BACKWARD_STEP))
        {
            rigPreview_.SetPaused(true);
            rigPreview_.SetCurrentTime(0.0f);
        }
        ImGui::SameLine();
        if (ImGui::SmallButton(rigPreview_.Paused() ? ICON_FA_PLAY : ICON_FA_PAUSE))
        {
            rigPreview_.SetPaused(!rigPreview_.Paused());
        }
        ImGui::SameLine();
        if (ImGui::SmallButton(ICON_FA_STOP))
        {
            rigPreview_.SetPaused(true);
        }
        ImGui::SameLine();
        if (ImGui::SmallButton(ICON_FA_FORWARD_STEP))
        {
            rigPreview_.SetPaused(true);
            rigPreview_.SetCurrentTime(clip.duration);
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%.3f s", rigPreview_.CurrentTime());
        ImGui::SameLine();
        if (ImGui::Checkbox("循环", &clip.loop))
        {
            edited = true;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%.3f s / %zu 通道", clip.duration, clip.channels.size());

        const Assets::FRigBone& selectedBone = rigPreview_.Asset().bones[workbenchBone_];
        ImGui::SameLine();
        ImGui::TextDisabled("当前骨骼: %s", selectedBone.name.c_str());

        bool hasPosition = false;
        bool hasRotation = false;
        bool hasScale = false;
        for (const FEditableRigChannel& channel : clip.channels)
        {
            if (channel.bone != workbenchBone_)
            {
                continue;
            }
            hasPosition |= channel.type == EEditableRigChannel::Position;
            hasRotation |= channel.type == EEditableRigChannel::Rotation;
            hasScale |= channel.type == EEditableRigChannel::Scale;
        }

        const auto addChannel = [&](const char* label, EEditableRigChannel type, bool exists)
        {
            if (exists)
            {
                ImGui::BeginDisabled();
            }
            const bool pressed = ImGui::SmallButton(label);
            if (exists)
            {
                ImGui::EndDisabled();
            }
            if (pressed)
            {
                FEditableRigChannel channel;
                channel.bone = workbenchBone_;
                channel.type = type;
                channel.keys.push_back({rigPreview_.CurrentTime(),
                                        type == EEditableRigChannel::Scale ? glm::vec3(1.0f) : glm::vec3(0.0f)});
                clip.channels.push_back(std::move(channel));
                timelineSelectedChannel_ = static_cast<int>(clip.channels.size()) - 1;
                timelineSelectedKey_ = 0;
                edited = true;
            }
        };

        ImGui::TextDisabled("添加轨道");
        ImGui::SameLine();
        addChannel("+ pos", EEditableRigChannel::Position, hasPosition);
        ImGui::SameLine();
        addChannel("+ rot", EEditableRigChannel::Rotation, hasRotation);
        ImGui::SameLine();
        addChannel("+ scale", EEditableRigChannel::Scale, hasScale);

        if (timelineSelectedChannel_ >= static_cast<int>(clip.channels.size()))
        {
            timelineSelectedChannel_ = -1;
            timelineSelectedKey_ = -1;
        }
        if (timelineSelectedChannel_ >= 0 && clip.channels[timelineSelectedChannel_].bone != workbenchBone_)
        {
            timelineSelectedChannel_ = -1;
            timelineSelectedKey_ = -1;
        }

        std::vector<int> visibleChannels;
        for (int channelIndex = 0; channelIndex < static_cast<int>(clip.channels.size()); ++channelIndex)
        {
            if (clip.channels[channelIndex].bone == workbenchBone_)
            {
                visibleChannels.push_back(channelIndex);
            }
        }

        ImGui::SameLine();
        const bool hasSelectedTrack = timelineSelectedChannel_ >= 0;
        if (!hasSelectedTrack)
        {
            ImGui::BeginDisabled();
        }
        if (ImGui::SmallButton(ICON_FA_PLUS " 关键帧"))
        {
            FEditableRigChannel& channel = clip.channels[timelineSelectedChannel_];
            channel.keys.push_back({rigPreview_.CurrentTime(), sampleChannel(channel, rigPreview_.CurrentTime())});
            timelineSelectedKey_ = static_cast<int>(channel.keys.size()) - 1;
            edited = true;
        }
        if (!hasSelectedTrack)
        {
            ImGui::EndDisabled();
        }

        timelineVisibleDuration_ = std::max(timelineVisibleDuration_, std::max(clip.duration * 1.1f, 0.1f));
        const float duration = timelineVisibleDuration_;
        const float rowHeight = 28.0f;
        const float rulerHeight = 30.0f;
        const float labelWidth = 104.0f;
        const ImU32 rulerColor = ImGui::GetColorU32(ImGuiCol_FrameBg);
        const ImU32 rowColor = ImGui::GetColorU32(ImGuiCol_TableRowBg);
        const ImU32 alternateRowColor = ImGui::GetColorU32(ImGuiCol_TableRowBgAlt);
        const ImU32 gridColor = ImGui::GetColorU32(ImGuiCol_Border, 0.46f);
        const ImU32 textColor = ImGui::GetColorU32(ImGuiCol_TextDisabled);
        const ImU32 accentColor = ImGui::GetColorU32(NextUI::Theme::Color(NextUI::Theme::EColor::AccentHover));
        const ImU32 selectedColor = ImGui::GetColorU32(ImGuiCol_Text);

        const ImVec2 rulerPos = ImGui::GetCursorScreenPos();
        const float totalWidth = ImGui::GetContentRegionAvail().x;
        const float timeWidth = std::max(totalWidth - labelWidth, 80.0f);
        ImGui::InvisibleButton("##timeline_ruler", ImVec2(totalWidth, rulerHeight));
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(rulerPos, ImVec2(rulerPos.x + totalWidth, rulerPos.y + rulerHeight), rulerColor);
        drawList->AddLine(ImVec2(rulerPos.x + labelWidth, rulerPos.y),
                          ImVec2(rulerPos.x + labelWidth, rulerPos.y + rulerHeight), gridColor);
        drawList->AddText(ImVec2(rulerPos.x + 7.0f, rulerPos.y + 7.0f), textColor, "属性轨道");

        const float roughStep = duration / std::max(2.0f, std::floor(timeWidth / 75.0f));
        const float tickOptions[] = {0.01f, 0.02f, 0.05f, 0.1f, 0.2f, 0.5f, 1.0f, 2.0f, 5.0f, 10.0f};
        float tickStep = tickOptions[std::size(tickOptions) - 1];
        for (float option : tickOptions)
        {
            if (option >= roughStep)
            {
                tickStep = option;
                break;
            }
        }
        for (float tick = 0.0f; tick <= duration + tickStep * 0.5f; tick += tickStep)
        {
            const float x = rulerPos.x + labelWidth + (tick / duration) * timeWidth;
            drawList->AddLine(ImVec2(x, rulerPos.y + rulerHeight - 8.0f), ImVec2(x, rulerPos.y + rulerHeight),
                              gridColor);
            drawList->AddText(ImVec2(x + 3.0f, rulerPos.y + 5.0f), textColor, fmt::format("{:.2f}", tick).c_str());
        }
        const float currentTime = std::clamp(rigPreview_.CurrentTime(), 0.0f, duration);
        const float rulerPlayheadX = rulerPos.x + labelWidth + (currentTime / duration) * timeWidth;
        drawList->AddTriangleFilled(ImVec2(rulerPlayheadX - 5.0f, rulerPos.y),
                                    ImVec2(rulerPlayheadX + 5.0f, rulerPos.y),
                                    ImVec2(rulerPlayheadX, rulerPos.y + 8.0f), accentColor);
        drawList->AddLine(ImVec2(rulerPlayheadX, rulerPos.y + 8.0f), ImVec2(rulerPlayheadX, rulerPos.y + rulerHeight),
                          accentColor, 2.0f);
        if (ImGui::IsItemHovered() && ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
            ImGui::GetIO().MousePos.x >= rulerPos.x + labelWidth)
        {
            const float time =
                std::clamp((ImGui::GetIO().MousePos.x - rulerPos.x - labelWidth) / timeWidth, 0.0f, 1.0f) * duration;
            rigPreview_.SetPaused(true);
            rigPreview_.SetCurrentTime(time);
        }

        const float detailHeight = timelineSelectedChannel_ >= 0 && timelineSelectedKey_ >= 0 ? 102.0f : 34.0f;
        const float timelineHeight = std::max(88.0f, ImGui::GetContentRegionAvail().y - detailHeight);
        ImGui::BeginChild("##timeline_tracks", ImVec2(0.0f, timelineHeight), ImGuiChildFlags_Borders,
                          ImGuiWindowFlags_AlwaysVerticalScrollbar);
        const ImVec2 canvasPos = ImGui::GetCursorScreenPos();
        const float canvasWidth = ImGui::GetContentRegionAvail().x;
        const float canvasTimeWidth = std::max(canvasWidth - labelWidth, 80.0f);
        const float canvasHeight =
            std::max(ImGui::GetContentRegionAvail().y, rowHeight * static_cast<float>(visibleChannels.size()));
        ImGui::InvisibleButton("##timeline_canvas", ImVec2(canvasWidth, canvasHeight));
        drawList = ImGui::GetWindowDrawList();

        for (int rowIndex = 0; rowIndex < static_cast<int>(visibleChannels.size()); ++rowIndex)
        {
            const int channelIndex = visibleChannels[rowIndex];
            const FEditableRigChannel& channel = clip.channels[channelIndex];
            const float y = canvasPos.y + rowIndex * rowHeight;
            const bool selectedTrack = channelIndex == timelineSelectedChannel_;
            drawList->AddRectFilled(ImVec2(canvasPos.x, y), ImVec2(canvasPos.x + canvasWidth, y + rowHeight),
                                    selectedTrack ? ImGui::GetColorU32(ImGuiCol_Header)
                                                  : (rowIndex % 2 == 0 ? rowColor : alternateRowColor));
            drawList->AddLine(ImVec2(canvasPos.x, y + rowHeight), ImVec2(canvasPos.x + canvasWidth, y + rowHeight),
                              gridColor);
            drawList->AddLine(ImVec2(canvasPos.x + labelWidth, y), ImVec2(canvasPos.x + labelWidth, y + rowHeight),
                              gridColor);

            const std::string label = FCharacterWorkbench::ChannelName(channel.type);
            drawList->PushClipRect(ImVec2(canvasPos.x, y), ImVec2(canvasPos.x + labelWidth - 4.0f, y + rowHeight),
                                   true);
            drawList->AddText(ImVec2(canvasPos.x + 7.0f, y + 6.0f), selectedTrack ? selectedColor : textColor,
                              label.c_str());
            drawList->PopClipRect();

            for (float tick = 0.0f; tick <= duration + tickStep * 0.5f; tick += tickStep)
            {
                const float x = canvasPos.x + labelWidth + (tick / duration) * canvasTimeWidth;
                drawList->AddLine(ImVec2(x, y), ImVec2(x, y + rowHeight), gridColor);
            }
            for (int keyIndex = 0; keyIndex < static_cast<int>(channel.keys.size()); ++keyIndex)
            {
                const FEditableRigKey& key = channel.keys[keyIndex];
                const float x =
                    canvasPos.x + labelWidth + (std::clamp(key.time, 0.0f, duration) / duration) * canvasTimeWidth;
                const float centerY = y + rowHeight * 0.5f;
                const bool selectedKey = channelIndex == timelineSelectedChannel_ && keyIndex == timelineSelectedKey_;
                const ImU32 keyColor = selectedKey ? selectedColor : accentColor;
                const ImVec2 points[] = {ImVec2(x, centerY - 6.0f), ImVec2(x + 6.0f, centerY),
                                         ImVec2(x, centerY + 6.0f), ImVec2(x - 6.0f, centerY)};
                drawList->AddConvexPolyFilled(points, 4, keyColor);
            }
        }

        const float bodyPlayheadX = canvasPos.x + labelWidth + (currentTime / duration) * canvasTimeWidth;
        drawList->AddLine(ImVec2(bodyPlayheadX, canvasPos.y), ImVec2(bodyPlayheadX, canvasPos.y + canvasHeight),
                          accentColor, 2.0f);

        const bool timelineHovered = ImGui::IsItemHovered();
        if (timelineHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            const ImVec2 mouse = ImGui::GetIO().MousePos;
            const int row = static_cast<int>(std::floor((mouse.y - canvasPos.y) / rowHeight));
            if (row >= 0 && row < static_cast<int>(visibleChannels.size()))
            {
                timelineSelectedChannel_ = visibleChannels[row];
                timelineSelectedKey_ = -1;
                if (mouse.x >= canvasPos.x + labelWidth)
                {
                    const float clickedTime =
                        std::clamp((mouse.x - canvasPos.x - labelWidth) / canvasTimeWidth, 0.0f, 1.0f) * duration;
                    float nearestDistance = 9.0f;
                    for (int keyIndex = 0;
                         keyIndex < static_cast<int>(clip.channels[timelineSelectedChannel_].keys.size()); ++keyIndex)
                    {
                        const float keyX = canvasPos.x + labelWidth +
                            (clip.channels[timelineSelectedChannel_].keys[keyIndex].time / duration) * canvasTimeWidth;
                        const float distance = std::abs(mouse.x - keyX);
                        if (distance < nearestDistance)
                        {
                            nearestDistance = distance;
                            timelineSelectedKey_ = keyIndex;
                        }
                    }
                    if (timelineSelectedKey_ >= 0)
                    {
                        timelineDraggingKey_ = true;
                        rigPreview_.SetPaused(true);
                        rigPreview_.SetCurrentTime(
                            clip.channels[timelineSelectedChannel_].keys[timelineSelectedKey_].time);
                    }
                    else
                    {
                        rigPreview_.SetPaused(true);
                        rigPreview_.SetCurrentTime(clickedTime);
                        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                        {
                            FEditableRigChannel& channel = clip.channels[timelineSelectedChannel_];
                            channel.keys.push_back({clickedTime, sampleChannel(channel, clickedTime)});
                            timelineSelectedKey_ = static_cast<int>(channel.keys.size()) - 1;
                            edited = true;
                        }
                    }
                }
            }
        }

        if (timelineDraggingKey_ && timelineSelectedChannel_ >= 0 && timelineSelectedKey_ >= 0)
        {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
            {
                FEditableRigChannel& channel = clip.channels[timelineSelectedChannel_];
                if (timelineSelectedKey_ < static_cast<int>(channel.keys.size()))
                {
                    const float draggedTime =
                        std::clamp((ImGui::GetIO().MousePos.x - canvasPos.x - labelWidth) / canvasTimeWidth, 0.0f,
                                   1.0f) *
                        duration;
                    channel.keys[timelineSelectedKey_].time = draggedTime;
                    rigPreview_.SetCurrentTime(draggedTime);
                    edited = true;
                }
            }
            else
            {
                timelineDraggingKey_ = false;
            }
        }
        ImGui::EndChild();

        if (timelineSelectedChannel_ >= 0)
        {
            FEditableRigChannel& channel = clip.channels[timelineSelectedChannel_];
            ImGui::Text("%s / %s", rigPreview_.Asset().bones[channel.bone].name.c_str(),
                        FCharacterWorkbench::ChannelName(channel.type));
            ImGui::SameLine();
            if (timelineSelectedKey_ >= 0 && timelineSelectedKey_ < static_cast<int>(channel.keys.size()))
            {
                ImGui::TextDisabled("关键帧 %d", timelineSelectedKey_ + 1);
                FEditableRigKey& key = channel.keys[timelineSelectedKey_];
                ImGui::SetNextItemWidth(112.0f);
                if (ImGui::DragFloat("时间##selected_key", &key.time, 0.005f, 0.0f,
                                     std::max(clip.duration + 2.0f, 2.0f), "%.3f s"))
                {
                    rigPreview_.SetPaused(true);
                    rigPreview_.SetCurrentTime(key.time);
                    edited = true;
                }
                ImGui::SameLine();
                if (ImGui::SmallButton(ICON_FA_TRASH " 删除关键帧"))
                {
                    channel.keys.erase(channel.keys.begin() + timelineSelectedKey_);
                    timelineSelectedKey_ = -1;
                    edited = true;
                }
                const float dragSpeed = channel.type == EEditableRigChannel::Rotation ? 0.25f : 0.005f;
                const char* format = channel.type == EEditableRigChannel::Rotation ? "%.2f deg" : "%.4f";
                if (timelineSelectedKey_ >= 0 &&
                    ImGui::DragFloat3("值##selected_key", &channel.keys[timelineSelectedKey_].value.x, dragSpeed, 0.0f,
                                      0.0f, format))
                {
                    edited = true;
                }
            }
            else
            {
                ImGui::TextDisabled("点击菱形关键帧；双击轨道空白处创建");
                ImGui::SameLine();
                if (ImGui::SmallButton(ICON_FA_TRASH " 删除轨道"))
                {
                    clip.channels.erase(clip.channels.begin() + timelineSelectedChannel_);
                    timelineSelectedChannel_ = -1;
                    timelineSelectedKey_ = -1;
                    edited = true;
                }
            }
        }

        if (edited)
        {
            float selectedTime = -1.0f;
            if (timelineSelectedChannel_ >= 0 && timelineSelectedChannel_ < static_cast<int>(clip.channels.size()) &&
                timelineSelectedKey_ >= 0 &&
                timelineSelectedKey_ < static_cast<int>(clip.channels[timelineSelectedChannel_].keys.size()))
            {
                selectedTime = clip.channels[timelineSelectedChannel_].keys[timelineSelectedKey_].time;
            }
            ApplyWorkbenchRigEdit();
            if (selectedTime >= 0.0f && timelineSelectedChannel_ >= 0 &&
                timelineSelectedChannel_ < static_cast<int>(clip.channels.size()))
            {
                const std::vector<FEditableRigKey>& keys = clip.channels[timelineSelectedChannel_].keys;
                float nearest = std::numeric_limits<float>::max();
                for (int index = 0; index < static_cast<int>(keys.size()); ++index)
                {
                    const float distance = std::abs(keys[index].time - selectedTime);
                    if (distance < nearest)
                    {
                        nearest = distance;
                        timelineSelectedKey_ = index;
                    }
                }
            }
        }
        ImGui::EndChild();
        ImGui::End();
        ImGui::PopStyleVar(2);
    }

    void ScadLibraryInterface::DrawEquipmentEditor()
    {
        std::vector<FEquipmentAttachment>& equipment = workbench_.Equipment();
        int removeIndex = -1;
        int duplicateIndex = -1;
        bool structuralEdit = false;
        bool metadataEdit = false;

        ImGui::TextDisabled("任意 kit 模块 -> 任意骨架；变换采用 SCAD 本地坐标。");
        ImGui::BeginChild("##equipment_list", ImVec2(0.0f, -38.0f), ImGuiChildFlags_None);
        for (int index = 0; index < static_cast<int>(equipment.size()); ++index)
        {
            FEquipmentAttachment& attachment = equipment[index];
            ImGui::PushID(index);
            const bool open = ImGui::TreeNodeEx(
                fmt::format("{}##equipment", attachment.label.empty() ? attachment.id : attachment.label).c_str(),
                ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_FramePadding);
            ImGui::SameLine(ImGui::GetContentRegionAvail().x - 54.0f);
            if (ImGui::SmallButton(ICON_FA_COPY "##duplicate"))
            {
                duplicateIndex = index;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton(ICON_FA_TRASH "##remove"))
            {
                removeIndex = index;
            }
            if (open)
            {
                if (ImGui::Checkbox("启用", &attachment.enabled))
                {
                    structuralEdit = true;
                }
                if (ImGui::InputText("名称", &attachment.label) || ImGui::InputText("ID", &attachment.id))
                {
                    metadataEdit = true;
                }

                const char* currentKit = "（选择 kit）";
                int currentKitIndex = -1;
                for (int kitIndex = 0; kitIndex < static_cast<int>(kits_.size()); ++kitIndex)
                {
                    if (kits_[kitIndex].filePath == attachment.kitPath)
                    {
                        currentKit = kits_[kitIndex].name.c_str();
                        currentKitIndex = kitIndex;
                        break;
                    }
                }
                if (ImGui::BeginCombo("来源 kit", currentKit))
                {
                    for (int kitIndex = 0; kitIndex < static_cast<int>(kits_.size()); ++kitIndex)
                    {
                        if (ImGui::Selectable(kits_[kitIndex].name.c_str(), kitIndex == currentKitIndex))
                        {
                            attachment.kitPath = kits_[kitIndex].filePath;
                            attachment.moduleName =
                                kits_[kitIndex].modules.empty() ? "" : kits_[kitIndex].modules.front().name;
                            currentKitIndex = kitIndex;
                            structuralEdit = true;
                        }
                    }
                    ImGui::EndCombo();
                }
                if (currentKitIndex >= 0)
                {
                    if (ImGui::BeginCombo("模块", attachment.moduleName.c_str()))
                    {
                        for (const FKitModuleInfo& moduleInfo : kits_[currentKitIndex].modules)
                        {
                            if (ImGui::Selectable(moduleInfo.name.c_str(), moduleInfo.name == attachment.moduleName))
                            {
                                attachment.moduleName = moduleInfo.name;
                                structuralEdit = true;
                            }
                        }
                        ImGui::EndCombo();
                    }
                }
                if (ImGui::InputTextWithHint("参数", "如 seed = 3", &attachment.arguments))
                {
                    structuralEdit = true;
                }

                const char* currentBone = attachment.bone.c_str();
                if (ImGui::BeginCombo("挂点骨架", currentBone))
                {
                    for (const Assets::FRigBone& bone : rigPreview_.Asset().bones)
                    {
                        if (ImGui::Selectable(bone.name.c_str(), bone.name == attachment.bone))
                        {
                            attachment.bone = bone.name;
                            structuralEdit = true;
                        }
                    }
                    ImGui::EndCombo();
                }

                bool transformEdit = false;
                transformEdit |= ImGui::DragFloat3("位置", &attachment.translation.x, 0.005f, 0.0f, 0.0f, "%.3f");
                transformEdit |=
                    ImGui::DragFloat3("旋转", &attachment.rotationDegrees.x, 0.25f, 0.0f, 0.0f, "%.2f deg");
                transformEdit |= ImGui::DragFloat3("缩放", &attachment.scale.x, 0.01f, 0.01f, 10.0f, "%.3f");
                if (transformEdit)
                {
                    workbench_.MarkEquipmentDirty();
                    rigPreview_.UpdateEquipmentTransform(static_cast<size_t>(index), attachment);
                }
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
        ImGui::EndChild();

        if (removeIndex >= 0)
        {
            equipment.erase(equipment.begin() + removeIndex);
            structuralEdit = true;
        }
        if (duplicateIndex >= 0)
        {
            FEquipmentAttachment copy = equipment[duplicateIndex];
            copy.id += "_copy";
            copy.label += " 副本";
            equipment.push_back(std::move(copy));
            structuralEdit = true;
        }
        if (ImGui::Button(ICON_FA_PLUS " 添加装备"))
        {
            FEquipmentAttachment attachment;
            if (!kits_.empty())
            {
                attachment.kitPath = kits_.front().filePath;
                if (!kits_.front().modules.empty())
                {
                    attachment.moduleName = kits_.front().modules.front().name;
                }
            }
            equipment.push_back(std::move(attachment));
            structuralEdit = true;
        }
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_ROTATE_RIGHT " 刷新装备"))
        {
            structuralEdit = true;
        }

        if (structuralEdit || metadataEdit)
        {
            workbench_.MarkEquipmentDirty();
        }
        if (structuralEdit)
        {
            workbenchEquipmentRebuildRequested_ = true;
        }
    }
} // namespace ScadLibrary
