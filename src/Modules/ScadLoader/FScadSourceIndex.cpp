#include "Engine/Common/CoreMinimal.hpp"
#include "Modules/ScadLoader/FScadSourceIndex.h"

#include "Modules/ScadLoader/FScadLexer.h"
#include "Modules/ScadLoader/FScadParser.h"

#include <cctype>

namespace Assets::Scad
{
    namespace
    {
        std::string MaskDirectives(const std::string& source)
        {
            std::string masked = source;
            size_t lineBegin = 0;
            while (lineBegin < masked.size())
            {
                const size_t lineEnd = masked.find('\n', lineBegin);
                const size_t end = lineEnd == std::string::npos ? masked.size() : lineEnd;
                size_t cursor = lineBegin;
                while (cursor < end && (masked[cursor] == ' ' || masked[cursor] == '\t' || masked[cursor] == '\r'))
                {
                    ++cursor;
                }
                const auto startsDirective = [&](std::string_view keyword)
                {
                    if (cursor + keyword.size() > end ||
                        std::string_view(masked).substr(cursor, keyword.size()) != keyword)
                    {
                        return false;
                    }
                    size_t after = cursor + keyword.size();
                    while (after < end && std::isspace(static_cast<unsigned char>(masked[after])))
                    {
                        ++after;
                    }
                    return after < end && masked[after] == '<';
                };
                if (startsDirective("use") || startsDirective("include"))
                {
                    for (size_t index = lineBegin; index < end; ++index)
                    {
                        masked[index] = ' ';
                    }
                }
                if (lineEnd == std::string::npos)
                {
                    break;
                }
                lineBegin = lineEnd + 1;
            }
            return masked;
        }

        size_t FindBodyEnd(const std::vector<Token>& tokens, size_t beginIndex, std::string& outError)
        {
            int parentheses = 0;
            int brackets = 0;
            int braces = 0;
            bool sawBodyBrace = false;
            for (size_t index = beginIndex; index < tokens.size(); ++index)
            {
                const Token& token = tokens[index];
                switch (token.kind)
                {
                case Tok::LParen: ++parentheses; break;
                case Tok::RParen: --parentheses; break;
                case Tok::LBracket: ++brackets; break;
                case Tok::RBracket: --brackets; break;
                case Tok::LBrace:
                    ++braces;
                    sawBodyBrace = true;
                    break;
                case Tok::RBrace:
                    --braces;
                    if (sawBodyBrace && braces == 0 && parentheses == 0 && brackets == 0)
                    {
                        return token.end;
                    }
                    break;
                case Tok::Semicolon:
                    if (!sawBodyBrace && braces == 0 && parentheses == 0 && brackets == 0)
                    {
                        return token.end;
                    }
                    break;
                case Tok::Eof:
                    outError = fmt::format("definition starting at line {} has no complete body",
                                           tokens[beginIndex].line);
                    return std::string::npos;
                default: break;
                }
            }
            outError = "definition has no complete body";
            return std::string::npos;
        }
    } // namespace

    const FScadDefinitionSpan* FScadSourceIndex::Find(EScadDefinitionKind kind, std::string_view name) const
    {
        const auto found = std::find_if(definitions.begin(), definitions.end(), [&](const FScadDefinitionSpan& item)
        { return item.kind == kind && item.name == name; });
        return found == definitions.end() ? nullptr : &*found;
    }

    bool BuildScadSourceIndex(const std::string& source, FScadSourceIndex& outIndex, std::string& outError)
    {
        outIndex.definitions.clear();
        const std::string masked = MaskDirectives(source);
        std::vector<Token> tokens;
        if (!ScadLexer::Tokenize(masked, tokens, outError))
        {
            return false;
        }
        Scope parsed;
        if (!ScadParser::Parse(tokens, parsed, outError))
        {
            return false;
        }

        int braceDepth = 0;
        for (size_t index = 0; index + 2 < tokens.size(); ++index)
        {
            const Token& token = tokens[index];
            if (token.kind == Tok::LBrace)
            {
                ++braceDepth;
                continue;
            }
            if (token.kind == Tok::RBrace)
            {
                --braceDepth;
                continue;
            }
            if (braceDepth != 0 || token.kind != Tok::Ident ||
                (token.text != "module" && token.text != "function"))
            {
                continue;
            }
            if (tokens[index + 1].kind != Tok::Ident || tokens[index + 2].kind != Tok::LParen)
            {
                outError = fmt::format("invalid {} definition at line {}", token.text, token.line);
                return false;
            }

            int parameterDepth = 0;
            size_t signatureEnd = tokens[index + 2].end;
            size_t bodyTokenIndex = index + 3;
            for (; bodyTokenIndex < tokens.size(); ++bodyTokenIndex)
            {
                if (tokens[bodyTokenIndex].kind == Tok::LParen)
                {
                    ++parameterDepth;
                }
                else if (tokens[bodyTokenIndex].kind == Tok::RParen)
                {
                    if (parameterDepth == 0)
                    {
                        signatureEnd = tokens[bodyTokenIndex].end;
                        ++bodyTokenIndex;
                        break;
                    }
                    --parameterDepth;
                }
            }
            const size_t definitionEnd = FindBodyEnd(tokens, bodyTokenIndex, outError);
            if (definitionEnd == std::string::npos)
            {
                return false;
            }
            FScadDefinitionSpan span;
            span.kind = token.text == "module" ? EScadDefinitionKind::Module : EScadDefinitionKind::Function;
            span.name = tokens[index + 1].text;
            span.begin = token.begin;
            span.end = definitionEnd;
            span.signatureBegin = token.begin;
            span.signatureEnd = signatureEnd;
            span.line = token.line;
            outIndex.definitions.push_back(std::move(span));

            while (index + 1 < tokens.size() && tokens[index + 1].begin < definitionEnd)
            {
                ++index;
            }
        }
        outError.clear();
        return true;
    }
} // namespace Assets::Scad
