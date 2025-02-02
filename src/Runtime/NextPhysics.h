#pragma once

#include <memory>
#include <glm/glm.hpp>

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyID.h>

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

    FNextPhysicsBody& CreateBody(ENextBodyShape shape, glm::vec3 position);

private:
    std::unique_ptr<FNextPhysicsContext> context_;
    std::vector<FNextPhysicsBody> bodies_;
};


