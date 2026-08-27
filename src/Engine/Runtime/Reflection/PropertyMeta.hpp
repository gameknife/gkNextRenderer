#pragma once
#include <string>
#include <cstdint>

namespace Reflection
{
    // Property flags for metadata
    enum class PropertyFlags : uint32_t
    {
        None          = 0,
        ReadOnly      = 1 << 0,    // Read-only in editor
        Hidden        = 1 << 1,    // Hidden from editor
        ScriptExposed = 1 << 2,    // Exposed to the script binding layer (C#)
        Transient     = 1 << 3,    // Not serialized
        HasRange      = 1 << 4,    // Has min/max range

        // Named for QuickJS, which no longer exists. Kept for one release so out-of-tree
        // registrations keep compiling; new code uses ScriptExposed.
        JSExposed [[deprecated("renamed to ScriptExposed")]] = ScriptExposed,
    };

    inline PropertyFlags operator|(PropertyFlags a, PropertyFlags b)
    {
        return static_cast<PropertyFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
    }

    inline PropertyFlags operator&(PropertyFlags a, PropertyFlags b)
    {
        return static_cast<PropertyFlags>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
    }

    inline PropertyFlags& operator|=(PropertyFlags& a, PropertyFlags b)
    {
        a = a | b;
        return a;
    }

    // Property metadata structure
    struct PropertyMeta
    {
        std::string displayName;    // Display name in editor
        std::string category;       // Category/group name
        std::string tooltip;        // Tooltip text
        PropertyFlags flags = PropertyFlags::ScriptExposed;  // Default: exposed to scripts
        float minValue = 0.0f;      // Min value for numeric types
        float maxValue = 0.0f;      // Max value for numeric types

        PropertyMeta() = default;
        
        PropertyMeta(const char* display, const char* cat, const char* tip = "",
                    PropertyFlags f = PropertyFlags::ScriptExposed,
                    float minVal = 0.0f, float maxVal = 0.0f)
            : displayName(display)
            , category(cat)
            , tooltip(tip)
            , flags(f)
            , minValue(minVal)
            , maxValue(maxVal)
        {
        }

        bool IsReadOnly() const
        {
            return HasFlag(PropertyFlags::ReadOnly);
        }

        bool IsHidden() const
        {
            return HasFlag(PropertyFlags::Hidden);
        }

        bool IsScriptExposed() const
        {
            return HasFlag(PropertyFlags::ScriptExposed);
        }

        [[deprecated("renamed to IsScriptExposed")]]
        bool IsJSExposed() const
        {
            return IsScriptExposed();
        }

        bool IsTransient() const
        {
            return HasFlag(PropertyFlags::Transient);
        }

        bool HasRangeLimit() const
        {
            return HasFlag(PropertyFlags::HasRange);
        }

        bool HasFlag(PropertyFlags f) const
        {
            return (static_cast<uint32_t>(flags) & static_cast<uint32_t>(f)) != 0;
        }
    };

    // Helper to create common property configurations
    namespace PropertyPresets
    {
        inline PropertyMeta ReadOnly(const char* display, const char* cat, const char* tip = "")
        {
            return PropertyMeta(display, cat, tip, PropertyFlags::ReadOnly | PropertyFlags::ScriptExposed);
        }

        inline PropertyMeta Editable(const char* display, const char* cat, const char* tip = "")
        {
            return PropertyMeta(display, cat, tip, PropertyFlags::ScriptExposed);
        }

        inline PropertyMeta Range(const char* display, const char* cat, float minVal, float maxVal, const char* tip = "")
        {
            return PropertyMeta(display, cat, tip, PropertyFlags::ScriptExposed | PropertyFlags::HasRange, minVal, maxVal);
        }

        inline PropertyMeta Hidden()
        {
            return PropertyMeta("", "", "", PropertyFlags::Hidden);
        }

        inline PropertyMeta Transient(const char* display, const char* cat, const char* tip = "")
        {
            return PropertyMeta(display, cat, tip, PropertyFlags::ScriptExposed | PropertyFlags::Transient);
        }
    }
}
