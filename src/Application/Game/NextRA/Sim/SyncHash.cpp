#include "Sim/SyncHash.h"

#include <algorithm>

namespace NextRA::Sim
{
    namespace
    {
        constexpr uint64_t fnvOffset = 14695981039346656037ull;
        constexpr uint64_t fnvPrime = 1099511628211ull;

        void MixU64(uint64_t& hash, uint64_t value)
        {
            for (int index = 0; index < 8; ++index)
            {
                hash ^= static_cast<uint8_t>((value >> (index * 8)) & 0xffu);
                hash *= fnvPrime;
            }
        }

        void MixI64(uint64_t& hash, int64_t value)
        {
            MixU64(hash, static_cast<uint64_t>(value));
        }

        void MixBool(uint64_t& hash, bool value)
        {
            MixU64(hash, value ? 1u : 0u);
        }
    }

    uint64_t ComputeSyncHash(const FSimWorld& world)
    {
        uint64_t hash = fnvOffset;
        MixU64(hash, world.CurrentTick());
        MixI64(hash, world.WinnerPlayerId());
        MixU64(hash, world.RandomState());

        std::vector<FActorId> actors = world.Actors();
        std::sort(actors.begin(), actors.end());
        for (FActorId actor : actors)
        {
            if (!world.IsAlive(actor))
            {
                continue;
            }

            MixU64(hash, actor);
            if (const FOwner* owner = world.TryGetOwner(actor))
            {
                MixU64(hash, owner->playerId);
            }
            if (const FUnitType* unitType = world.TryGetUnitType(actor))
            {
                MixU64(hash, unitType->typeId);
            }
            if (const FSimTransform* transform = world.TryGetTransform(actor))
            {
                MixI64(hash, transform->pos.x.raw);
                MixI64(hash, transform->pos.y.raw);
                MixI64(hash, transform->pos.z.raw);
                MixI64(hash, transform->facing.value);
            }
            if (const FHealth* health = world.TryGetHealth(actor))
            {
                MixI64(hash, health->hp);
                MixI64(hash, health->maxHp);
            }
            if (const FAttack* attack = world.TryGetAttack(actor))
            {
                MixI64(hash, attack->targetActor);
                MixI64(hash, attack->cooldownLeft);
                MixBool(hash, attack->attackMove);
            }
            if (const FTurret* turret = world.TryGetTurret(actor))
            {
                MixI64(hash, turret->facing.value);
                MixI64(hash, turret->targetActor);
            }
            if (const FMobile* mobile = world.TryGetMobile(actor))
            {
                MixBool(hash, mobile->hasGoal);
                MixI64(hash, mobile->goal.x.raw);
                MixI64(hash, mobile->goal.z.raw);
                MixU64(hash, mobile->pathCursor);
            }
            if (const FProduction* production = world.TryGetProduction(actor))
            {
                MixU64(hash, production->queuedTypeId);
                MixI64(hash, production->progressLeft);
            }
            MixBool(hash, world.IsBase(actor));
        }

        return hash;
    }
}
