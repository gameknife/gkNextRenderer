#pragma once

#include "Sim/SimComponents.h"

#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>

namespace NextRA::Sim
{
    class FOccupancyGrid
    {
    public:
        void Clear();
        void Add(FActorId actor, CPos cell);
        bool IsOccupied(CPos cell) const;
        bool IsOccupiedByOther(CPos cell, FActorId actor) const;
        std::span<const FActorId> ActorsAt(CPos cell) const;

    private:
        static int64_t Key(CPos cell);

        std::unordered_map<int64_t, std::vector<FActorId>> actorsByCell_;
    };
}
