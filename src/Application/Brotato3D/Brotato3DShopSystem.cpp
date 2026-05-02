#include "Brotato3DGameInstance.hpp"
#include "Brotato3DCommon.hpp"

#include <spdlog/spdlog.h>

#include "Brotato3DAudio.hpp"

using namespace Brotato3DUtil;

void Brotato3DGameInstance::SelectUpgrade(size_t choiceIndex)
{
    if (choiceIndex >= currentUpgradeChoices_.size())
    {
        return;
    }
    Brotato3D::PlayUiClickSfx();
    ApplyUpgrade(currentUpgradeChoices_[choiceIndex].stat, currentUpgradeChoices_[choiceIndex].delta);
    --player_.pendingLevelUps;
    if (player_.pendingLevelUps > 0)
    {
        RollUpgradeChoices();
        appState_ = Brotato3D::EAppState::LevelUpPicking;
        ClearMovementInput();
    }
    else
    {
        currentUpgradeChoices_.clear();
        appState_ = Brotato3D::EAppState::Playing;
        ClearMovementInput();
    }
}

void Brotato3DGameInstance::BuyShopItem(size_t slotIndex)
{
    if (slotIndex >= shopOffers_.size())
    {
        return;
    }
    const Brotato3D::FShopItemDef item = shopOffers_[slotIndex];
    if (player_.materials < item.cost)
    {
        return;
    }
    if (item.isWeaponCard)
    {
        if (!BuyWeaponCard(item))
        {
            return;
        }
    }
    else if (item.isPassiveItem)
    {
        if (!BuyPassiveItem(item))
        {
            return;
        }
    }
    else
    {
        player_.materials -= item.cost;
        ApplyShopItem(item);
    }
    Brotato3D::PlayUiClickSfx();
    Brotato3D::PlayShopBuySfx();
    shopOffers_.erase(shopOffers_.begin() + static_cast<std::ptrdiff_t>(slotIndex));
}

void Brotato3DGameInstance::RerollShop()
{
    if (shop_.Reroll(player_.materials, player_.ownedItemIds, equippedWeapons_, shopOffers_))
    {
        Brotato3D::PlayUiClickSfx();
        spdlog::info("[Brotato3D] Shop rerolled, materials={}", player_.materials);
    }
}

void Brotato3DGameInstance::ContinueFromShop()
{
    Brotato3D::PlayUiClickSfx();
    appState_ = Brotato3D::EAppState::Playing;
    ClearMovementInput();
    waveSystem_.EndIntermissionAndAdvance();
    if (waveSystem_.ConsumeVictory())
    {
        EnterResult(false);
    }
    else
    {
        BeginWaveBanner();
    }
}

void Brotato3DGameInstance::ApplyShopItem(const Brotato3D::FShopItemDef& item)
{
    if (item.stat == "damagePct")
    {
        player_.stats.damagePct += item.delta;
    }
    else if (item.stat == "damageFlat")
    {
        player_.stats.damageFlat += item.delta;
    }
    else if (item.stat == "atkSpeedPct")
    {
        player_.stats.atkSpeedPct += item.delta;
    }
    else if (item.stat == "rangePct")
    {
        player_.stats.rangePct += item.delta;
    }
    else if (item.stat == "moveSpeedPct")
    {
        player_.stats.moveSpeedPct += item.delta;
    }
    else if (item.stat == "pickupRadiusPct")
    {
        player_.stats.pickupRadiusPct += item.delta;
    }
    else if (item.stat == "critChancePct")
    {
        player_.stats.critChancePct += item.delta;
    }
    else if (item.stat == "critMultiplier")
    {
        player_.stats.critMultiplier += item.delta;
    }
    else if (item.stat == "maxHpFlat")
    {
        player_.stats.maxHpFlatBonus += static_cast<int>(std::round(item.delta));
        player_.maxHp = static_cast<int>(player_.stats.maxHpFlat) + player_.stats.maxHpFlatBonus;
        player_.currentHp = std::min(player_.maxHp, player_.currentHp + static_cast<int>(std::round(item.delta)));
    }
    else if (item.stat == "healPct")
    {
        player_.currentHp = std::min(player_.maxHp, player_.currentHp + static_cast<int>(std::round(player_.maxHp * item.delta)));
    }
}

bool Brotato3DGameInstance::BuyPassiveItem(const Brotato3D::FShopItemDef& item)
{
    if (player_.ownedItemIds.size() >= 6 ||
        std::find(player_.ownedItemIds.begin(), player_.ownedItemIds.end(), item.id) != player_.ownedItemIds.end())
    {
        return false;
    }

    const Brotato3D::FItemDef* itemDef = GetItemDef(item.id);
    if (!itemDef)
    {
        spdlog::warn("[Brotato3D] passive item '{}' not found", item.id);
        return false;
    }

    player_.materials -= item.cost;
    player_.ownedItemIds.push_back(item.id);
    ApplyPassiveItemStats(*itemDef);
    PushFloatingText(player_.worldPos + glm::vec3(0.0f, 1.1f, 0.0f), itemDef->name, glm::vec4(0.65f, 0.95f, 1.0f, 1.0f),
                     700.0f);
    return true;
}

void Brotato3DGameInstance::ApplyPassiveItemStats(const Brotato3D::FItemDef& item)
{
    if (item.trigger != "passive_stat")
    {
        return;
    }

    if (item.effect == "stat_pickupRadiusPct")
    {
        player_.stats.pickupRadiusPct += item.value;
    }
    else if (item.effect == "stat_moveSpeedPct")
    {
        player_.stats.moveSpeedPct += item.value;
    }
    else if (item.effect == "stat_damagePct")
    {
        player_.stats.damagePct += item.value;
    }
    else if (item.effect == "stat_atkSpeedPct")
    {
        player_.stats.atkSpeedPct += item.value;
    }
    else if (item.effect == "stat_rangePct")
    {
        player_.stats.rangePct += item.value;
    }
    else if (item.effect == "stat_critChancePct")
    {
        player_.stats.critChancePct += item.value;
    }
}

Brotato3D::FPlayerStats Brotato3DGameInstance::GetEffectiveStats() const
{
    Brotato3D::FPlayerStats stats = player_.stats;
    stats.damagePct += dynamicStatBuffs_.damagePct;
    stats.damageFlat += dynamicStatBuffs_.damageFlat;
    stats.atkSpeedPct += dynamicStatBuffs_.atkSpeedPct;
    stats.rangePct += dynamicStatBuffs_.rangePct;
    stats.moveSpeedPct += dynamicStatBuffs_.moveSpeedPct;
    stats.pickupRadiusPct += dynamicStatBuffs_.pickupRadiusPct;
    stats.critChancePct += dynamicStatBuffs_.critChancePct;
    return stats;
}

void Brotato3DGameInstance::StartShopping()
{
    shop_.SetWaveIndex(waveSystem_.GetCurrentWaveIndex());
    shop_.Roll(4, player_.ownedItemIds, equippedWeapons_, shopOffers_);
    Brotato3D::PlayShopOpenSfx();
    appState_ = Brotato3D::EAppState::Shopping;
    ClearMovementInput();
}

