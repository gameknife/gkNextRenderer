#include "MagicaLegoScriptParser.hpp"
#include <sstream>
#include <algorithm>
#include <cctype>
#include <regex>

namespace MagicaLego
{
    // ==================== FScriptContext ====================

    void FScriptContext::SetVariable(const std::string& name, int value)
    {
        variables_[name] = value;
    }

    bool FScriptContext::GetVariable(const std::string& name, int& outValue) const
    {
        auto it = variables_.find(name);
        if (it != variables_.end())
        {
            outValue = it->second;
            return true;
        }
        return false;
    }

    std::string FScriptContext::SubstituteVariables(const std::string& line) const
    {
        std::string result = line;

        // Find all $varName patterns and replace with values
        std::regex varPattern(R"(\$([a-zA-Z_][a-zA-Z0-9_]*))");
        std::smatch match;
        std::string::const_iterator searchStart = result.cbegin();

        std::string processed;
        size_t lastPos = 0;

        while (std::regex_search(searchStart, result.cend(), match, varPattern))
        {
            // Append text before match
            size_t matchPos = match.position() + (searchStart - result.cbegin());
            processed += result.substr(lastPos, matchPos - lastPos);

            // Get variable name and value
            std::string varName = match[1].str();
            int value = 0;
            if (GetVariable(varName, value))
            {
                processed += std::to_string(value);
            }
            else
            {
                // Keep original if variable not found
                processed += match[0].str();
            }

            lastPos = matchPos + match[0].length();
            searchStart = match.suffix().first;
        }

        // Append remaining text
        processed += result.substr(lastPos);

        return processed;
    }

    // ==================== FScriptParser ====================

    std::string FScriptParser::TrimLine(const std::string& line) const
    {
        auto start = line.find_first_not_of(" \t\r\n");
        if (start == std::string::npos)
        {
            return "";
        }
        auto end = line.find_last_not_of(" \t\r\n");
        return line.substr(start, end - start + 1);
    }

    bool FScriptParser::ParseVarDeclaration(const std::string& line, FScriptContext& context,
        std::string& varName, int& varValue, std::string& error) const
    {
        // Pattern: var <name> = <value>
        std::regex varPattern(R"(var\s+([a-zA-Z_][a-zA-Z0-9_]*)\s*=\s*(.+))");
        std::smatch match;

        if (!std::regex_match(line, match, varPattern))
        {
            return false;
        }

        varName = match[1].str();
        std::string valueStr = match[2].str();

        // Substitute any variables in the value
        valueStr = context.SubstituteVariables(valueStr);

        try
        {
            varValue = std::stoi(valueStr);
            return true;
        }
        catch (const std::exception&)
        {
            error = fmt::format("Invalid variable value: {}", valueStr);
            return false;
        }
    }

    bool FScriptParser::ParseRepeatHeader(const std::string& line, FScriptContext& context,
        int& outCount, std::string& outIterator, std::string& error) const
    {
        // Pattern: repeat <count> as <iterator>
        std::regex repeatPattern(R"(repeat\s+(\S+)\s+as\s+([a-zA-Z_][a-zA-Z0-9_]*))");
        std::smatch match;

        if (!std::regex_match(line, match, repeatPattern))
        {
            error = "Invalid repeat syntax. Use: repeat <count> as <iterator>";
            return false;
        }

        std::string countStr = match[1].str();
        outIterator = match[2].str();

        // Substitute variables in count
        countStr = context.SubstituteVariables(countStr);

        try
        {
            outCount = std::stoi(countStr);
            if (outCount < 0)
            {
                error = "Repeat count must be non-negative";
                return false;
            }
            return true;
        }
        catch (const std::exception&)
        {
            error = fmt::format("Invalid repeat count: {}", countStr);
            return false;
        }
    }

    std::vector<std::string> FScriptParser::Parse(const std::string& text, std::string& error)
    {
        // Split text into lines
        std::vector<std::string> lines;
        std::istringstream stream(text);
        std::string line;
        while (std::getline(stream, line))
        {
            lines.push_back(line);
        }

        FScriptContext context;
        size_t index = 0;
        return ParseLines(lines, index, context, error, 0);
    }

    std::vector<std::string> FScriptParser::ParseLines(
        const std::vector<std::string>& lines,
        size_t& index,
        FScriptContext& context,
        std::string& error,
        int depth)
    {
        std::vector<std::string> output;

        while (index < lines.size())
        {
            std::string trimmed = TrimLine(lines[index]);

            // Skip empty lines and comments
            if (trimmed.empty() || trimmed[0] == '#')
            {
                index++;
                continue;
            }

            // Check for 'end' keyword
            std::string lower = trimmed;
            std::transform(lower.begin(), lower.end(), lower.begin(),
                [](unsigned char c) { return std::tolower(c); });

            if (lower == "end")
            {
                if (depth == 0)
                {
                    error = fmt::format("Unexpected 'end' at line {}", index + 1);
                    return {};
                }
                index++;
                return output;
            }

            // Check for variable declaration
            std::string varName;
            int varValue;
            if (lower.substr(0, 4) == "var ")
            {
                if (ParseVarDeclaration(trimmed, context, varName, varValue, error))
                {
                    context.SetVariable(varName, varValue);
                    index++;
                    continue;
                }
                else if (!error.empty())
                {
                    error = fmt::format("Line {}: {}", index + 1, error);
                    return {};
                }
            }

            // Check for repeat block
            if (lower.substr(0, 7) == "repeat ")
            {
                int count;
                std::string iterator;
                if (!ParseRepeatHeader(trimmed, context, count, iterator, error))
                {
                    error = fmt::format("Line {}: {}", index + 1, error);
                    return {};
                }

                // Collect repeat body
                index++;
                std::vector<std::string> bodyLines;
                size_t bodyStart = index;
                int nestedDepth = 1;

                while (index < lines.size() && nestedDepth > 0)
                {
                    std::string bodyTrimmed = TrimLine(lines[index]);
                    std::string bodyLower = bodyTrimmed;
                    std::transform(bodyLower.begin(), bodyLower.end(), bodyLower.begin(),
                        [](unsigned char c) { return std::tolower(c); });

                    if (bodyLower.substr(0, 7) == "repeat ")
                    {
                        nestedDepth++;
                    }
                    else if (bodyLower == "end")
                    {
                        nestedDepth--;
                        if (nestedDepth == 0)
                        {
                            break;
                        }
                    }

                    bodyLines.push_back(lines[index]);
                    index++;
                }

                if (nestedDepth != 0)
                {
                    error = fmt::format("Unclosed repeat block starting at line {}", bodyStart);
                    return {};
                }

                // Execute repeat
                for (int i = 0; i < count; i++)
                {
                    // Create a new context with the iterator variable
                    FScriptContext loopContext = context;
                    loopContext.SetVariable(iterator, i);

                    // Parse body with loop context
                    size_t bodyIndex = 0;
                    std::string loopError;
                    auto expanded = ParseLines(bodyLines, bodyIndex, loopContext, loopError, depth + 1);

                    if (!loopError.empty())
                    {
                        error = loopError;
                        return {};
                    }

                    for (const auto& cmd : expanded)
                    {
                        output.push_back(cmd);
                    }
                }

                index++;  // Skip 'end'
                continue;
            }

            // Regular command - substitute variables and add
            std::string substituted = context.SubstituteVariables(trimmed);
            output.push_back(substituted);
            index++;
        }

        // Note: We don't check for unclosed blocks here because:
        // 1. When depth > 0, we're processing loop body content (which doesn't include 'end')
        // 2. Unclosed repeat blocks are detected during body collection (nestedDepth != 0 check)

        return output;
    }
}
