#pragma once

// ============================================================================
// MechanismSystem.hpp - Drives every kit_astro mechanism from its indexed scene
// node. Displacement pieces (rail car, pendulum arm, seesaw plank, crumbling
// slab, gate grid, cage dome) get a kinematic box bound to the ab_part_* node
// and are moved with MoveKinematicBody; rotationally symmetric ones (spin disc,
// roller, conveyor) keep their implicit static collider and instead hand the
// player a surface velocity. Launch pads and the zipline publish requests the
// player controller consumes.
// ============================================================================

#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "Engine/Runtime/Subsystems/NextPhysicsTypes.hpp"

#include "Application/Game/NextAstrobot/Level/LevelIndex.hpp"
#include "Application/Game/NextAstrobot/NextAstrobotConfig.hpp"

class NextPhysics;

namespace Assets
{
    class Node;
    class Scene;
}

namespace NextAstrobot
{
    /// What the mechanisms want to do to the player this frame.
    struct FMechanismEffects
    {
        glm::vec3 surfaceVelocity{0.0f};
        bool launch = false;
        float launchHeight = 0.0f;
        bool zipAvailable = false;
        glm::vec3 zipFrom{0.0f};
        glm::vec3 zipTo{0.0f};
        float zipSpeed = 8.0f;
    };

    class FMechanismSystem
    {
    public:
        void Configure(const FWorldConfig& config) { config_ = config; }
        void Bind(Assets::Scene& scene, NextPhysics* physics, const FLevelIndex& index);
        void Unbind();

        /// Step one frame. `playerFoot` is last frame's foot position by design: the
        /// mechanisms move before the character does, so the seesaw and the surface tests
        /// have to read the position the player actually stood at.
        void Update(float time, float deltaSeconds, const glm::vec3& playerFoot, bool playerOnGround,
                    bool jumpPressed);

        /// Latches the buttons/gates with this index open (also called by a lever or a key).
        void TriggerGate(int index);
        /// Raises the dome of the given cage, and reports whether this call is what opened it.
        bool OpenCage(const Assets::Node* cageNode);
        /// Parks the zipline trolley at the rider's progress along the cable (0..1).
        void SetZipProgress(float progress01);

        const FMechanismEffects& Effects() const { return effects_; }
        /// World-space point standing on top of the named mechanism. Agent scripts use it
        /// through the astro.ride cvar so a "ride the platform" test does not have to guess
        /// where a moving piece happens to be this frame.
        bool TryGetRidePoint(const std::string& name, glm::vec3& outPoint) const;
        /// Normalized phase of a named mechanism, for agent scripts: "moving", "gate_1", ...
        bool QueryPhase(const std::string& name, float& outPhase) const;
        size_t Count() const { return runtime_.size(); }

    private:
        struct FRuntime
        {
            FMechanismRecord record;
            NextBodyID body;
            glm::quat rootBindRotation{1.0f, 0.0f, 0.0f, 0.0f};
            glm::vec3 boxLocalOffset{0.0f}; // part-local centre of the kinematic box
            glm::vec3 boxExtent{0.0f};      // full extent, as CreateBoxBody expects
            float phase = 0.0f;             // normalized progress, exposed to agent scripts
            float value = 0.0f;             // generic driven value (angle, offset, timer)
            float timer = 0.0f;
            bool latched = false;           // button pressed / gate open / cage open
            bool collapsed = false;         // crumbling slab has fallen
            std::string queryName;
        };

        void CreateKinematicPieces(Assets::Scene& scene, NextPhysics* physics);
        void ApplyPart(FRuntime& runtime, const glm::vec3& localTranslation, const glm::quat& localRotation,
                       float deltaSeconds);
        /// Player foot expressed in the mechanism's local frame (engine axes, so
        /// local.y is the SCAD z height).
        glm::vec3 ToLocal(const FMechanismRecord& record, const glm::vec3& world) const;

        FWorldConfig config_{};
        Assets::Scene* scene_ = nullptr;
        NextPhysics* physics_ = nullptr;
        std::vector<FRuntime> runtime_;
        FMechanismEffects effects_{};
    };
}
