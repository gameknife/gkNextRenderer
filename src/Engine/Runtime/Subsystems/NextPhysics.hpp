#pragma once

#include <glm/glm.hpp>
#include <glm/detail/type_quat.hpp>

#include "Engine/Assets/AssetsFwd.hpp"
#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Runtime/Subsystems/NextPhysicsTypes.hpp"

enum class ENextBodyShape
{
    Box,
    Sphere,
    Capsule,
    Cylinder,
    Cone,
    ConvexHull,
    ConvexDecomposition,
    HeightField,
    Mesh,
    SoftBody
};

struct FNextPhysicsBody
{
    glm::vec3 position;
    glm::quat rotation;
    glm::vec3 velocity;
    ENextBodyShape shape;
    NextBodyID bodyID;
    NextMotionType motionType;
};

struct FNextPhysicsDebugState
{
    NextMotionType motionType = NextMotionType::Static;
    NextObjectLayer objectLayer = NextLayers::NON_MOVING;
    bool isActive = false;
    bool isValid = false;
};

struct FNextPhysicsBodyStats
{
    size_t total = 0;
    size_t dynamic = 0;
    size_t kinematic = 0;
    size_t staticBodies = 0;
};

enum class ECharacterGroundState
{
    OnGround,
    OnSteepGround,
    NotSupported,
    InAir,
};

struct FCharacterControllerSettings
{
    float height = 1.75f;
    float radius = 0.3f;
    float maxSlopeAngle = 50.0f;
    float maxStepHeight = 0.35f;
    float mass = 70.0f;
    float maxStrength = 100.0f;
    float padding = 0.02f;
    glm::vec3 initialPosition{0.0f, 1.0f, 0.0f};
};

using NextVehicleID = uint32_t;
constexpr NextVehicleID invalidNextVehicleId = 0;

struct FNextWheelSettings
{
    glm::vec3 position{0.0f};
    float radius = 0.5f;
    float width = 0.36f;
    float suspensionMin = 0.12f;
    float suspensionMax = 0.42f;
    bool steered = false;
    bool driven = false;
    float suspensionFrequency = 1.5f;
    float suspensionDamping = 0.5f;
    float suspensionPreload = 0.0f;
};

struct FNextVehicleEngineSettings
{
    float maxTorque = 0.0f;
    float minRPM = 700.0f;
    float maxRPM = 3000.0f;
    float inertia = 1.0f;
    std::vector<glm::vec2> normalizedTorque{{0.0f, 0.72f}, {0.4f, 1.0f}, {0.72f, 0.82f}, {1.0f, 0.35f}};
};

struct FNextVehicleTransmissionSettings
{
    bool automatic = true;
    std::vector<float> gearRatios{3.8f, 2.4f, 1.55f, 1.0f, 0.72f};
    float reverseRatio = -3.5f;
    float shiftUpRPM = 2600.0f;
    float shiftDownRPM = 1300.0f;
    float clutchStrength = 18.0f;
};

struct FNextVehicleSettings
{
    glm::vec3 chassisHalfExtent{3.1f, 0.55f, 1.05f};
    float mass = 3500.0f;
    float maxEngineTorque = 900.0f;
    float maxSteerAngleDeg = 32.0f;
    glm::vec3 centerOfMassOffset{0.0f, -0.35f, 0.0f};
    float frontAntiRollStiffness = 0.0f;
    float rearAntiRollStiffness = 0.0f;
    FNextVehicleEngineSettings engine;
    FNextVehicleTransmissionSettings transmission;
    std::vector<FNextWheelSettings> wheels;
    glm::vec3 initialPosition{0.0f, 1.2f, 0.0f};
    glm::quat initialRotation{1.0f, 0.0f, 0.0f, 0.0f};
};

struct FNextVehicleInput
{
    float throttle = 0.0f;
    float steer = 0.0f;
    float brake = 0.0f;
    float handbrake = 0.0f;
};

struct FNextVehicleTelemetry
{
    int gear = 0;
    float rpm = 0.0f;
    float maxRPM = 0.0f;
    float engineTorque = 0.0f;
    float forwardSpeed = 0.0f;
    std::vector<float> wheelSlip;
    std::vector<bool> wheelContact;
};

class INextCharacterControllerBackend
{
public:
    virtual ~INextCharacterControllerBackend() = default;
    virtual void Update(const glm::vec3& inputDirection, float speed, bool jump, float deltaSeconds) = 0;
    virtual glm::vec3 GetPosition() const = 0;
    virtual glm::vec3 GetLinearVelocity() const = 0;
    virtual ECharacterGroundState GetGroundState() const = 0;
    virtual bool IsValid() const = 0;
};

class NextPhysics
{
public:
    GK_NON_COPIABLE(NextPhysics)

    NextPhysics() = default;
    virtual ~NextPhysics() = default;

    virtual void Start() = 0;
    virtual void Tick(double deltaSeconds) = 0;
    virtual void Stop() = 0;
    virtual void SetPaused(bool paused) = 0;
    virtual bool IsPaused() const = 0;
    
    virtual NextBodyID CreateSphereBody(glm::vec3 position, float radius, NextMotionType motionType) = 0;
    virtual NextBodyID CreateBoxBody(glm::vec3 position, glm::vec3 extent, NextMotionType motionType) = 0;
    virtual NextBodyID CreateBoxBody(glm::vec3 position, glm::quat rotation, glm::vec3 extent,
                                     NextMotionType motionType) = 0;
    virtual NextBodyID CreateMeshBody(const NextMeshShapeHandle& meshShape, glm::vec3 position,
                                      glm::quat rotation, glm::vec3 scale, NextMotionType motionType,
                                      NextObjectLayer layer) = 0;
    virtual NextBodyID CreatePlaneBody(glm::vec3 position, glm::vec3 normal, NextMotionType motionType) = 0;
    virtual NextMeshShapeHandle CreateMeshShape(Assets::Model& model) = 0;

    virtual void AddForceToBody(NextBodyID bodyID, const glm::vec3& force) = 0;

    virtual void MoveKinematicBody(NextBodyID bodyID, const glm::vec3& position,
                                   const glm::quat& rotation, float deltaSeconds) = 0;
    virtual void SetBodyTransform(NextBodyID bodyID, const glm::vec3& position,
                                  const glm::quat& rotation, bool resetVelocity) = 0;
    virtual void SetBodyVelocity(NextBodyID bodyID, const glm::vec3& linearVelocity,
                                 const glm::vec3& angularVelocity) = 0;

    virtual FNextPhysicsBody* GetBody(NextBodyID bodyID) = 0;
    virtual FNextPhysicsBodyStats GetBodyStats() const = 0;
    virtual FNextPhysicsDebugState GetBodyDebugState(NextBodyID bodyID) const = 0;
    virtual glm::vec4 GetBodyDebugColor(NextBodyID bodyID) const = 0;
    virtual void RemoveBody(NextBodyID bodyID) = 0;

    virtual void SetBodyActive(NextBodyID bodyID, bool active) = 0;
    virtual void DrawDebugBodies() const = 0;

    virtual void OnSceneStarted() = 0;
    virtual void OnSceneDestroyed() = 0;
    virtual std::unique_ptr<INextCharacterControllerBackend> CreateCharacterController(
        const FCharacterControllerSettings& settings) = 0;
    virtual NextVehicleID CreateWheeledVehicle(const FNextVehicleSettings& settings) = 0;
    virtual void RemoveVehicle(NextVehicleID vehicleID) = 0;
    virtual void SetVehicleInput(NextVehicleID vehicleID, const FNextVehicleInput& input) = 0;
    virtual void SetVehicleDiffLock(NextVehicleID vehicleID, bool locked) = 0;
    virtual void SetVehicleAllWheelDrive(NextVehicleID vehicleID, bool enabled) = 0;
    virtual bool GetVehicleTelemetry(NextVehicleID vehicleID, FNextVehicleTelemetry& telemetry) const = 0;
    virtual bool GetVehicleBodyTransform(NextVehicleID vehicleID, glm::vec3& position, glm::quat& rotation) = 0;
    virtual bool GetVehicleWheelLocalTransform(NextVehicleID vehicleID, int wheel, glm::vec3& position,
                                               glm::quat& rotation) = 0;
    virtual NextBodyID GetVehicleWheelContactBody(NextVehicleID vehicleID, int wheel) = 0;
    virtual void SetVehicleWheelFrictionScale(NextVehicleID vehicleID, int wheel, float longitudinal,
                                              float lateral) = 0;
    virtual void SetVehicleBodyTransform(NextVehicleID vehicleID, const glm::vec3& position,
                                         const glm::quat& rotation) = 0;
    virtual NextBodyID GetVehicleBodyID(NextVehicleID vehicleID) const = 0;
};
