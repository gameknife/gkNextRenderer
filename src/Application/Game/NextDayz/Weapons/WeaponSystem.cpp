#include "WeaponSystem.hpp"

#include <algorithm>
#include <cmath>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Assets/GPU/UniformBuffer.hpp"
#include "Engine/Runtime/Components/PhysicsComponent.hpp"
#include "Engine/Runtime/Components/RenderComponent.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/Interface/DebugDraw.hpp"
#include "Engine/Runtime/Scene/SceneBuilder.hpp"

#include "Application/Game/NextDayz/Inventory/Inventory.hpp"
#include "Application/Game/NextDayz/Player/PlayerController.hpp"

namespace NextDayz
{
    namespace
    {
        constexpr float kReloadSeconds = 2.2f;
        constexpr float kHitscanRange = 400.0f;
        const std::string kEmptyId;
    }

    void WeaponSystem::SetViewModelAssets(uint32_t modelId, uint32_t materialId)
    {
        viewModelModelId_ = modelId;
        viewModelMaterialId_ = materialId;
        viewModelAssetsSet_ = true;
    }

    void WeaponSystem::OnSceneLoaded(NextEngine& engine)
    {
        viewModelOffset_ = config_.ViewModelHipOffset;
        if (!viewModelAssetsSet_)
        {
            return;
        }
        Assets::Scene& scene = engine.GetScene();
        // rayCastVisible = false so the player's own shots never hit the view model.
        viewModelNode_ = Assets::SceneBuilder::CreateRenderNode(
            "nd_viewmodel", glm::vec3(0.0f, -1000.0f, 0.0f), glm::vec3(1.0f), scene.GenerateInstanceId(),
            viewModelModelId_, viewModelMaterialId_, false, glm::quat(1.0f, 0.0f, 0.0f, 0.0f), false);
        auto physics = std::make_shared<Runtime::PhysicsComponent>();
        physics->SetMobility(Runtime::ENodeMobility::Dynamic);
        viewModelNode_->AddComponent(physics);
        scene.AddNode(viewModelNode_);
        scene.MarkDirty();
    }

    void WeaponSystem::OnSceneUnloaded()
    {
        viewModelNode_.reset();
    }

    bool WeaponSystem::Equip(int slot, const std::string& weaponId)
    {
        if (slot < 0 || slot >= static_cast<int>(slots_.size()))
        {
            return false;
        }
        const FWeaponDef* def = FindWeaponDef(weaponId);
        if (!def)
        {
            return false;
        }
        previousSlot_ = activeSlot_;
        slots_[slot].weaponId = weaponId;
        slots_[slot].ammoInMag = def->magSize; // fresh magazine on equip
        activeSlot_ = slot;
        reloading_ = false;
        reloadTimer_ = 0.0f;
        RefreshViewModelVisibility();
        return true;
    }

    void WeaponSystem::SwitchSlot(int slot)
    {
        if (slot < 0 || slot >= static_cast<int>(slots_.size()) || slot == activeSlot_)
        {
            return;
        }
        previousSlot_ = activeSlot_;
        activeSlot_ = slot;
        reloading_ = false;
        reloadTimer_ = 0.0f;
        RefreshViewModelVisibility();
    }

    void WeaponSystem::SwitchPrevious()
    {
        SwitchSlot(previousSlot_);
    }

    void WeaponSystem::RequestReload(Inventory& inventory)
    {
        const FWeaponDef* def = ActiveWeapon();
        if (!def || reloading_)
        {
            return;
        }
        if (slots_[activeSlot_].ammoInMag >= def->magSize)
        {
            return;
        }
        if (inventory.CountOf(std::string(AmmoItemId(def->ammo))) <= 0)
        {
            return;
        }
        reloading_ = true;
        reloadTimer_ = kReloadSeconds;
    }

    const FWeaponDef* WeaponSystem::ActiveWeapon() const
    {
        return FindWeaponDef(slots_[activeSlot_].weaponId);
    }

    std::string WeaponSystem::ActiveWeaponId() const
    {
        return slots_[activeSlot_].weaponId;
    }

    std::string WeaponSystem::ActiveDisplayName() const
    {
        const FWeaponDef* def = ActiveWeapon();
        return def ? std::string(def->displayName) : std::string("Unarmed");
    }

    int WeaponSystem::AmmoInMag() const
    {
        return slots_[activeSlot_].ammoInMag;
    }

    int WeaponSystem::AmmoReserve(const Inventory& inventory) const
    {
        const FWeaponDef* def = ActiveWeapon();
        return def ? inventory.CountOf(std::string(AmmoItemId(def->ammo))) : 0;
    }

    const std::string& WeaponSystem::SlotWeaponId(int slot) const
    {
        if (slot < 0 || slot >= static_cast<int>(slots_.size()))
        {
            return kEmptyId;
        }
        return slots_[slot].weaponId;
    }

    bool WeaponSystem::ConsumeFiredThisFrame()
    {
        const bool fired = firedThisFrame_;
        firedThisFrame_ = false;
        return fired;
    }

    void WeaponSystem::RefreshViewModelVisibility()
    {
        if (viewModelNode_)
        {
            if (auto render = viewModelNode_->GetComponent<Runtime::RenderComponent>())
            {
                render->SetVisible(HasActiveWeapon());
            }
        }
    }

    void WeaponSystem::FireOneShot(PlayerController& player, NextEngine& engine)
    {
        const FWeaponDef* def = ActiveWeapon();
        if (!def)
        {
            return;
        }

        const glm::vec3 origin = player.EyePosition();
        glm::vec3 dir = player.Forward();

        // Random cone spread around the view direction.
        const float spread = player.IsAiming() ? def->spreadAds : def->spreadHip;
        if (spread > 0.0f)
        {
            std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
            const glm::vec3 right = player.Right();
            const glm::vec3 up = glm::normalize(glm::cross(right, dir));
            const float ax = dist(rng_) * spread;
            const float ay = dist(rng_) * spread;
            dir = glm::normalize(dir + right * std::tan(ax) + up * std::tan(ay));
        }

        glm::vec3 hitPoint = origin + dir * kHitscanRange;
        bool hit = false;
        engine.RayCast(origin, dir, [&](Assets::RayCastResult result) {
            if (result.Hit)
            {
                hitPoint = glm::vec3(result.HitPoint);
                hit = true;
            }
            return true;
        });

        // Muzzle roughly at the view-model tip; tracer + impact marker feedback.
        const glm::vec3 muzzle = origin + player.Forward() * 0.6f + player.Right() * 0.12f -
                                 glm::normalize(glm::cross(player.Right(), player.Forward())) * 0.08f;
        if (Runtime::IDebugDraw* draw = engine.GetDebugDraw())
        {
            draw->AddLine(muzzle, hitPoint, glm::vec4(1.0f, 0.85f, 0.3f, 1.0f), 2.0f, true);
            if (hit)
            {
                draw->AddPoint(hitPoint, glm::vec4(1.0f, 0.6f, 0.15f, 1.0f), 6.0f, 40, true);
            }
        }
    }

    void WeaponSystem::Update(float deltaSeconds, PlayerController& player, Inventory& inventory, NextEngine& engine)
    {
        fireCooldown_ = std::max(0.0f, fireCooldown_ - deltaSeconds);

        if (reloading_)
        {
            reloadTimer_ -= deltaSeconds;
            if (reloadTimer_ <= 0.0f)
            {
                reloading_ = false;
                if (const FWeaponDef* def = ActiveWeapon())
                {
                    const std::string ammoId(AmmoItemId(def->ammo));
                    const int need = def->magSize - slots_[activeSlot_].ammoInMag;
                    const int got = inventory.Consume(ammoId, std::max(0, need));
                    slots_[activeSlot_].ammoInMag += got;
                }
            }
        }

        const FWeaponDef* def = ActiveWeapon();

        // Feed ADS target FOV to the camera controller.
        player.SetAimFov(def ? def->adsFov : player.CurrentFov());

        firedThisFrame_ = false;
        if (def && !reloading_ && fireCooldown_ <= 0.0f && triggerDown_)
        {
            const bool canPull = def->fullAuto || !triggerConsumed_;
            if (canPull)
            {
                if (slots_[activeSlot_].ammoInMag > 0)
                {
                    FireOneShot(player, engine);
                    slots_[activeSlot_].ammoInMag -= 1;
                    fireCooldown_ = def->fireInterval;
                    firedThisFrame_ = true;
                }
                triggerConsumed_ = true; // empty click also consumes for semi-auto
            }
        }
        if (!triggerDown_)
        {
            triggerConsumed_ = false;
        }

        // View-model follows the camera; lerp offset toward hip/ADS pose.
        if (viewModelNode_)
        {
            const glm::vec3 targetOffset = player.IsAiming() ? config_.ViewModelAdsOffset : config_.ViewModelHipOffset;
            const float t = 1.0f - std::exp(-config_.ViewModelLerpSpeed * std::max(0.0f, deltaSeconds));
            viewModelOffset_ = glm::mix(viewModelOffset_, targetOffset, t);

            const bool show = HasActiveWeapon() && player.IsFirstPerson();
            if (auto render = viewModelNode_->GetComponent<Runtime::RenderComponent>())
            {
                render->SetVisible(show);
            }
            if (show)
            {
                const glm::vec3 eye = player.EyePosition();
                const glm::vec3 forward = player.Forward();
                const glm::vec3 right = player.Right();
                const glm::vec3 up = glm::normalize(glm::cross(right, forward));
                // Proper (det +1) rotation basis: object +Z -> forward, +Y -> up,
                // +X = up x forward (right() is left-handed w.r.t. forward/up).
                const glm::vec3 xAxis = glm::normalize(glm::cross(up, forward));
                const glm::vec3 pos =
                    eye + right * viewModelOffset_.x + up * viewModelOffset_.y + forward * viewModelOffset_.z;
                viewModelNode_->Translation() = pos;
                viewModelNode_->Rotation() = glm::quat_cast(glm::mat3(xAxis, up, forward));
                viewModelNode_->RecalcTransform(true);
            }
            else
            {
                viewModelNode_->Translation() = glm::vec3(0.0f, -1000.0f, 0.0f);
                viewModelNode_->RecalcTransform(true);
            }
            engine.GetScene().MarkTransformDirty();
        }
    }
}
