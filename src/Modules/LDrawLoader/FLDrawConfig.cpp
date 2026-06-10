#include "Modules/LDrawLoader/FLDrawConfig.h"
#include "Engine/Utilities/FileHelper.hpp"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <sstream>
#include <filesystem>

namespace Assets
{
    namespace
    {
        constexpr std::array<const char*, 6> kLDrawIndexedDirectories = {
            "parts",
            "p",
            "parts/s",
            "p/48",
            "p/8",
            "p/4"
        };

        struct RealisticColorOverride
        {
            int code;
            glm::vec3 srgb;
        };

        constexpr std::array<RealisticColorOverride, 117> kRealisticColorOverrides = {{
            {0,   { 33.0f / 255.0f,  33.0f / 255.0f,  33.0f / 255.0f}},
            {1,   { 13.0f / 255.0f, 105.0f / 255.0f, 171.0f / 255.0f}},
            {2,   { 40.0f / 255.0f, 127.0f / 255.0f,  70.0f / 255.0f}},
            {3,   {  0.0f / 255.0f, 143.0f / 255.0f, 155.0f / 255.0f}},
            {4,   {196.0f / 255.0f,  40.0f / 255.0f,  27.0f / 255.0f}},
            {5,   {205.0f / 255.0f,  98.0f / 255.0f, 152.0f / 255.0f}},
            {6,   { 98.0f / 255.0f,  71.0f / 255.0f,  50.0f / 255.0f}},
            {7,   {161.0f / 255.0f, 165.0f / 255.0f, 162.0f / 255.0f}},
            {8,   {109.0f / 255.0f, 110.0f / 255.0f, 108.0f / 255.0f}},
            {9,   {180.0f / 255.0f, 210.0f / 255.0f, 227.0f / 255.0f}},
            {10,  { 75.0f / 255.0f, 151.0f / 255.0f,  74.0f / 255.0f}},
            {11,  { 85.0f / 255.0f, 165.0f / 255.0f, 175.0f / 255.0f}},
            {12,  {242.0f / 255.0f, 112.0f / 255.0f,  94.0f / 255.0f}},
            {13,  {252.0f / 255.0f, 151.0f / 255.0f, 172.0f / 255.0f}},
            {14,  {245.0f / 255.0f, 205.0f / 255.0f,  47.0f / 255.0f}},
            {15,  {242.0f / 255.0f, 243.0f / 255.0f, 242.0f / 255.0f}},
            {17,  {194.0f / 255.0f, 218.0f / 255.0f, 184.0f / 255.0f}},
            {18,  {249.0f / 255.0f, 233.0f / 255.0f, 153.0f / 255.0f}},
            {19,  {215.0f / 255.0f, 197.0f / 255.0f, 153.0f / 255.0f}},
            {20,  {193.0f / 255.0f, 202.0f / 255.0f, 222.0f / 255.0f}},
            {21,  {224.0f / 255.0f, 255.0f / 255.0f, 176.0f / 255.0f}},
            {22,  {107.0f / 255.0f,  50.0f / 255.0f, 123.0f / 255.0f}},
            {23,  { 35.0f / 255.0f,  71.0f / 255.0f, 139.0f / 255.0f}},
            {25,  {218.0f / 255.0f, 133.0f / 255.0f,  64.0f / 255.0f}},
            {26,  {146.0f / 255.0f,  57.0f / 255.0f, 120.0f / 255.0f}},
            {27,  {164.0f / 255.0f, 189.0f / 255.0f,  70.0f / 255.0f}},
            {28,  {149.0f / 255.0f, 138.0f / 255.0f, 115.0f / 255.0f}},
            {29,  {228.0f / 255.0f, 173.0f / 255.0f, 200.0f / 255.0f}},
            {30,  {172.0f / 255.0f, 120.0f / 255.0f, 186.0f / 255.0f}},
            {31,  {225.0f / 255.0f, 213.0f / 255.0f, 237.0f / 255.0f}},
            {32,  {  0.0f / 255.0f,  20.0f / 255.0f,  20.0f / 255.0f}},
            {33,  {123.0f / 255.0f, 182.0f / 255.0f, 232.0f / 255.0f}},
            {34,  {132.0f / 255.0f, 182.0f / 255.0f, 141.0f / 255.0f}},
            {35,  {217.0f / 255.0f, 228.0f / 255.0f, 167.0f / 255.0f}},
            {36,  {205.0f / 255.0f,  84.0f / 255.0f,  75.0f / 255.0f}},
            {37,  {228.0f / 255.0f, 173.0f / 255.0f, 200.0f / 255.0f}},
            {38,  {255.0f / 255.0f,  43.0f / 255.0f,   0.0f / 255.0f}},
            {40,  {166.0f / 255.0f, 145.0f / 255.0f, 130.0f / 255.0f}},
            {41,  {170.0f / 255.0f, 229.0f / 255.0f, 255.0f / 255.0f}},
            {42,  {198.0f / 255.0f, 255.0f / 255.0f,   0.0f / 255.0f}},
            {43,  {193.0f / 255.0f, 223.0f / 255.0f, 240.0f / 255.0f}},
            {44,  {150.0f / 255.0f, 112.0f / 255.0f, 159.0f / 255.0f}},
            {46,  {247.0f / 255.0f, 241.0f / 255.0f, 141.0f / 255.0f}},
            {47,  {252.0f / 255.0f, 252.0f / 255.0f, 252.0f / 255.0f}},
            {52,  {156.0f / 255.0f, 149.0f / 255.0f, 199.0f / 255.0f}},
            {54,  {255.0f / 255.0f, 246.0f / 255.0f, 123.0f / 255.0f}},
            {57,  {226.0f / 255.0f, 176.0f / 255.0f,  96.0f / 255.0f}},
            {65,  {236.0f / 255.0f, 201.0f / 255.0f,  53.0f / 255.0f}},
            {66,  {202.0f / 255.0f, 176.0f / 255.0f,   0.0f / 255.0f}},
            {67,  {255.0f / 255.0f, 255.0f / 255.0f, 255.0f / 255.0f}},
            {68,  {243.0f / 255.0f, 207.0f / 255.0f, 155.0f / 255.0f}},
            {69,  {142.0f / 255.0f,  66.0f / 255.0f, 133.0f / 255.0f}},
            {70,  {105.0f / 255.0f,  64.0f / 255.0f,  39.0f / 255.0f}},
            {71,  {163.0f / 255.0f, 162.0f / 255.0f, 164.0f / 255.0f}},
            {72,  { 99.0f / 255.0f,  95.0f / 255.0f,  97.0f / 255.0f}},
            {73,  {110.0f / 255.0f, 153.0f / 255.0f, 201.0f / 255.0f}},
            {74,  {161.0f / 255.0f, 196.0f / 255.0f, 139.0f / 255.0f}},
            {77,  {220.0f / 255.0f, 144.0f / 255.0f, 149.0f / 255.0f}},
            {78,  {246.0f / 255.0f, 215.0f / 255.0f, 179.0f / 255.0f}},
            {79,  {255.0f / 255.0f, 255.0f / 255.0f, 255.0f / 255.0f}},
            {80,  {140.0f / 255.0f, 140.0f / 255.0f, 140.0f / 255.0f}},
            {82,  {219.0f / 255.0f, 172.0f / 255.0f,  52.0f / 255.0f}},
            {84,  {170.0f / 255.0f, 125.0f / 255.0f,  85.0f / 255.0f}},
            {85,  { 52.0f / 255.0f,  43.0f / 255.0f, 117.0f / 255.0f}},
            {86,  {124.0f / 255.0f,  92.0f / 255.0f,  69.0f / 255.0f}},
            {89,  {155.0f / 255.0f, 178.0f / 255.0f, 239.0f / 255.0f}},
            {92,  {204.0f / 255.0f, 142.0f / 255.0f, 104.0f / 255.0f}},
            {100, {238.0f / 255.0f, 196.0f / 255.0f, 182.0f / 255.0f}},
            {115, {199.0f / 255.0f, 210.0f / 255.0f,  60.0f / 255.0f}},
            {134, {174.0f / 255.0f, 122.0f / 255.0f,  89.0f / 255.0f}},
            {135, {171.0f / 255.0f, 173.0f / 255.0f, 172.0f / 255.0f}},
            {137, {106.0f / 255.0f, 122.0f / 255.0f, 150.0f / 255.0f}},
            {142, {220.0f / 255.0f, 188.0f / 255.0f, 129.0f / 255.0f}},
            {148, { 62.0f / 255.0f,  60.0f / 255.0f,  57.0f / 255.0f}},
            {151, { 14.0f / 255.0f,  94.0f / 255.0f,  77.0f / 255.0f}},
            {179, {160.0f / 255.0f, 160.0f / 255.0f, 160.0f / 255.0f}},
            {183, {242.0f / 255.0f, 243.0f / 255.0f, 242.0f / 255.0f}},
            {191, {248.0f / 255.0f, 187.0f / 255.0f,  61.0f / 255.0f}},
            {212, {159.0f / 255.0f, 195.0f / 255.0f, 233.0f / 255.0f}},
            {216, {143.0f / 255.0f,  76.0f / 255.0f,  42.0f / 255.0f}},
            {226, {253.0f / 255.0f, 234.0f / 255.0f, 140.0f / 255.0f}},
            {232, {125.0f / 255.0f, 187.0f / 255.0f, 221.0f / 255.0f}},
            {256, { 33.0f / 255.0f,  33.0f / 255.0f,  33.0f / 255.0f}},
            {272, { 32.0f / 255.0f,  58.0f / 255.0f,  86.0f / 255.0f}},
            {273, { 13.0f / 255.0f, 105.0f / 255.0f, 171.0f / 255.0f}},
            {288, { 39.0f / 255.0f,  70.0f / 255.0f,  44.0f / 255.0f}},
            {294, {189.0f / 255.0f, 198.0f / 255.0f, 173.0f / 255.0f}},
            {297, {170.0f / 255.0f, 127.0f / 255.0f,  46.0f / 255.0f}},
            {308, { 53.0f / 255.0f,  33.0f / 255.0f,   0.0f / 255.0f}},
            {313, {171.0f / 255.0f, 217.0f / 255.0f, 255.0f / 255.0f}},
            {320, {123.0f / 255.0f,  46.0f / 255.0f,  47.0f / 255.0f}},
            {321, { 70.0f / 255.0f, 155.0f / 255.0f, 195.0f / 255.0f}},
            {322, {104.0f / 255.0f, 195.0f / 255.0f, 226.0f / 255.0f}},
            {323, {211.0f / 255.0f, 242.0f / 255.0f, 234.0f / 255.0f}},
            {324, {196.0f / 255.0f,   0.0f / 255.0f,  38.0f / 255.0f}},
            {326, {226.0f / 255.0f, 249.0f / 255.0f, 154.0f / 255.0f}},
            {330, {119.0f / 255.0f, 119.0f / 255.0f,  78.0f / 255.0f}},
            {334, {187.0f / 255.0f, 165.0f / 255.0f,  61.0f / 255.0f}},
            {335, {149.0f / 255.0f, 121.0f / 255.0f, 118.0f / 255.0f}},
            {366, {209.0f / 255.0f, 131.0f / 255.0f,   4.0f / 255.0f}},
            {373, {135.0f / 255.0f, 124.0f / 255.0f, 144.0f / 255.0f}},
            {375, {193.0f / 255.0f, 194.0f / 255.0f, 193.0f / 255.0f}},
            {378, {120.0f / 255.0f, 144.0f / 255.0f, 129.0f / 255.0f}},
            {379, { 94.0f / 255.0f, 116.0f / 255.0f, 140.0f / 255.0f}},
            {383, {224.0f / 255.0f, 224.0f / 255.0f, 224.0f / 255.0f}},
            {406, {  0.0f / 255.0f,  29.0f / 255.0f, 104.0f / 255.0f}},
            {449, {129.0f / 255.0f,   0.0f / 255.0f, 123.0f / 255.0f}},
            {450, {203.0f / 255.0f, 132.0f / 255.0f,  66.0f / 255.0f}},
            {462, {226.0f / 255.0f, 155.0f / 255.0f,  63.0f / 255.0f}},
            {484, {160.0f / 255.0f,  95.0f / 255.0f,  52.0f / 255.0f}},
            {490, {215.0f / 255.0f, 240.0f / 255.0f,   0.0f / 255.0f}},
            {493, {101.0f / 255.0f, 103.0f / 255.0f,  97.0f / 255.0f}},
            {494, {208.0f / 255.0f, 208.0f / 255.0f, 208.0f / 255.0f}},
            {496, {163.0f / 255.0f, 162.0f / 255.0f, 164.0f / 255.0f}},
            {503, {199.0f / 255.0f, 193.0f / 255.0f, 183.0f / 255.0f}},
            {504, {137.0f / 255.0f, 135.0f / 255.0f, 136.0f / 255.0f}},
            {511, {250.0f / 255.0f, 250.0f / 255.0f, 250.0f / 255.0f}}
        }};

        void ApplyRealisticColorOverrides(std::unordered_map<int, LDrawColor>& colors)
        {
            for (const auto& overrideColor : kRealisticColorOverrides)
            {
                auto it = colors.find(overrideColor.code);
                if (it == colors.end())
                    continue;

                it->second.diffuse = LDrawColorTable::SrgbToLinear(overrideColor.srgb);
            }
        }

        std::string ToLower(const std::string& s)
        {
            std::string result = s;
            std::transform(result.begin(), result.end(), result.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return result;
        }

        void IndexLDrawPath(
            std::unordered_map<std::string, std::string>& pathIndex,
            const std::string& filename,
            const std::string& resolvedPath,
            const std::string& prefix)
        {
            const std::string key = ToLower(filename);
            if (pathIndex.find(key) == pathIndex.end())
            {
                pathIndex[key] = resolvedPath;
            }

            if (prefix.find('/') != std::string::npos)
            {
                const std::string subPrefix = prefix.substr(prefix.find('/') + 1);
                const std::string subKey = ToLower(subPrefix + "/" + filename);
                if (pathIndex.find(subKey) == pathIndex.end())
                {
                    pathIndex[subKey] = resolvedPath;
                }
            }
        }
    }

    static glm::vec3 HexToVec3(const std::string& hex)
    {
        // hex format: #RRGGBB
        if (hex.size() < 7 || hex[0] != '#')
            return glm::vec3(0);
        const unsigned int r = static_cast<unsigned int>(std::stoul(hex.substr(1, 2), nullptr, 16));
        const unsigned int g = static_cast<unsigned int>(std::stoul(hex.substr(3, 2), nullptr, 16));
        const unsigned int b = static_cast<unsigned int>(std::stoul(hex.substr(5, 2), nullptr, 16));
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
        std::string ldconfigContent;
        if (!LoadLDrawTextResource(ldconfigPath, ldconfigContent))
        {
            SPDLOG_WARN("LDraw: cannot open LDConfig at {}", ldconfigPath);
            return;
        }

        std::istringstream file(ldconfigContent);
        std::string line;
        while (std::getline(file, line))
        {
            // Match lines starting with "0 !COLOUR"
            if (line.find("0 !COLOUR") == std::string::npos)
                continue;

            LDrawColor color{};
            color.alpha = 1.0f;
            color.luminance = 0.0f;
            color.hasSecondaryDiffuse = false;
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
                else if (token == "LUMINANCE")
                {
                    iss >> color.luminance;
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

                    std::string materialToken;
                    while (iss >> materialToken)
                    {
                        if (materialToken == "VALUE")
                        {
                            std::string secondaryHex;
                            iss >> secondaryHex;
                            color.secondaryDiffuse = SrgbToLinear(HexToVec3(secondaryHex));
                            color.hasSecondaryDiffuse = true;
                        }
                    }
                }
            }

            colors_[color.code] = color;
        }

        ApplyRealisticColorOverrides(colors_);

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

    void LDrawFileResolver::BuildIndex(const std::string& ldrawRoot)
    {
        pathIndex_.clear();

        for (const char* subdir : kLDrawIndexedDirectories)
        {
            const std::string prefix(subdir);
            std::filesystem::path dirPath = std::filesystem::path(ldrawRoot) / prefix;
            if (!std::filesystem::exists(dirPath))
            {
                continue;
            }

            for (const auto& entry : std::filesystem::directory_iterator(dirPath))
            {
                if (!entry.is_regular_file())
                {
                    continue;
                }

                const std::string filename = entry.path().filename().string();
                const std::string resolvedPath = entry.path().lexically_normal().generic_string();
                IndexLDrawPath(pathIndex_, filename, resolvedPath, prefix);
            }
        }

        SPDLOG_INFO("LDraw: indexed {} files in library", pathIndex_.size());
    }

    bool LDrawFileResolver::BuildIndexFromMountedRoot(const std::string& ldrawRootEntry)
    {
        pathIndex_.clear();

        if (!EnsureLDrawLibraryPakMounted())
        {
            return false;
        }

        Utilities::Package::FPackageFileSystem* pakSystem = Utilities::Package::FPackageFileSystem::TryGetInstance();
        if (pakSystem == nullptr)
        {
            return false;
        }

        std::string normalizedRoot = ldrawRootEntry;
        std::replace(normalizedRoot.begin(), normalizedRoot.end(), '\\', '/');
        if (!normalizedRoot.empty() && normalizedRoot.back() == '/')
        {
            normalizedRoot.pop_back();
        }

        const std::string rootPrefix = normalizedRoot + "/";
        const std::vector<std::string> mountedEntries = pakSystem->ListMountedEntries(rootPrefix);
        if (mountedEntries.empty())
        {
            SPDLOG_WARN("LDraw: no mounted entries found under '{}'", normalizedRoot);
            return false;
        }

        for (const char* subdir : kLDrawIndexedDirectories)
        {
            const std::string relativePrefix = std::string(subdir) + "/";
            for (const std::string& mountedEntry : mountedEntries)
            {
                std::string normalizedEntry = mountedEntry;
                std::replace(normalizedEntry.begin(), normalizedEntry.end(), '\\', '/');
                if (normalizedEntry.rfind(rootPrefix, 0) != 0)
                {
                    continue;
                }

                const std::string relativePath = normalizedEntry.substr(rootPrefix.size());
                if (relativePath.rfind(relativePrefix, 0) != 0)
                {
                    continue;
                }

                const std::string filename = relativePath.substr(relativePrefix.size());
                if (filename.empty() || filename.find('/') != std::string::npos)
                {
                    continue;
                }

                IndexLDrawPath(pathIndex_, filename, normalizedEntry, subdir);
            }
        }

        SPDLOG_INFO("LDraw: indexed {} files in mounted pak root '{}'", pathIndex_.size(), normalizedRoot);
        return !pathIndex_.empty();
    }

    std::string LDrawFileResolver::Resolve(const std::string& filename) const
    {
        // Normalize: backslash -> forward slash, lowercase
        std::string normalized = filename;
        std::replace(normalized.begin(), normalized.end(), '\\', '/');
        normalized = ToLower(normalized);

        auto it = pathIndex_.find(normalized);
        if (it != pathIndex_.end())
            return it->second;

        // Try just the filename part
        auto slashPos = normalized.rfind('/');
        if (slashPos != std::string::npos)
        {
            std::string nameOnly = normalized.substr(slashPos + 1);
            it = pathIndex_.find(nameOnly);
            if (it != pathIndex_.end())
                return it->second;
        }

        if (warnOnMissing_)
        {
            SPDLOG_WARN("LDraw: cannot resolve file '{}'", filename);
        }
        return {};
    }

    bool EnsureLDrawLibraryPakMounted()
    {
        Utilities::Package::FPackageFileSystem* pakSystem = Utilities::Package::FPackageFileSystem::TryGetInstance();
        if (pakSystem == nullptr)
        {
            return false;
        }

        const std::string ldconfigEntry = std::string(kLDrawLibraryRootEntry) + "/LDConfig.ldr";
        if (pakSystem->HasMountedEntry(ldconfigEntry))
        {
            return true;
        }

        const std::string pakPath = Utilities::FileHelper::GetPlatformFilePath(kLDrawLibraryPakPath);
        if (!std::filesystem::exists(pakPath))
        {
            SPDLOG_WARN("LDraw: pak file not found at '{}'", pakPath);
            return false;
        }

        pakSystem->MountPak(pakPath);
        if (!pakSystem->HasMountedEntry(ldconfigEntry))
        {
            SPDLOG_WARN("LDraw: pak '{}' mounted but '{}' was not found", pakPath, ldconfigEntry);
            return false;
        }

        return true;
    }

    bool LoadLDrawTextResource(const std::string& pathOrEntry, std::string& outText)
    {
        outText.clear();

        std::error_code ec;
        const std::filesystem::path path(pathOrEntry);
        if (path.is_absolute() || std::filesystem::exists(path, ec))
        {
            std::ifstream file(path, std::ios::binary);
            if (!file.is_open())
            {
                return false;
            }

            outText.assign(
                std::istreambuf_iterator<char>(file),
                std::istreambuf_iterator<char>());
            return true;
        }

        Utilities::Package::FPackageFileSystem* pakSystem = Utilities::Package::FPackageFileSystem::TryGetInstance();
        if (pakSystem != nullptr)
        {
            std::vector<uint8_t> fileData;
            if (pakSystem->LoadMountedFile(pathOrEntry, fileData))
            {
                outText.assign(fileData.begin(), fileData.end());
                return true;
            }
        }

        std::ifstream file(pathOrEntry, std::ios::binary);
        if (!file.is_open())
        {
            return false;
        }

        outText.assign(
            std::istreambuf_iterator<char>(file),
            std::istreambuf_iterator<char>());
        return true;
    }
}
