#include "Sim/OccupancyGrid.h"

#include <algorithm>

namespace NextRA::Sim
{
    void FOccupancyGrid::Clear()
    {
        actorsByCell_.clear();
    }

    void FOccupancyGrid::Add(FActorId actor, CPos cell)
    {
        std::vector<FActorId>& actors = actorsByCell_[Key(cell)];
        actors.push_back(actor);
        std::sort(actors.begin(), actors.end());
    }

    bool FOccupancyGrid::IsOccupied(CPos cell) const
    {
        const auto it = actorsByCell_.find(Key(cell));
        return it != actorsByCell_.end() && !it->second.empty();
    }

    bool FOccupancyGrid::IsOccupiedByOther(CPos cell, FActorId actor) const
    {
        const auto it = actorsByCell_.find(Key(cell));
        if (it == actorsByCell_.end())
        {
            return false;
        }

        return std::any_of(it->second.begin(), it->second.end(), [actor](FActorId other) {
            return other != actor;
        });
    }

    std::span<const FActorId> FOccupancyGrid::ActorsAt(CPos cell) const
    {
        const auto it = actorsByCell_.find(Key(cell));
        if (it == actorsByCell_.end())
        {
            return {};
        }
        return it->second;
    }

    int64_t FOccupancyGrid::Key(CPos cell)
    {
        return (static_cast<int64_t>(cell.x) << 32) ^ static_cast<uint32_t>(cell.z);
    }
}
