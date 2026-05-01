#pragma once

#include "Common/CoreMinimal.hpp"

class Brotato3DGameInstance;

namespace Brotato3D
{
    void RenderHUD(Brotato3DGameInstance& gameInstance);
    void RenderUpgradeModal(Brotato3DGameInstance& gameInstance);
    void RenderShopModal(Brotato3DGameInstance& gameInstance);
    void RenderResultModal(Brotato3DGameInstance& gameInstance);
}
