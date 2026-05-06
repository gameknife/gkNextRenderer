#pragma once

#include "Common/CoreMinimal.hpp"

#include <glm/glm.hpp>

namespace Voyage3D
{
    inline const char* U8Text(const char8_t* text)
    {
        return reinterpret_cast<const char*>(text);
    }

    enum class EAppState : uint8_t
    {
        MainMenu,
        NewGame,
        Sailing,
        NavalCombat,
        InPort,
        Trading,
        ShipUpgrade,
        Tavern,
        Paused,
        Result,
    };

    struct FInputState
    {
        bool keyW = false;
        bool keyA = false;
        bool keyS = false;
        bool keyD = false;
        bool keyShift = false;
        float stormDebuffMs = 0.0f;
    };

    struct FFloatingText
    {
        glm::vec3 worldPos = glm::vec3(0.0f);
        std::string text;
        glm::vec4 color = glm::vec4(1.0f);
        float lifeMs = 1000.0f;
        float remainingMs = 1000.0f;
        float fontScale = 1.0f;
    };

    struct FMuzzleFlash
    {
        glm::vec3 worldPos = glm::vec3(0.0f);
        glm::vec3 color = glm::vec3(1.0f, 0.75f, 0.25f);
        float lifeMs = 90.0f;
        float remainingMs = 90.0f;
    };

    struct FExpandingRing
    {
        glm::vec3 worldPos = glm::vec3(0.0f);
        glm::vec4 color = glm::vec4(1.0f);
        float lifeMs = 450.0f;
        float remainingMs = 450.0f;
        float maxRadius = 3.0f;
    };

    inline const glm::vec3 HiddenPosition(0.0f, -30.0f, 0.0f);
}
