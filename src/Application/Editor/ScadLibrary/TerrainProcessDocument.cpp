#include "Engine/Common/CoreMinimal.hpp"

#include "TerrainProcessDocument.hpp"

#include <algorithm>
#include <cmath>
#include <fmt/format.h>
#include <optional>
#include <sstream>

namespace ScadLibrary
{
    namespace
    {
        using Assets::Scad::CallArg;
        using Assets::Scad::Expr;
        using Assets::Scad::ExprKind;
        using Assets::Scad::ExprPtr;
        using Assets::Scad::Stmt;
        using Assets::Scad::StmtKind;
        using Assets::Scad::Value;

        struct FSourceSpan
        {
            size_t begin = std::string::npos;
            size_t end = std::string::npos;

            bool Valid() const { return begin != std::string::npos && end != std::string::npos && begin < end; }
        };

        std::string Trim(std::string value)
        {
            const auto whitespace = [](unsigned char character) { return std::isspace(character); };
            value.erase(value.begin(), std::find_if_not(value.begin(), value.end(), whitespace));
            value.erase(std::find_if_not(value.rbegin(), value.rend(), whitespace).base(), value.end());
            return value;
        }

        std::vector<size_t> BuildLineOffsets(const std::string& source)
        {
            std::vector<size_t> offsets{0};
            for (size_t index = 0; index < source.size(); ++index)
            {
                if (source[index] == '\n')
                {
                    offsets.push_back(index + 1);
                }
            }
            return offsets;
        }

        size_t FindMatchingParen(const std::string& source, size_t openParen)
        {
            int depth = 0;
            bool lineComment = false;
            bool blockComment = false;
            bool stringLiteral = false;
            bool escaped = false;
            for (size_t index = openParen; index < source.size(); ++index)
            {
                const char character = source[index];
                const char next = index + 1 < source.size() ? source[index + 1] : '\0';
                if (lineComment)
                {
                    if (character == '\n')
                    {
                        lineComment = false;
                    }
                    continue;
                }
                if (blockComment)
                {
                    if (character == '*' && next == '/')
                    {
                        ++index;
                        blockComment = false;
                    }
                    continue;
                }
                if (stringLiteral)
                {
                    if (!escaped && character == '"')
                    {
                        stringLiteral = false;
                    }
                    escaped = !escaped && character == '\\';
                    if (character != '\\')
                    {
                        escaped = false;
                    }
                    continue;
                }
                if (character == '/' && next == '/')
                {
                    ++index;
                    lineComment = true;
                    continue;
                }
                if (character == '/' && next == '*')
                {
                    ++index;
                    blockComment = true;
                    continue;
                }
                if (character == '"')
                {
                    stringLiteral = true;
                    continue;
                }
                if (character == '(')
                {
                    ++depth;
                }
                else if (character == ')' && --depth == 0)
                {
                    return index;
                }
            }
            return std::string::npos;
        }

        size_t FindStatementEnd(const std::string& source, size_t begin)
        {
            int parenDepth = 0;
            int bracketDepth = 0;
            int braceDepth = 0;
            bool sawBrace = false;
            bool lineComment = false;
            bool blockComment = false;
            bool stringLiteral = false;
            bool escaped = false;
            for (size_t index = begin; index < source.size(); ++index)
            {
                const char character = source[index];
                const char next = index + 1 < source.size() ? source[index + 1] : '\0';
                if (lineComment)
                {
                    if (character == '\n')
                    {
                        lineComment = false;
                    }
                    continue;
                }
                if (blockComment)
                {
                    if (character == '*' && next == '/')
                    {
                        ++index;
                        blockComment = false;
                    }
                    continue;
                }
                if (stringLiteral)
                {
                    if (!escaped && character == '"')
                    {
                        stringLiteral = false;
                    }
                    escaped = !escaped && character == '\\';
                    if (character != '\\')
                    {
                        escaped = false;
                    }
                    continue;
                }
                if (character == '/' && next == '/')
                {
                    ++index;
                    lineComment = true;
                    continue;
                }
                if (character == '/' && next == '*')
                {
                    ++index;
                    blockComment = true;
                    continue;
                }
                if (character == '"')
                {
                    stringLiteral = true;
                    continue;
                }

                if (character == '(')
                    ++parenDepth;
                else if (character == ')')
                    --parenDepth;
                else if (character == '[')
                    ++bracketDepth;
                else if (character == ']')
                    --bracketDepth;
                else if (character == '{')
                {
                    ++braceDepth;
                    sawBrace = true;
                }
                else if (character == '}')
                {
                    --braceDepth;
                    if (sawBrace && parenDepth == 0 && bracketDepth == 0 && braceDepth == 0)
                    {
                        size_t end = index + 1;
                        while (end < source.size() && (source[end] == ' ' || source[end] == '\t'))
                        {
                            ++end;
                        }
                        if (end < source.size() && source[end] == ';')
                        {
                            ++end;
                        }
                        return end;
                    }
                }
                else if (character == ';' && parenDepth == 0 && bracketDepth == 0 && braceDepth == 0)
                {
                    return index + 1;
                }
            }
            return std::string::npos;
        }

        FSourceSpan FindStatementSpan(const std::string& source, const std::vector<size_t>& lineOffsets, int line,
                                      const std::string& token)
        {
            if (line <= 0 || static_cast<size_t>(line) > lineOffsets.size())
            {
                return {};
            }
            const size_t lineBegin = lineOffsets[static_cast<size_t>(line) - 1];
            const size_t lineEnd = source.find('\n', lineBegin);
            const size_t tokenBegin = source.find(token, lineBegin);
            if (tokenBegin == std::string::npos || (lineEnd != std::string::npos && tokenBegin >= lineEnd))
            {
                return {};
            }
            return {tokenBegin, FindStatementEnd(source, tokenBegin)};
        }

        std::optional<Value> LiteralValue(const ExprPtr& expression)
        {
            if (!expression)
            {
                return std::nullopt;
            }
            switch (expression->kind)
            {
            case ExprKind::Number:
                return Value::MakeNumber(expression->num);
            case ExprKind::Bool:
                return Value::MakeBool(expression->boolean);
            case ExprKind::Str:
                return Value::MakeStr(expression->str);
            case ExprKind::Undef:
                return Value{};
            case ExprKind::VectorLit:
                {
                    std::vector<Value> values;
                    values.reserve(expression->list.size());
                    for (const ExprPtr& child : expression->list)
                    {
                        const std::optional<Value> value = LiteralValue(child);
                        if (!value)
                        {
                            return std::nullopt;
                        }
                        values.push_back(*value);
                    }
                    return Value::MakeVec(std::move(values));
                }
            case ExprKind::Unary:
                {
                    const std::optional<Value> value =
                        expression->list.empty() ? std::nullopt : LiteralValue(expression->list[0]);
                    if (!value || !value->IsNumber())
                    {
                        return std::nullopt;
                    }
                    if (expression->str == "-")
                    {
                        return Value::MakeNumber(-value->num);
                    }
                    if (expression->str == "+")
                    {
                        return value;
                    }
                    return std::nullopt;
                }
            default:
                return std::nullopt;
            }
        }

        const ExprPtr* FindArgument(const Stmt& statement, size_t positionalIndex, std::string_view name)
        {
            for (const CallArg& argument : statement.args)
            {
                if (argument.name == name)
                {
                    return &argument.value;
                }
            }
            size_t index = 0;
            for (const CallArg& argument : statement.args)
            {
                if (!argument.name.empty())
                {
                    continue;
                }
                if (index++ == positionalIndex)
                {
                    return &argument.value;
                }
            }
            return nullptr;
        }

        const ExprPtr* FindArgument(const Expr& expression, size_t positionalIndex, std::string_view name)
        {
            for (const CallArg& argument : expression.args)
            {
                if (argument.name == name)
                {
                    return &argument.value;
                }
            }
            size_t index = 0;
            for (const CallArg& argument : expression.args)
            {
                if (!argument.name.empty())
                {
                    continue;
                }
                if (index++ == positionalIndex)
                {
                    return &argument.value;
                }
            }
            return nullptr;
        }

        bool ReadLiteralNumber(const ExprPtr& expression, double& value)
        {
            const std::optional<Value> literal = LiteralValue(expression);
            if (!literal || !literal->IsNumber())
            {
                return false;
            }
            value = literal->num;
            return true;
        }

        bool ContainsTerrainHeightCall(const ExprPtr& expression)
        {
            if (!expression)
            {
                return false;
            }
            if (expression->kind == ExprKind::Call && expression->str == "gk_terrain_height")
            {
                return true;
            }
            return std::any_of(expression->list.begin(), expression->list.end(), ContainsTerrainHeightCall);
        }

        bool ReadNumber(const Stmt& statement, size_t positionalIndex, std::string_view name, double& value,
                        double fallback)
        {
            value = fallback;
            const ExprPtr* expression = FindArgument(statement, positionalIndex, name);
            if (expression == nullptr)
            {
                return true;
            }
            const std::optional<Value> literal = LiteralValue(*expression);
            if (!literal || !literal->IsNumber())
            {
                return false;
            }
            value = literal->num;
            return true;
        }

        bool ReadBool(const Stmt& statement, size_t positionalIndex, std::string_view name, bool& value, bool fallback)
        {
            value = fallback;
            const ExprPtr* expression = FindArgument(statement, positionalIndex, name);
            if (expression == nullptr)
            {
                return true;
            }
            const std::optional<Value> literal = LiteralValue(*expression);
            if (!literal)
            {
                return false;
            }
            if (literal->type == Value::Type::Bool)
            {
                value = literal->boolean;
                return true;
            }
            return false;
        }

        bool ReadVec2List(const Stmt& statement, size_t positionalIndex, std::string_view name,
                          std::vector<glm::dvec2>& points)
        {
            const ExprPtr* expression = FindArgument(statement, positionalIndex, name);
            if (expression == nullptr)
            {
                return false;
            }
            const std::optional<Value> literal = LiteralValue(*expression);
            if (!literal || literal->type != Value::Type::Vec)
            {
                return false;
            }
            points.clear();
            for (const Value& point : literal->vec)
            {
                if (point.type != Value::Type::Vec || point.vec.size() < 2 || !point.vec[0].IsNumber() ||
                    !point.vec[1].IsNumber())
                {
                    return false;
                }
                points.emplace_back(point.vec[0].num, point.vec[1].num);
            }
            return true;
        }

        bool ReadFixedVector(const Stmt& statement, size_t positionalIndex, std::string_view name, size_t count,
                             double* values)
        {
            const ExprPtr* expression = FindArgument(statement, positionalIndex, name);
            if (expression == nullptr)
            {
                return false;
            }
            const std::optional<Value> literal = LiteralValue(*expression);
            if (!literal || literal->type != Value::Type::Vec || literal->vec.size() < count)
            {
                return false;
            }
            for (size_t index = 0; index < count; ++index)
            {
                if (!literal->vec[index].IsNumber())
                {
                    return false;
                }
                values[index] = literal->vec[index].num;
            }
            return true;
        }

        bool ReadScatterFilter(const Stmt& statement, FTerrainProcessRule& rule)
        {
            const ExprPtr* expression = FindArgument(statement, 4, "filt");
            if (expression == nullptr)
            {
                return true;
            }
            const std::optional<Value> literal = LiteralValue(*expression);
            if (!literal || literal->type != Value::Type::Vec)
            {
                return false;
            }
            const std::vector<Value>& values = literal->vec;
            const auto number = [&](size_t index, double& destination)
            {
                if (index >= values.size())
                {
                    return true;
                }
                if (!values[index].IsNumber())
                {
                    return false;
                }
                destination = values[index].num;
                return true;
            };
            if (!number(0, rule.minHeight) || !number(1, rule.maxHeight) || !number(2, rule.maxSlope) ||
                !number(3, rule.avoidWater))
            {
                return false;
            }
            if (values.size() >= 5)
            {
                if (values[4].type != Value::Type::Vec)
                {
                    return false;
                }
                rule.biomes.clear();
                for (const Value& biome : values[4].vec)
                {
                    if (biome.type == Value::Type::Str)
                    {
                        rule.biomes.push_back(biome.str);
                    }
                    else if (biome.IsNumber())
                    {
                        rule.biomes.push_back(fmt::format("{:.0f}", biome.num));
                    }
                    else
                    {
                        return false;
                    }
                }
            }
            return true;
        }

        std::string FormatNumber(double value)
        {
            if (std::abs(value) < 0.0000000005)
            {
                value = 0.0;
            }
            return fmt::format("{:.9g}", value);
        }

        std::string FormatPoint(glm::dvec2 point)
        {
            return fmt::format("[{}, {}]", FormatNumber(point.x), FormatNumber(point.y));
        }

        std::string FormatPoints(const std::vector<glm::dvec2>& points)
        {
            std::string source = "[";
            for (size_t index = 0; index < points.size(); ++index)
            {
                if (index > 0)
                {
                    source += ", ";
                }
                source += FormatPoint(points[index]);
            }
            source += "]";
            return source;
        }

        std::string EscapeString(std::string_view value)
        {
            std::string escaped;
            escaped.reserve(value.size());
            for (const char character : value)
            {
                if (character == '\\' || character == '"')
                {
                    escaped.push_back('\\');
                }
                escaped.push_back(character);
            }
            return escaped;
        }

        std::string SerializeTerrain(const std::string& variable, const Assets::Scad::FTerrainSpec& terrain)
        {
            std::ostringstream source;
            source << variable << " = [\"gkterr1\", " << FormatPoint(terrain.size) << ", [" << terrain.cells.x << ", "
                   << terrain.cells.y << "], " << terrain.seed << ", [" << FormatNumber(terrain.baseHeight) << ", "
                   << FormatNumber(terrain.relief) << ", " << FormatNumber(terrain.roughness) << "], "
                   << (terrain.hasWaterLevel ? FormatNumber(terrain.waterLevel) : "undef") << ", \""
                   << EscapeString(terrain.palette) << "\",\n    [";
            for (size_t index = 0; index < terrain.features.size(); ++index)
            {
                const Assets::Scad::FTerrainFeature& feature = terrain.features[index];
                if (index > 0)
                {
                    source << ",";
                }
                source << "\n        [\"" << FTerrainProcessDocument::FeatureTypeName(feature.type) << "\", ";
                using EType = Assets::Scad::FTerrainFeature::EType;
                switch (feature.type)
                {
                case EType::Mountain:
                    source << FormatPoint(feature.at) << ", " << FormatNumber(feature.radius) << ", "
                           << FormatNumber(feature.height) << ", " << FormatNumber(feature.rugged);
                    break;
                case EType::Ridge:
                    source << FormatPoints(feature.pts) << ", " << FormatNumber(feature.width) << ", "
                           << FormatNumber(feature.height);
                    break;
                case EType::Plateau:
                    source << FormatPoint(feature.at) << ", " << FormatNumber(feature.radius) << ", "
                           << FormatNumber(feature.height);
                    break;
                case EType::Lake:
                    source << FormatPoint(feature.at) << ", " << FormatNumber(feature.radius) << ", "
                           << FormatNumber(feature.depth);
                    break;
                case EType::River:
                    source << FormatPoints(feature.pts) << ", " << FormatNumber(feature.width) << ", "
                           << FormatNumber(feature.depth);
                    break;
                case EType::Road:
                    source << FormatPoints(feature.pts) << ", " << FormatNumber(feature.width);
                    break;
                case EType::Pad:
                    source << FormatPoint(feature.at) << ", " << FormatPoint(feature.size) << ", "
                           << FormatNumber(feature.rot);
                    break;
                }
                source << "]";
            }
            source << "\n    ]];";
            return source.str();
        }

        std::string SerializeBiomes(const std::vector<std::string>& biomes)
        {
            std::string source = "[";
            for (size_t index = 0; index < biomes.size(); ++index)
            {
                if (index > 0)
                {
                    source += ", ";
                }
                const std::string& biome = biomes[index];
                const bool numeric = !biome.empty() &&
                    std::all_of(biome.begin(), biome.end(),
                                [](unsigned char character) { return std::isdigit(character); });
                source += numeric ? biome : fmt::format("\"{}\"", EscapeString(biome));
            }
            source += "]";
            return source;
        }

        std::string SerializeRule(const std::string& terrainVariable, const FTerrainProcessRule& rule)
        {
            std::string call;
            switch (rule.type)
            {
            case ETerrainProcessRuleType::HeightAnchor:
                call = fmt::format("translate([{}, {}, gk_terrain_height({}, {}, {}){}])", FormatNumber(rule.x),
                                   FormatNumber(rule.y), terrainVariable, FormatNumber(rule.sampleX),
                                   FormatNumber(rule.sampleY),
                                   rule.dz == 0.0 ? std::string{} : fmt::format(" + {}", FormatNumber(rule.dz)));
                break;
            case ETerrainProcessRuleType::Place:
                call = fmt::format("ter_place({}, {}, {}, dz = {})", terrainVariable, FormatNumber(rule.x),
                                   FormatNumber(rule.y), FormatNumber(rule.dz));
                break;
            case ETerrainProcessRuleType::PlaceTilt:
                call = fmt::format("ter_place_tilt({}, {}, {}, dz = {}, maxTilt = {}, probe = {})", terrainVariable,
                                   FormatNumber(rule.x), FormatNumber(rule.y), FormatNumber(rule.dz),
                                   FormatNumber(rule.maxTilt), FormatNumber(rule.probe));
                break;
            case ETerrainProcessRuleType::Snap:
                call = fmt::format("ter_snap({}, at = [{}, {}], dz = {})", terrainVariable, FormatNumber(rule.x),
                                   FormatNumber(rule.y), FormatNumber(rule.dz));
                break;
            case ETerrainProcessRuleType::Along:
                call = fmt::format("ter_along({}, {}, step = {}, seed = {}, offset = {}, dz = {})", terrainVariable,
                                   FormatPoints(rule.points), FormatNumber(rule.step), rule.seed,
                                   FormatNumber(rule.offset), FormatNumber(rule.dz));
                break;
            case ETerrainProcessRuleType::Scatter:
                call = fmt::format(
                    "ter_scatter({}, {}, {}, {}, [{}, {}, {}, {}, {}], rot = {}, dz = {})", terrainVariable, rule.seed,
                    rule.count,
                    rule.circularRegion
                        ? fmt::format("[{}, {}, {}]", FormatNumber(rule.regionCenter.x),
                                      FormatNumber(rule.regionCenter.y), FormatNumber(rule.regionRadius))
                        : fmt::format("[{}, {}, {}, {}]", FormatNumber(rule.region.x), FormatNumber(rule.region.y),
                                      FormatNumber(rule.region.z), FormatNumber(rule.region.w)),
                    FormatNumber(rule.minHeight), FormatNumber(rule.maxHeight), FormatNumber(rule.maxSlope),
                    FormatNumber(rule.avoidWater), SerializeBiomes(rule.biomes), rule.randomRotation ? "true" : "false",
                    FormatNumber(rule.dz));
                break;
            }
            const std::string child = Trim(rule.childSource);
            if (child.empty())
            {
                return call + "\n    cube([2, 2, 2], center = true);";
            }
            return call + "\n    " + child;
        }

        std::optional<ETerrainProcessRuleType> RuleTypeFromName(std::string_view name)
        {
            if (name == "ter_place")
                return ETerrainProcessRuleType::Place;
            if (name == "ter_place_tilt")
                return ETerrainProcessRuleType::PlaceTilt;
            if (name == "ter_snap")
                return ETerrainProcessRuleType::Snap;
            if (name == "ter_along")
                return ETerrainProcessRuleType::Along;
            if (name == "ter_scatter")
                return ETerrainProcessRuleType::Scatter;
            return std::nullopt;
        }

        bool ReadHeightAnchor(const Stmt& statement, const std::string& terrainVariable, const std::string& source,
                              const FSourceSpan& span, FTerrainProcessRule& rule)
        {
            if (statement.name != "translate" || !span.Valid())
            {
                return false;
            }
            const ExprPtr* vectorArgument = FindArgument(statement, 0, "v");
            if (vectorArgument == nullptr || !*vectorArgument || (*vectorArgument)->kind != ExprKind::VectorLit ||
                (*vectorArgument)->list.size() < 3)
            {
                return false;
            }
            const ExprPtr& heightExpression = (*vectorArgument)->list[2];
            if (!ContainsTerrainHeightCall(heightExpression) ||
                !ReadLiteralNumber((*vectorArgument)->list[0], rule.x) ||
                !ReadLiteralNumber((*vectorArgument)->list[1], rule.y))
            {
                return false;
            }

            ExprPtr heightCall = heightExpression;
            rule.dz = 0.0;
            if (heightExpression->kind == ExprKind::Binary && heightExpression->list.size() == 2)
            {
                double offset = 0.0;
                if (heightExpression->str == "+" && ContainsTerrainHeightCall(heightExpression->list[0]) &&
                    ReadLiteralNumber(heightExpression->list[1], offset))
                {
                    heightCall = heightExpression->list[0];
                    rule.dz = offset;
                }
                else if (heightExpression->str == "+" && ContainsTerrainHeightCall(heightExpression->list[1]) &&
                         ReadLiteralNumber(heightExpression->list[0], offset))
                {
                    heightCall = heightExpression->list[1];
                    rule.dz = offset;
                }
                else if (heightExpression->str == "-" && ContainsTerrainHeightCall(heightExpression->list[0]) &&
                         ReadLiteralNumber(heightExpression->list[1], offset))
                {
                    heightCall = heightExpression->list[0];
                    rule.dz = -offset;
                }
                else
                {
                    return false;
                }
            }
            if (!heightCall || heightCall->kind != ExprKind::Call || heightCall->str != "gk_terrain_height")
            {
                return false;
            }
            const ExprPtr* terrainArgument = FindArgument(*heightCall, 0, "t");
            const ExprPtr* xArgument = FindArgument(*heightCall, 1, "x");
            const ExprPtr* yArgument = FindArgument(*heightCall, 2, "y");
            if (terrainArgument == nullptr || !*terrainArgument || (*terrainArgument)->kind != ExprKind::Ident ||
                (*terrainArgument)->str != terrainVariable || xArgument == nullptr || yArgument == nullptr ||
                !ReadLiteralNumber(*xArgument, rule.sampleX) || !ReadLiteralNumber(*yArgument, rule.sampleY))
            {
                return false;
            }

            const size_t openParen = source.find('(', span.begin);
            const size_t closeParen = FindMatchingParen(source, openParen);
            if (closeParen == std::string::npos || closeParen + 1 > span.end)
            {
                return false;
            }
            rule.type = ETerrainProcessRuleType::HeightAnchor;
            rule.sourceBegin = span.begin;
            rule.sourceEnd = span.end;
            rule.childSource = Trim(source.substr(closeParen + 1, span.end - closeParen - 1));
            return !rule.childSource.empty();
        }

        bool ReadRule(const Stmt& statement, const std::string& terrainVariable, const std::string& source,
                      const FSourceSpan& span, FTerrainProcessRule& rule)
        {
            const std::optional<ETerrainProcessRuleType> type = RuleTypeFromName(statement.name);
            if (!type || !span.Valid())
            {
                return false;
            }
            const ExprPtr* terrainArgument = FindArgument(statement, 0, "t");
            if (terrainArgument == nullptr || !*terrainArgument || (*terrainArgument)->kind != ExprKind::Ident ||
                (*terrainArgument)->str != terrainVariable)
            {
                return false;
            }
            rule.type = *type;
            rule.sourceBegin = span.begin;
            rule.sourceEnd = span.end;

            double value = 0.0;
            bool parsed = true;
            switch (rule.type)
            {
            case ETerrainProcessRuleType::HeightAnchor:
                return false;
            case ETerrainProcessRuleType::Place:
            case ETerrainProcessRuleType::PlaceTilt:
                parsed = ReadNumber(statement, 1, "x", rule.x, 0.0) && ReadNumber(statement, 2, "y", rule.y, 0.0) &&
                    ReadNumber(statement, 3, "dz", rule.dz, 0.0);
                if (parsed && rule.type == ETerrainProcessRuleType::PlaceTilt)
                {
                    parsed = ReadNumber(statement, 4, "maxTilt", rule.maxTilt, 12.0) &&
                        ReadNumber(statement, 5, "probe", rule.probe, 0.8);
                }
                break;
            case ETerrainProcessRuleType::Snap:
                {
                    double at[2] = {0.0, 0.0};
                    const ExprPtr* atExpression = FindArgument(statement, 1, "at");
                    if (atExpression != nullptr)
                    {
                        parsed = ReadFixedVector(statement, 1, "at", 2, at);
                    }
                    rule.x = at[0];
                    rule.y = at[1];
                    parsed = parsed && ReadNumber(statement, 2, "dz", rule.dz, 0.0);
                    break;
                }
            case ETerrainProcessRuleType::Along:
                parsed = ReadVec2List(statement, 1, "pts", rule.points) &&
                    ReadNumber(statement, 2, "step", rule.step, 6.0) && ReadNumber(statement, 3, "seed", value, 0.0);
                rule.seed = static_cast<int>(std::llround(value));
                parsed = parsed && ReadNumber(statement, 4, "offset", rule.offset, 0.0) &&
                    ReadNumber(statement, 5, "dz", rule.dz, 0.0);
                break;
            case ETerrainProcessRuleType::Scatter:
                {
                    parsed = ReadNumber(statement, 1, "seed", value, 0.0);
                    rule.seed = static_cast<int>(std::llround(value));
                    parsed = parsed && ReadNumber(statement, 2, "n", value, 10.0);
                    rule.count = std::max(0, static_cast<int>(std::llround(value)));
                    const ExprPtr* regionExpression = FindArgument(statement, 3, "region");
                    const std::optional<Value> regionValue =
                        regionExpression != nullptr ? LiteralValue(*regionExpression) : std::nullopt;
                    parsed = parsed && regionValue && regionValue->type == Value::Type::Vec &&
                        (regionValue->vec.size() == 3 || regionValue->vec.size() >= 4);
                    if (parsed)
                    {
                        for (size_t index = 0; index < std::min<size_t>(4, regionValue->vec.size()); ++index)
                            parsed = parsed && regionValue->vec[index].IsNumber();
                    }
                    parsed = parsed && ReadScatterFilter(statement, rule) &&
                        ReadBool(statement, 5, "rot", rule.randomRotation, true) &&
                        ReadNumber(statement, 6, "dz", rule.dz, 0.0);
                    if (parsed && regionValue->vec.size() == 3)
                    {
                        rule.circularRegion = true;
                        rule.regionCenter = {regionValue->vec[0].num, regionValue->vec[1].num};
                        rule.regionRadius = std::max(0.1, regionValue->vec[2].num);
                    }
                    else if (parsed)
                    {
                        rule.circularRegion = false;
                        rule.region = {regionValue->vec[0].num, regionValue->vec[1].num, regionValue->vec[2].num,
                                       regionValue->vec[3].num};
                    }
                    break;
                }
            }
            if (!parsed)
            {
                return false;
            }

            const size_t openParen = source.find('(', span.begin);
            const size_t closeParen = FindMatchingParen(source, openParen);
            if (closeParen == std::string::npos || closeParen + 1 > span.end)
            {
                return false;
            }
            rule.childSource = Trim(source.substr(closeParen + 1, span.end - closeParen - 1));
            return !rule.childSource.empty();
        }
    } // namespace

    bool FTerrainProcessDocument::Parse(const std::string& source, const Assets::Scad::Scope& topLevel,
                                        const std::map<std::string, Assets::Scad::Value>& topLevelVariables,
                                        std::string& outError, std::vector<std::string>& outWarnings)
    {
        source_ = source;
        terrain_ = {};
        rules_.clear();
        terrainAssignmentBegin_ = std::string::npos;
        terrainAssignmentEnd_ = std::string::npos;
        insertionPoint_ = std::string::npos;
        outError.clear();
        outWarnings.clear();

        const Stmt* terrainCall = nullptr;
        for (const Assets::Scad::StmtPtr& statement : topLevel)
        {
            if (statement && statement->kind == StmtKind::Instance && statement->name == "gk_terrain" &&
                !statement->args.empty() && statement->args[0].value &&
                statement->args[0].value->kind == ExprKind::Ident)
            {
                terrainCall = statement.get();
                terrainVariable_ = statement->args[0].value->str;
                break;
            }
        }
        if (terrainCall == nullptr)
        {
            outError = "没有找到形如 gk_terrain(TERR) 的顶层地形调用";
            return false;
        }

        const auto variable = topLevelVariables.find(terrainVariable_);
        if (variable == topLevelVariables.end())
        {
            outError = fmt::format("无法求值地形变量 {}", terrainVariable_);
            return false;
        }
        if (!Assets::Scad::ScadTerrain::DecodeSpec(variable->second, terrain_, outError, outWarnings))
        {
            return false;
        }

        const std::vector<size_t> lineOffsets = BuildLineOffsets(source);
        for (const Assets::Scad::StmtPtr& statement : topLevel)
        {
            if (!statement)
            {
                continue;
            }
            if (statement->kind == StmtKind::Assign && statement->name == terrainVariable_)
            {
                const FSourceSpan span = FindStatementSpan(source, lineOffsets, statement->line, statement->name);
                if (span.Valid())
                {
                    terrainAssignmentBegin_ = span.begin;
                    terrainAssignmentEnd_ = span.end;
                }
                continue;
            }
            if (statement.get() == terrainCall)
            {
                const FSourceSpan span = FindStatementSpan(source, lineOffsets, statement->line, statement->name);
                if (span.Valid())
                {
                    insertionPoint_ = span.end;
                }
                continue;
            }
            if (statement->kind != StmtKind::Instance)
            {
                continue;
            }
            const bool terrainRule = RuleTypeFromName(statement->name).has_value();
            const ExprPtr* translateVector =
                statement->name == "translate" ? FindArgument(*statement, 0, "v") : nullptr;
            const bool heightAnchor = translateVector != nullptr && *translateVector &&
                (*translateVector)->kind == ExprKind::VectorLit && (*translateVector)->list.size() >= 3 &&
                ContainsTerrainHeightCall((*translateVector)->list[2]);
            if (!terrainRule && !heightAnchor)
            {
                continue;
            }

            const FSourceSpan span = FindStatementSpan(source, lineOffsets, statement->line, statement->name);
            FTerrainProcessRule rule;
            const bool parsed = heightAnchor ? ReadHeightAnchor(*statement, terrainVariable_, source, span, rule)
                                             : ReadRule(*statement, terrainVariable_, source, span, rule);
            if (parsed)
            {
                insertionPoint_ =
                    insertionPoint_ == std::string::npos ? rule.sourceEnd : std::max(insertionPoint_, rule.sourceEnd);
                rules_.push_back(std::move(rule));
            }
            else
            {
                outWarnings.push_back(
                    fmt::format("第 {} 行 {} 使用了其他地形或非字面量参数，已保留在源码中但不进入过程面板",
                                statement->line, statement->name));
            }
        }

        if (terrainAssignmentBegin_ == std::string::npos || terrainAssignmentEnd_ == std::string::npos)
        {
            outError = fmt::format("找到了 {} 的求值结果，但无法定位它的源码赋值", terrainVariable_);
            return false;
        }
        if (insertionPoint_ == std::string::npos)
        {
            insertionPoint_ = terrainAssignmentEnd_;
        }
        return true;
    }

    FTerrainProcessRule& FTerrainProcessDocument::AddRule(ETerrainProcessRuleType type, std::string childSource)
    {
        FTerrainProcessRule rule;
        rule.type = type;
        rule.childSource = childSource.empty() ? "cube([2, 2, 2], center = true);" : std::move(childSource);
        if (type == ETerrainProcessRuleType::Along)
        {
            rule.points = {{-10.0, 0.0}, {10.0, 0.0}};
        }
        rules_.push_back(std::move(rule));
        return rules_.back();
    }

    void FTerrainProcessDocument::DuplicateRule(size_t index, bool offsetPosition)
    {
        if (index >= rules_.size())
        {
            return;
        }
        FTerrainProcessRule copy = rules_[index];
        copy.sourceBegin = std::string::npos;
        copy.sourceEnd = std::string::npos;
        copy.removed = false;
        if (offsetPosition)
        {
            copy.x += 2.0;
            copy.y += 2.0;
        }
        rules_.push_back(std::move(copy));
    }

    void FTerrainProcessDocument::RemoveRule(size_t index)
    {
        if (index >= rules_.size())
        {
            return;
        }
        if (rules_[index].sourceBegin == std::string::npos)
        {
            rules_.erase(rules_.begin() + static_cast<std::ptrdiff_t>(index));
        }
        else
        {
            rules_[index].removed = true;
        }
    }

    size_t FTerrainProcessDocument::ActiveRuleCount() const
    {
        return static_cast<size_t>(
            std::count_if(rules_.begin(), rules_.end(), [](const FTerrainProcessRule& rule) { return !rule.removed; }));
    }

    std::string FTerrainProcessDocument::BuildSource() const
    {
        struct FReplacement
        {
            size_t begin = 0;
            size_t end = 0;
            std::string source;
        };
        std::vector<FReplacement> replacements;
        replacements.push_back(
            {terrainAssignmentBegin_, terrainAssignmentEnd_, SerializeTerrain(terrainVariable_, terrain_)});

        std::string additions;
        for (const FTerrainProcessRule& rule : rules_)
        {
            if (rule.sourceBegin == std::string::npos)
            {
                if (!rule.removed)
                {
                    additions += "\n\n// Added by ScadLibrary terrain process editor\n";
                    additions += SerializeRule(terrainVariable_, rule);
                }
                continue;
            }
            replacements.push_back({rule.sourceBegin, rule.sourceEnd,
                                    rule.removed ? std::string{} : SerializeRule(terrainVariable_, rule)});
        }
        if (!additions.empty())
        {
            replacements.push_back({insertionPoint_, insertionPoint_, std::move(additions)});
        }

        std::sort(replacements.begin(), replacements.end(),
                  [](const FReplacement& left, const FReplacement& right) { return left.begin > right.begin; });
        std::string result = source_;
        for (const FReplacement& replacement : replacements)
        {
            if (replacement.begin == std::string::npos || replacement.end == std::string::npos ||
                replacement.begin > replacement.end || replacement.end > result.size())
            {
                continue;
            }
            result.replace(replacement.begin, replacement.end - replacement.begin, replacement.source);
        }
        return result;
    }

    const char* FTerrainProcessDocument::FeatureTypeName(Assets::Scad::FTerrainFeature::EType type)
    {
        using EType = Assets::Scad::FTerrainFeature::EType;
        switch (type)
        {
        case EType::Mountain:
            return "mountain";
        case EType::Ridge:
            return "ridge";
        case EType::Plateau:
            return "plateau";
        case EType::Lake:
            return "lake";
        case EType::River:
            return "river";
        case EType::Road:
            return "road";
        case EType::Pad:
            return "pad";
        }
        return "unknown";
    }

    const char* FTerrainProcessDocument::RuleTypeName(ETerrainProcessRuleType type)
    {
        switch (type)
        {
        case ETerrainProcessRuleType::HeightAnchor:
            return "terrain_height_anchor";
        case ETerrainProcessRuleType::Place:
            return "ter_place";
        case ETerrainProcessRuleType::PlaceTilt:
            return "ter_place_tilt";
        case ETerrainProcessRuleType::Snap:
            return "ter_snap";
        case ETerrainProcessRuleType::Along:
            return "ter_along";
        case ETerrainProcessRuleType::Scatter:
            return "ter_scatter";
        }
        return "ter_unknown";
    }
} // namespace ScadLibrary
