#pragma once

#include <memory>
#include <glm/glm.hpp>

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyID.h>
#include <Jolt/Physics/Body/MotionType.h>

#include "Vulkan/Vulkan.hpp"

struct FNextPhysicsContext;

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
    glm::vec3 velocity;
    ENextBodyShape shape;
    JPH::BodyID bodyID;
};

class NextPhysics final
{
public:
    VULKAN_NON_COPIABLE(NextPhysics)

    NextPhysics();
    ~NextPhysics();

    void Start();
    void Tick();
    void Stop();

    JPH::BodyID CreateBody(ENextBodyShape shape, glm::vec3 position);

    JPH::BodyID CreateSphereBody(glm::vec3 position, float radius, JPH::EMotionType motionType);
    JPH::BodyID CreatePlaneBody(glm::vec3 position, glm::vec3 extent, JPH::EMotionType motionType);

    FNextPhysicsBody& GetBody(JPH::BodyID bodyID) { return bodies_[bodyID]; }
private:

    JPH::BodyID AddBodyInternal(FNextPhysicsBody& body);
    
    std::unique_ptr<FNextPhysicsContext> context_;
    std::unordered_map<JPH::BodyID, FNextPhysicsBody> bodies_;
};
