#pragma once

#include "NextTotalwarTypes.h"

#include <cstdint>
#include <functional>
#include <vector>

namespace NextGameplay
{
    class FNavGrid;
}

namespace NextTotalwar
{
    enum class ECombatEventType : uint8_t
    {
        Hit,
        Death,
        Rout,
        Rally,
        RegimentDestroyed,
        Volley,
    };

    struct FCombatEvent
    {
        ECombatEventType type = ECombatEventType::Hit;
        int16_t regiment = -1;
        int16_t soldier = -1;
        glm::vec3 worldPos{};
        float yaw = 0.0f;
        int16_t sourceRegiment = -1;
        int16_t sourceSoldier = -1;
    };

    struct FBattleContext
    {
        std::function<float(float, float)> sampleGround;
        const NextGameplay::FNavGrid* navGrid = nullptr;
        float worldHalfExtent = 200.0f;
    };

    struct FBattleState
    {
        std::vector<FCombatEvent> events;
        uint64_t combatTicks = 0;

        FBattleState()
        {
            events.reserve(4096);
        }
    };

    class FDeterministicRng
    {
    public:
        explicit FDeterministicRng(uint64_t seed = 1337) : state_(seed ? seed : 1) {}

        uint32_t NextU32()
        {
            uint64_t value = (state_ += 0x9e3779b97f4a7c15ULL);
            value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
            value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
            return static_cast<uint32_t>((value ^ (value >> 31U)) >> 32U);
        }

        float UnitFloat()
        {
            return static_cast<float>(NextU32() >> 8U) * (1.0f / 16777216.0f);
        }

        bool Chance(float probability) { return UnitFloat() < probability; }
        float Jitter(float amount) { return (UnitFloat() * 2.0f - 1.0f) * amount; }

    private:
        uint64_t state_;
    };
}
