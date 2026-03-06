#include "Assets/Loaders/FLDrawConfig.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cctype>
#include <sstream>
#include <filesystem>

namespace Assets
{
    static glm::vec3 HexToVec3(const std::string& hex)
    {
        // hex format: #RRGGBB
        if (hex.size() < 7 || hex[0] != '#')
            return glm::vec3(0);
        unsigned int r = std::stoul(hex.substr(1, 2), nullptr, 16);
        unsigned int g = std::stoul(hex.substr(3, 2), nullptr, 16);
        unsigned int b = std::stoul(hex.substr(5, 2), nullptr, 16);
        return glm::vec3(r / 255.0f, g / 255.0f, b / 255.0f);
    }

    glm::vec3 LDrawColorTable::SrgbToLinear(glm::vec3 srgb)
    {
        auto convert = [](float c) -> float
        {
            return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
        };
        return glm::vec3(convert(srgb.r), convert(srgb.g), convert(srgb.b));
    }

    void LDrawColorTable::Parse(const std::string& ldconfigPath)
    {
        std::ifstream file(ldconfigPath);
        if (!file.is_open())
        {
            SPDLOG_WARN("LDraw: cannot open LDConfig at {}", ldconfigPath);
            return;
        }

        std::string line;
        while (std::getline(file, line))
        {
            // Match lines starting with "0 !COLOUR"
            if (line.find("0 !COLOUR") == std::string::npos)
                continue;

            LDrawColor color{};
            color.alpha = 1.0f;
            color.finish = LDrawColor::Finish::Solid;

            std::istringstream iss(line);
            std::string token;

            // Skip "0" and "!COLOUR"
            iss >> token >> token;

            // Read name
            iss >> color.name;

            // Parse key-value pairs
            while (iss >> token)
            {
                if (token == "CODE")
                {
                    iss >> color.code;
                }
                else if (token == "VALUE")
                {
                    std::string hex;
                    iss >> hex;
                    color.diffuse = SrgbToLinear(HexToVec3(hex));
                }
                else if (token == "EDGE")
                {
                    std::string hex;
                    iss >> hex;
                    color.edge = SrgbToLinear(HexToVec3(hex));
                }
                else if (token == "ALPHA")
                {
                    int a;
                    iss >> a;
                    color.alpha = a / 255.0f;
                }
                else if (token == "CHROME")
                {
                    color.finish = LDrawColor::Finish::Chrome;
                }
                else if (token == "PEARLESCENT")
                {
                    color.finish = LDrawColor::Finish::Pearlescent;
                }
                else if (token == "RUBBER")
                {
                    color.finish = LDrawColor::Finish::Rubber;
                }
                else if (token == "MATTE_METALLIC")
                {
                    color.finish = LDrawColor::Finish::MatteMetallic;
                }
                else if (token == "MATERIAL")
                {
                    // Glitter/Speckle are specified via MATERIAL keyword
                    std::string matType;
                    iss >> matType;
                    if (matType == "GLITTER")
                        color.finish = LDrawColor::Finish::Glitter;
                    else if (matType == "SPECKLE")
                        color.finish = LDrawColor::Finish::Speckle;
                }
            }

            colors_[color.code] = color;
        }

        SPDLOG_INFO("LDraw: parsed LDConfig with {} colors", colors_.size());
    }

    const LDrawColor* LDrawColorTable::GetColor(int code) const
    {
        auto it = colors_.find(code);
        if (it != colors_.end())
            return &it->second;
        return nullptr;
    }

    // ------------------------------------------------------------------
    // LDrawFileResolver
    // ------------------------------------------------------------------

    static std::string ToLower(const std::string& s)
    {
        std::string result = s;
        std::transform(result.begin(), result.end(), result.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return result;
    }

    void LDrawFileResolver::BuildIndex(const std::string& ldrawRoot)
    {
        ldrawRoot_ = ldrawRoot;

        // Scan directories (non-recursive) in priority order.
        // Each entry: {dirToScan, relativePrefix} where prefix is from ldrawRoot.
        const std::vector<std::pair<std::string, std::string>> dirs = {
            {"parts",   "parts"},
            {"p",       "p"},
            {"parts/s", "parts/s"},
            {"p/48",    "p/48"},
            {"p/8",     "p/8"}
        };

        for (const auto& [subdir, prefix] : dirs)
        {
            std::filesystem::path dirPath = std::filesystem::path(ldrawRoot) / subdir;
            if (!std::filesystem::exists(dirPath))
                continue;

            for (const auto& entry : std::filesystem::directory_iterator(dirPath))
            {
                if (!entry.is_regular_file())
                    continue;

                std::string filename = entry.path().filename().string();
                std::string key = ToLower(filename);
                std::string relPath = prefix + "/" + filename;

                // Only insert first occurrence (priority order)
                if (pathIndex_.find(key) == pathIndex_.end())
                    pathIndex_[key] = relPath;

                // Also index with subdirectory prefix (e.g., "s/3024s01.dat")
                if (prefix.find('/') != std::string::npos)
                {
                    // e.g., "parts/s" -> subKey = "s/filename"
                    std::string subPrefix = prefix.substr(prefix.find('/') + 1);
                    std::string subKey = ToLower(subPrefix + "/" + filename);
                    if (pathIndex_.find(subKey) == pathIndex_.end())
                        pathIndex_[subKey] = relPath;
                }
            }
        }

        SPDLOG_INFO("LDraw: indexed {} files in library", pathIndex_.size());
    }

    std::string LDrawFileResolver::Resolve(const std::string& filename) const
    {
        // Normalize: backslash -> forward slash, lowercase
        std::string normalized = filename;
        std::replace(normalized.begin(), normalized.end(), '\\', '/');
        normalized = ToLower(normalized);

        auto it = pathIndex_.find(normalized);
        if (it != pathIndex_.end())
            return ldrawRoot_ + it->second;

        // Try just the filename part
        auto slashPos = normalized.rfind('/');
        if (slashPos != std::string::npos)
        {
            std::string nameOnly = normalized.substr(slashPos + 1);
            it = pathIndex_.find(nameOnly);
            if (it != pathIndex_.end())
                return ldrawRoot_ + it->second;
        }

        SPDLOG_WARN("LDraw: cannot resolve file '{}'", filename);
        return {};
    }
}
