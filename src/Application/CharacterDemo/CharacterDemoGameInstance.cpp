#include "CharacterDemoGameInstance.hpp"

#include <imgui.h>
#include <SDL3/SDL_events.h>

#include "Assets/Loaders/FProcModel.h"
#include "Assets/Core/Node.h"
#include "Runtime/Components/PhysicsComponent.h"
#include "Runtime/Components/RenderComponent.h"
#include "Runtime/Engine.hpp"
#include "Runtime/Scene/SceneList.hpp"
#include "Runtime/Platform/PlatformCommon.h"
#include "Vulkan/WindowSurface.hpp"

std::unique_ptr<NextGameInstanceBase> CreateGameInstance(Vulkan::WindowConfig& config, Options& options,
                                                         NextEngine* engine)
{
    return std::make_unique<CharacterDemoGameInstance>(config, options, engine);
}

CharacterDemoGameInstance::CharacterDemoGameInstance(Vulkan::WindowConfig& config, Options& options, NextEngine* engine)
    : NextGameInstanceBase(config, options, engine)
    , engine_(engine)
{
}

void CharacterDemoGameInstance::OnInit()
{
    // Default to procedural playground; user can override via command line --scene
    std::string initialScene = "CharacterPlayground.proc";
    if (!GOption->SceneName.empty())
    {
        initialScene = GOption->SceneName;
    }
    engine_->RequestLoadScene(initialScene);
}

void CharacterDemoGameInstance::OnTick(double deltaSeconds)
{
    if (!characterController_.IsValid())
    {
        return;
    }

    // Build movement direction in world space from camera yaw
    glm::vec3 forward(std::sin(yaw_), 0.0f, std::cos(yaw_));
    glm::vec3 right(-std::cos(yaw_), 0.0f, std::sin(yaw_));

    glm::vec3 moveDir(0.0f);
    if (keyForward_) moveDir += forward;
    if (keyBack_)    moveDir -= forward;
    if (keyRight_)   moveDir += right;
    if (keyLeft_)    moveDir -= right;

    if (glm::length(moveDir) > 0.001f)
    {
        moveDir = glm::normalize(moveDir);
    }

    float speed = keySprint_ ? runSpeed_ : walkSpeed_;

    characterController_.Update(moveDir, speed, keyJump_, static_cast<float>(deltaSeconds));
    keyJump_ = false; // consume jump

    UpdateCharacterNode();
}

void CharacterDemoGameInstance::OnDestroy()
{
    characterController_.Destroy();
}

void CharacterDemoGameInstance::BeforeSceneRebuild(
    std::vector<std::shared_ptr<Assets::Node>>& nodes,
    std::vector<Assets::Model>& models,
    std::vector<Assets::FMaterial>& materials,
    std::vector<Assets::LightObject>& lights,
    std::vector<Assets::AnimationTrack>& tracks)
{
    // Create a capsule-like visual: a box that approximates the character shape
    // Height 1.75, width 0.6
    const float halfW = 0.3f;
    const float halfH = 0.875f;
    models.push_back(Assets::FProcModel::CreateBox(
        glm::vec3(-halfW, 0.0f, -halfW),
        glm::vec3(halfW, halfH * 2.0f, halfW)));
    capsuleModelId_ = static_cast<uint32_t>(models.size() - 1);

    // A distinct green material for the character
    materials.push_back({Assets::Material::Lambertian(glm::vec3(0.2f, 0.8f, 0.3f))});
    characterMatId_ = static_cast<uint32_t>(materials.size() - 1);
}

void CharacterDemoGameInstance::OnSceneLoaded()
{
    NextGameInstanceBase::OnSceneLoaded();

    // Create the character controller
    FCharacterControllerSettings settings;
    settings.height = 1.75f;
    settings.radius = 0.3f;

    // Place character at scene camera position or a default spawn
    const auto& cam = engine_->GetScene().GetRenderCamera();
    glm::mat4 invModelView = glm::inverse(cam.ModelView);
    glm::vec3 camPos = glm::vec3(invModelView[3]);
    settings.initialPosition = glm::vec3(camPos.x, camPos.y, camPos.z);

    characterController_.Create(engine_->GetPhysicsEngine(), settings);

    // Extract yaw from camera
    glm::vec3 camForward = -glm::vec3(invModelView[2]);
    yaw_ = std::atan2(camForward.x, camForward.z);
    pitch_ = std::asin(glm::clamp(camForward.y, -1.0f, 1.0f));

    // Create the character visual node
    uint32_t instanceId = engine_->GetScene().GenerateInstanceId();
    characterNode_ = Assets::Node::CreateNode("CharacterBody",
        settings.initialPosition, glm::quat(1, 0, 0, 0), glm::vec3(1), instanceId);

    auto renderComp = std::make_shared<Runtime::RenderComponent>();
    renderComp->SetModelId(capsuleModelId_);
    renderComp->SetMaterial({characterMatId_});
    renderComp->SetVisible(true);
    characterNode_->AddComponent(renderComp);

    // Prevent Scene::AddNode from auto-creating a static mesh body for the visual-only character node.
    auto physicsComp = std::make_shared<Runtime::PhysicsComponent>();
    physicsComp->SetMobility(Runtime::ENodeMobility::Dynamic);
    characterNode_->AddComponent(physicsComp);

    engine_->GetScene().AddNode(characterNode_);
    engine_->GetScene().MarkDirty();

    // Capture mouse
    mouseCaptured_ = true;
    resetMouse_ = true;
    SDL_SetWindowRelativeMouseMode(engine_->GetWindow().Handle(), true);
}

bool CharacterDemoGameInstance::OnRenderUI()
{
    if (!characterController_.IsValid())
    {
        return false;
    }

    ImGui::SetNextWindowPos(ImVec2(10, 10));
    ImGui::SetNextWindowSize(ImVec2(280, 0));
    ImGui::Begin("Character Demo", nullptr,
                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);

    glm::vec3 pos = characterController_.GetPosition();
    glm::vec3 vel = characterController_.GetLinearVelocity();
    bool onGround = characterController_.IsOnGround();

    ImGui::Text("Position: %.1f, %.1f, %.1f", pos.x, pos.y, pos.z);
    ImGui::Text("Velocity: %.1f, %.1f, %.1f", vel.x, vel.y, vel.z);
    ImGui::Text("On Ground: %s", onGround ? "Yes" : "No");
    ImGui::Separator();
    ImGui::Text("WASD - Move | Shift - Run");
    ImGui::Text("Space - Jump | Mouse - Look");
    ImGui::Text("ESC - Release Mouse");

    ImGui::SliderFloat("Walk Speed", &walkSpeed_, 1.0f, 10.0f);
    ImGui::SliderFloat("Run Speed", &runSpeed_, 5.0f, 20.0f);
    ImGui::SliderFloat("Camera Dist", &cameraDistance_, 1.0f, 15.0f);

    ImGui::End();
    return true;
}

bool CharacterDemoGameInstance::OverrideRenderCamera(Assets::Camera& OutRenderCamera) const
{
    if (!characterController_.IsValid())
    {
        return false;
    }

    glm::vec3 charPos = characterController_.GetPosition();
    // Camera target: character feet + some height
    glm::vec3 target = charPos + glm::vec3(0.0f, cameraHeight_, 0.0f);

    // Spherical coordinates for camera offset
    float cosP = std::cos(pitch_);
    glm::vec3 cameraOffset(
        -std::sin(yaw_) * cosP * cameraDistance_,
        std::sin(pitch_) * cameraDistance_,
        -std::cos(yaw_) * cosP * cameraDistance_
    );

    glm::vec3 cameraPos = target + cameraOffset;

    // Compute a stable up vector to avoid gimbal lock at extreme pitch.
    // The camera "right" is always horizontal for an orbit camera:
    glm::vec3 right(-std::cos(yaw_), 0.0f, std::sin(yaw_));
    glm::vec3 forward = glm::normalize(target - cameraPos);
    glm::vec3 up = glm::normalize(glm::cross(right, forward));

    OutRenderCamera.ModelView = glm::lookAt(cameraPos, target, up);
    OutRenderCamera.FieldOfView = 60.0f;

    return true;
}

bool CharacterDemoGameInstance::OnKey(SDL_Event& event)
{
    if (event.type != SDL_EVENT_KEY_DOWN && event.type != SDL_EVENT_KEY_UP)
    {
        return false;
    }

    bool pressed = (event.type == SDL_EVENT_KEY_DOWN);
    SDL_Keycode key = event.key.key;

    switch (key)
    {
    case SDLK_W: keyForward_ = pressed; return true;
    case SDLK_S: keyBack_ = pressed; return true;
    case SDLK_A: keyLeft_ = pressed; return true;
    case SDLK_D: keyRight_ = pressed; return true;
    case SDLK_SPACE:
        if (pressed) keyJump_ = true;
        return true;
    case SDLK_LSHIFT:
    case SDLK_RSHIFT:
        keySprint_ = pressed;
        return true;
    case SDLK_ESCAPE:
        if (pressed)
        {
            mouseCaptured_ = !mouseCaptured_;
            resetMouse_ = true;
            SDL_SetWindowRelativeMouseMode(engine_->GetWindow().Handle(), mouseCaptured_);
        }
        return true;
    default:
        return false;
    }
}

bool CharacterDemoGameInstance::OnCursorPosition(double xpos, double ypos)
{
    if (!mouseCaptured_)
    {
        return false;
    }

    if (SDL_GetWindowRelativeMouseMode(engine_->GetWindow().Handle()))
    {
        yaw_ -= static_cast<float>(xpos) * mouseSensitivity_;
        pitch_ += static_cast<float>(ypos) * mouseSensitivity_;
    }
    else
    {
        if (resetMouse_)
        {
            mousePos_ = glm::dvec2(xpos, ypos);
            resetMouse_ = false;
            return true;
        }

        const glm::dvec2 delta = glm::dvec2(xpos, ypos) - mousePos_;
        mousePos_ = glm::dvec2(xpos, ypos);

        yaw_ -= static_cast<float>(delta.x) * mouseSensitivity_;
        pitch_ += static_cast<float>(delta.y) * mouseSensitivity_;
    }

    if (resetMouse_)
    {
        resetMouse_ = false;
        return true;
    }

    // Clamp pitch to avoid camera going exactly vertical
    constexpr float maxPitch = glm::radians(88.0f);
    pitch_ = glm::clamp(pitch_, -maxPitch, maxPitch);

    return true;
}

bool CharacterDemoGameInstance::OnMouseButton(SDL_Event& event)
{
    // Click to capture mouse if not captured
    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && !mouseCaptured_)
    {
        mouseCaptured_ = true;
        resetMouse_ = true;
        SDL_SetWindowRelativeMouseMode(engine_->GetWindow().Handle(), true);
        return true;
    }
    return false;
}

bool CharacterDemoGameInstance::OnScroll(double xoffset, double yoffset)
{
    // Zoom camera in/out
    cameraDistance_ -= static_cast<float>(yoffset) * 0.5f;
    cameraDistance_ = glm::clamp(cameraDistance_, 1.0f, 20.0f);
    return true;
}

void CharacterDemoGameInstance::UpdateCharacterNode()
{
    if (!characterNode_)
    {
        return;
    }

    glm::vec3 pos = characterController_.GetPosition();
    characterNode_->SetTranslation(pos);

    // Rotate character to face movement direction (yaw)
    glm::quat rotation = glm::angleAxis(yaw_, glm::vec3(0.0f, 1.0f, 0.0f));
    characterNode_->SetRotation(rotation);
    characterNode_->RecalcTransform(true);

    engine_->GetScene().MarkDirty();
}
