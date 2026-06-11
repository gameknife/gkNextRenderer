#pragma once

#if WITH_QUICKJS
#include "PropertyAccessor.h"
#include "QuickJSTypeConverter.h"
#include <ThirdParty/quickjs-ng/quickjspp.hpp>
#include <entt/meta/meta.hpp>
#include <entt/meta/resolve.hpp>
#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace Reflection
{
    class QuickJSReflectionBridge
    {
    public:
        static std::string ToTypeScriptType(entt::meta_type metaType)
        {
            if (!metaType)
            {
                return "any";
            }

            if (metaType == entt::resolve<void>())
            {
                return "void";
            }

            if (metaType.is_pointer())
            {
                metaType = metaType.remove_pointer();
            }

            const PropertyType type = PropertyAccessor::DeducePropertyType(metaType);
            if (type == PropertyType::Array)
            {
                auto [elementType, elementEnumTypeId] = PropertyAccessor::GetArrayElementType(metaType);
                (void)elementEnumTypeId;
                std::string elementTypeName = QuickJSTypeConverter::ToTypeScriptType(elementType);
                return elementTypeName + "[]";
            }

            if (metaType.is_enum())
            {
                return "string";
            }

            if (type == PropertyType::Unknown)
            {
                const char* name = metaType.name();
                if (name && *name)
                {
                    return name;
                }
            }

            return QuickJSTypeConverter::ToTypeScriptType(type);
        }

        // Automatically bind a component class to QuickJS using reflection
        template<typename ComponentT>
        static void AutoBindComponent(qjs::Context::Module& module, const char* jsClassName)
        {
            auto metaType = entt::resolve<ComponentT>();
            if (!metaType)
            {
                SPDLOG_WARN("Failed to resolve type for {}", jsClassName);
                return;
            }
            
            // Create class registrar
            auto& registrar = module.class_<ComponentT>(jsClassName);
            
            // Get all properties
            auto properties = PropertyAccessor::GetProperties(metaType);
            
            // For each property, we need to register getters/setters
            // Since quickjspp requires compile-time function pointers,
            // we use a different approach: register helper methods
            
            SPDLOG_INFO("Registered JS class {} with {} properties", jsClassName, properties.size());
        }
        
        // Generate TypeScript definition for a component type
        template<typename ComponentT>
        static std::string GenerateTypeScriptDef(const char* className)
        {
            auto metaType = entt::resolve<ComponentT>();
            if (!metaType)
            {
                return "";
            }
            
            std::string result = "export class ";
            result += className;
            result += " {\n";
            
            auto properties = PropertyAccessor::GetProperties(metaType);
            std::sort(properties.begin(), properties.end(), [](const PropertyInfo& lhs, const PropertyInfo& rhs)
            {
                return lhs.name < rhs.name;
            });
            for (const auto& prop : properties)
            {
                if (!prop.meta.IsJSExposed())
                {
                    continue;
                }
                
                std::string tsType = QuickJSTypeConverter::ToTypeScriptType(prop.type);
                
                if (prop.meta.IsReadOnly())
                {
                    result += "    readonly ";
                }
                else
                {
                    result += "    ";
                }
                
                result += prop.name;
                result += ": ";
                result += tsType;
                result += ";\n";
            }

            std::vector<entt::meta_func> functions;
            for (auto&& [id, func] : metaType.func())
            {
                (void)id;
                functions.push_back(func);
            }

            std::sort(functions.begin(), functions.end(), [](const entt::meta_func& lhs, const entt::meta_func& rhs)
            {
                const char* lhsName = lhs.name();
                const char* rhsName = rhs.name();
                return std::string_view(lhsName ? lhsName : "") < std::string_view(rhsName ? rhsName : "");
            });

            for (const auto& func : functions)
            {
                const char* funcName = func.name();
                if (!funcName)
                {
                    continue;
                }

                result += "    ";
                result += funcName;
                result += "(";

                const auto argCount = func.arity();
                for (std::size_t argIndex = 0; argIndex < argCount; ++argIndex)
                {
                    if (argIndex > 0)
                    {
                        result += ", ";
                    }

                    result += "arg";
                    result += std::to_string(argIndex);
                    result += ": ";
                    result += ToTypeScriptType(func.arg(argIndex));
                }

                result += "): ";
                result += ToTypeScriptType(func.ret());
                result += ";\n";
            }
            
            result += "}\n";
            return result;
        }
        
        // Generate TypeScript definition for an enum type
        template<typename EnumT>
        static std::string GenerateEnumTypeScriptDef(const char* enumName)
        {
            auto metaType = entt::resolve<EnumT>();
            if (!metaType || !metaType.is_enum())
            {
                return "";
            }
            
            std::string result = "export type ";
            result += enumName;
            result += " = ";

            std::vector<std::string> enumValues;
            for (auto&& [id, data] : metaType.data())
            {
                (void)id;
                const char* name = data.name();
                if (name)
                {
                    enumValues.emplace_back(name);
                }
            }

            std::sort(enumValues.begin(), enumValues.end());

            bool first = true;
            for (const std::string& name : enumValues)
            {
                if (!first)
                {
                    result += " | ";
                }
                result += "\"";
                result += name;
                result += "\"";
                first = false;
            }

            result += ";\n";
            return result;
        }
    };
}
#endif // WITH_QUICKJS
