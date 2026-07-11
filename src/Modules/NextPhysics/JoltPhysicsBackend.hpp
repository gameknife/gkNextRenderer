#pragma once

#include "Engine/Runtime/Subsystems/NextPhysics.h"

struct FNextPhysicsContext;

namespace Modules::Physics
{
    class FJoltPhysicsBackend final : public NextPhysics
    {
    public:
        FJoltPhysicsBackend();
        ~FJoltPhysicsBackend() override;

        void Start() override;
        void Tick(double deltaSeconds) override;
        void Stop() override;
        void SetPaused(bool paused) override;
        bool IsPaused() const override { return paused_; }

        NextBodyID CreateSphereBody(glm::vec3 position, float radius, NextMotionType motionType) override;
        NextBodyID CreateBoxBody(glm::vec3 position, glm::vec3 extent, NextMotionType motionType) override;
        NextBodyID CreateBoxBody(glm::vec3 position, glm::quat rotation, glm::vec3 extent,
                                 NextMotionType motionType) override;
        NextBodyID CreateMeshBody(const NextMeshShapeHandle& meshShape, glm::vec3 position,
                                  glm::quat rotation, glm::vec3 scale, NextMotionType motionType,
                                  NextObjectLayer layer) override;
        NextBodyID CreatePlaneBody(glm::vec3 position, glm::vec3 normal, NextMotionType motionType) override;
        NextMeshShapeHandle CreateMeshShape(Assets::Model& model) override;

        void AddForceToBody(NextBodyID bodyID, const glm::vec3& force) override;
        void MoveKinematicBody(NextBodyID bodyID, const glm::vec3& position,
                               const glm::quat& rotation, float deltaSeconds) override;
        void SetBodyTransform(NextBodyID bodyID, const glm::vec3& position,
                              const glm::quat& rotation, bool resetVelocity) override;
        void SetBodyVelocity(NextBodyID bodyID, const glm::vec3& linearVelocity,
                             const glm::vec3& angularVelocity) override;
        FNextPhysicsBody* GetBody(NextBodyID bodyID) override;
        FNextPhysicsBodyStats GetBodyStats() const override;
        FNextPhysicsDebugState GetBodyDebugState(NextBodyID bodyID) const override;
        glm::vec4 GetBodyDebugColor(NextBodyID bodyID) const override;
        void RemoveBody(NextBodyID bodyID) override;
        void SetBodyActive(NextBodyID bodyID, bool active) override;
        void DrawDebugBodies() const override;
        void OnSceneStarted() override;
        void OnSceneDestroyed() override;
        std::unique_ptr<INextCharacterControllerBackend> CreateCharacterController(
            const FCharacterControllerSettings& settings) override;
        NextVehicleID CreateWheeledVehicle(const FNextVehicleSettings& settings) override;
        void RemoveVehicle(NextVehicleID vehicleID) override;
        void SetVehicleInput(NextVehicleID vehicleID, const FNextVehicleInput& input) override;
        bool GetVehicleBodyTransform(NextVehicleID vehicleID, glm::vec3& position, glm::quat& rotation) override;
        bool GetVehicleWheelLocalTransform(NextVehicleID vehicleID, int wheel, glm::vec3& position,
                                           glm::quat& rotation) override;
        NextBodyID GetVehicleWheelContactBody(NextVehicleID vehicleID, int wheel) override;
        void SetVehicleWheelFrictionScale(NextVehicleID vehicleID, int wheel, float longitudinal,
                                          float lateral) override;
        void SetVehicleBodyTransform(NextVehicleID vehicleID, const glm::vec3& position,
                                     const glm::quat& rotation) override;
        NextBodyID GetVehicleBodyID(NextVehicleID vehicleID) const override;

    private:
        NextBodyID AddBodyInternal(FNextPhysicsBody& body, bool optimizeBroadPhase);

        std::unique_ptr<FNextPhysicsContext> context_;
        std::unordered_map<NextBodyID, FNextPhysicsBody> bodies_;
        double timeElapsed_{};
        double timeSimulated_{};
        bool paused_ = false;
        struct FVehicleData;
        std::unordered_map<NextVehicleID, std::unique_ptr<FVehicleData>> vehicles_;
        NextVehicleID nextVehicleID_ = 1;
    };
}
