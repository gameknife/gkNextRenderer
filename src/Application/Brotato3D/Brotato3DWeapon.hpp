#pragma once

#include "Common/CoreMinimal.hpp"
#include "Brotato3DDataLoader.hpp"

namespace Brotato3D
{
    struct FWeaponRuntime
    {
        std::string weaponId;
        const FWeaponDef* def = nullptr;
        float cooldownMs = 0.0f;
    };
}
