#include "Engine/Common/CoreMinimal.hpp"

#include "ScadSceneDocument.hpp"

#include "Modules/ScadLoader/FScadLexer.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fmt/format.h>
#include <optional>

namespace ScadLibrary
{
    namespace
    {
        using Assets::Scad::CallArg;
        using Assets::Scad::ExprKind;
        using Assets::Scad::ExprPtr;
        using Assets::Scad::FScadSourceEdit;
        using Assets::Scad::Stmt;
        using Assets::Scad::StmtKind;
        using Assets::Scad::Tok;
        using Assets::Scad::Token;

        constexpr float kPlacementEpsilon = 1.0e-4f;

        std::string TrimSegmentText(std::string value)
        {
            const auto space = [](unsigned char character) { return std::isspace(character) != 0; };
            value.erase(value.begin(), std::find_if_not(value.begin(), value.end(), space));
            value.erase(std::find_if_not(value.rbegin(), value.rend(), space).base(), value.end());
            return value;
        }

        std::string SegmentSummaryLine(const std::string& value, size_t maxLength)
        {
            std::string flattened;
            flattened.reserve(value.size());
            bool pendingSpace = false;
            for (const char character : value)
            {
                if (std::isspace(static_cast<unsigned char>(character)))
                {
                    pendingSpace = !flattened.empty();
                    continue;
                }
                if (pendingSpace)
                {
                    flattened.push_back(' ');
                    pendingSpace = false;
                }
                flattened.push_back(character);
            }
            if (flattened.size() > maxLength)
            {
                flattened.resize(maxLength);
                flattened += "...";
            }
            return flattened;
        }

        std::optional<double> SegmentLiteralNumber(const ExprPtr& expression)
        {
            if (!expression)
            {
                return std::nullopt;
            }
            if (expression->kind == ExprKind::Number)
            {
                return expression->num;
            }
            if (expression->kind == ExprKind::Unary && expression->str == "-" && !expression->list.empty())
            {
                const std::optional<double> inner = SegmentLiteralNumber(expression->list[0]);
                return inner ? std::optional<double>(-*inner) : std::nullopt;
            }
            return std::nullopt;
        }

        // Reads a literal numeric vector. Anything that depends on a variable
        // or a call stays unrecognized on purpose: the object editor may only
        // claim statements it can write back exactly.
        std::optional<std::vector<double>> SegmentLiteralVector(const ExprPtr& expression, size_t minComponents,
                                                         size_t maxComponents)
        {
            if (!expression || expression->kind != ExprKind::VectorLit)
            {
                return std::nullopt;
            }
            if (expression->list.size() < minComponents || expression->list.size() > maxComponents)
            {
                return std::nullopt;
            }
            std::vector<double> components;
            components.reserve(expression->list.size());
            for (const ExprPtr& element : expression->list)
            {
                const std::optional<double> value = SegmentLiteralNumber(element);
                if (!value)
                {
                    return std::nullopt;
                }
                components.push_back(*value);
            }
            return components;
        }

        const ExprPtr* FindSegmentArgument(const Stmt& statement, size_t position, const char* name)
        {
            size_t positional = 0;
            for (const CallArg& argument : statement.args)
            {
                if (argument.name.empty())
                {
                    if (positional == position)
                    {
                        return &argument.value;
                    }
                    ++positional;
                }
                else if (name != nullptr && argument.name == name)
                {
                    return &argument.value;
                }
            }
            return nullptr;
        }

        // Name and raw argument text of the last depth-0 call in `text`, which
        // for a transform chain is the module call at its tip. Reading the
        // arguments from the bytes keeps expressions the AST would not print
        // back identically.
        struct FTerminalCall
        {
            std::string name;
            std::string arguments;
            bool valid = false;
        };

        FTerminalCall FindTerminalCall(const std::string& text)
        {
            FTerminalCall result;
            std::vector<Token> tokens;
            std::string error;
            if (!Assets::Scad::ScadLexer::Tokenize(text, tokens, error))
            {
                return result;
            }

            int depth = 0;
            size_t callName = std::string::npos;
            size_t callOpen = 0;
            for (size_t index = 0; index + 1 < tokens.size(); ++index)
            {
                const Token& token = tokens[index];
                if (token.kind == Tok::LParen || token.kind == Tok::LBracket || token.kind == Tok::LBrace)
                {
                    ++depth;
                    continue;
                }
                if (token.kind == Tok::RParen || token.kind == Tok::RBracket || token.kind == Tok::RBrace)
                {
                    --depth;
                    continue;
                }
                if (depth != 0 || token.kind != Tok::Ident || tokens[index + 1].kind != Tok::LParen)
                {
                    continue;
                }
                callName = index;
                callOpen = index + 1;
            }
            if (callName == std::string::npos)
            {
                return result;
            }

            int parenDepth = 0;
            for (size_t index = callOpen; index < tokens.size(); ++index)
            {
                if (tokens[index].kind == Tok::LParen)
                {
                    ++parenDepth;
                }
                else if (tokens[index].kind == Tok::RParen && --parenDepth == 0)
                {
                    result.name = tokens[callName].text;
                    result.arguments =
                        TrimSegmentText(text.substr(tokens[callOpen].end, tokens[index].begin - tokens[callOpen].end));
                    result.valid = true;
                    return result;
                }
            }
            return result;
        }

        std::string StripDisableModifier(const std::string& text)
        {
            size_t cursor = 0;
            while (cursor < text.size() &&
                   (text[cursor] == '*' || std::isspace(static_cast<unsigned char>(text[cursor])) != 0))
            {
                ++cursor;
            }
            return text.substr(cursor);
        }

        // A transform may appear at most once and only in the canonical
        // color -> translate -> rotate -> scale order, so re-serializing the
        // statement reproduces the same placement.
        int TransformRank(const std::string& name)
        {
            if (name == "color")
            {
                return 0;
            }
            if (name == "translate")
            {
                return 1;
            }
            if (name == "rotate")
            {
                return 2;
            }
            if (name == "scale")
            {
                return 3;
            }
            return -1;
        }
    } // namespace

    const char* ScadSegmentKindLabel(EScadSegmentKind kind)
    {
        switch (kind)
        {
        case EScadSegmentKind::Instance:
            return "实例";
        case EScadSegmentKind::Terrain:
            return "地形";
        case EScadSegmentKind::TerrainRule:
            return "过程规则";
        case EScadSegmentKind::Source:
            return "源码";
        }
        return "节点";
    }

    bool FBenchItem::SamePlacement(const FBenchItem& other) const
    {
        const auto same = [](float left, float right) { return std::fabs(left - right) <= kPlacementEpsilon; };
        if (moduleName != other.moduleName || hasColor != other.hasColor || disabled != other.disabled)
        {
            return false;
        }
        if (!same(x, other.x) || !same(y, other.y) || !same(z, other.z))
        {
            return false;
        }
        if (!same(rotX, other.rotX) || !same(rotY, other.rotY) || !same(rotZ, other.rotZ))
        {
            return false;
        }
        if (!same(scale, other.scale) || !same(scaleY, other.scaleY) || !same(scaleZ, other.scaleZ))
        {
            return false;
        }
        if (hasColor)
        {
            for (int channel = 0; channel < 4; ++channel)
            {
                if (!same(color[channel], other.color[channel]))
                {
                    return false;
                }
            }
        }
        return std::strncmp(args, other.args, sizeof(args)) == 0;
    }

    std::string FScadSceneDocument::SerializeInstance(const FBenchItem& item)
    {
        std::string line;
        if (item.disabled)
        {
            line += "*";
        }
        if (item.hasColor)
        {
            line += fmt::format("color([{:.5f}, {:.5f}, {:.5f}, {:.5f}]) ", item.color[0], item.color[1], item.color[2],
                                item.color[3]);
        }
        line += fmt::format("translate([{:.4f}, {:.4f}, {:.4f}]) rotate([{:.4f}, {:.4f}, {:.4f}]) "
                            "scale([{:.5f}, {:.5f}, {:.5f}]) {}({});",
                            item.x, item.y, item.z, item.rotX, item.rotY, item.rotZ, item.scale, item.scaleY,
                            item.scaleZ, item.moduleName, item.args);
        return line;
    }

    void FScadSceneDocument::Clear()
    {
        source_.clear();
        segments_.clear();
        instances_.clear();
        parsedInstances_.clear();
        sourceSegmentReplacements_.clear();
        deletedSpans_.clear();
        terrain_ = FTerrainProcessDocument{};
        terrainWarnings_.clear();
        hasTerrain_ = false;
        directiveInsertPoint_ = 0;
    }

    size_t FScadSceneDocument::SourceSegmentCount() const
    {
        return static_cast<size_t>(std::count_if(segments_.begin(), segments_.end(),
                                                 [](const FScadSceneSegment& segment)
                                                 { return segment.kind == EScadSegmentKind::Source; }));
    }

    size_t FScadSceneDocument::InstanceSegmentCount() const
    {
        return static_cast<size_t>(std::count_if(segments_.begin(), segments_.end(),
                                                 [](const FScadSceneSegment& segment)
                                                 { return segment.kind == EScadSegmentKind::Instance; }));
    }

    bool FScadSceneDocument::Parse(std::string source, const Assets::Scad::FScadSourceIndex& index,
                                   const std::map<std::string, Assets::Scad::Value>& topLevelVariables,
                                   const std::function<bool(const std::string&)>& isKitModule,
                                   std::vector<std::string>& outWarnings)
    {
        Clear();
        source_ = std::move(source);

        // Terrain first: it owns whole statements, and anything it claims must
        // not also be offered as a plain source statement.
        std::string terrainError;
        hasTerrain_ = terrain_.Parse(source_, index, topLevelVariables, terrainError, terrainWarnings_);
        if (!hasTerrain_ && !terrainError.empty() && source_.find("gk_terrain") != std::string::npos)
        {
            outWarnings.push_back(fmt::format("地形节点未进入过程编辑器: {}", terrainError));
        }
        outWarnings.insert(outWarnings.end(), terrainWarnings_.begin(), terrainWarnings_.end());

        // Byte ranges the terrain document already owns.
        std::vector<std::pair<size_t, int>> terrainRuleSpans;
        if (hasTerrain_)
        {
            for (size_t ruleIndex = 0; ruleIndex < terrain_.Rules().size(); ++ruleIndex)
            {
                const FTerrainProcessRule& rule = terrain_.Rules()[ruleIndex];
                if (rule.sourceBegin != std::string::npos)
                {
                    terrainRuleSpans.emplace_back(rule.sourceBegin, static_cast<int>(ruleIndex));
                }
            }
        }

        segments_.reserve(index.statements.size());
        for (size_t statementIndex = 0; statementIndex < index.statements.size(); ++statementIndex)
        {
            const Assets::Scad::FScadStatementSpan& span = index.statements[statementIndex];
            const Assets::Scad::StmtPtr& statement =
                statementIndex < index.topLevel.size() ? index.topLevel[statementIndex] : nullptr;

            FScadSceneSegment segment;
            segment.begin = span.begin;
            segment.end = span.end;
            segment.line = span.line;
            segment.endLine = span.endLine;
            segment.name = span.name;
            segment.statementKind = span.kind;
            segment.disabled = span.Disabled();

            const auto terrainRule =
                std::find_if(terrainRuleSpans.begin(), terrainRuleSpans.end(),
                             [&](const std::pair<size_t, int>& entry) { return entry.first == span.begin; });
            if (terrainRule != terrainRuleSpans.end())
            {
                segment.kind = EScadSegmentKind::TerrainRule;
                segment.ruleIndex = terrainRule->second;
                segments_.push_back(std::move(segment));
                continue;
            }
            if (hasTerrain_ &&
                ((statement && statement->kind == StmtKind::Assign && statement->name == terrain_.TerrainVariable()) ||
                 span.name == "gk_terrain"))
            {
                segment.kind = EScadSegmentKind::Terrain;
                segments_.push_back(std::move(segment));
                continue;
            }

            if (statement && statement->kind == StmtKind::Instance)
            {
                FBenchItem item;
                if (ClassifyInstance(*statement, span, isKitModule, item))
                {
                    item.segmentIndex = static_cast<int>(segments_.size());
                    segment.kind = EScadSegmentKind::Instance;
                    segment.instanceIndex = static_cast<int>(instances_.size());
                    instances_.push_back(std::move(item));
                    segments_.push_back(std::move(segment));
                    continue;
                }
            }

            segments_.push_back(std::move(segment));
        }

        parsedInstances_ = instances_;

        // New `use <...>` lines go after the last existing directive.
        directiveInsertPoint_ = 0;
        size_t cursor = 0;
        while (cursor < source_.size())
        {
            const size_t lineEnd = source_.find('\n', cursor);
            const size_t end = lineEnd == std::string::npos ? source_.size() : lineEnd;
            const std::string line = TrimSegmentText(source_.substr(cursor, end - cursor));
            if (line.rfind("use", 0) == 0 || line.rfind("include", 0) == 0)
            {
                directiveInsertPoint_ = end == source_.size() ? end : end + 1;
            }
            if (lineEnd == std::string::npos)
            {
                break;
            }
            cursor = lineEnd + 1;
        }

        RebuildSegmentLabels();
        return true;
    }

    bool FScadSceneDocument::ClassifyInstance(const Assets::Scad::Stmt& statement,
                                              const Assets::Scad::FScadStatementSpan& span,
                                              const std::function<bool(const std::string&)>& isKitModule,
                                              FBenchItem& outItem) const
    {
        const Stmt* cursor = &statement;
        int lastRank = -1;
        while (cursor != nullptr && TransformRank(cursor->name) >= 0)
        {
            const int rank = TransformRank(cursor->name);
            if (rank <= lastRank || cursor->children.size() != 1 || !cursor->children[0])
            {
                return false;
            }
            lastRank = rank;

            if (cursor->name == "color")
            {
                const ExprPtr* argument = FindSegmentArgument(*cursor, 0, "c");
                const std::optional<std::vector<double>> components =
                    argument != nullptr ? SegmentLiteralVector(*argument, 3, 4) : std::nullopt;
                if (!components)
                {
                    return false;
                }
                outItem.hasColor = true;
                outItem.color[3] = 1.0f;
                for (size_t channel = 0; channel < components->size(); ++channel)
                {
                    outItem.color[channel] = static_cast<float>((*components)[channel]);
                }
            }
            else if (cursor->name == "translate")
            {
                const ExprPtr* argument = FindSegmentArgument(*cursor, 0, "v");
                const std::optional<std::vector<double>> components =
                    argument != nullptr ? SegmentLiteralVector(*argument, 2, 3) : std::nullopt;
                if (!components)
                {
                    return false;
                }
                outItem.x = static_cast<float>((*components)[0]);
                outItem.y = static_cast<float>((*components)[1]);
                outItem.z = components->size() > 2 ? static_cast<float>((*components)[2]) : 0.0f;
            }
            else if (cursor->name == "rotate")
            {
                const ExprPtr* argument = FindSegmentArgument(*cursor, 0, "a");
                if (argument == nullptr)
                {
                    return false;
                }
                if (const std::optional<std::vector<double>> components = SegmentLiteralVector(*argument, 3, 3))
                {
                    outItem.rotX = static_cast<float>((*components)[0]);
                    outItem.rotY = static_cast<float>((*components)[1]);
                    outItem.rotZ = static_cast<float>((*components)[2]);
                }
                else if (const std::optional<double> angle = SegmentLiteralNumber(*argument))
                {
                    // rotate(a) with a scalar spins around Z.
                    outItem.rotZ = static_cast<float>(*angle);
                }
                else
                {
                    return false;
                }
            }
            else // scale
            {
                const ExprPtr* argument = FindSegmentArgument(*cursor, 0, "v");
                if (argument == nullptr)
                {
                    return false;
                }
                if (const std::optional<std::vector<double>> components = SegmentLiteralVector(*argument, 3, 3))
                {
                    outItem.scale = static_cast<float>((*components)[0]);
                    outItem.scaleY = static_cast<float>((*components)[1]);
                    outItem.scaleZ = static_cast<float>((*components)[2]);
                }
                else if (const std::optional<double> uniform = SegmentLiteralNumber(*argument))
                {
                    outItem.scale = static_cast<float>(*uniform);
                    outItem.scaleY = outItem.scale;
                    outItem.scaleZ = outItem.scale;
                }
                else
                {
                    return false;
                }
            }
            cursor = cursor->children[0].get();
        }

        if (cursor == nullptr || cursor->kind != StmtKind::Instance || !cursor->children.empty())
        {
            return false;
        }
        if (!isKitModule || !isKitModule(cursor->name))
        {
            return false;
        }
        if (span.begin >= span.end || span.end > source_.size())
        {
            return false;
        }

        const FTerminalCall terminal = FindTerminalCall(source_.substr(span.begin, span.end - span.begin));
        if (!terminal.valid || terminal.name != cursor->name)
        {
            return false;
        }

        outItem.moduleName = cursor->name;
        outItem.sourceLine = cursor->line;
        outItem.sourceBegin = span.begin;
        outItem.sourceEnd = span.end;
        outItem.disabled = span.Disabled();
        std::snprintf(outItem.args, sizeof(outItem.args), "%s", terminal.arguments.c_str());
        return true;
    }

    void FScadSceneDocument::RebuildSegmentLabels()
    {
        for (FScadSceneSegment& segment : segments_)
        {
            if (segment.kind == EScadSegmentKind::Instance && segment.instanceIndex >= 0 &&
                segment.instanceIndex < static_cast<int>(instances_.size()))
            {
                segment.label = instances_[segment.instanceIndex].moduleName;
                continue;
            }
            if (segment.begin < segment.end && segment.end <= source_.size())
            {
                segment.label = SegmentSummaryLine(source_.substr(segment.begin, segment.end - segment.begin), 72);
            }
            else
            {
                segment.label = segment.name;
            }
        }
    }

    bool FScadSceneDocument::IsSwitchable(size_t segmentIndex) const
    {
        if (segmentIndex >= segments_.size())
        {
            return false;
        }
        const FScadSceneSegment& segment = segments_[segmentIndex];
        if (segment.kind == EScadSegmentKind::Terrain || segment.kind == EScadSegmentKind::TerrainRule)
        {
            // Terrain statements are switched off through the process editor,
            // which owns their bytes.
            return false;
        }
        return segment.statementKind == Assets::Scad::StmtKind::Instance;
    }

    bool FScadSceneDocument::SetSegmentDisabled(size_t segmentIndex, bool disabled)
    {
        if (!IsSwitchable(segmentIndex))
        {
            return false;
        }
        FScadSceneSegment& segment = segments_[segmentIndex];
        if (segment.disabled == disabled)
        {
            return true;
        }
        segment.disabled = disabled;
        segment.disabledByEditor = true;
        if (segment.kind == EScadSegmentKind::Instance && segment.instanceIndex >= 0 &&
            segment.instanceIndex < static_cast<int>(instances_.size()))
        {
            instances_[segment.instanceIndex].disabled = disabled;
        }
        return true;
    }

    bool FScadSceneDocument::ExplodeSegment(size_t segmentIndex, std::vector<FBenchItem> produced,
                                            std::string& outError)
    {
        if (segmentIndex >= segments_.size())
        {
            outError = "无效的节点索引";
            return false;
        }
        FScadSceneSegment& segment = segments_[segmentIndex];
        if (segment.kind != EScadSegmentKind::Source || !IsSwitchable(segmentIndex))
        {
            outError = "只有源码中的调用语句可以展开为实例";
            return false;
        }
        if (segment.disabled)
        {
            outError = "该结构已经关闭";
            return false;
        }
        if (produced.empty())
        {
            outError = "该结构没有产生可编辑的 Kit 实例（可能只有图元或未知模块）";
            return false;
        }

        segment.disabled = true;
        segment.disabledByEditor = true;
        segment.explodedInstances = static_cast<int>(produced.size());
        for (FBenchItem& item : produced)
        {
            item.sourceBegin = std::string::npos;
            item.sourceEnd = std::string::npos;
            item.insertAt = segment.end;
            item.originSegment = static_cast<int>(segmentIndex);
            item.segmentIndex = -1;
            item.disabled = false;
            item.removed = false;
            instances_.push_back(std::move(item));
        }
        return true;
    }

    bool FScadSceneDocument::CollapseSegment(size_t segmentIndex)
    {
        if (segmentIndex >= segments_.size())
        {
            return false;
        }
        FScadSceneSegment& segment = segments_[segmentIndex];
        if (segment.kind != EScadSegmentKind::Source || segment.explodedInstances == 0)
        {
            return false;
        }
        const int origin = static_cast<int>(segmentIndex);
        instances_.erase(std::remove_if(instances_.begin(), instances_.end(),
                                        [&](const FBenchItem& item) { return item.originSegment == origin; }),
                         instances_.end());
        segment.disabled = false;
        segment.disabledByEditor = true;
        segment.explodedInstances = 0;
        ReindexInstances();
        return true;
    }

    std::string FScadSceneDocument::GetSegmentSource(size_t segmentIndex) const
    {
        if (segmentIndex >= segments_.size())
        {
            return {};
        }
        const auto replacement = sourceSegmentReplacements_.find(segmentIndex);
        if (replacement != sourceSegmentReplacements_.end())
        {
            return replacement->second;
        }
        const FScadSceneSegment& segment = segments_[segmentIndex];
        if (segment.begin >= segment.end || segment.end > source_.size())
        {
            return {};
        }
        return source_.substr(segment.begin, segment.end - segment.begin);
    }

    bool FScadSceneDocument::ReplaceSegmentSource(size_t segmentIndex, std::string replacement)
    {
        if (segmentIndex >= segments_.size())
        {
            return false;
        }
        const FScadSceneSegment& segment = segments_[segmentIndex];
        if (segment.begin >= segment.end || segment.end > source_.size())
        {
            return false;
        }
        const std::string original = source_.substr(segment.begin, segment.end - segment.begin);
        if (replacement == original)
        {
            sourceSegmentReplacements_.erase(segmentIndex);
        }
        else
        {
            sourceSegmentReplacements_[segmentIndex] = std::move(replacement);
        }
        return true;
    }

    int FScadSceneDocument::AddInstance(FBenchItem item)
    {
        item.sourceBegin = std::string::npos;
        item.sourceEnd = std::string::npos;
        item.insertAt = source_.size();
        item.segmentIndex = static_cast<int>(segments_.size());
        item.originSegment = -1;
        item.removed = false;
        instances_.push_back(std::move(item));
        FScadSceneSegment segment;
        segment.kind = EScadSegmentKind::Instance;
        segment.statementKind = Assets::Scad::StmtKind::Instance;
        segment.line = 0;
        segment.endLine = 0;
        segment.name = instances_.back().moduleName;
        segment.label = instances_.back().moduleName;
        segment.instanceIndex = static_cast<int>(instances_.size()) - 1;
        segments_.push_back(std::move(segment));
        return static_cast<int>(instances_.size()) - 1;
    }

    void FScadSceneDocument::RemoveInstance(int instanceIndex)
    {
        if (instanceIndex < 0 || instanceIndex >= static_cast<int>(instances_.size()))
        {
            return;
        }
        const FBenchItem& item = instances_[static_cast<size_t>(instanceIndex)];
        if (item.sourceBegin != std::string::npos && item.segmentIndex >= 0 &&
            item.segmentIndex < static_cast<int>(segments_.size()))
        {
            // Keep the deletion in the write set: the statement it came from
            // still exists in the file.
            deletedSpans_.push_back({item.sourceBegin, item.sourceEnd});
            segments_[static_cast<size_t>(item.segmentIndex)].instanceIndex = -1;
        }
        else if (item.segmentIndex >= 0 && item.segmentIndex < static_cast<int>(segments_.size()))
        {
            const int removedSegment = item.segmentIndex;
            segments_.erase(segments_.begin() + removedSegment);
            for (FBenchItem& remaining : instances_)
            {
                if (remaining.segmentIndex > removedSegment)
                {
                    --remaining.segmentIndex;
                }
            }
        }
        instances_.erase(instances_.begin() + instanceIndex);
        ReindexInstances();
    }

    void FScadSceneDocument::ReindexInstances()
    {
        for (FScadSceneSegment& segment : segments_)
        {
            segment.instanceIndex = -1;
        }
        for (size_t index = 0; index < instances_.size(); ++index)
        {
            const int owner = instances_[index].segmentIndex;
            if (owner >= 0 && owner < static_cast<int>(segments_.size()))
            {
                segments_[static_cast<size_t>(owner)].instanceIndex = static_cast<int>(index);
            }
        }
    }

    std::string FScadSceneDocument::BuildSource(const FScadSceneWriteOptions& options) const
    {
        std::vector<FScadSourceEdit> edits;
        if (hasTerrain_)
        {
            terrain_.CollectEdits(edits);
        }

        // Statements switched on or off in place.
        for (const FScadSceneSegment& segment : segments_)
        {
            if (!segment.disabledByEditor || segment.kind == EScadSegmentKind::Instance ||
                segment.begin >= segment.end || segment.end > source_.size())
            {
                continue;
            }
            const std::string original = source_.substr(segment.begin, segment.end - segment.begin);
            std::string rewritten = StripDisableModifier(original);
            if (segment.disabled)
            {
                rewritten = "*" + rewritten;
            }
            if (rewritten != original)
            {
                edits.push_back({segment.begin, segment.end, std::move(rewritten)});
            }
        }

        for (const auto& [segmentIndex, replacement] : sourceSegmentReplacements_)
        {
            if (segmentIndex >= segments_.size())
            {
                continue;
            }
            const FScadSceneSegment& segment = segments_[segmentIndex];
            if (segment.begin < segment.end && segment.end <= source_.size())
            {
                edits.push_back({segment.begin, segment.end, replacement});
            }
        }

        // Instances deleted from the object list.
        for (const std::pair<size_t, size_t>& span : deletedSpans_)
        {
            edits.push_back({span.first, span.second, std::string{}});
        }

        // Instances: only the ones that actually changed are rewritten, so
        // opening and saving an untouched file is a no-op.
        for (size_t index = 0; index < instances_.size(); ++index)
        {
            const FBenchItem& item = instances_[index];
            if (item.sourceBegin != std::string::npos && item.sourceEnd != std::string::npos)
            {
                if (item.removed)
                {
                    edits.push_back({item.sourceBegin, item.sourceEnd, std::string{}});
                    continue;
                }
                const auto pristine = std::find_if(parsedInstances_.begin(), parsedInstances_.end(),
                                                   [&](const FBenchItem& original)
                                                   { return original.sourceBegin == item.sourceBegin; });
                if (pristine == parsedInstances_.end() || !item.SamePlacement(*pristine))
                {
                    edits.push_back({item.sourceBegin, item.sourceEnd, SerializeInstance(item)});
                }
                continue;
            }
            if (item.removed)
            {
                continue;
            }
            const size_t insertAt =
                std::min(item.insertAt == std::string::npos ? source_.size() : item.insertAt, source_.size());
            edits.push_back({insertAt, insertAt, "\n" + SerializeInstance(item)});
        }

        // Kit dependencies the new instances need.
        std::string directives;
        for (const std::string& usePath : options.requiredUsePaths)
        {
            const std::string directive = "<" + usePath + ">";
            if (source_.find(directive) != std::string::npos || directives.find(directive) != std::string::npos)
            {
                continue;
            }
            directives += fmt::format("use <{}>\n", usePath);
        }
        if (!directives.empty())
        {
            const size_t insertAt = std::min(directiveInsertPoint_, source_.size());
            edits.push_back({insertAt, insertAt, std::move(directives)});
        }

        return Assets::Scad::ApplyScadSourceEdits(source_, std::move(edits));
    }
} // namespace ScadLibrary
