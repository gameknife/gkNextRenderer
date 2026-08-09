#include "Modules/ScadLoader/FScadLexer.h"

#include <cctype>
#include <cstdlib>

namespace Assets::Scad
{
    namespace
    {
        bool IsIdentStart(char c)
        {
            return std::isalpha(static_cast<unsigned char>(c)) || c == '_';
        }

        bool IsIdentChar(char c)
        {
            return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
        }
    } // namespace

    bool ScadLexer::Tokenize(const std::string& source, std::vector<Token>& outTokens, std::string& outError)
    {
        outTokens.clear();
        outError.clear();

        const size_t n = source.size();
        size_t i = 0;
        int line = 1;

        auto push = [&](Tok kind, size_t begin, size_t end, std::string text = std::string())
        {
            Token t;
            t.kind = kind;
            t.text = std::move(text);
            t.line = line;
            t.begin = begin;
            t.end = end;
            outTokens.push_back(std::move(t));
        };

        while (i < n)
        {
            const char c = source[i];

            // Newlines / whitespace.
            if (c == '\n')
            {
                ++line;
                ++i;
                continue;
            }
            if (std::isspace(static_cast<unsigned char>(c)))
            {
                ++i;
                continue;
            }

            // Comments.
            if (c == '/' && i + 1 < n && source[i + 1] == '/')
            {
                i += 2;
                while (i < n && source[i] != '\n')
                {
                    ++i;
                }
                continue;
            }
            if (c == '/' && i + 1 < n && source[i + 1] == '*')
            {
                i += 2;
                while (i + 1 < n && !(source[i] == '*' && source[i + 1] == '/'))
                {
                    if (source[i] == '\n')
                    {
                        ++line;
                    }
                    ++i;
                }
                i += 2; // skip closing */
                continue;
            }

            // String literal.
            if (c == '"')
            {
                const size_t start = i;
                ++i;
                std::string value;
                while (i < n && source[i] != '"')
                {
                    char ch = source[i];
                    if (ch == '\\' && i + 1 < n)
                    {
                        const char esc = source[i + 1];
                        switch (esc)
                        {
                        case 'n': value.push_back('\n'); break;
                        case 't': value.push_back('\t'); break;
                        case 'r': value.push_back('\r'); break;
                        case '"': value.push_back('"'); break;
                        case '\\': value.push_back('\\'); break;
                        default: value.push_back(esc); break;
                        }
                        i += 2;
                        continue;
                    }
                    if (ch == '\n')
                    {
                        ++line;
                    }
                    value.push_back(ch);
                    ++i;
                }
                if (i >= n)
                {
                    outError = "Unterminated string literal at line " + std::to_string(line);
                    return false;
                }
                ++i; // closing quote
                Token t;
                t.kind = Tok::String;
                t.text = std::move(value);
                t.line = line;
                t.begin = start;
                t.end = i;
                outTokens.push_back(std::move(t));
                continue;
            }

            // Number (supports 1, 1.5, .5, 1e3, 1.5E-2).
            if (std::isdigit(static_cast<unsigned char>(c)) ||
                (c == '.' && i + 1 < n && std::isdigit(static_cast<unsigned char>(source[i + 1]))))
            {
                const size_t start = i;
                while (i < n && std::isdigit(static_cast<unsigned char>(source[i])))
                {
                    ++i;
                }
                if (i < n && source[i] == '.')
                {
                    ++i;
                    while (i < n && std::isdigit(static_cast<unsigned char>(source[i])))
                    {
                        ++i;
                    }
                }
                if (i < n && (source[i] == 'e' || source[i] == 'E'))
                {
                    size_t j = i + 1;
                    if (j < n && (source[j] == '+' || source[j] == '-'))
                    {
                        ++j;
                    }
                    if (j < n && std::isdigit(static_cast<unsigned char>(source[j])))
                    {
                        i = j;
                        while (i < n && std::isdigit(static_cast<unsigned char>(source[i])))
                        {
                            ++i;
                        }
                    }
                }
                Token t;
                t.kind = Tok::Number;
                t.number = std::strtod(source.substr(start, i - start).c_str(), nullptr);
                t.line = line;
                t.begin = start;
                t.end = i;
                outTokens.push_back(std::move(t));
                continue;
            }

            // Special variable ($fn, $fa, $fs, $t, ...).
            if (c == '$')
            {
                const size_t start = i;
                ++i;
                while (i < n && IsIdentChar(source[i]))
                {
                    ++i;
                }
                Token t;
                t.kind = Tok::Special;
                t.text = source.substr(start, i - start);
                t.line = line;
                t.begin = start;
                t.end = i;
                outTokens.push_back(std::move(t));
                continue;
            }

            // Identifier / keyword.
            if (IsIdentStart(c))
            {
                const size_t start = i;
                while (i < n && IsIdentChar(source[i]))
                {
                    ++i;
                }
                Token t;
                t.kind = Tok::Ident;
                t.text = source.substr(start, i - start);
                t.line = line;
                t.begin = start;
                t.end = i;
                outTokens.push_back(std::move(t));
                continue;
            }

            // Operators / punctuation.
            auto two = [&](char a, char b) { return c == a && i + 1 < n && source[i + 1] == b; };
            if (two('<', '=')) { push(Tok::Le, i, i + 2); i += 2; continue; }
            if (two('>', '=')) { push(Tok::Ge, i, i + 2); i += 2; continue; }
            if (two('=', '=')) { push(Tok::EqEq, i, i + 2); i += 2; continue; }
            if (two('!', '=')) { push(Tok::NotEq, i, i + 2); i += 2; continue; }
            if (two('&', '&')) { push(Tok::AndAnd, i, i + 2); i += 2; continue; }
            if (two('|', '|')) { push(Tok::OrOr, i, i + 2); i += 2; continue; }

            switch (c)
            {
            case '(': push(Tok::LParen, i, i + 1); break;
            case ')': push(Tok::RParen, i, i + 1); break;
            case '{': push(Tok::LBrace, i, i + 1); break;
            case '}': push(Tok::RBrace, i, i + 1); break;
            case '[': push(Tok::LBracket, i, i + 1); break;
            case ']': push(Tok::RBracket, i, i + 1); break;
            case ',': push(Tok::Comma, i, i + 1); break;
            case ';': push(Tok::Semicolon, i, i + 1); break;
            case ':': push(Tok::Colon, i, i + 1); break;
            case '=': push(Tok::Assign, i, i + 1); break;
            case '.': push(Tok::Dot, i, i + 1); break;
            case '+': push(Tok::Plus, i, i + 1); break;
            case '-': push(Tok::Minus, i, i + 1); break;
            case '*': push(Tok::Star, i, i + 1); break;
            case '/': push(Tok::Slash, i, i + 1); break;
            case '%': push(Tok::Percent, i, i + 1); break;
            case '<': push(Tok::Lt, i, i + 1); break;
            case '>': push(Tok::Gt, i, i + 1); break;
            case '!': push(Tok::Not, i, i + 1); break;
            case '?': push(Tok::Question, i, i + 1); break;
            case '#': push(Tok::Hash, i, i + 1); break;
            default:
                outError = "Unexpected character '" + std::string(1, c) + "' at line " + std::to_string(line);
                return false;
            }
            ++i;
        }

        Token eof;
        eof.kind = Tok::Eof;
        eof.line = line;
        eof.begin = n;
        eof.end = n;
        outTokens.push_back(eof);
        return true;
    }
} // namespace Assets::Scad
