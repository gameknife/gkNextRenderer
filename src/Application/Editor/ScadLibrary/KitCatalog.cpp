#include "KitCatalog.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace ScadLibrary
{
    namespace
    {
        // Category = the token after the namespace prefix: "hc_nature_tree" -> "nature".
        // Two-part names ("hc_boxc") fall into "misc".
        std::string DeriveCategory(const std::string& name)
        {
            const size_t first = name.find('_');
            if (first == std::string::npos)
            {
                return "misc";
            }
            const size_t second = name.find('_', first + 1);
            if (second == std::string::npos)
            {
                return "misc";
            }
            return name.substr(first + 1, second - first - 1);
        }

        std::string NormalizeWhitespace(const std::string& text)
        {
            std::string out;
            out.reserve(text.size());
            bool pendingSpace = false;
            for (const char c : text)
            {
                if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
                {
                    pendingSpace = !out.empty();
                    continue;
                }
                if (pendingSpace)
                {
                    out.push_back(' ');
                    pendingSpace = false;
                }
                out.push_back(c);
            }
            return out;
        }

        void ParseModules(const std::string& source, FKitInfo& kit)
        {
            size_t pos = 0;
            int line = 1;
            const size_t n = source.size();
            while (pos < n)
            {
                // Only match "module" at the start of a line (top-level defs in the
                // generated kits); skips nested helpers which are not callable anyway.
                if (source.compare(pos, 7, "module ") == 0)
                {
                    size_t p = pos + 7;
                    while (p < n && (source[p] == ' ' || source[p] == '\t'))
                    {
                        p++;
                    }
                    const size_t nameStart = p;
                    while (p < n && (std::isalnum(static_cast<unsigned char>(source[p])) || source[p] == '_'))
                    {
                        p++;
                    }
                    const std::string name = source.substr(nameStart, p - nameStart);
                    while (p < n && source[p] != '(' && source[p] != '\n')
                    {
                        p++;
                    }
                    std::string params;
                    if (p < n && source[p] == '(')
                    {
                        int depth = 0;
                        const size_t paramStart = p + 1;
                        while (p < n)
                        {
                            if (source[p] == '(')
                            {
                                depth++;
                            }
                            else if (source[p] == ')')
                            {
                                depth--;
                                if (depth == 0)
                                {
                                    break;
                                }
                            }
                            p++;
                        }
                        if (p < n)
                        {
                            params = NormalizeWhitespace(source.substr(paramStart, p - paramStart));
                        }
                    }
                    if (!name.empty())
                    {
                        kit.modules.push_back({name, params, DeriveCategory(name), line});
                    }
                }
                // Advance to the next line.
                while (pos < n && source[pos] != '\n')
                {
                    pos++;
                }
                pos++;
                line++;
            }
        }
    } // namespace

    std::vector<FKitInfo> ScanKits(const std::string& libDirAbs)
    {
        std::vector<FKitInfo> kits;
        std::error_code ec;
        if (!std::filesystem::is_directory(libDirAbs, ec))
        {
            return kits;
        }
        for (const auto& entry : std::filesystem::directory_iterator(libDirAbs, ec))
        {
            const std::filesystem::path& path = entry.path();
            if (!entry.is_regular_file(ec) || path.extension() != ".scad" ||
                path.filename().string().rfind("kit_", 0) != 0)
            {
                continue;
            }
            // kit_layout / kit_terrain / kit_road / kit_geo_city are rule
            // libraries: placement combinators and data-driven generators that
            // need a terrain, a network table or a footprint to produce
            // anything. They are not browsable parts, and evaluating them with
            // default arguments yields nothing.
            if (path.filename() == "kit_layout.scad" || path.filename() == "kit_terrain.scad" ||
                path.filename() == "kit_road.scad" || path.filename() == "kit_geo_city.scad")
            {
                continue;
            }
            std::ifstream in(path, std::ios::binary);
            if (!in)
            {
                continue;
            }
            std::ostringstream buffer;
            buffer << in.rdbuf();

            FKitInfo kit;
            kit.name = path.stem().string();
            kit.filePath = std::filesystem::absolute(path, ec).string();
            ParseModules(buffer.str(), kit);
            std::sort(kit.modules.begin(), kit.modules.end(),
                      [](const FKitModuleInfo& a, const FKitModuleInfo& b)
                      { return a.category != b.category ? a.category < b.category : a.name < b.name; });
            kits.push_back(std::move(kit));
        }
        std::sort(kits.begin(), kits.end(), [](const FKitInfo& a, const FKitInfo& b) { return a.name < b.name; });
        return kits;
    }

    std::vector<FKitInfo> LoadKits(const std::string& libDirAbs, bool& outFromCatalog)
    {
        outFromCatalog = false;
        const std::filesystem::path catalogPath = std::filesystem::path(libDirAbs) / "catalog.json";
        std::ifstream in(catalogPath, std::ios::binary);
        if (in)
        {
            try
            {
                const nlohmann::json catalog = nlohmann::json::parse(in);
                std::vector<FKitInfo> kits;
                std::error_code ec;
                for (const nlohmann::json& kitJson : catalog.at("kits"))
                {
                    FKitInfo kit;
                    kit.name = kitJson.at("name").get<std::string>();
                    kit.filePath = std::filesystem::absolute(
                                       std::filesystem::path(libDirAbs) / kitJson.at("file").get<std::string>(), ec)
                                       .string();
                    kit.scaleClass = kitJson.value("scaleClass", "");
                    for (const nlohmann::json& moduleJson : kitJson.at("modules"))
                    {
                        FKitModuleInfo moduleInfo;
                        moduleInfo.name = moduleJson.at("name").get<std::string>();
                        moduleInfo.params = moduleJson.value("params", "");
                        moduleInfo.category = moduleJson.value("category", "misc");
                        moduleInfo.line = moduleJson.value("line", 0);
                        if (moduleJson.value("ok", false))
                        {
                            const auto& footprint = moduleJson.at("footprint");
                            moduleInfo.hasMetrics = true;
                            moduleInfo.footprintX = footprint.at(0).get<float>();
                            moduleInfo.footprintY = footprint.at(1).get<float>();
                            moduleInfo.height = moduleJson.value("height", 0.0f);
                            moduleInfo.triangles = moduleJson.value("triangles", 0);
                        }
                        kit.modules.push_back(std::move(moduleInfo));
                    }
                    std::sort(kit.modules.begin(), kit.modules.end(),
                              [](const FKitModuleInfo& a, const FKitModuleInfo& b)
                              { return a.category != b.category ? a.category < b.category : a.name < b.name; });
                    kits.push_back(std::move(kit));
                }
                std::sort(kits.begin(), kits.end(),
                          [](const FKitInfo& a, const FKitInfo& b) { return a.name < b.name; });
                outFromCatalog = true;
                return kits;
            }
            catch (const std::exception&)
            {
                // Malformed catalog: fall through to the text scan.
            }
        }
        return ScanKits(libDirAbs);
    }
}
