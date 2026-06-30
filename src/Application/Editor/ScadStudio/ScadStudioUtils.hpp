#pragma once

#include <algorithm>
#include <cctype>
#include <chrono>
#include <string>

namespace ScadStudio
{
    inline int64_t NowUnixSeconds()
    {
        return std::chrono::duration_cast<std::chrono::seconds>(
                   std::chrono::system_clock::now().time_since_epoch())
            .count();
    }

    inline std::string TrimCopy(std::string text)
    {
        const size_t b = text.find_first_not_of(" \t\r\n");
        if (b == std::string::npos)
        {
            return "";
        }
        const size_t e = text.find_last_not_of(" \t\r\n");
        return text.substr(b, e - b + 1);
    }

    inline std::string ToLower(std::string text)
    {
        std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return text;
    }
}
