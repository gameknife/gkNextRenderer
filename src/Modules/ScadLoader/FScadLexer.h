#pragma once

// ============================================================================
// FScadLexer.h - Tokenizer for the OpenSCAD (.scad) subset.
// ============================================================================

#include <string>
#include <vector>

namespace Assets::scad
{
    enum class Tok
    {
        Number,
        String,
        Ident,   // identifiers + keywords (module/function/if/for/let/...)
        Special, // $-prefixed special variable, text holds name incl. '$'
        LParen,
        RParen,
        LBrace,
        RBrace,
        LBracket,
        RBracket,
        Comma,
        Semicolon,
        Colon,
        Assign,
        Dot,
        Plus,
        Minus,
        Star,
        Slash,
        Percent,
        Lt,
        Gt,
        Le,
        Ge,
        EqEq,
        NotEq,
        AndAnd,
        OrOr,
        Not,
        Question,
        Hash,
        Eof
    };

    struct Token
    {
        Tok kind = Tok::Eof;
        std::string text;   // identifier / string content / special name
        double number = 0.0;
        int line = 1;
    };

    class ScadLexer
    {
    public:
        // Tokenizes source. Always appends a trailing Eof token.
        // Returns false on a fatal lexing error (outError filled with line info).
        static bool Tokenize(const std::string& source, std::vector<Token>& outTokens, std::string& outError);
    };
} // namespace Assets::scad
