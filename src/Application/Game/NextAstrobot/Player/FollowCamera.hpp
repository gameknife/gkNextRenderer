#pragma once

// ============================================================================
// FollowCamera.hpp - Third-person chase camera for the platformer: a damped
// boom behind a yaw that drifts toward the direction of travel and can be
// steered manually, with a spring arm that pulls in when the level gets between
// the player and the lens. Also owns the title / intro camera, which is read
// from the level's own gk_camera markers rather than authored in code.
// ============================================================================

#include <string>

#include <glm/glm.hpp>

#include "Application/Game/NextAstrobot/NextAstrobotConfig.hpp"

namespace Assets
{
    struct Camera;
    class Node;
    class Scene;
}

namespace NextAstrobot
{
    /// The level's own gk_camera markers: a still for the title screen and a keyed path
    /// for the intro fly-through. Both are authored in the .scad file, so a new level
    /// frames its own opening with no code change.
    class FLevelCameras
    {
    public:
        void Bind(Assets::Scene& scene, const std::string& titleName, const std::string& introPathName);
        void Clear();

        bool HasTitle() const { return hasTitle_; }
        bool HasIntro() const { return introNode_ != nullptr; }
        float IntroDuration() const { return introDuration_; }

        void FillTitle(Assets::Camera& outCamera) const;
        /// Samples the intro track at `elapsed` seconds, moving the path node.
        void AdvanceIntro(Assets::Scene& scene, float elapsed) const;
        /// Reads back whatever pose AdvanceIntro left the path node in.
        void FillIntro(Assets::Camera& outCamera) const;

    private:
        bool hasTitle_ = false;
        glm::mat4 titleModelView_{1.0f};
        float titleFov_ = 50.0f;
        Assets::Node* introNode_ = nullptr;
        float introFov_ = 52.0f;
        float introDuration_ = 0.0f;
    };

    class FFollowCamera
    {
    public:
        void Configure(const FCameraConfig& config) { config_ = config; }

        /// Places the camera immediately (level load, respawn, ejecting from a cutscene).
        void Snap(const glm::vec3& footPosition, float yaw);
        /// `scene` is optional: pass it and the boom shortens against the level geometry,
        /// leave it null (or before the scene is ready) and the boom stays at full length.
        void Update(const glm::vec3& footPosition, const glm::vec3& horizontalVelocity, float deltaSeconds,
                    Assets::Scene* scene = nullptr);

        /// Right stick / RMB drag, in normalized units per second.
        void AddManualYaw(float amount);

        void Fill(Assets::Camera& outCamera) const;
        float Yaw() const { return yaw_; }
        /// 1.0 when the boom is at full length, less while something is in the way.
        float SpringArm01() const { return boomScale_; }
        /// How far the lens actually ended up from what it is looking at, in metres.
        float BoomDistance() const { return glm::length(position_ - target_); }
        glm::vec3 Forward() const;
        glm::vec3 Right() const;
        glm::vec3 Position() const { return position_; }

    private:
        FCameraConfig config_{};
        float yaw_ = 0.0f;
        float manualIdleSeconds_ = 10.0f;
        float boomScale_ = 1.0f;
        glm::vec3 position_{0.0f, 5.0f, 10.0f};
        glm::vec3 target_{0.0f};
    };
}
