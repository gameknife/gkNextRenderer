#include "Modules/NextAI/AI/LlamaPidFile.hpp"

#include <fstream>
#include <sstream>
#include <vector>

namespace NextAI
{
    static std::string TrimCopy(const std::string& s)
    {
        size_t b = 0;
        while (b < s.size() && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r' || s[b] == '\n')) ++b;
        size_t e = s.size();
        while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r' || s[e - 1] == '\n')) --e;
        return s.substr(b, e - b);
    }

    FLlamaPidInfo ParseLlamaPidFile(const std::string& content)
    {
        FLlamaPidInfo info;
        std::vector<std::string> lines;
        std::istringstream is(content);
        std::string line;
        while (std::getline(is, line))
        {
            lines.push_back(TrimCopy(line));
        }
        if (lines.size() < 4) return info;

        try
        {
            info.pid = std::stoi(lines[0]);
            info.host = lines[1];
            info.port = std::stoi(lines[2]);
            info.model = lines[3];
        }
        catch (...)
        {
            return FLlamaPidInfo{};
        }
        if (info.pid <= 0 || info.host.empty() || info.port <= 0 || info.model.empty())
        {
            return FLlamaPidInfo{};
        }
        info.valid = true;
        return info;
    }

    FLlamaPidInfo ReadLlamaPidFile(const std::string& pidFilePath)
    {
        std::ifstream file(pidFilePath, std::ios::binary);
        if (!file.is_open()) return {};
        std::ostringstream buf;
        buf << file.rdbuf();
        return ParseLlamaPidFile(buf.str());
    }
}
