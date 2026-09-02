#include "Application/Game/NextAstrobot/Mechanisms/MechanismSystem.hpp"

#include <algorithm>
#include <cmath>

#include <fmt/format.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Runtime/Scene/NodeUtils.hpp"
#include "Engine/Runtime/Subsystems/NextPhysics.hpp"

#include "Application/Game/NextAstrobot/Mechanisms/MechanismCurves.hpp"

namespace NextAstrobot
{
    namespace
    {
        // SCAD is Z-up, the engine is Y-up: world = (x, z, -y). A SCAD rotation about
        // its own Y axis (pendulum swing, seesaw tilt) is therefore an engine rotation
        // about -Z, and a SCAD rotation about Z (spin disc) is one about engine +Y.
        glm::quat ScadRotY(float degrees)
        {
            return glm::angleAxis(glm::radians(degrees), glm::vec3(0.0f, 0.0f, -1.0f));
        }
        glm::quat ScadRotX(float degrees)
        {
            return glm::angleAxis(glm::radians(degrees), glm::vec3(1.0f, 0.0f, 0.0f));
        }
        glm::quat ScadRotZ(float degrees)
        {
            return glm::angleAxis(glm::radians(degrees), glm::vec3(0.0f, 1.0f, 0.0f));
        }

        bool InBox2D(const glm::vec3& local, float halfX, float halfZ)
        {
            return std::abs(local.x) <= halfX && std::abs(local.z) <= halfZ;
        }

        bool InDisc2D(const glm::vec3& local, float radius)
        {
            return local.x * local.x + local.z * local.z <= radius * radius;
        }

        /// A capsule-free cylinder test used by the fan draught and the fountain column:
        /// `local` is already in the module's frame, `axis` its centre line from the
        /// origin, `length` how far the stream reaches and `radius` how wide it is.
        bool InCylinder(const glm::vec3& local, const glm::vec3& axis, float length, float radius)
        {
            const float along = glm::dot(local, axis);
            if (along < 0.0f || along > length)
            {
                return false;
            }
            const glm::vec3 offset = local - axis * along;
            return glm::dot(offset, offset) <= radius * radius;
        }
    }

    glm::vec3 FMechanismSystem::ToLocal(const FMechanismRecord& record, const glm::vec3& world) const
    {
        return glm::inverse(record.root.worldRot) * (world - record.root.worldPos);
    }

    void FMechanismSystem::Unbind()
    {
        if (physics_)
        {
            for (FRuntime& runtime : runtime_)
            {
                if (!runtime.body.IsInvalid())
                {
                    physics_->RemoveBody(runtime.body);
                }
            }
        }
        runtime_.clear();
        scene_ = nullptr;
        physics_ = nullptr;
        effects_ = FMechanismEffects{};
    }

    void FMechanismSystem::Bind(Assets::Scene& scene, NextPhysics* physics, const FLevelIndex& index)
    {
        Unbind();
        scene_ = &scene;
        physics_ = physics;

        runtime_.reserve(index.mechanisms.size());
        int gateCounter = 0;
        int movingCounter = 0;
        int chestCounter = 0;
        for (const FMechanismRecord& record : index.mechanisms)
        {
            FRuntime runtime;
            runtime.record = record;
            if (record.root.node)
            {
                runtime.rootBindRotation = record.root.node->Rotation();
            }
            if (record.part)
            {
                runtime.partBindScale = record.part->Scale();
            }
            switch (record.kind)
            {
            case EMechanismKind::Gate:
                runtime.queryName = fmt::format("gate_{}", static_cast<int>(record.root.Number("idx", ++gateCounter)));
                break;
            case EMechanismKind::Button:
                runtime.queryName = fmt::format("button_{}", static_cast<int>(record.root.Number("idx", 0.0)));
                break;
            case EMechanismKind::Lever:
                runtime.queryName = fmt::format("lever_{}", static_cast<int>(record.root.Number("idx", 0.0)));
                break;
            case EMechanismKind::CheckpointFlag:
                runtime.queryName = fmt::format("flag_{}", static_cast<int>(record.root.Number("idx", 0.0)));
                break;
            case EMechanismKind::ChestLid:
                runtime.queryName = chestCounter == 0 ? "chest" : fmt::format("chest_{}", chestCounter);
                ++chestCounter;
                break;
            case EMechanismKind::MovingPlatform:
                runtime.queryName = movingCounter == 0 ? "moving" : fmt::format("moving_{}", movingCounter);
                ++movingCounter;
                break;
            default:
                runtime.queryName = MechanismKindName(record.kind);
                break;
            }
            runtime_.push_back(std::move(runtime));
        }

        CreateKinematicPieces(scene, physics);
    }

    void FMechanismSystem::CreateKinematicPieces(Assets::Scene& scene, NextPhysics* physics)
    {
        if (!physics)
        {
            return;
        }
        for (FRuntime& runtime : runtime_)
        {
            Assets::Node* part = runtime.record.part;
            if (!part)
            {
                continue;
            }
            const FIndexedNode& root = runtime.record.root;
            glm::vec3 offset(0.0f);
            glm::vec3 extent(0.0f);
            switch (runtime.record.kind)
            {
            case EMechanismKind::MovingPlatform:
            {
                // Deck occupies SCAD z 0.3..0.7 above the rail plane.
                const float length = static_cast<float>(root.Number("L", 3.0));
                const float width = static_cast<float>(root.Number("W", 2.0));
                offset = glm::vec3(0.0f, 0.5f, 0.0f);
                extent = glm::vec3(length, 0.4f, width);
                break;
            }
            case EMechanismKind::Pendulum:
            {
                const float arm = static_cast<float>(root.Number("arm", 5.0));
                const float width = static_cast<float>(root.Number("w", 2.2));
                offset = glm::vec3(0.0f, -arm, 0.0f);
                extent = glm::vec3(2.4f, 0.3f, width);
                break;
            }
            case EMechanismKind::Seesaw:
            {
                const float length = static_cast<float>(root.Number("L", 6.0));
                const float width = static_cast<float>(root.Number("W", 2.0));
                offset = glm::vec3(0.0f);
                extent = glm::vec3(length, 0.18f, width);
                break;
            }
            case EMechanismKind::Crumble:
            {
                const float length = static_cast<float>(root.Number("L", 2.0));
                const float depth = static_cast<float>(root.Number("D", 2.0));
                offset = glm::vec3(0.0f, 0.2f, 0.0f);
                extent = glm::vec3(length, 0.4f, depth);
                break;
            }
            case EMechanismKind::Gate:
            {
                const float width = static_cast<float>(root.Number("w", 4.0));
                const float height = static_cast<float>(root.Number("h", 3.0));
                offset = glm::vec3(0.0f, (height + 0.1f) * 0.5f, 0.0f);
                extent = glm::vec3(width, height + 0.1f, 0.3f);
                break;
            }
            default:
                // Spring cap, button cap, zipline car, roller drum and the cage dome carry
                // no collider of their own; their shells already provide the standing surface.
                continue;
            }

            // Boxes are 2 cm larger than the visual so the character never falls into the
            // seam between a moving piece and the static shell around it.
            extent += glm::vec3(0.02f);
            const glm::vec3 worldCentre = part->WorldTranslation() + part->WorldRotation() * offset;
            const NextBodyID body =
                physics->CreateBoxBody(worldCentre, part->WorldRotation(), extent, NextMotionType::Kinematic);
            if (body.IsInvalid())
            {
                continue;
            }
            // Takes ownership away from the implicit static mesh body created at load.
            scene.BindPhysicsBody(*part, body, Runtime::ENodeMobility::Kinematic);
            runtime.body = body;
            runtime.boxLocalOffset = offset;
            runtime.boxExtent = extent;
        }
    }

    void FMechanismSystem::ApplyPart(FRuntime& runtime, const glm::vec3& localTranslation,
                                     const glm::quat& localRotation, float deltaSeconds,
                                     const glm::vec3* localScale)
    {
        Assets::Node* part = runtime.record.part;
        if (!part)
        {
            return;
        }
        part->SetTransform(localTranslation, localRotation, localScale ? *localScale : part->Scale());
        if (!runtime.body.IsInvalid() && physics_)
        {
            const glm::quat worldRot = part->WorldRotation();
            const glm::vec3 worldCentre = part->WorldTranslation() + worldRot * runtime.boxLocalOffset;
            physics_->MoveKinematicBody(runtime.body, worldCentre, worldRot, deltaSeconds);
        }
    }

    void FMechanismSystem::TriggerGate(int index)
    {
        for (FRuntime& runtime : runtime_)
        {
            if (runtime.record.kind != EMechanismKind::Gate)
            {
                continue;
            }
            if (static_cast<int>(runtime.record.root.Number("idx", 0.0)) != index)
            {
                continue;
            }
            // A locked gate needs its key; nothing in sky_garden sets locked, but the
            // contract exists so a later level can use it.
            if (runtime.record.root.Bool("locked", false))
            {
                continue;
            }
            runtime.latched = true;
        }
    }

    bool FMechanismSystem::Latch(EMechanismKind kind, const Assets::Node* root)
    {
        for (FRuntime& runtime : runtime_)
        {
            if (runtime.record.kind != kind || runtime.record.root.node != root || runtime.latched)
            {
                continue;
            }
            runtime.latched = true;
            if (kind == EMechanismKind::Lever)
            {
                // A lever is a punch-driven button: same idx namespace, same gates.
                TriggerGate(static_cast<int>(runtime.record.root.Number("idx", 0.0)));
            }
            return true;
        }
        return false;
    }

    void FMechanismSystem::SetZipProgress(float progress01)
    {
        for (FRuntime& runtime : runtime_)
        {
            if (runtime.record.kind == EMechanismKind::Zipline)
            {
                runtime.phase = std::clamp(progress01, 0.0f, 1.0f);
            }
        }
    }

    bool FMechanismSystem::TryGetRidePoint(const std::string& name, glm::vec3& outPoint) const
    {
        for (const FRuntime& runtime : runtime_)
        {
            if (runtime.queryName != name)
            {
                continue;
            }
            const FIndexedNode& root = runtime.record.root;
            const Assets::Node* part = runtime.record.part;
            // Heights are the top face of whatever the player stands on, plus a few
            // centimetres so the drop lands rather than starting in penetration.
            switch (runtime.record.kind)
            {
            case EMechanismKind::MovingPlatform:
                if (!part) return false;
                outPoint = part->WorldTranslation() + part->WorldRotation() * glm::vec3(0.0f, 0.75f, 0.0f);
                return true;
            case EMechanismKind::Pendulum:
            {
                if (!part) return false;
                const float arm = static_cast<float>(root.Number("arm", 5.0));
                outPoint = part->WorldTranslation() + part->WorldRotation() * glm::vec3(0.0f, -arm + 0.25f, 0.0f);
                return true;
            }
            case EMechanismKind::Seesaw:
                if (!part) return false;
                outPoint = part->WorldTranslation() + part->WorldRotation() * glm::vec3(0.0f, 0.15f, 0.0f);
                return true;
            case EMechanismKind::Crumble:
                if (!part) return false;
                outPoint = part->WorldTranslation() + part->WorldRotation() * glm::vec3(0.0f, 0.45f, 0.0f);
                return true;
            case EMechanismKind::SpinDisc:
            {
                const float radius = static_cast<float>(root.Number("r", 3.0));
                outPoint = root.worldPos + root.worldRot * glm::vec3(radius * 0.65f, 0.60f, 0.0f);
                return true;
            }
            case EMechanismKind::BouncePad:
            {
                const float radius = static_cast<float>(root.Number("r", 1.2));
                outPoint = root.worldPos + root.worldRot * glm::vec3(0.0f, 0.3f + 0.45f * radius + 0.05f, 0.0f);
                return true;
            }
            case EMechanismKind::Spring:
                outPoint = root.worldPos +
                           root.worldRot * glm::vec3(0.0f, static_cast<float>(root.Number("h", 1.2)) + 0.05f, 0.0f);
                return true;
            case EMechanismKind::Roller:
                outPoint = root.worldPos +
                           root.worldRot *
                               glm::vec3(0.0f, 2.0f * static_cast<float>(root.Number("r", 1.0)) + 0.35f, 0.0f);
                return true;
            case EMechanismKind::Conveyor:
                outPoint = root.worldPos + root.worldRot * glm::vec3(0.0f, 0.55f, 0.0f);
                return true;
            case EMechanismKind::Button:
            {
                const float radius = static_cast<float>(root.Number("r", 1.0));
                outPoint = root.worldPos + root.worldRot * glm::vec3(0.0f, 0.26f + 0.32f * radius + 0.05f, 0.0f);
                return true;
            }
            case EMechanismKind::LaserBeam:
                // Standing in the beam line is the point: a script waits for the dark half
                // of the cycle and runs through.
                outPoint = root.worldPos +
                           root.worldRot * glm::vec3(static_cast<float>(root.Number("L", 6.0)) * 0.5f, 0.2f, 0.0f);
                return true;
            case EMechanismKind::Fan:
                // Two metres downstream of the hub, inside the draught.
                outPoint = root.worldPos + root.worldRot * glm::vec3(0.0f, 0.2f, 2.0f);
                return true;
            case EMechanismKind::Fountain:
                outPoint = root.worldPos + root.worldRot * glm::vec3(0.0f, 0.4f, 0.0f);
                return true;
            case EMechanismKind::Windmill:
                outPoint = root.worldPos + root.worldRot * glm::vec3(3.0f, 0.2f, 0.0f);
                return true;
            case EMechanismKind::ChestLid:
            case EMechanismKind::Lever:
                // In front of the prop (kit front is SCAD -y, which is the module's +z),
                // within punch range.
                outPoint = root.worldPos + root.worldRot * glm::vec3(0.0f, 0.2f, 1.2f);
                return true;
            case EMechanismKind::SpikeBall:
            case EMechanismKind::CheckpointFlag:
            case EMechanismKind::Zipline:
            case EMechanismKind::Gate:
            case EMechanismKind::Cage:
                outPoint = root.worldPos + root.worldRot * glm::vec3(0.0f, 0.2f, 0.0f);
                return true;
            case EMechanismKind::Count:
                return false;
            }
        }
        return false;
    }

    bool FMechanismSystem::TryGetFaceYaw(const std::string& name, const glm::vec3& from, float& outYaw) const
    {
        for (const FRuntime& runtime : runtime_)
        {
            if (runtime.queryName != name)
            {
                continue;
            }
            const glm::vec3 toRoot = runtime.record.root.worldPos - from;
            if (toRoot.x * toRoot.x + toRoot.z * toRoot.z <= 0.04f)
            {
                return false;
            }
            outYaw = std::atan2(toRoot.x, toRoot.z);
            return true;
        }
        return false;
    }

    bool FMechanismSystem::QueryPhase(const std::string& name, float& outPhase) const
    {
        for (const FRuntime& runtime : runtime_)
        {
            if (runtime.queryName == name)
            {
                outPhase = runtime.phase;
                return true;
            }
        }
        return false;
    }

    void FMechanismSystem::Update(float time, float deltaSeconds, const glm::vec3& playerFoot, bool playerOnGround,
                                  bool jumpPressed)
    {
        effects_ = FMechanismEffects{};
        if (!scene_)
        {
            return;
        }

        for (FRuntime& runtime : runtime_)
        {
            const FIndexedNode& root = runtime.record.root;
            const glm::vec3 local = ToLocal(runtime.record, playerFoot);
            const float tolerance = config_.FootContactTolerance;

            switch (runtime.record.kind)
            {
            case EMechanismKind::MovingPlatform:
            {
                const float rail = static_cast<float>(root.Number("rail", 10.0));
                const float length = static_cast<float>(root.Number("L", 3.0));
                const float speed = static_cast<float>(root.Number("speed", 2.5));
                const float travel = std::max(rail - length, 0.01f);
                // One full there-and-back is 2 * travel at the authored speed.
                const float period = 2.0f * travel / std::max(speed, 0.05f);
                const float phaseOffset = static_cast<float>(root.Number("phase", 0.0)) * period;
                runtime.phase = PingPong01(time + phaseOffset, period);
                const float x = -rail * 0.5f + length * 0.5f + runtime.phase * travel;
                ApplyPart(runtime, glm::vec3(x, runtime.record.partBindTranslation.y,
                                             runtime.record.partBindTranslation.z),
                          runtime.record.partBindRotation, deltaSeconds);
                break;
            }
            case EMechanismKind::Pendulum:
            {
                const float period = static_cast<float>(root.Number("period", 3.0));
                const float phase01 = static_cast<float>(root.Number("phase", 0.0));
                const float amplitude = static_cast<float>(root.Number("ang", 25.0));
                const float angle = Swing(time, period, phase01, amplitude);
                runtime.value = angle;
                runtime.phase = period > 0.0f ? std::fmod(time / period, 1.0f) : 0.0f;
                ApplyPart(runtime, runtime.record.partBindTranslation, ScadRotY(angle), deltaSeconds);
                break;
            }
            case EMechanismKind::Seesaw:
            {
                const float amp = static_cast<float>(root.Number("amp", 12.0));
                const float speed = static_cast<float>(root.Number("speed", 25.0));
                const float length = static_cast<float>(root.Number("L", 6.0));
                const float width = static_cast<float>(root.Number("W", 2.0));
                float target = 0.0f;
                // The plank sits 0.98 above the base; a foot on it is within tolerance of that.
                if (playerOnGround && InBox2D(local, length * 0.5f, width * 0.5f) &&
                    std::abs(local.y - 1.07f) < 0.6f)
                {
                    // Standing on the +x half tips that side down, which in SCAD is a
                    // positive rotation about Y.
                    target = local.x >= 0.0f ? amp : -amp;
                }
                runtime.value = Approach(runtime.value, target, speed, deltaSeconds);
                runtime.phase = amp > 0.0f ? (runtime.value / amp) * 0.5f + 0.5f : 0.0f;
                ApplyPart(runtime, runtime.record.partBindTranslation, ScadRotY(runtime.value), deltaSeconds);
                break;
            }
            case EMechanismKind::SpinDisc:
            {
                const float radius = static_cast<float>(root.Number("r", 3.0));
                const float speed = static_cast<float>(root.Number("speed", 30.0));
                runtime.value = std::fmod(runtime.value + speed * deltaSeconds, 360.0f);
                runtime.phase = runtime.value / 360.0f;
                // Rotationally symmetric, so only the visual turns; the implicit static
                // collider stays where it is and the rider gets a surface velocity instead.
                if (Assets::Node* node = root.node)
                {
                    node->SetTransform(node->Translation(), runtime.rootBindRotation * ScadRotZ(runtime.value),
                                   node->Scale());
                }
                if (playerOnGround && InDisc2D(local, radius) && std::abs(local.y - 0.55f) < tolerance + 0.25f)
                {
                    // v = omega x r with omega along the disc's local up (engine +Y),
                    // expressed back in world space. Getting this sign wrong sends the
                    // rider the opposite way from the disc they are standing on.
                    const float omega = glm::radians(speed);
                    const glm::vec3 tangentLocal(local.z * omega, 0.0f, -local.x * omega);
                    effects_.surfaceVelocity += root.worldRot * tangentLocal;
                }
                break;
            }
            case EMechanismKind::Crumble:
            {
                const float length = static_cast<float>(root.Number("L", 2.0));
                const float depth = static_cast<float>(root.Number("D", 2.0));
                const float warn = static_cast<float>(root.Number("warn", 0.6));
                const float respawn = static_cast<float>(root.Number("respawn", 4.0));
                const bool stoodOn = playerOnGround && InBox2D(local, length * 0.5f, depth * 0.5f) &&
                                     std::abs(local.y - 0.4f) < tolerance + 0.25f;
                if (!runtime.collapsed)
                {
                    runtime.timer = stoodOn ? runtime.timer + deltaSeconds : 0.0f;
                    if (runtime.timer >= warn)
                    {
                        runtime.collapsed = true;
                        runtime.timer = 0.0f;
                        if (!runtime.body.IsInvalid() && physics_)
                        {
                            physics_->SetBodyActive(runtime.body, false);
                        }
                    }
                    // Rattle before it goes, so the drop is telegraphed.
                    const float shake = warn > 0.0f ? std::sin(time * 60.0f) * 0.03f * (runtime.timer / warn) : 0.0f;
                    runtime.phase = warn > 0.0f ? std::clamp(runtime.timer / warn, 0.0f, 1.0f) : 0.0f;
                    ApplyPart(runtime,
                              runtime.record.partBindTranslation + glm::vec3(shake, 0.0f, 0.0f),
                              runtime.record.partBindRotation, deltaSeconds);
                }
                else
                {
                    runtime.timer += deltaSeconds;
                    runtime.phase = 1.0f; // collapsed: the phase stays saturated until it resets
                    const float drop = std::min(runtime.timer * 1.5f, 1.2f);
                    ApplyPart(runtime, runtime.record.partBindTranslation - glm::vec3(0.0f, drop, 0.0f),
                              runtime.record.partBindRotation, deltaSeconds);
                    if (Assets::Node* part = runtime.record.part)
                    {
                        Assets::NodeUtils::SetVisibleRecursive(part, drop < 1.15f);
                    }
                    if (runtime.timer >= respawn)
                    {
                        runtime.collapsed = false;
                        runtime.timer = 0.0f;
                        if (!runtime.body.IsInvalid() && physics_)
                        {
                            physics_->SetBodyActive(runtime.body, true);
                        }
                        if (Assets::Node* part = runtime.record.part)
                        {
                            Assets::NodeUtils::SetVisibleRecursive(part, true);
                        }
                    }
                }
                break;
            }
            case EMechanismKind::BouncePad:
            {
                const float radius = static_cast<float>(root.Number("r", 1.2));
                const float launch = static_cast<float>(root.Number("launch", 6.0));
                const float domeTop = 0.3f + 0.45f * radius;
                if (playerOnGround && InDisc2D(local, radius + 0.2f) &&
                    std::abs(local.y - domeTop) < tolerance + 0.4f)
                {
                    effects_.launch = true;
                    effects_.launchHeight = std::max(effects_.launchHeight, launch);
                }
                break;
            }
            case EMechanismKind::Spring:
            {
                const float radius = static_cast<float>(root.Number("r", 0.8));
                const float height = static_cast<float>(root.Number("h", 1.2));
                const float launch = static_cast<float>(root.Number("launch", 8.0));
                const bool stoodOn = playerOnGround && InDisc2D(local, radius + 0.3f) &&
                                     std::abs(local.y - height) < tolerance + 0.6f;
                if (stoodOn && runtime.timer <= 0.0f)
                {
                    effects_.launch = true;
                    effects_.launchHeight = std::max(effects_.launchHeight, launch);
                    runtime.timer = 0.3f; // compress-and-release, also a re-trigger guard
                }
                runtime.timer = std::max(0.0f, runtime.timer - deltaSeconds);
                // Compress in the first half of the animation, spring back in the second.
                const float compress = runtime.timer > 0.15f ? (0.3f - runtime.timer) / 0.15f
                                                             : runtime.timer / 0.15f;
                runtime.phase = compress;
                ApplyPart(runtime,
                          runtime.record.partBindTranslation - glm::vec3(0.0f, compress * 0.15f, 0.0f),
                          runtime.record.partBindRotation, deltaSeconds);
                break;
            }
            case EMechanismKind::Roller:
            {
                const float length = static_cast<float>(root.Number("L", 5.0));
                const float radius = static_cast<float>(root.Number("r", 1.0));
                const float speed = static_cast<float>(root.Number("speed", 2.5));
                const float angularDegrees = radius > 0.0f ? glm::degrees(speed / radius) : 0.0f;
                runtime.value = std::fmod(runtime.value + angularDegrees * deltaSeconds, 360.0f);
                runtime.phase = runtime.value / 360.0f;
                ApplyPart(runtime, runtime.record.partBindTranslation, ScadRotX(runtime.value), deltaSeconds);
                const float drumTop = 2.0f * radius + 0.3f;
                if (playerOnGround && InBox2D(local, length * 0.5f, radius * 0.7f) &&
                    std::abs(local.y - drumTop) < tolerance + 0.3f)
                {
                    // The drum turns about +x, so the top surface travels toward SCAD -y,
                    // which is engine +z in the mechanism's local frame.
                    effects_.surfaceVelocity += root.worldRot * glm::vec3(0.0f, 0.0f, speed);
                }
                break;
            }
            case EMechanismKind::Conveyor:
            {
                const float length = static_cast<float>(root.Number("L", 6.0));
                const float width = static_cast<float>(root.Number("W", 2.0));
                const float speed = static_cast<float>(root.Number("speed", 2.0));
                runtime.phase = std::fmod(time * 0.5f, 1.0f);
                if (playerOnGround && InBox2D(local, length * 0.5f, width * 0.5f) &&
                    std::abs(local.y - 0.5f) < tolerance + 0.25f)
                {
                    effects_.surfaceVelocity += root.worldRot * glm::vec3(speed, 0.0f, 0.0f);
                }
                break;
            }
            case EMechanismKind::Zipline:
            {
                const float length = static_cast<float>(root.Number("L", 14.0));
                const float drop = static_cast<float>(root.Number("drop", 4.0));
                const float speed = static_cast<float>(root.Number("speed", 8.0));
                // The cable runs from (0, 2.9) to (L, 2.9 - drop) in SCAD; the rider hangs
                // an arm plus a body below it.
                constexpr float kCableHeight = 2.9f;
                constexpr float kHangDrop = 1.9f;
                const glm::vec3 fromLocal(0.0f, kCableHeight - kHangDrop, 0.0f);
                const glm::vec3 toLocal(length, kCableHeight - drop - kHangDrop, 0.0f);
                if (jumpPressed && glm::length(glm::vec3(local.x, 0.0f, local.z)) < 1.5f &&
                    local.y > -1.0f && local.y < kCableHeight)
                {
                    effects_.zipAvailable = true;
                    effects_.zipFrom = root.worldPos + root.worldRot * fromLocal;
                    effects_.zipTo = root.worldPos + root.worldRot * toLocal;
                    effects_.zipSpeed = speed;
                }
                // The trolley follows whoever is riding, otherwise it parks at the top.
                const float t = runtime.phase;
                ApplyPart(runtime,
                          glm::vec3(t * length, kCableHeight - t * drop, 0.0f),
                          runtime.record.partBindRotation, deltaSeconds);
                break;
            }
            case EMechanismKind::Button:
            {
                const float radius = static_cast<float>(root.Number("r", 1.0));
                const float capTop = 0.26f + 0.32f * radius;
                if (!runtime.latched && playerOnGround && InDisc2D(local, radius) &&
                    std::abs(local.y - capTop) < tolerance + 0.4f)
                {
                    runtime.latched = true;
                    TriggerGate(static_cast<int>(root.Number("idx", 0.0)));
                }
                runtime.phase = runtime.latched ? 1.0f : 0.0f;
                const float press = runtime.latched ? 0.15f : 0.0f;
                ApplyPart(runtime, runtime.record.partBindTranslation - glm::vec3(0.0f, press, 0.0f),
                          runtime.record.partBindRotation, deltaSeconds);
                break;
            }
            case EMechanismKind::Gate:
            {
                const float height = static_cast<float>(root.Number("h", 3.0));
                const float target = runtime.latched ? 1.0f : 0.0f;
                // One second from closed to fully raised.
                runtime.phase = Approach(runtime.phase, target, 1.0f, deltaSeconds);
                ApplyPart(runtime,
                          runtime.record.partBindTranslation + glm::vec3(0.0f, runtime.phase * (height + 0.2f), 0.0f),
                          runtime.record.partBindRotation, deltaSeconds);
                break;
            }
            case EMechanismKind::Cage:
            {
                const float target = runtime.latched ? 1.0f : 0.0f;
                runtime.phase = Approach(runtime.phase, target, 1.0f, deltaSeconds);
                ApplyPart(runtime,
                          runtime.record.partBindTranslation + glm::vec3(0.0f, runtime.phase * 1.5f, 0.0f),
                          runtime.record.partBindRotation, deltaSeconds);
                break;
            }
            case EMechanismKind::SpikeBall:
            {
                const float amplitude = static_cast<float>(root.Number("ang", 35.0));
                const float period = static_cast<float>(root.Number("period", 2.6));
                const float phase01 = static_cast<float>(root.Number("phase", 0.0));
                runtime.value = Swing(time, period, phase01, amplitude);
                runtime.phase = period > 0.0f ? std::fmod(time / period, 1.0f) : 0.0f;
                // The lethal volume is not computed here: HazardSystem follows the ball
                // node itself, so the swing and the kill test cannot drift apart.
                ApplyPart(runtime, runtime.record.partBindTranslation, ScadRotY(runtime.value), deltaSeconds);
                break;
            }
            case EMechanismKind::LaserBeam:
            {
                const float period = static_cast<float>(root.Number("period", 2.4));
                const float duty = static_cast<float>(root.Number("duty", 0.55));
                const float phase01 = static_cast<float>(root.Number("phase", 0.0));
                runtime.phase = period > 0.0f ? std::fmod(time / period + phase01, 1.0f) : 0.0f;
                // Visible means lethal: HazardSystem reads the beam node's own visibility
                // rather than keeping a second copy of this timing.
                const bool lit = duty >= 1.0f || runtime.phase < duty;
                runtime.value = lit ? 1.0f : 0.0f;
                if (Assets::Node* beam = runtime.record.part)
                {
                    Assets::NodeUtils::SetVisibleRecursive(beam, lit);
                }
                break;
            }
            case EMechanismKind::Fan:
            {
                const float scale = static_cast<float>(root.Number("s", 1.0));
                const float speed = static_cast<float>(root.Number("speed", 520.0));
                const float power = static_cast<float>(root.Number("power", 6.0));
                const float range = static_cast<float>(root.Number("range", 7.0));
                runtime.value = std::fmod(runtime.value + speed * deltaSeconds, 360.0f);
                runtime.phase = runtime.value / 360.0f;
                ApplyPart(runtime, runtime.record.partBindTranslation, ScadRotY(runtime.value), deltaSeconds);
                // The draught is a cylinder down the module's front (SCAD -y, the local
                // +z), starting at the hub. It is tested against the player's chest: the
                // stream is a metre and a half off the ground, so a foot test never
                // reaches it while the player is in the air where the push matters.
                const glm::vec3 hub(0.0f, 1.5f * scale, 0.0f);
                const glm::vec3 chestLocal =
                    ToLocal(runtime.record, playerFoot + glm::vec3(0.0f, 0.75f, 0.0f)) - hub;
                if (InCylinder(chestLocal, glm::vec3(0.0f, 0.0f, 1.0f), range, 1.1f * scale + 0.9f))
                {
                    effects_.wind += root.worldRot * glm::vec3(0.0f, 0.0f, power);
                }
                break;
            }
            case EMechanismKind::Fountain:
            {
                const float height = static_cast<float>(root.Number("h", 4.0));
                const float radius = static_cast<float>(root.Number("r", 0.45));
                const float period = static_cast<float>(root.Number("period", 3.2));
                const float phase01 = static_cast<float>(root.Number("phase", 0.0));
                const float lift = static_cast<float>(root.Number("lift", 6.0));
                // The column breathes between 55% and 100% of its authored height, and
                // only lifts as far as it currently reaches.
                runtime.phase = PingPong01(time + phase01 * period, period);
                const float swell = 0.55f + 0.45f * runtime.phase;
                runtime.value = swell;
                const glm::vec3 columnScale = runtime.partBindScale * glm::vec3(1.0f, swell, 1.0f);
                ApplyPart(runtime, runtime.record.partBindTranslation, runtime.record.partBindRotation,
                          deltaSeconds, &columnScale);
                const glm::vec3 local = ToLocal(runtime.record, playerFoot) - glm::vec3(0.0f, 0.3f, 0.0f);
                if (lift > 0.0f &&
                    InCylinder(local, glm::vec3(0.0f, 1.0f, 0.0f), height * swell, radius + 0.9f))
                {
                    effects_.liftSpeed = std::max(effects_.liftSpeed, lift);
                }
                break;
            }
            case EMechanismKind::Windmill:
            {
                const float speed = static_cast<float>(root.Number("speed", 26.0));
                runtime.value = std::fmod(runtime.value + speed * deltaSeconds, 360.0f);
                runtime.phase = runtime.value / 360.0f;
                ApplyPart(runtime, runtime.record.partBindTranslation, ScadRotY(runtime.value), deltaSeconds);
                break;
            }
            case EMechanismKind::ChestLid:
            {
                const float target = runtime.latched ? 1.0f : 0.0f;
                // A punched chest throws its lid back fast and leaves it open.
                runtime.phase = Approach(runtime.phase, target, 3.0f, deltaSeconds);
                ApplyPart(runtime, runtime.record.partBindTranslation,
                          runtime.record.partBindRotation * ScadRotX(-55.0f * runtime.phase), deltaSeconds);
                break;
            }
            case EMechanismKind::Lever:
            {
                const float target = runtime.latched ? 1.0f : 0.0f;
                runtime.phase = Approach(runtime.phase, target, 2.5f, deltaSeconds);
                // Bind pose is -30 degrees about SCAD y; a punch swings it through to +30.
                ApplyPart(runtime, runtime.record.partBindTranslation,
                          runtime.record.partBindRotation * ScadRotY(60.0f * runtime.phase), deltaSeconds);
                break;
            }
            case EMechanismKind::CheckpointFlag:
            {
                const float height = static_cast<float>(root.Number("h", 3.0));
                const float target = runtime.latched ? 1.0f : 0.0f;
                runtime.phase = Approach(runtime.phase, target, 1.2f, deltaSeconds);
                // Authored furled at the foot of the pole; reaching the checkpoint runs it
                // up to just under the finial.
                const float rise = std::max(height - 1.0f, 0.0f);
                ApplyPart(runtime,
                          runtime.record.partBindTranslation + glm::vec3(0.0f, runtime.phase * rise, 0.0f),
                          runtime.record.partBindRotation, deltaSeconds);
                break;
            }
            case EMechanismKind::Count:
                break;
            }
        }
    }
}
