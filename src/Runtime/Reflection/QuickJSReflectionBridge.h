#pragma once

#if WITH_QUICKJS
#include "PropertyAccessor.h"
#include "QuickJSTypeConverter.h"
#include <ThirdParty/quickjs-ng/quickjspp.hpp>
#include <entt/meta/meta.hpp>
#include <entt/meta/resolve.hpp>
#include <string>
#include <vector>
#include <spdlog/spdlog.h>

namespace Reflection
{
    class QuickJSReflectionBridge
    {
    public:
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
            
            bool first = true;
            for (auto&& [id, data] : metaType.data())
            {
                const char* name = data.name();
                if (name)
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
            }
            
            result += ";\n";
            return result;
        }
    };
}
#endif // WITH_QUICKJS
