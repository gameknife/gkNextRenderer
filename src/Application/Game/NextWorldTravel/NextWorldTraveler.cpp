#include "NextWorldTraveler.h"

#include "NextWorldTravelConfig.hpp"

#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Runtime/Components/TerrainComponent.hpp"
#include "Engine/Runtime/Engine.hpp"

#include <algorithm>
#include <cmath>

namespace NextWorldTravel
{
    namespace
    {
        // A nav cell counts as street level when its sampled ground is within
        // this of the terrain surface. Roads are laid ~0.35 m proud of the
        // heightfield and pavements a little more, so the band has to clear
        // those while still rejecting the first floor of anything.
        constexpr float kStreetLevelBand = 2.0f;
        // A spawn whose reachable component is smaller than this is an island —
        // a courtyard, a roof, a strip of pavement between two walls. Walking
        // there would look broken even though every cell is legitimately
        // walkable.
        constexpr int kMinReachableCells = 900;
        // A component this size is a street network rather than a yard, so the
        // search can stop looking.
        constexpr int kGoodReachableCells = 20000;
        // Each candidate costs one flood fill over the window, so the search is
        // capped rather than exhaustive.
        constexpr int kSpawnCandidateBudget = 40;
        constexpr float kSpawnSearchStep = 12.0f;
    }

    void FNextWorldTraveler::ConfigurePool()
    {
        NextGameplay::Sim::FCharacterPoolConfig config;
        config.poolCapacity = 1;
        config.navCellSize = Config::kNavCellSize;
        config.agentRadius = Config::kAgentRadius;
        config.separationRadius = 0.0f;
        config.separationStrength = 0.0f;
        config.useRig = true;
        config.rigPath = Config::kRigPath;
        config.rigVisual.baseWalkSpeed = Config::kWalkSpeed;
        config.rigVisual.sizeJitterRange = 0.0f;
        config.nodeNamePrefix = "next_world_traveler";
        config.slotTints = {Config::kCharacterTint};
        config.navMaxStepHeight = Config::kNavMaxStepHeight;
        config.navClearanceHeight = Config::kNavClearanceHeight;
        config.navMaxSlopeAngle = Config::kNavMaxSlopeAngle;
        config.navSampleCeiling = Config::kNavSampleCeiling;
        // The first window is always centred on the tile origin; setting it here
        // means the pool's own OnSceneLoaded builds the right grid once instead
        // of building the whole-scene default and then being corrected.
        const float half = Config::kNavWindowHalfSize;
        config.navWorldMin = glm::vec3(-half, 0.0f, -half);
        config.navWorldMax = glm::vec3(half, 0.0f, half);
        config.groundSampler = [this](float x, float z, float currentY)
        {
            return GroundHeight(x, z, currentY);
        };
        pool_.Configure(config);
    }

    void FNextWorldTraveler::InjectAssets(std::vector<Assets::Model>& models,
                                  std::vector<Assets::FMaterial>& materials)
    {
        ConfigurePool();
        pool_.InjectAssets(models, materials);
    }

    float FNextWorldTraveler::GroundHeight(float x, float z, float fallbackY) const
    {
        float navGround = 0.0f;
        if (pool_.NavReady() &&
            pool_.NavGrid().SampleGroundHeight(glm::vec3(x, fallbackY, z), navGround))
        {
            return navGround;
        }
        if (terrain_ != nullptr && terrain_->HasData())
        {
            return terrain_->SampleHeight(x, z);
        }
        return fallbackY;
    }

    float FNextWorldTraveler::FloorToleranceFor(const glm::vec2& center, float halfSize) const
    {
        if (terrain_ == nullptr || !terrain_->HasData())
        {
            return Config::kNavFloorToleranceSlack;
        }
        // floorHeightTolerance is an absolute band around the query's reference
        // height and does not propagate along a path, so it has to cover the
        // whole window's relief or walking uphill reads as changing storey.
        float lowest = std::numeric_limits<float>::max();
        float highest = std::numeric_limits<float>::lowest();
        constexpr int kSamples = 9;
        for (int iz = 0; iz < kSamples; ++iz)
        {
            for (int ix = 0; ix < kSamples; ++ix)
            {
                const float u = static_cast<float>(ix) / (kSamples - 1) * 2.0f - 1.0f;
                const float v = static_cast<float>(iz) / (kSamples - 1) * 2.0f - 1.0f;
                const float h = terrain_->SampleHeight(center.x + u * halfSize, center.y + v * halfSize);
                lowest = std::min(lowest, h);
                highest = std::max(highest, h);
            }
        }
        return (highest - lowest) + Config::kNavFloorToleranceSlack;
    }

    bool FNextWorldTraveler::FindNearestDrySearchCenter(const glm::vec2& searchCenter,
                                                        glm::vec2& outCenter) const
    {
        if (terrain_ == nullptr || !terrain_->HasData())
        {
            return false;
        }

        const float halfX = terrain_->GetSizeX() * 0.5f;
        const float halfZ = terrain_->GetSizeY() * 0.5f;
        if (halfX <= 0.0f || halfZ <= 0.0f)
        {
            return false;
        }

        float bestDistance = std::numeric_limits<float>::max();
        bool found = false;
        for (float z = -halfZ + kSpawnSearchStep; z < halfZ; z += kSpawnSearchStep)
        {
            for (float x = -halfX + kSpawnSearchStep; x < halfX; x += kSpawnSearchStep)
            {
                if (terrain_->IsWater(x, z))
                {
                    continue;
                }

                const float distance = glm::distance(glm::vec2(x, z), searchCenter);
                if (distance < bestDistance)
                {
                    bestDistance = distance;
                    outCenter = {x, z};
                    found = true;
                }
            }
        }
        return found;
    }

    void FNextWorldTraveler::RebuildNavWindow(Assets::Scene& scene, const glm::vec2& center)
    {
        const float half = Config::kNavWindowHalfSize;
        navMin_ = glm::vec3(center.x - half, 0.0f, center.y - half);
        navMax_ = glm::vec3(center.x + half, 0.0f, center.y + half);
        pool_.SetNavFloorTolerance(FloorToleranceFor(center, half));
        pool_.RebuildNavGrid(scene, navMin_, navMax_);
        ++navRebuilds_;
    }

    bool FNextWorldTraveler::IsStreetLevel(float x, float z, float navGroundY) const
    {
        if (terrain_ == nullptr || !terrain_->HasData())
        {
            return true;
        }
        if (terrain_->IsWater(x, z))
        {
            return false;
        }
        return std::abs(navGroundY - terrain_->SampleHeight(x, z)) <= kStreetLevelBand;
    }

    bool FNextWorldTraveler::FindStreetSpawn(const glm::vec2& searchCenter, glm::vec3& outPosition) const
    {
        if (!pool_.NavReady())
        {
            return false;
        }
        const NextGameplay::FNavGrid& nav = pool_.NavGrid();

        // Walk outward from the search centre so the character starts near the
        // middle of the tile rather than wherever the grid scan happens to hit
        // a walkable cell first.
        struct FCandidate
        {
            glm::vec3 position;
            float distance;
        };
        std::vector<FCandidate> candidates;
        const int steps = static_cast<int>(Config::kNavWindowHalfSize / kSpawnSearchStep);
        for (int iz = -steps; iz <= steps; ++iz)
        {
            for (int ix = -steps; ix <= steps; ++ix)
            {
                const float x = searchCenter.x + static_cast<float>(ix) * kSpawnSearchStep;
                const float z = searchCenter.y + static_cast<float>(iz) * kSpawnSearchStep;
                float groundY = 0.0f;
                if (!nav.SampleGroundHeight(glm::vec3(x, 0.0f, z), groundY))
                {
                    continue;
                }
                if (!nav.IsWalkable(glm::vec3(x, groundY, z)) || !IsStreetLevel(x, z, groundY))
                {
                    continue;
                }
                candidates.push_back({glm::vec3(x, groundY, z),
                                      glm::distance(glm::vec2(x, z), searchCenter)});
            }
        }
        std::sort(candidates.begin(), candidates.end(),
                  [](const FCandidate& a, const FCandidate& b) { return a.distance < b.distance; });

        // Reachability, not walkability: in a downtown most walkable cells are
        // flat roofs, and a legitimately walkable pocket can still be a
        // courtyard nothing connects to. The flood fill runs on the grid that is
        // already built, so evaluating a batch of candidates is cheap.
        //
        // The best component wins rather than the first adequate one: a
        // courtyard next to the tile centre would otherwise beat the street
        // network twenty metres further out, and the character would spend the
        // session pacing a car park.
        glm::vec3 best{0.0f};
        int bestReachable = 0;
        const int budget = std::min(static_cast<int>(candidates.size()), kSpawnCandidateBudget);
        for (int i = 0; i < budget; ++i)
        {
            const FCandidate& candidate = candidates[static_cast<size_t>(i)];
            const std::vector<uint8_t> mask = nav.BuildReachabilityMask(candidate.position,
                                                                        candidate.position.y);
            if (mask.empty())
            {
                continue;
            }
            int reachable = 0;
            for (const uint8_t cell : mask)
            {
                reachable += cell != 0 ? 1 : 0;
            }
            if (reachable > bestReachable)
            {
                bestReachable = reachable;
                best = candidate.position;
            }
            if (bestReachable >= kGoodReachableCells)
            {
                break; // already a proper street network; stop paying for more
            }
        }

        if (bestReachable < kMinReachableCells)
        {
            SPDLOG_WARN("NextWorldTravel: best spawn candidate reaches only {} nav cells (want {})",
                        bestReachable, kMinReachableCells);
            return false;
        }
        outPosition = best;
        SPDLOG_INFO("NextWorldTravel: spawn at ({:.1f}, {:.1f}, {:.1f}), {} reachable nav cells "
                    "out of {} candidates",
                    outPosition.x, outPosition.y, outPosition.z, bestReachable, budget);
        return true;
    }

    bool FNextWorldTraveler::OnSceneLoaded(Assets::Scene& scene, NextEngine& engine,
                                   const FGeoTerrainSet* terrain)
    {
        engine_ = &engine;
        scene_ = &scene;
        terrain_ = terrain;
        character_ = nullptr;
        hasRoamTarget_ = false;
        roamElapsed_ = 0.0f;
        roamPause_ = 0.0f;
        mode_ = EWalkMode::Roam;

        const float half = Config::kNavWindowHalfSize;
        glm::vec2 searchCenter(0.0f);
        if (terrain_ != nullptr && terrain_->HasData() && terrain_->IsWater(0.0f, 0.0f))
        {
            glm::vec2 dryCenter(0.0f);
            if (FindNearestDrySearchCenter(searchCenter, dryCenter))
            {
                searchCenter = dryCenter;
                SPDLOG_INFO("NextWorldTravel: tile origin is water; moving initial nav window to ({:.1f}, {:.1f})",
                            searchCenter.x, searchCenter.y);
            }
            else
            {
                SPDLOG_WARN("NextWorldTravel: tile has no dry terrain sample for the initial nav window");
            }
        }

        navMin_ = glm::vec3(searchCenter.x - half, 0.0f, searchCenter.y - half);
        navMax_ = glm::vec3(searchCenter.x + half, 0.0f, searchCenter.y + half);
        pool_.SetNavWorldBounds(navMin_, navMax_);
        pool_.SetNavFloorTolerance(FloorToleranceFor(searchCenter, half));
        pool_.OnSceneLoaded(scene); // builds the first nav window
        navRebuilds_ = 1;
        if (!pool_.NavReady())
        {
            status_ = "nav grid failed to build";
            SPDLOG_ERROR("NextWorldTravel: nav grid did not build — is KeepCPUMeshData enabled?");
            return false;
        }

        glm::vec3 spawn{0.0f};
        if (!FindStreetSpawn(searchCenter, spawn))
        {
            status_ = "no reachable street found in this tile";
            SPDLOG_ERROR("NextWorldTravel: no reachable street-level ground within {} m of the tile centre",
                         Config::kNavWindowHalfSize);
            return false;
        }

        character_ = pool_.Acquire(0, spawn, Config::kCharacterTint);
        if (character_ == nullptr)
        {
            status_ = "character pool refused the slot";
            return false;
        }
        character_->speed = Config::kWalkSpeed;
        lastPosition_ = spawn;
        status_ = "roaming";
        return true;
    }

    void FNextWorldTraveler::OnSceneUnloaded()
    {
        if (playerController_.IsValid())
        {
            playerController_.Destroy();
        }
        character_ = nullptr;
        pool_.Clear();
        terrain_ = nullptr;
        scene_ = nullptr;
        engine_ = nullptr;
        hasRoamTarget_ = false;
        status_ = "not spawned";
    }

    glm::vec3 FNextWorldTraveler::Position() const
    {
        return character_ != nullptr ? character_->position : lastPosition_;
    }

    float FNextWorldTraveler::Yaw() const { return character_ != nullptr ? character_->yaw : 0.0f; }

    bool FNextWorldTraveler::IsMoving() const { return lastSpeed_ > 0.05f; }

    void FNextWorldTraveler::SetMode(EWalkMode mode)
    {
        if (mode == mode_ || character_ == nullptr)
        {
            return;
        }
        mode_ = mode;
        if (mode_ == EWalkMode::Player)
        {
            character_->moving = false;
            character_->follower.Clear();
            hasRoamTarget_ = false;
            NextPhysics* physics = engine_ != nullptr ? engine_->GetPhysicsEngine() : nullptr;
            if (physics == nullptr)
            {
                status_ = "no physics engine — staying in roam mode";
                mode_ = EWalkMode::Roam;
                return;
            }
            FCharacterControllerSettings settings;
            settings.height = Config::kCharacterHeight;
            settings.radius = Config::kAgentRadius;
            settings.maxSlopeAngle = Config::kNavMaxSlopeAngle;
            settings.maxStepHeight = Config::kNavMaxStepHeight;
            // Lifted slightly: the capsule spawns against the collision mesh,
            // and the nav grid's ground sample is the mesh surface itself.
            settings.initialPosition = character_->position + glm::vec3(0.0f, 0.1f, 0.0f);
            playerController_.Create(physics, settings);
            status_ = "player control";
        }
        else
        {
            if (playerController_.IsValid())
            {
                character_->position = playerController_.GetPosition();
                playerController_.Destroy();
            }
            playerIntent_ = glm::vec3(0.0f);
            status_ = "roaming";
        }
    }

    void FNextWorldTraveler::SetPlayerIntent(const glm::vec3& moveDirection, bool sprint, bool jump)
    {
        playerIntent_ = moveDirection;
        playerSprint_ = sprint;
        playerJump_ = playerJump_ || jump;
    }

    bool FNextWorldTraveler::WalkTo(const glm::vec3& worldTarget)
    {
        if (character_ == nullptr)
        {
            return false;
        }
        SetMode(EWalkMode::Roam);
        glm::vec3 target = worldTarget;
        target.y = GroundHeight(target.x, target.z, character_->position.y);
        const bool found = pool_.MoveTo(*character_, target);
        roamTarget_ = target;
        hasRoamTarget_ = true;
        roamElapsed_ = 0.0f;
        roamPause_ = 0.0f;
        status_ = found ? "walking to destination" : "no route — walking straight at it";
        return found;
    }

    bool FNextWorldTraveler::PickRoamTarget(glm::vec3& outTarget)
    {
        if (!pool_.NavReady() || character_ == nullptr)
        {
            return false;
        }
        const NextGameplay::FNavGrid& nav = pool_.NavGrid();
        std::uniform_real_distribution<float> angleDist(0.0f, 6.2831853f);
        std::uniform_real_distribution<float> radiusDist(Config::kRoamMinDistance,
                                                         Config::kRoamMaxDistance);
        const glm::vec3 from = character_->position;

        for (int attempt = 0; attempt < Config::kRoamTargetAttempts; ++attempt)
        {
            const float angle = angleDist(rng_);
            const float radius = radiusDist(rng_);
            const float x = from.x + std::cos(angle) * radius;
            const float z = from.z + std::sin(angle) * radius;
            // Keep the destination inside the current window with room to
            // spare, so the route does not immediately leave the grid.
            if (x < navMin_.x + 6.0f || x > navMax_.x - 6.0f ||
                z < navMin_.z + 6.0f || z > navMax_.z - 6.0f)
            {
                continue;
            }
            float groundY = 0.0f;
            if (!nav.SampleGroundHeight(glm::vec3(x, 0.0f, z), groundY))
            {
                continue;
            }
            if (!nav.IsWalkable(glm::vec3(x, groundY, z)) || !IsStreetLevel(x, z, groundY))
            {
                continue;
            }
            const glm::vec3 target(x, groundY, z);
            // A path that the search actually found; MoveTo falls back to a
            // straight line otherwise, which walks through walls.
            if (nav.FindPath(from, target, from.y).empty())
            {
                continue;
            }
            outTarget = target;
            return true;
        }
        return false;
    }

    void FNextWorldTraveler::TickRoam(float deltaSeconds, Assets::Scene& scene)
    {
        (void)scene;
        if (character_ == nullptr)
        {
            return;
        }
        if (roamPause_ > 0.0f)
        {
            roamPause_ -= deltaSeconds;
            return;
        }

        if (hasRoamTarget_)
        {
            roamElapsed_ += deltaSeconds;
            const bool arrived = pool_.Arrived(*character_);
            if (!arrived && roamElapsed_ < Config::kRoamTimeoutSeconds)
            {
                return;
            }
            hasRoamTarget_ = false;
            character_->moving = false;
            character_->follower.Clear();
            roamPause_ = Config::kRoamPauseSeconds;
            status_ = arrived ? "arrived — picking a new destination" : "route timed out — retrying";
            return;
        }

        glm::vec3 target{0.0f};
        if (!PickRoamTarget(target))
        {
            // Nothing reachable in range; wait a beat rather than spinning on
            // the search every frame.
            roamPause_ = Config::kRoamPauseSeconds;
            status_ = "no reachable destination nearby";
            return;
        }
        pool_.MoveTo(*character_, target);
        roamTarget_ = target;
        hasRoamTarget_ = true;
        roamElapsed_ = 0.0f;
        status_ = "roaming";
    }

    void FNextWorldTraveler::TickPlayer(float deltaSeconds, Assets::Scene& scene)
    {
        (void)scene;
        if (character_ == nullptr || !playerController_.IsValid())
        {
            return;
        }
        const float speed = playerSprint_ ? Config::kRunSpeed : Config::kWalkSpeed;
        playerController_.Update(playerIntent_, speed, playerJump_, deltaSeconds);
        playerJump_ = false;

        character_->position = playerController_.GetPosition();
        const glm::vec3 velocity = playerController_.GetLinearVelocity();
        const glm::vec2 horizontal(velocity.x, velocity.z);
        if (glm::length(horizontal) > 0.15f)
        {
            character_->yaw = std::atan2(horizontal.x, horizontal.y);
        }
        character_->anim = glm::length(horizontal) > 0.15f
                               ? NextGameplay::Sim::EAnimHint::Walk
                               : NextGameplay::Sim::EAnimHint::Idle;
        character_->speed = speed;
    }

    void FNextWorldTraveler::MaybeSlideNavWindow(Assets::Scene& scene)
    {
        if (character_ == nullptr)
        {
            return;
        }
        const glm::vec3 position = character_->position;
        const float margin = Config::kNavRebuildMargin;
        const bool nearEdge = position.x - navMin_.x < margin || navMax_.x - position.x < margin ||
                              position.z - navMin_.z < margin || navMax_.z - position.z < margin;
        if (!nearEdge)
        {
            return;
        }
        // Re-centring drops the old grid: any route in flight is on cells that
        // no longer exist, so the destination is abandoned and a new one picked
        // on the new window.
        RebuildNavWindow(scene, glm::vec2(position.x, position.z));
        character_->moving = false;
        character_->follower.Clear();
        hasRoamTarget_ = false;
        roamPause_ = 0.2f;
    }

    void FNextWorldTraveler::ApplyVisual(float deltaSeconds, Assets::Scene& scene)
    {
        if (character_ == nullptr || !character_->visual)
        {
            return;
        }
        character_->visual->SetMoveSpeed(character_->speed);
        character_->visual->SetAnimHint(character_->anim);
        character_->visual->SetWorldTransform(character_->position, character_->yaw);
        character_->visual->Tick(deltaSeconds);
        scene.MarkTransformDirty();
    }

    void FNextWorldTraveler::Tick(float deltaSeconds, Assets::Scene& scene)
    {
        if (character_ == nullptr || deltaSeconds <= 0.0f)
        {
            return;
        }
        if (paused_)
        {
            // The simulation state is left exactly as it was — speed included,
            // because the pool integrates movement from it — and only the rig
            // is told to stand still, so the character is not frozen mid-stride
            // while a camera orbits past it.
            lastSpeed_ = 0.0f;
            if (character_->visual)
            {
                character_->visual->SetMoveSpeed(0.0f);
                character_->visual->SetAnimHint(NextGameplay::Sim::EAnimHint::Idle);
                character_->visual->Tick(deltaSeconds);
                scene.MarkTransformDirty();
            }
            return;
        }
        const glm::vec3 previous = character_->position;

        if (mode_ == EWalkMode::Roam)
        {
            TickRoam(deltaSeconds, scene);
            // The pool integrates movement, snaps to the sampled ground and
            // drives the rig; player mode does the same work by hand because
            // the physics controller owns the position instead.
            NextGameplay::Sim::FSimCharacter* characters[] = {character_};
            pool_.Tick(deltaSeconds, scene, std::span<NextGameplay::Sim::FSimCharacter*>(characters, 1));
        }
        else
        {
            TickPlayer(deltaSeconds, scene);
            ApplyVisual(deltaSeconds, scene);
        }

        lastSpeed_ = glm::length(glm::vec2(character_->position.x - previous.x,
                                           character_->position.z - previous.z)) / deltaSeconds;
        lastPosition_ = character_->position;
        MaybeSlideNavWindow(scene);
    }
}
