#pragma once
#include <string>
#include <cstdint>

namespace Reflection
{
    // Property flags for metadata
    enum class PropertyFlags : uint32_t
    {
        None        = 0,
        ReadOnly    = 1 << 0,    // Read-only in editor
        Hidden      = 1 << 1,    // Hidden from editor
        JSExposed   = 1 << 2,    // Exposed to JavaScript
        Transient   = 1 << 3,    // Not serialized
        HasRange    = 1 << 4,    // Has min/max range
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
        PropertyFlags flags = PropertyFlags::JSExposed;  // Default: exposed to JS
        float minValue = 0.0f;      // Min value for numeric types
        float maxValue = 0.0f;      // Max value for numeric types

        PropertyMeta() = default;
        
        PropertyMeta(const char* display, const char* cat, const char* tip = "",
                    PropertyFlags f = PropertyFlags::JSExposed,
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

        bool IsJSExposed() const
        {
            return HasFlag(PropertyFlags::JSExposed);
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
            return PropertyMeta(display, cat, tip, PropertyFlags::ReadOnly | PropertyFlags::JSExposed);
        }

        inline PropertyMeta Editable(const char* display, const char* cat, const char* tip = "")
        {
            return PropertyMeta(display, cat, tip, PropertyFlags::JSExposed);
        }

        inline PropertyMeta Range(const char* display, const char* cat, float minVal, float maxVal, const char* tip = "")
        {
            return PropertyMeta(display, cat, tip, PropertyFlags::JSExposed | PropertyFlags::HasRange, minVal, maxVal);
        }

        inline PropertyMeta Hidden()
        {
            return PropertyMeta("", "", "", PropertyFlags::Hidden);
        }

        inline PropertyMeta Transient(const char* display, const char* cat, const char* tip = "")
        {
            return PropertyMeta(display, cat, tip, PropertyFlags::JSExposed | PropertyFlags::Transient);
        }
    }
}
