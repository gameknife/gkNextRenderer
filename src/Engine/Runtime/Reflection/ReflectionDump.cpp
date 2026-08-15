#include "Engine/Runtime/Reflection/ReflectionDump.hpp"
#include "Engine/Runtime/Reflection/ReflectionRegistry.hpp"
#include "Engine/Runtime/Reflection/PropertyAccessor.hpp"
#include "Engine/Runtime/Reflection/PropertyTypes.hpp"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <filesystem>
#include <fstream>

namespace Reflection
{
    namespace
    {
        nlohmann::json DumpProperty(const PropertyInfo& info)
        {
            nlohmann::json property{
                {"name", info.name},
                {"propId", info.propId},
                {"type", PropertyTypeToString(info.type)},
                {"readOnly", info.meta.IsReadOnly()},
                {"hidden", info.meta.IsHidden()},
                {"scriptExposed", info.meta.IsScriptExposed()},
                {"displayName", info.meta.displayName},
                {"category", info.meta.category},
                {"tooltip", info.meta.tooltip},
            };

            if (info.meta.HasRangeLimit())
            {
                property["min"] = info.meta.minValue;
                property["max"] = info.meta.maxValue;
            }
            if (info.type == PropertyType::Enum)
            {
                property["enumTypeId"] = info.enumTypeId;
            }
            if (info.type == PropertyType::Array)
            {
                property["elementType"] = PropertyTypeToString(info.elementType);
            }
            return property;
        }
    }

    bool DumpManifest(const std::string& outputPath)
    {
        RegisterAllReflection();

        nlohmann::json manifest;
        manifest["version"] = 1;
        manifest["types"] = nlohmann::json::array();

        for (const FReflectedType& reflected : GetReflectedTypes())
        {
            std::vector<PropertyInfo> properties = PropertyAccessor::GetProperties(reflected.meta);
            // entt hands back data members in its own internal order, which is stable within a
            // build but not something to depend on across compilers. The manifest is committed and
            // reviewed as a diff, so sort by name and let it change only when reflection changes.
            std::sort(properties.begin(), properties.end(),
                      [](const PropertyInfo& lhs, const PropertyInfo& rhs) { return lhs.name < rhs.name; });

            nlohmann::json entry{
                {"name", reflected.name},
                {"typeId", reflected.meta.id()},
                {"kind", reflected.isComponent ? "component" : "node"},
                {"properties", nlohmann::json::array()},
            };
            for (const PropertyInfo& info : properties)
            {
                entry["properties"].push_back(DumpProperty(info));
            }
            manifest["types"].push_back(std::move(entry));
        }

        std::error_code ec;
        const std::filesystem::path path(outputPath);
        if (path.has_parent_path())
        {
            std::filesystem::create_directories(path.parent_path(), ec);
        }

        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        if (!stream)
        {
            SPDLOG_ERROR("--dump-reflection: cannot write {}", outputPath);
            return false;
        }
        // Two-space indent and a trailing newline so the committed file reads like source and
        // produces line-granular diffs. '\n' explicitly: the file is committed, so a CRLF copy on
        // Windows would look modified everywhere.
        stream << manifest.dump(2) << '\n';
        if (!stream)
        {
            SPDLOG_ERROR("--dump-reflection: failed while writing {}", outputPath);
            return false;
        }

        SPDLOG_INFO("--dump-reflection: wrote {} types to {}", manifest["types"].size(), outputPath);
        return true;
    }
}
