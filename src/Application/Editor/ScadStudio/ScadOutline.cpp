#include "ScadOutline.hpp"

#include "Engine/Assets/Loaders/FScadLexer.h"
#include "Engine/Assets/Loaders/FScadParser.h"
#include "Engine/Assets/Loaders/FScadTypes.h"

#include <cctype>

namespace ScadStudio
{
    using namespace Assets::scad;

    namespace
    {
        // Strip `use <...>` / `include <...>` directive lines. The parser expects these
        // gone (the loader's ExtractDirectives does the same). Generated models rarely
        // use them, but be safe.
        std::string StripDirectives(const std::string& source)
        {
            std::string out;
            out.reserve(source.size());
            size_t i = 0;
            while (i < source.size())
            {
                size_t lineEnd = source.find('\n', i);
                if (lineEnd == std::string::npos)
                {
                    lineEnd = source.size();
                }
                const std::string_view line(source.data() + i, lineEnd - i);

                size_t firstNonWs = line.find_first_not_of(" \t");
                bool isDirective = false;
                if (firstNonWs != std::string_view::npos)
                {
                    const std::string_view trimmed = line.substr(firstNonWs);
                    isDirective = trimmed.rfind("use", 0) == 0 || trimmed.rfind("include", 0) == 0;
                    // Only treat as a directive when followed by '<' (use <foo>).
                    if (isDirective && trimmed.find('<') == std::string_view::npos)
                    {
                        isDirective = false;
                    }
                }

                if (!isDirective)
                {
                    out.append(line);
                }
                out.push_back('\n');
                i = lineEnd + 1;
            }
            return out;
        }

        // Render a short, human-readable summary of an expression for arg display.
        std::string SummarizeExpr(const ExprPtr& expr)
        {
            if (!expr)
            {
                return "";
            }
            switch (expr->kind)
            {
            case ExprKind::Number:
            {
                // Trim trailing zeros for readability.
                std::string s = std::to_string(expr->num);
                const size_t dot = s.find('.');
                if (dot != std::string::npos)
                {
                    size_t last = s.find_last_not_of('0');
                    if (last == dot)
                    {
                        last -= 1;
                    }
                    s.erase(last + 1);
                }
                return s;
            }
            case ExprKind::Bool:
                return expr->boolean ? "true" : "false";
            case ExprKind::Str:
                return "\"" + expr->str + "\"";
            case ExprKind::Ident:
                return expr->str;
            case ExprKind::VectorLit:
            {
                std::string s = "[";
                for (size_t i = 0; i < expr->list.size(); ++i)
                {
                    if (i > 0)
                    {
                        s += ",";
                    }
                    if (i >= 4) // cap long vectors
                    {
                        s += "...";
                        break;
                    }
                    s += SummarizeExpr(expr->list[i]);
                }
                s += "]";
                return s;
            }
            case ExprKind::Unary:
                return expr->str + SummarizeExpr(expr->list.empty() ? nullptr : expr->list[0]);
            default:
                return "..";
            }
        }

        std::string SummarizeArgs(const std::vector<CallArg>& args)
        {
            std::string s;
            for (size_t i = 0; i < args.size(); ++i)
            {
                if (i > 0)
                {
                    s += ", ";
                }
                if (i >= 4)
                {
                    s += "...";
                    break;
                }
                if (!args[i].name.empty())
                {
                    s += args[i].name + "=";
                }
                s += SummarizeExpr(args[i].value);
            }
            return s;
        }

        FOutlineNode BuildNode(const StmtPtr& stmt);

        void BuildChildren(const Scope& scope, std::vector<FOutlineNode>& out)
        {
            for (const StmtPtr& stmt : scope)
            {
                if (stmt)
                {
                    out.push_back(BuildNode(stmt));
                }
            }
        }

        FOutlineNode BuildNode(const StmtPtr& stmt)
        {
            FOutlineNode node;
            node.line = stmt->line;

            switch (stmt->kind)
            {
            case StmtKind::Assign:
                node.kind = "assign";
                node.label = stmt->name + " = " + SummarizeExpr(stmt->value);
                break;
            case StmtKind::ModuleDef:
            {
                node.kind = "module";
                std::string params;
                for (size_t i = 0; i < stmt->params.size(); ++i)
                {
                    if (i > 0)
                    {
                        params += ", ";
                    }
                    params += stmt->params[i].name;
                }
                node.label = "module " + stmt->name + "(" + params + ")";
                BuildChildren(stmt->body, node.children);
                break;
            }
            case StmtKind::FunctionDef:
                node.kind = "function";
                node.label = "function " + stmt->name + "()";
                break;
            case StmtKind::Instance:
            default:
            {
                node.kind = "instance";
                const std::string argStr = SummarizeArgs(stmt->args);
                node.label = stmt->name + "(" + argStr + ")";
                BuildChildren(stmt->children, node.children);
                if (!stmt->elseChildren.empty())
                {
                    FOutlineNode elseNode;
                    elseNode.kind = "instance";
                    elseNode.label = "else";
                    elseNode.line = stmt->line;
                    BuildChildren(stmt->elseChildren, elseNode.children);
                    node.children.push_back(std::move(elseNode));
                }
                break;
            }
            }
            return node;
        }
    } // namespace

    FOutlineResult BuildScadOutline(const std::string& source)
    {
        FOutlineResult result;
        if (source.empty())
        {
            result.ok = true;
            return result;
        }

        const std::string stripped = StripDirectives(source);

        std::vector<Token> tokens;
        std::string error;
        if (!ScadLexer::Tokenize(stripped, tokens, error))
        {
            result.ok = false;
            result.error = error;
            return result;
        }

        Scope scope;
        if (!ScadParser::Parse(tokens, scope, error))
        {
            result.ok = false;
            result.error = error;
            return result;
        }

        result.ok = true;
        BuildChildren(scope, result.roots);
        return result;
    }
}
