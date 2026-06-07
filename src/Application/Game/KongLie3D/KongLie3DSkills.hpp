#pragma once

#include "Engine/Common/CoreMinimal.hpp"
#include "KongLie3DPiece.hpp"

namespace KongLie3D
{
    class FBattleSystem;

    bool TryCastW(FPieceRuntime& piece, FBattleSystem& battleSystem);
    bool CastUltimate(FPieceRuntime& piece, FBattleSystem& battleSystem);
}
