#pragma once

#include <memory>
#include <glm/glm.hpp>
#include <glm/detail/type_quat.hpp>

#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Runtime/Subsystems/NextPhysicsTypes.h"

struct FNextPhysicsContext;

namespace Assets
{
    class Model;
}

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

class NextPhysics final
{
public:
    GK_NON_COPIABLE(NextPhysics)

    NextPhysics();
    ~NextPhysics();

    void Start();
    void Tick(double DeltaSeconds);
    void Stop();
    void SetPaused(bool paused);
    bool IsPaused() const { return paused_; }
    
    NextBodyID CreateSphereBody(glm::vec3 position, float radius, NextMotionType motionType);
    NextBodyID CreateBoxBody(glm::vec3 position, glm::vec3 extent, NextMotionType motionType);
    NextBodyID CreateBoxBody(glm::vec3 position, glm::quat rotation, glm::vec3 extent, NextMotionType motionType);
    NextBodyID CreateMeshBody(NextRefConst<NextMeshShapeSettings> meshShapeSettings, glm::vec3 position, glm::quat rotation, glm::vec3 scale, NextMotionType motionType, NextObjectLayer layer);
    NextBodyID CreatePlaneBody(glm::vec3 position, glm::vec3 normal, NextMotionType motionType);
    NextMeshShapeSettings* CreateMeshShape(Assets::Model& model);

    void AddForceToBody(NextBodyID bodyID, const glm::vec3& force);

    void MoveKinematicBody(NextBodyID bodyID, const glm::vec3& position, const glm::quat& rotation, float deltaSeconds);
    void SetBodyTransform(NextBodyID bodyID, const glm::vec3& position, const glm::quat& rotation, bool resetVelocity);
    void SetBodyVelocity(NextBodyID bodyID, const glm::vec3& linearVelocity, const glm::vec3& angularVelocity);

    FNextPhysicsBody* GetBody(NextBodyID bodyID);
    FNextPhysicsBodyStats GetBodyStats() const;
    FNextPhysicsDebugState GetBodyDebugState(NextBodyID bodyID) const;
    glm::vec4 GetBodyDebugColor(NextBodyID bodyID) const;
    void RemoveBody(NextBodyID bodyID);

    void SetBodyActive(NextBodyID bodyID, bool active);
    void DrawDebugBodies() const;

    void OnSceneStarted();
    void OnSceneDestroyed();
private:

    NextBodyID AddBodyInternal(FNextPhysicsBody& body, bool optimizeBroadPhase);
    
    std::unique_ptr<FNextPhysicsContext> context_;
    std::unordered_map<NextBodyID, FNextPhysicsBody> bodies_;

    double TimeElapsed {};
    double TimeSimulated {};
    bool paused_ = false;
};
