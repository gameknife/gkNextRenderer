#pragma once

#include "Engine/Common/CoreMinimal.hpp"

#include <glm/vec3.hpp>

class NextEngine;

namespace NextDayz
{
    enum class EWorldAnchorType : uint8_t
    {
        PlayerSafe,
        Zombie,
        Loot,
        Well,
    };

    struct FWorldAnchorHandle
    {
        uint32_t index = 0;
        uint32_t generation = 0;
        bool IsValid() const { return generation != 0; }
    };

    struct FWorldAnchor
    {
        EWorldAnchorType type = EWorldAnchorType::Loot;
        std::string profile;
        glm::vec3 worldPos{};
        uint32_t nodeInstanceId = 0;
    };

    class WorldAnchorRegistry
    {
    public:
        void Scan(NextEngine& engine);
        void Clear();

        const FWorldAnchor* Resolve(FWorldAnchorHandle handle) const;
        std::vector<FWorldAnchorHandle> Find(EWorldAnchorType type) const;
        size_t Count(EWorldAnchorType type) const;
        uint32_t Generation() const { return generation_; }

    private:
        std::vector<FWorldAnchor> anchors_;
        uint32_t generation_ = 0;
    };
}
