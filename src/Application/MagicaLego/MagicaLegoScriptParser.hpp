#pragma once
#include "Common/CoreMinimal.hpp"

namespace MagicaLego
{
    class FScriptContext
    {
    public:
        void SetVariable(const std::string& name, int value);
        bool GetVariable(const std::string& name, int& outValue) const;
        std::string SubstituteVariables(const std::string& line) const;

    private:
        std::unordered_map<std::string, int> variables_;
    };

    class FScriptParser
    {
    public:
        std::vector<std::string> Parse(const std::string& text, std::string& error);

    private:
        struct FRepeatBlock
        {
            int count;
            std::string iteratorName;
            std::vector<std::string> body;
        };

        std::vector<std::string> ParseLines(
            const std::vector<std::string>& lines,
            size_t& index,
            FScriptContext& context,
            std::string& error,
            int depth = 0);

        std::string TrimLine(const std::string& line) const;
        bool ParseRepeatHeader(const std::string& line, FScriptContext& context,
            int& outCount, std::string& outIterator, std::string& error) const;
        bool ParseVarDeclaration(const std::string& line, FScriptContext& context,
            std::string& varName, int& varValue, std::string& error) const;
    };
}
