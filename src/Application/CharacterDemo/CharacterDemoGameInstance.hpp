#pragma once

#include "Common/CoreMinimal.hpp"
#include "Runtime/Engine.hpp"
#include "Runtime/Subsystems/NextCharacterController.h"

class CharacterDemoGameInstance : public NextGameInstanceBase
{
public:
    CharacterDemoGameInstance(Vulkan::WindowConfig& config, Options& options, NextEngine* engine);
    ~CharacterDemoGameInstance() override = default;

    void OnInit() override;
    void OnTick(double deltaSeconds) override;
    void OnDestroy() override;

    void BeforeSceneRebuild(std::vector<std::shared_ptr<Assets::Node>>& nodes,
                            std::vector<Assets::Model>& models,
                            std::vector<Assets::FMaterial>& materials,
                            std::vector<Assets::LightObject>& lights,
                            std::vector<Assets::AnimationTrack>& tracks) override;
    void OnSceneLoaded() override;

    bool OnRenderUI() override;
    bool OverrideRenderCamera(Assets::Camera& OutRenderCamera) const override;

    bool OnKey(SDL_Event& event) override;
    bool OnCursorPosition(double xpos, double ypos) override;
    bool OnMouseButton(SDL_Event& event) override;
    bool OnScroll(double xoffset, double yoffset) override;

private:
    glm::vec3 GetMoveForward() const;
    glm::vec3 GetMoveRight() const;
    glm::vec3 GetViewForward() const;
    glm::vec3 GetEyePosition() const;
    void SetFirstPersonMode(bool enabled);
    void FireProjectile();
    void UpdateCharacterNode();

    NextEngine* engine_;
    NextCharacterController characterController_;

    // Character visual node
    std::shared_ptr<Assets::Node> characterNode_;
    uint32_t capsuleModelId_ = 0;
    uint32_t characterMatId_ = 0;
    uint32_t projectileModelId_ = 0;
    uint32_t projectileMatId_ = 0;

    // Input state
    bool keyForward_ = false;
    bool keyBack_ = false;
    bool keyLeft_ = false;
    bool keyRight_ = false;
    bool keyJump_ = false;
    bool keySprint_ = false;
    bool mouseCaptured_ = false;
    bool resetMouse_ = true;
    bool showPhysicsDebug_ = false;
    glm::dvec2 mousePos_{0.0, 0.0};

    // Camera
    bool firstPersonMode_ = false;
    float yaw_ = 0.0f;       // horizontal rotation in radians
    float pitch_ = 0.0f;     // vertical rotation in radians

    // Settings
    float firstPersonEyeHeight_ = 1.55f;
    float walkSpeed_ = 4.0f;
    float runSpeed_ = 8.0f;
    float mouseSensitivity_ = 0.002f;
    float cameraDistance_ = 5.0f;   // third-person camera distance
    float cameraHeight_ = 2.0f;    // camera height offset above character
    float projectileSize_ = 0.2f;
    float projectileSpawnDistance_ = 0.9f;
    float projectileForce_ = 1000.0f;
};
