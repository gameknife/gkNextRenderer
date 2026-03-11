#pragma once

#include <memory>
#include <glm/glm.hpp>
#include <glm/detail/type_quat.hpp>

#include "Vulkan/DebugUtilities.hpp"
#include "Runtime/Subsystems/NextPhysicsTypes.h"

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

class NextPhysics final
{
public:
    VULKAN_NON_COPIABLE(NextPhysics)

    NextPhysics();
    ~NextPhysics();

    void Start();
    void Tick(double DeltaSeconds);
    void Stop();
    
    NextBodyID CreateSphereBody(glm::vec3 position, float radius, NextMotionType motionType);
    NextBodyID CreateBoxBody(glm::vec3 position, glm::vec3 extent, NextMotionType motionType);
    NextBodyID CreateBoxBody(glm::vec3 position, glm::quat rotation, glm::vec3 extent, NextMotionType motionType);
    NextBodyID CreateMeshBody(NextRefConst<NextMeshShapeSettings> meshShapeSettings, glm::vec3 position, glm::quat rotation, glm::vec3 scale, NextMotionType motionType, NextObjectLayer layer);
    NextBodyID CreatePlaneBody(glm::vec3 position, glm::vec3 normal, NextMotionType motionType);
    NextMeshShapeSettings* CreateMeshShape(Assets::Model& model);

    void AddForceToBody(NextBodyID bodyID, const glm::vec3& force);

    void MoveKinematicBody(NextBodyID bodyID, const glm::vec3& position, const glm::quat& rotation, float deltaSeconds);

    FNextPhysicsBody* GetBody(NextBodyID bodyID);
    void RemoveBody(NextBodyID bodyID);

    void SetBodyActive(NextBodyID bodyID, bool active);

    void OnSceneStarted();
    void OnSceneDestroyed();
private:

    NextBodyID AddBodyInternal(FNextPhysicsBody& body, bool optimizeBroadPhase);
    
    std::unique_ptr<FNextPhysicsContext> context_;
    std::unordered_map<NextBodyID, FNextPhysicsBody> bodies_;

    double TimeElapsed {};
    double TimeSimulated {};
};
