#include "WeaponSystem.hpp"

#include <algorithm>
#include <cmath>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/constants.hpp>
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
        constexpr float kHitscanRange = 400.0f;
        const std::string kEmptyId;
    }

    void WeaponSystem::SetViewModelAssets(const std::array<uint32_t, kWeapons.size()>& modelIds,
                                          const std::array<uint32_t, kWeapons.size()>& materialIds)
    {
        viewModelModelIds_ = modelIds;
        viewModelMaterialIds_ = materialIds;
        viewModelAssetsSet_ = true;
    }

    void WeaponSystem::OnSceneLoaded(NextEngine& engine)
    {
        slots_ = {};
        activeSlot_ = 0;
        previousSlot_ = 1;
        reloading_ = false;
        switching_ = false;
        shotSequence_ = 0;
        viewModelOffset_ = config_.ViewModelHipOffset;
        viewModelRecoil_ = 0.0f;
        viewModelRecoilVelocity_ = 0.0f;
        viewModelVisible_ = false;
        viewModelInScene_ = false;
        viewModelWeaponIndex_ = -1;
        shotEvents_.clear();
        hitEvents_.clear();
        if (!viewModelAssetsSet_)
        {
            return;
        }
        Assets::Scene& scene = engine.GetScene();
        // rayCastVisible = false so the player's own shots never hit the view model.
        viewModelNode_ = Assets::SceneBuilder::CreateRenderNode(
            "nd_viewmodel", glm::vec3(0.0f, -1000.0f, 0.0f), glm::vec3(1.0f), scene.GenerateInstanceId(),
            viewModelModelIds_[0], viewModelMaterialIds_[0], false,
            glm::quat(1.0f, 0.0f, 0.0f, 0.0f), false);
        auto physics = std::make_shared<Runtime::PhysicsComponent>();
        physics->SetMobility(Runtime::ENodeMobility::Dynamic);
        viewModelNode_->AddComponent(physics);
        scene.AddNode(viewModelNode_);
        viewModelInScene_ = true;
        scene.MarkDirty();
    }

    void WeaponSystem::OnSceneUnloaded()
    {
        viewModelNode_.reset();
        viewModelVisible_ = false;
        viewModelInScene_ = false;
        viewModelWeaponIndex_ = -1;
        shotEvents_.clear();
        hitEvents_.clear();
    }

    void WeaponSystem::ResetRuntime()
    {
        slots_ = {};
        activeSlot_ = 0;
        previousSlot_ = 1;
        triggerDown_ = false;
        presentationSuppressed_ = false;
        triggerConsumed_ = false;
        fireCooldown_ = 0.0f;
        reloading_ = false;
        reloadTimer_ = 0.0f;
        switching_ = false;
        switchCommitted_ = false;
        switchTargetSlot_ = -1;
        switchTimer_ = 0.0f;
        shotSequence_ = 0;
        shotEvents_.clear();
        hitEvents_.clear();
        RefreshViewModelVisibility();
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
        switching_ = false;
        switchCommitted_ = false;
        switchTargetSlot_ = -1;
        switchTimer_ = 0.0f;
        RefreshViewModelVisibility();
        return true;
    }

    void WeaponSystem::SwitchSlot(int slot)
    {
        if (slot < 0 || slot >= static_cast<int>(slots_.size()) || slot == activeSlot_ || switching_)
        {
            return;
        }
        reloading_ = false;
        reloadTimer_ = 0.0f;
        switching_ = true;
        switchCommitted_ = false;
        switchTargetSlot_ = slot;
        switchTimer_ = std::max(config_.SwitchSeconds, 0.01f);
    }

    void WeaponSystem::SwitchPrevious()
    {
        SwitchSlot(previousSlot_);
    }

    void WeaponSystem::RequestReload(Inventory& inventory)
    {
        const FWeaponDef* def = ActiveWeapon();
        if (!def || reloading_ || switching_)
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
        reloadTimer_ = std::max(config_.ReloadSeconds, 0.01f);
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

    std::vector<FShotEvent> WeaponSystem::ConsumeShotEvents()
    {
        std::vector<FShotEvent> events;
        events.swap(shotEvents_);
        return events;
    }

    std::vector<FWeaponHitEvent> WeaponSystem::ConsumeHitEvents()
    {
        std::vector<FWeaponHitEvent> events;
        events.swap(hitEvents_);
        return events;
    }

    bool WeaponSystem::ViewModelRecoilActive() const
    {
        return std::abs(viewModelRecoil_) > 0.0001f || std::abs(viewModelRecoilVelocity_) > 0.001f;
    }

    EWeaponPresentationAction WeaponSystem::PresentationAction() const
    {
        if (switching_)
        {
            return EWeaponPresentationAction::Switch;
        }
        if (reloading_)
        {
            return EWeaponPresentationAction::Reload;
        }
        return EWeaponPresentationAction::None;
    }

    float WeaponSystem::PresentationActionTime() const
    {
        if (switching_)
        {
            return glm::clamp(1.0f - switchTimer_ / std::max(config_.SwitchSeconds, 0.01f), 0.0f, 1.0f);
        }
        if (reloading_)
        {
            return glm::clamp(1.0f - reloadTimer_ / std::max(config_.ReloadSeconds, 0.01f), 0.0f, 1.0f);
        }
        return 0.0f;
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

        const glm::vec3 cameraOrigin = player.CameraPosition();
        const glm::vec3 cameraDirection = player.Forward();

        // Resolve the point under the centre-screen crosshair from the real
        // render-camera origin. In TPS this is the offset shoulder camera, not
        // the character eye position.
        glm::vec3 aimPoint = cameraOrigin + cameraDirection * kHitscanRange;
        engine.RayCast(cameraOrigin, cameraDirection, [&](Assets::RayCastResult result) {
            if (result.Hit && result.T <= kHitscanRange)
            {
                aimPoint = glm::vec3(result.HitPoint);
            }
            return true;
        });

        const glm::vec3 playerUp =
            glm::normalize(glm::cross(player.Right(), cameraDirection));
        const glm::vec3 muzzle = player.EyePosition() + cameraDirection * 0.6f +
                                 player.Right() * 0.12f - playerUp * 0.08f;
        glm::vec3 dir = glm::normalize(aimPoint - muzzle);

        // Random cone spread around the muzzle-to-crosshair direction.
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

        const uint64_t sequence = ++shotSequence_;
        std::uniform_real_distribution<float> pelletSpread(-1.0f, 1.0f);
        for (int pellet = 0; pellet < std::max(def->pellets, 1); ++pellet)
        {
            glm::vec3 pelletDirection = dir;
            if (pellet > 0)
            {
                const glm::vec3 right = player.Right();
                const glm::vec3 up = glm::normalize(glm::cross(right, dir));
                pelletDirection = glm::normalize(dir + right * std::tan(pelletSpread(rng_) * spread) +
                                                 up * std::tan(pelletSpread(rng_) * spread));
            }
            glm::vec3 hitPoint = muzzle + pelletDirection * kHitscanRange;
            uint32_t hitInstanceId = 0;
            bool hit = false;
            engine.RayCast(muzzle, pelletDirection, [&](Assets::RayCastResult result) {
                if (result.Hit && result.T <= kHitscanRange)
                {
                    hitPoint = glm::vec3(result.HitPoint);
                    hitInstanceId = result.InstanceId;
                    hit = true;
                }
                return true;
            });
            if (hit)
            {
                hitEvents_.push_back({sequence, std::string(def->id), muzzle, pelletDirection, hitInstanceId,
                                      hitPoint, def->baseDamage});
            }
            if (Runtime::IDebugDraw* draw = engine.GetDebugDraw())
            {
                draw->AddLine(muzzle, hitPoint, glm::vec4(1.0f, 0.85f, 0.3f, 1.0f), 2.0f, true);
                if (hit)
                {
                    draw->AddPoint(hitPoint, glm::vec4(1.0f, 0.6f, 0.15f, 1.0f), 6.0f, 40, true);
                }
            }
        }

        std::uniform_real_distribution<float> yawDist(-def->cameraYawDegrees, def->cameraYawDegrees);
        FShotEvent event;
        event.sequence = sequence;
        event.weaponId = std::string(def->id);
        event.cameraImpulseRadians =
            glm::radians(glm::vec2(yawDist(rng_), -def->cameraKickDegrees));
        event.viewModelImpulse = def->viewModelKick;
        event.rigRecoilScale = def->rigRecoilScale;
        shotEvents_.push_back(event);
        viewModelRecoilVelocity_ += event.viewModelImpulse;
    }

    void WeaponSystem::Update(float deltaSeconds, PlayerController& player, Inventory& inventory, NextEngine& engine)
    {
        const float dt = std::max(0.0f, deltaSeconds);
        fireCooldown_ -= dt;

        if (reloading_)
        {
            reloadTimer_ -= dt;
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

        if (switching_)
        {
            switchTimer_ -= dt;
            if (!switchCommitted_ && PresentationActionTime() >= 0.5f)
            {
                previousSlot_ = activeSlot_;
                activeSlot_ = switchTargetSlot_;
                switchCommitted_ = true;
                RefreshViewModelVisibility();
            }
            if (switchTimer_ <= 0.0f)
            {
                switching_ = false;
                switchCommitted_ = false;
                switchTargetSlot_ = -1;
                switchTimer_ = 0.0f;
            }
        }

        const FWeaponDef* def = ActiveWeapon();

        // Feed ADS target FOV to the camera controller.
        player.SetAimFov(def ? def->adsFov : player.CurrentFov());

        if (def && !reloading_ && !switching_ && triggerDown_)
        {
            constexpr int kMaxShotsPerFrame = 8;
            int shotsThisFrame = 0;
            const bool newPull = !triggerConsumed_;
            if (newPull && fireCooldown_ <= 0.0f)
            {
                if (slots_[activeSlot_].ammoInMag > 0)
                {
                    FireOneShot(player, engine);
                    slots_[activeSlot_].ammoInMag -= 1;
                    fireCooldown_ = def->fireInterval;
                    ++shotsThisFrame;
                }
                triggerConsumed_ = true;
            }
            while (def->fullAuto && triggerConsumed_ && fireCooldown_ <= 0.0f &&
                   shotsThisFrame < kMaxShotsPerFrame && slots_[activeSlot_].ammoInMag > 0)
            {
                FireOneShot(player, engine);
                slots_[activeSlot_].ammoInMag -= 1;
                fireCooldown_ += def->fireInterval;
                ++shotsThisFrame;
            }
        }
        if (!triggerDown_)
        {
            triggerConsumed_ = false;
            fireCooldown_ = std::max(0.0f, fireCooldown_);
        }

        // View-model follows the camera; lerp offset toward hip/ADS pose.
        if (viewModelNode_)
        {
            const int weaponIndex = WeaponIndex(ActiveWeaponId());
            if (weaponIndex >= 0 && weaponIndex != viewModelWeaponIndex_)
            {
                viewModelWeaponIndex_ = weaponIndex;
                if (auto render = viewModelNode_->GetComponent<Runtime::RenderComponent>())
                {
                    render->SetModelId(viewModelModelIds_[weaponIndex]);
                    std::array<uint32_t, 16> materials = {0};
                    materials[0] = viewModelMaterialIds_[weaponIndex];
                    render->SetMaterials(materials);
                }
                engine.GetScene().MarkDirty();
            }

            const float recoilDt = std::max(0.0f, deltaSeconds);
            viewModelRecoilVelocity_ +=
                (-config_.ViewModelRecoilSpring * viewModelRecoil_ -
                 config_.ViewModelRecoilDamping * viewModelRecoilVelocity_) *
                recoilDt;
            viewModelRecoil_ += viewModelRecoilVelocity_ * recoilDt;

            glm::vec3 targetOffset = player.IsAiming() ? config_.ViewModelAdsOffset : config_.ViewModelHipOffset;
            const float actionTime = PresentationActionTime();
            const float actionArc = std::sin(glm::pi<float>() * actionTime);
            if (reloading_)
            {
                targetOffset.y -= actionArc * 0.18f;
                targetOffset.x += actionArc * 0.08f;
            }
            else if (switching_)
            {
                targetOffset.y -= actionArc * 0.42f;
                targetOffset.z -= actionArc * 0.18f;
            }
            const float t = 1.0f - std::exp(-config_.ViewModelLerpSpeed * std::max(0.0f, deltaSeconds));
            viewModelOffset_ = glm::mix(viewModelOffset_, targetOffset, t);

            const bool show = HasActiveWeapon() && player.IsFirstPerson() && !presentationSuppressed_;
            if (show != viewModelVisible_)
            {
                viewModelVisible_ = show;
                if (auto render = viewModelNode_->GetComponent<Runtime::RenderComponent>())
                {
                    render->SetVisible(show);
                }
                viewModelNode_->Scale() =
                    show ? glm::vec3(config_.ViewModelScale) : glm::vec3(0.0f);
                if (!show && viewModelInScene_)
                {
                    engine.GetScene().RemoveNodeByInstanceId(viewModelNode_->GetInstanceId());
                    viewModelInScene_ = false;
                }
                else if (show && !viewModelInScene_)
                {
                    engine.GetScene().AddNode(viewModelNode_);
                    viewModelInScene_ = true;
                }
                engine.GetScene().MarkDirty();
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
                    eye + right * viewModelOffset_.x + up * viewModelOffset_.y +
                    forward * (viewModelOffset_.z - viewModelRecoil_);
                viewModelNode_->Translation() = pos;
                viewModelNode_->Rotation() =
                    glm::quat_cast(glm::mat3(xAxis, up, forward)) *
                    glm::angleAxis(reloading_ ? actionArc * 0.45f : 0.0f, glm::vec3(0.0f, 0.0f, 1.0f)) *
                    glm::angleAxis(-viewModelRecoil_ * 0.8f, glm::vec3(1.0f, 0.0f, 0.0f));
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
