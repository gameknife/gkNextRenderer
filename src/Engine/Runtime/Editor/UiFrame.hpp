#pragma once

#include "Engine/Common/CoreMinimal.hpp"

namespace NextUI
{
    enum class EUiDeveloperLayer : uint8_t
    {
        None = 0,
        Statistics = 1u << 0u,
        Console = 1u << 1u,
        Memory = 1u << 2u,
        All = (1u << 0u) | (1u << 1u) | (1u << 2u),
    };

    constexpr EUiDeveloperLayer operator&(const EUiDeveloperLayer lhs, const EUiDeveloperLayer rhs)
    {
        return static_cast<EUiDeveloperLayer>(static_cast<uint8_t>(lhs) & static_cast<uint8_t>(rhs));
    }

    constexpr EUiDeveloperLayer operator|(const EUiDeveloperLayer lhs, const EUiDeveloperLayer rhs)
    {
        return static_cast<EUiDeveloperLayer>(static_cast<uint8_t>(lhs) | static_cast<uint8_t>(rhs));
    }

    constexpr bool HasUiLayer(const EUiDeveloperLayer mask, const EUiDeveloperLayer layer)
    {
        return (static_cast<uint8_t>(mask) & static_cast<uint8_t>(layer)) != 0;
    }

    struct FUiFramePolicy
    {
        bool allowApplicationUi = true;
        EUiDeveloperLayer allowedDeveloperLayers = EUiDeveloperLayer::All;
    };

    struct FUiFrameResult
    {
        EUiDeveloperLayer requestedDeveloperLayers = EUiDeveloperLayer::All;

        static constexpr FUiFrameResult FromLegacyHandled(const bool handled)
        {
            return {handled ? EUiDeveloperLayer::None : EUiDeveloperLayer::All};
        }
    };
}
