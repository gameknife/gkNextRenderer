#include "Modules/NextPhysics/JoltPhysicsBackend.hpp"

#include "Engine/Common/CoreMinimal.hpp"

// The Jolt headers don't include Jolt.h. Always include Jolt.h before including any other Jolt header.
// You can use Jolt.h in your precompiled header to speed up compilation.
#include <Jolt/Jolt.h>

// Jolt includes
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/OffsetCenterOfMassShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/PlaneShape.h>
#include <Jolt/Physics/Collision/PhysicsMaterialSimple.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyActivationListener.h>
#include <Jolt/Physics/Collision/Shape/ScaledShape.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Vehicle/VehicleConstraint.h>
#include <Jolt/Physics/Vehicle/WheeledVehicleController.h>
#include <Jolt/Physics/Vehicle/VehicleCollisionTester.h>

// STL includes
#include <iostream>
#include <cstdarg>
#include <thread>
#include <glm/ext.hpp>

#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/Subsystems/TaskCoordinator.hpp"
#include "Engine/Runtime/Utilities/NextEngineHelper.hpp"
#include "Engine/Assets/Core/Model.hpp"


// Disable common warnings triggered by Jolt, you can use JPH_SUPPRESS_WARNING_PUSH / JPH_SUPPRESS_WARNING_POP to store and restore the warning state
JPH_SUPPRESS_WARNINGS

// All Jolt symbols are in the JPH namespace
using namespace JPH;

// If you want your code to compile using single or double precision write 0.0_r to get a Real value that compiles to double or float depending if JPH_DOUBLE_PRECISION is set or not.
using namespace JPH::literals;
using Modules::Physics::FJoltPhysicsBackend;

struct FJoltPhysicsBackend::FVehicleData
{
    NextBodyID bodyID;
    Ref<VehicleConstraint> constraint;
    std::vector<std::pair<float, float>> frictionScales;
    std::vector<bool> differentialDriven;
    float lastThrottle = 0.0f;
};

namespace
{
    BodyID ToJoltBodyID(NextBodyID bodyId)
    {
        return BodyID(bodyId.Value());
    }

    NextBodyID FromJoltBodyID(const BodyID& bodyId)
    {
        return NextBodyID(bodyId.GetIndexAndSequenceNumber());
    }

    EMotionType ToJoltMotionType(NextMotionType motionType)
    {
        switch (motionType)
        {
        case NextMotionType::Static: return EMotionType::Static;
        case NextMotionType::Kinematic: return EMotionType::Kinematic;
        case NextMotionType::Dynamic: return EMotionType::Dynamic;
        }
        return EMotionType::Static;
    }

    NextMotionType FromJoltMotionType(EMotionType motionType)
    {
        switch (motionType)
        {
        case EMotionType::Static: return NextMotionType::Static;
        case EMotionType::Kinematic: return NextMotionType::Kinematic;
        case EMotionType::Dynamic: return NextMotionType::Dynamic;
        }
        return NextMotionType::Static;
    }

    class FJoltMeshShape final : public NextMeshShape
    {
    public:
        explicit FJoltMeshShape(MeshShapeSettings* settings) : settings_(settings) {}

        RefConst<MeshShapeSettings> settings_;
    };

    constexpr uint kMaxBodies = 65536;
    constexpr uint kMaxBodyPairs = 131072;
    constexpr uint kMaxContactConstraints = 32768;
    constexpr uint kTempAllocatorSize = 16 * 1024 * 1024;
    constexpr uint kVelocitySolverSteps = 10;
    constexpr uint kPositionSolverSteps = 2;
    constexpr float kSleepVelocityThreshold = 0.05f;
    constexpr float kTimeBeforeSleep = 0.35f;
    constexpr double kFixedDeltaTime = 1.0 / 60.0;
    constexpr int kMaxCollisionStepsPerTick = 4;
    constexpr uint32_t kBroadPhaseOptimizeBatchSize = 32;
    constexpr float kDynamicSphereFriction = 0.8f;
    constexpr float kDynamicSphereLinearDamping = 0.35f;
    constexpr float kDynamicSphereAngularDamping = 0.5f;
    constexpr float kDynamicSphereInertiaMultiplier = 2.0f;
    constexpr float kDynamicBoxFriction = 0.22f;
    constexpr float kDynamicBoxGravityFactor = 1.15f;
    constexpr float kDynamicBoxLinearDamping = 0.025f;
    constexpr float kDynamicBoxAngularDamping = 0.02f;
    constexpr float kDynamicBoxInertiaMultiplier = 1.0f;
    constexpr uint kMaxJobThreads = 8;

    float ComputeBoxConvexRadius(const Vec3& halfExtent)
    {
        const float minHE = std::min({halfExtent.GetX(), halfExtent.GetY(), halfExtent.GetZ()});
        return std::min(cDefaultConvexRadius, std::max(0.00025f, minHE * 0.1f));
    }

    void ConfigureDynamicBoxSettings(BodyCreationSettings& settings)
    {
        settings.mFriction = kDynamicBoxFriction;
        settings.mGravityFactor = kDynamicBoxGravityFactor;
        settings.mLinearDamping = kDynamicBoxLinearDamping;
        settings.mAngularDamping = kDynamicBoxAngularDamping;
        settings.mInertiaMultiplier = kDynamicBoxInertiaMultiplier;
        settings.mMotionQuality = EMotionQuality::LinearCast;
    }

    void ConfigureDynamicSphereSettings(BodyCreationSettings& settings)
    {
        settings.mFriction = kDynamicSphereFriction;
        settings.mLinearDamping = kDynamicSphereLinearDamping;
        settings.mAngularDamping = kDynamicSphereAngularDamping;
        settings.mInertiaMultiplier = kDynamicSphereInertiaMultiplier;
        settings.mMotionQuality = EMotionQuality::LinearCast;
    }

    void WakeDynamicBody(BodyInterface& bodyInterface, const BodyID& bodyId)
    {
        if (bodyId.IsInvalid() || bodyInterface.GetMotionType(bodyId) == EMotionType::Static)
        {
            return;
        }

        bodyInterface.ActivateBody(bodyId);
        bodyInterface.ResetSleepTimer(bodyId);
    }

    void DrawAuxObb(const glm::mat4& worldTransform, const glm::vec3& localMin, const glm::vec3& localMax,
                    const glm::vec4& color, float size)
    {
        const glm::vec3 corners[8] = {
            {localMin.x, localMin.y, localMin.z},
            {localMax.x, localMin.y, localMin.z},
            {localMin.x, localMax.y, localMin.z},
            {localMax.x, localMax.y, localMin.z},
            {localMin.x, localMin.y, localMax.z},
            {localMax.x, localMin.y, localMax.z},
            {localMin.x, localMax.y, localMax.z},
            {localMax.x, localMax.y, localMax.z}
        };

        glm::vec3 transformed[8];
        for (int i = 0; i < 8; ++i)
        {
            transformed[i] = glm::vec3(worldTransform * glm::vec4(corners[i], 1.0f));
        }

        Runtime::EngineHelper::DrawAuxLine(transformed[0], transformed[1], color, size);
        Runtime::EngineHelper::DrawAuxLine(transformed[1], transformed[3], color, size);
        Runtime::EngineHelper::DrawAuxLine(transformed[3], transformed[2], color, size);
        Runtime::EngineHelper::DrawAuxLine(transformed[2], transformed[0], color, size);
        Runtime::EngineHelper::DrawAuxLine(transformed[4], transformed[5], color, size);
        Runtime::EngineHelper::DrawAuxLine(transformed[5], transformed[7], color, size);
        Runtime::EngineHelper::DrawAuxLine(transformed[7], transformed[6], color, size);
        Runtime::EngineHelper::DrawAuxLine(transformed[6], transformed[4], color, size);
        Runtime::EngineHelper::DrawAuxLine(transformed[0], transformed[4], color, size);
        Runtime::EngineHelper::DrawAuxLine(transformed[1], transformed[5], color, size);
        Runtime::EngineHelper::DrawAuxLine(transformed[2], transformed[6], color, size);
        Runtime::EngineHelper::DrawAuxLine(transformed[3], transformed[7], color, size);
    }

    glm::vec3 ToGlmVec3(const Vec3& value)
    {
        return glm::vec3(value.GetX(), value.GetY(), value.GetZ());
    }

    glm::mat4 ToGlmMat4(const Mat44& value)
    {
        const Vec4 col0 = value.GetColumn4(0);
        const Vec4 col1 = value.GetColumn4(1);
        const Vec4 col2 = value.GetColumn4(2);
        const Vec4 col3 = value.GetColumn4(3);
        return glm::mat4(
            glm::vec4(col0.GetX(), col0.GetY(), col0.GetZ(), col0.GetW()),
            glm::vec4(col1.GetX(), col1.GetY(), col1.GetZ(), col1.GetW()),
            glm::vec4(col2.GetX(), col2.GetY(), col2.GetZ(), col2.GetW()),
            glm::vec4(col3.GetX(), col3.GetY(), col3.GetZ(), col3.GetW()));
    }

    glm::vec4 SelectDebugBodyColor(NextMotionType motionType, NextObjectLayer objectLayer, bool isActive, bool isValid)
    {
        if (!isValid)
        {
            return glm::vec4(1.0f, 0.0f, 1.0f, 1.0f);
        }

        if (objectLayer == NextLayers::HIDDEN)
        {
            return glm::vec4(1.0f, 0.2f, 0.2f, 1.0f);
        }

        switch (motionType)
        {
        case NextMotionType::Static:
            return glm::vec4(0.35f, 0.65f, 1.0f, 1.0f);
        case NextMotionType::Kinematic:
            return glm::vec4(0.2f, 0.95f, 0.95f, 1.0f);
        case NextMotionType::Dynamic:
            return isActive
                ? glm::vec4(0.25f, 1.0f, 0.35f, 1.0f)
                : glm::vec4(1.0f, 0.75f, 0.2f, 1.0f);
        default:
            return glm::vec4(0.9f, 0.9f, 0.9f, 1.0f);
        }
    }
}

// Callback for traces, connect this to your own trace function if you have one
static void TraceImpl(const char *inFMT, ...)
{
    // Format the message
    va_list list;
    va_start(list, inFMT);
    char buffer[1024];
    vsnprintf(buffer, sizeof(buffer), inFMT, list);
    va_end(list);

    // Print to the TTY
    std::cout << buffer << std::endl;
}

#ifdef JPH_ENABLE_ASSERTS

// Callback for asserts, connect this to your own assert handler if you have one
static bool AssertFailedImpl(const char *inExpression, const char *inMessage, const char *inFile, uint inLine)
{
    // Print to the TTY
    std::cout << inFile << ":" << inLine << ": (" << inExpression << ") " << (inMessage != nullptr? inMessage : "") << std::endl;

    // Breakpoint
    return true;
};

#endif // JPH_ENABLE_ASSERTS

// Layer that objects can be in, determines which other objects it can collide with
// Typically you at least want to have 1 layer for moving bodies and 1 layer for static bodies, but you can have more
// layers if you want. E.g. you could have a layer for high detail collision (which is not used by the physics simulation
// but only if you do collision testing).

/// Class that determines if two object layers can collide
class ObjectLayerPairFilterImpl : public ObjectLayerPairFilter
{
public:
    virtual bool ShouldCollide(ObjectLayer inObject1, ObjectLayer inObject2) const override
    {
        switch (inObject1)
        {
        case NextLayers::NON_MOVING:
            return inObject2 == NextLayers::MOVING; // Non moving only collides with moving
        case NextLayers::MOVING:
            return inObject2 != NextLayers::HIDDEN; // Moving collides with everything
        case NextLayers::HIDDEN:
            return false; // Triggers only collide with moving
        default:
            JPH_ASSERT(false);
            return false;
        }
    }
};

// Each broadphase layer results in a separate bounding volume tree in the broad phase. You at least want to have
// a layer for non-moving and moving objects to avoid having to update a tree full of static objects every frame.
// You can have a 1-on-1 mapping between object layers and broadphase layers (like in this case) but if you have
// many object layers you'll be creating many broad phase trees, which is not efficient. If you want to fine tune
// your broadphase layers define JPH_TRACK_BROADPHASE_STATS and look at the stats reported on the TTY.
namespace BroadPhaseLayers
{
    static constexpr BroadPhaseLayer nonMoving(0);
    static constexpr BroadPhaseLayer moving(1);
    static constexpr BroadPhaseLayer hidden(2);
    static constexpr uint numLayers(3);
};

// BroadPhaseLayerInterface implementation
// This defines a mapping between object and broadphase layers.
class BPLayerInterfaceImpl final : public BroadPhaseLayerInterface
{
public:
                                    BPLayerInterfaceImpl()
    {
        // Create a mapping table from object to broad phase layer
        mObjectToBroadPhase_[NextLayers::NON_MOVING] = BroadPhaseLayers::nonMoving;
        mObjectToBroadPhase_[NextLayers::MOVING] = BroadPhaseLayers::moving;
        mObjectToBroadPhase_[NextLayers::HIDDEN] = BroadPhaseLayers::hidden;
    }

    virtual uint GetNumBroadPhaseLayers() const override
    {
        return BroadPhaseLayers::numLayers;
    }

    virtual BroadPhaseLayer GetBroadPhaseLayer(ObjectLayer inLayer) const override
    {
        JPH_ASSERT(inLayer < NextLayers::NUM_LAYERS);
        return mObjectToBroadPhase_[inLayer];
    }

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    virtual const char * GetBroadPhaseLayerName(BroadPhaseLayer inLayer) const override
    {
        switch ((BroadPhaseLayer::Type)inLayer)
        {
        case (BroadPhaseLayer::Type)BroadPhaseLayers::NON_MOVING: return "NON_MOVING";
        case (BroadPhaseLayer::Type)BroadPhaseLayers::MOVING: return "MOVING";
        case (BroadPhaseLayer::Type)BroadPhaseLayers::HIDDEN: return "HIDDEN";
        default: JPH_ASSERT(false); return "INVALID";
        }
    }
#endif // JPH_EXTERNAL_PROFILE || JPH_PROFILE_ENABLED

private:
    BroadPhaseLayer mObjectToBroadPhase_[NextLayers::NUM_LAYERS];
};

/// Class that determines if an object layer can collide with a broadphase layer
class ObjectVsBroadPhaseLayerFilterImpl : public ObjectVsBroadPhaseLayerFilter
{
public:
    virtual bool ShouldCollide(ObjectLayer inLayer1, BroadPhaseLayer inLayer2) const override
    {
        switch (inLayer1)
        {
        case NextLayers::NON_MOVING:
            return inLayer2 == BroadPhaseLayers::moving;
        case NextLayers::MOVING:
            return inLayer2 != BroadPhaseLayers::hidden;
        case NextLayers::HIDDEN:
            return false;
        default:
            JPH_ASSERT(false);
            return false;
        }
    }
};

// An example contact listener
class MyContactListener : public ContactListener
{
public:
    // See: ContactListener
    virtual ValidateResult OnContactValidate(const Body &inBody1, const Body &inBody2, RVec3Arg inBaseOffset, const CollideShapeResult &inCollisionResult) override
    {
        //cout << "Contact validate callback" << endl;

        // Allows you to ignore a contact before it is created (using layers to not make objects collide is cheaper!)
        return ValidateResult::AcceptAllContactsForThisBodyPair;
    }

    virtual void OnContactAdded(const Body &inBody1, const Body &inBody2, const ContactManifold &inManifold, ContactSettings &ioSettings) override
    {
        //cout << "A contact was added" << endl;
    }

    virtual void OnContactPersisted(const Body &inBody1, const Body &inBody2, const ContactManifold &inManifold, ContactSettings &ioSettings) override
    {
        //cout << "A contact was persisted" << endl;
    }

    virtual void OnContactRemoved(const SubShapeIDPair &inSubShapePair) override
    {
        //cout << "A contact was removed" << endl;
    }
};

// An example activation listener
class MyBodyActivationListener : public BodyActivationListener
{
public:
    virtual void OnBodyActivated(const BodyID &inBodyID, uint64 inBodyUserData) override
    {
        //cout << "A body got activated" << endl;
    }

    virtual void OnBodyDeactivated(const BodyID &inBodyID, uint64 inBodyUserData) override
    {
        //cout << "A body went to sleep" << endl;
    }
};

struct FNextPhysicsContext
{
    FNextPhysicsContext():
        tempAllocator(kTempAllocatorSize),
        jobSystem(cMaxPhysicsJobs, cMaxPhysicsBarriers, std::min(kMaxJobThreads, std::thread::hardware_concurrency() - 1))
    {
        // This determines how many mutexes to allocate to protect rigid bodies from concurrent access. Set it to 0 for the default settings.
        const uint cNumBodyMutexes = 0;

        // Dense piles can produce several contacts per body. If either of these buffers fills,
        // Jolt drops contacts and dynamic bodies can interpenetrate or fall through the world.
        physicsSystem.Init(kMaxBodies, cNumBodyMutexes, kMaxBodyPairs, kMaxContactConstraints,
                           broadPhaseLayerInterface, objectVsBroadphaseLayerFilter, objectVsObjectLayerFilter);

        PhysicsSettings settings = physicsSystem.GetPhysicsSettings();
        settings.mNumVelocitySteps = kVelocitySolverSteps;
        settings.mNumPositionSteps = kPositionSolverSteps;
        settings.mPointVelocitySleepThreshold = kSleepVelocityThreshold;
        settings.mTimeBeforeSleep = kTimeBeforeSleep;
        physicsSystem.SetPhysicsSettings(settings);
        
        
        physicsSystem.SetBodyActivationListener(&bodyActivationListener);
        physicsSystem.SetContactListener(&contactListener);
    }

    ~FNextPhysicsContext()
    {
        
    }
    // Large contact islands can exceed the preallocated block. Falling back to malloc is slower
    // than the normal path but avoids turning a temporary capacity spike into a fatal allocation.
    TempAllocatorImplWithMallocFallback tempAllocator;

    // We need a job system that will execute physics jobs on multiple threads. Typically
    // you would implement the JobSystem interface yourself and let Jolt Physics run on top
    // of your own job scheduler. JobSystemThreadPool is an example implementation.
    JobSystemThreadPool jobSystem;

    // Create mapping table from object layer to broadphase layer
    // Note: As this is an interface, PhysicsSystem will take a reference to this so this instance needs to stay alive!
    // Also have a look at BroadPhaseLayerInterfaceTable or BroadPhaseLayerInterfaceMask for a simpler interface.
    BPLayerInterfaceImpl broadPhaseLayerInterface;

    // Create class that filters object vs broadphase layers
    // Note: As this is an interface, PhysicsSystem will take a reference to this so this instance needs to stay alive!
    // Also have a look at ObjectVsBroadPhaseLayerFilterTable or ObjectVsBroadPhaseLayerFilterMask for a simpler interface.
    ObjectVsBroadPhaseLayerFilterImpl objectVsBroadphaseLayerFilter;

    // Create class that filters object vs object layers
    // Note: As this is an interface, PhysicsSystem will take a reference to this so this instance needs to stay alive!
    // Also have a look at ObjectLayerPairFilterTable or ObjectLayerPairFilterMask for a simpler interface.
    ObjectLayerPairFilterImpl objectVsObjectLayerFilter;
    
    PhysicsSystem physicsSystem;

    // A body activation listener gets notified when bodies activate and go to sleep
    // Note that this is called from a job so whatever you do here needs to be thread safe.
    // Registering one is entirely optional.
    MyBodyActivationListener bodyActivationListener;

    // A contact listener gets notified when bodies (are about to) collide, and when they separate again.
    // Note that this is called from a job so whatever you do here needs to be thread safe.
    // Registering one is entirely optional.
    MyContactListener contactListener;
};

FJoltPhysicsBackend::FJoltPhysicsBackend()
{
    
}

FJoltPhysicsBackend::~FJoltPhysicsBackend()
{
    
}

void FJoltPhysicsBackend::Start()
{
    // Register allocation hook. In this example we'll just let Jolt use malloc / free but you can override these if you want (see Memory.h).
    // This needs to be done before any other Jolt function is called.
    RegisterDefaultAllocator();

    // Install trace and assert callbacks
    Trace = TraceImpl;
    JPH_IF_ENABLE_ASSERTS(AssertFailed = AssertFailedImpl;)

    // Create a factory, this class is responsible for creating instances of classes based on their name or hash and is mainly used for deserialization of saved data.
    // It is not directly used in this example but still required.
    Factory::sInstance = new Factory();

    // Register all physics types with the factory and install their collision handlers with the CollisionDispatch class.
    // If you have your own custom shape types you probably need to register their handlers with the CollisionDispatch before calling this function.
    // If you implement your own default material (PhysicsMaterial::sDefault) make sure to initialize it before this function or else this function will create one for you.
    RegisterTypes();

    context_.reset(new FNextPhysicsContext());

    //context_->physics_system.SetGravity(Vec3(0,-9.8f,0));
    // The main way to interact with the bodies in the physics system is through the body interface. There is a locking and a non-locking
    // variant of this. We're going to use the locking version (even though we're not planning to access bodies from multiple threads)
    BodyInterface &bodyInterface = context_->physicsSystem.GetBodyInterface();

    //CreateBody(ENextBodyShape::Box, glm::vec3(0.0f, -1.0f, 0.0f));
    //CreateBody(ENextBodyShape::Sphere, glm::vec3(0.0f, 5.0f, 0.0f));

    
    // We simulate the physics world in discrete time steps. 60 Hz is a good rate to update the physics system.
    

    // Optional step: Before starting the physics simulation you can optimize the broad phase. This improves collision detection performance (it's pointless here because we only have 2 bodies).
    // You should definitely not call this every frame or when e.g. streaming in a new level section as it is an expensive operation.
    // Instead insert all new objects in batches instead of 1 at a time to keep the broad phase efficient.
}

void FJoltPhysicsBackend::KickTick(double deltaSeconds)
{
    CompleteTick();
    if (paused_ || deltaSeconds <= 0.0)
    {
        return;
    }

    // Bound the backlog so a hitch cannot create a permanent spiral of death. The remainder
    // below one fixed step is preserved to keep normal render/physics rates in sync.
    const double maxAccumulatedTime = kFixedDeltaTime * kMaxCollisionStepsPerTick;
    accumulatedTime_ = std::min(accumulatedTime_ + deltaSeconds, maxAccumulatedTime);
    const int collisionSteps = std::min(
        kMaxCollisionStepsPerTick,
        static_cast<int>(std::floor(accumulatedTime_ / kFixedDeltaTime)));
    if (collisionSteps == 0)
    {
        return;
    }

    const double simulatedDelta = kFixedDeltaTime * collisionSteps;
    accumulatedTime_ -= simulatedDelta;

    pendingDynamicBodyIds_.clear();
    pendingDynamicBodyIds_.reserve(bodies_.size());
    for (const auto& [bodyId, body] : bodies_)
    {
        if (body.motionType == NextMotionType::Dynamic)
        {
            pendingDynamicBodyIds_.push_back(bodyId);
        }
    }

    const bool optimizeBroadPhase = pendingBodyAddCount_ >= kBroadPhaseOptimizeBatchSize;
    const bool publishSleepingTransition = previousActiveRigidBodyCount_ > 0;
    pendingBodyAddCount_ = 0;
    updatePending_ = true;

    Tasks::TaskCoordinator::GetInstance()->AddNamedTask(
        Tasks::ENamedTaskThread::PHYSICS,
        [this, simulatedDelta, collisionSteps, optimizeBroadPhase,
         publishSleepingTransition](Tasks::ResTask&)
        {
            if (optimizeBroadPhase)
            {
                context_->physicsSystem.OptimizeBroadPhase();
            }

            // Jolt waits for its internal jobs, but this outer task remains asynchronous to the frame.
            const EPhysicsUpdateError updateError =
                context_->physicsSystem.Update(static_cast<float>(simulatedDelta), collisionSteps,
                                               &context_->tempAllocator, &context_->jobSystem);
            pendingUpdate_.errorMask = static_cast<uint32_t>(updateError);
            pendingUpdate_.activeBodyCount =
                context_->physicsSystem.GetNumActiveBodies(EBodyType::RigidBody);
            pendingUpdate_.collisionSteps = static_cast<uint32_t>(collisionSteps);
            pendingUpdate_.bodies.clear();

            if (pendingUpdate_.activeBodyCount > 0 || publishSleepingTransition)
            {
                BodyInterface& bodyInterface = context_->physicsSystem.GetBodyInterface();
                pendingUpdate_.bodies.reserve(pendingDynamicBodyIds_.size());
                for (NextBodyID bodyId : pendingDynamicBodyIds_)
                {
                    const BodyID joltBodyId = ToJoltBodyID(bodyId);
                    RVec3 position;
                    Quat rotation;
                    bodyInterface.GetPositionAndRotation(joltBodyId, position, rotation);
                    const Vec3 velocity = bodyInterface.GetLinearVelocity(joltBodyId);
                    pendingUpdate_.bodies.push_back({
                        bodyId,
                        glm::vec3(position.GetX(), position.GetY(), position.GetZ()),
                        glm::quat(rotation.GetW(), rotation.GetX(), rotation.GetY(), rotation.GetZ()),
                        glm::vec3(velocity.GetX(), velocity.GetY(), velocity.GetZ())});
                }
            }
        });
}

void FJoltPhysicsBackend::CompleteTick()
{
    if (!updatePending_)
    {
        return;
    }

    Tasks::TaskCoordinator::GetInstance()->WaitForNamedTask(Tasks::ENamedTaskThread::PHYSICS);
    updatePending_ = false;
    ++updateCallCount_;
    simulatedStepCount_ += pendingUpdate_.collisionSteps;

    for (const FPendingBodyState& state : pendingUpdate_.bodies)
    {
        if (auto it = bodies_.find(state.id); it != bodies_.end())
        {
            it->second.position = state.position;
            it->second.rotation = state.rotation;
            it->second.velocity = state.velocity;
        }
    }

    if (pendingUpdate_.errorMask != 0 && pendingUpdate_.errorMask != lastUpdateErrorMask_)
    {
        SPDLOG_ERROR("[Physics] Jolt update dropped contacts (error mask 0x{:x}, active rigid bodies {}, "
                     "body-pair capacity {}, contact capacity {}). Increase physics capacities or reduce overlap.",
                     pendingUpdate_.errorMask, pendingUpdate_.activeBodyCount,
                     kMaxBodyPairs, kMaxContactConstraints);
    }
    else if (pendingUpdate_.errorMask == 0 && lastUpdateErrorMask_ != 0)
    {
        SPDLOG_INFO("[Physics] Jolt update capacity recovered");
    }
    lastUpdateErrorMask_ = pendingUpdate_.errorMask;
    previousActiveRigidBodyCount_ = pendingUpdate_.activeBodyCount;

    if (pendingUpdate_.activeBodyCount > 0)
    {
        NextEngine::GetInstance()->GetScene().MarkTransformDirty();
    }
}

void FJoltPhysicsBackend::SetPaused(bool paused)
{
    CompleteTick();
    paused_ = paused;
}

FNextPhysicsBodyStats FJoltPhysicsBackend::GetBodyStats() const
{
    FNextPhysicsBodyStats stats{};
    stats.total = bodies_.size();
    stats.updateCalls = updateCallCount_;
    stats.simulatedSteps = simulatedStepCount_;

    for (const auto& [bodyId, body] : bodies_)
    {
        (void)bodyId;

        switch (body.motionType)
        {
        case NextMotionType::Dynamic:
            ++stats.dynamic;
            break;
        case NextMotionType::Kinematic:
            ++stats.kinematic;
            break;
        case NextMotionType::Static:
            ++stats.staticBodies;
            break;
        default:
            break;
        }
    }

    return stats;
}

void FJoltPhysicsBackend::Stop()
{
    CompleteTick();
    OnSceneDestroyed();
    
    // Unregisters all types with the factory and cleans up the default material
    UnregisterTypes();

    // Destroy the factory
    delete Factory::sInstance;
    Factory::sInstance = nullptr;

    context_.reset();
}

NextBodyID FJoltPhysicsBackend::AddBodyInternal(FNextPhysicsBody& body)
{
    bodies_[body.bodyID] = body;
    ++pendingBodyAddCount_;
    return body.bodyID;
}

NextBodyID FJoltPhysicsBackend::CreateSphereBody(glm::vec3 position, float radius, NextMotionType motionType)
{
    CompleteTick();
    BodyInterface &bodyInterface = context_->physicsSystem.GetBodyInterface();

    // Now create a dynamic body to bounce on the floor
    // Note that this uses the shorthand version of creating and adding a body to the world
    BodyCreationSettings sphereSettings(new SphereShape(radius), RVec3(position.x, position.y, position.z),
                                        Quat::sIdentity(), ToJoltMotionType(motionType), NextLayers::MOVING);
    if (motionType == NextMotionType::Dynamic)
    {
        ConfigureDynamicSphereSettings(sphereSettings);
    }
    else
    {
        sphereSettings.mFriction = kDynamicSphereFriction;
    }
    BodyID bodyId = bodyInterface.CreateAndAddBody(sphereSettings, EActivation::Activate);

    // Now you can interact with the dynamic body, in this case we're going to give it a velocity.
    // (note that if we had used CreateBody then we could have set the velocity straight on the body before adding it to the physics system)
    //body_interface.SetLinearVelocity(body_id, Vec3(0.0f, -5.0f, 0.0f));
    FNextPhysicsBody body { position, glm::quat(1,0,0,0), glm::vec3(0.0f), ENextBodyShape::Sphere,
                            FromJoltBodyID(bodyId), motionType };
    return AddBodyInternal(body);
}

NextBodyID FJoltPhysicsBackend::CreateBoxBody(glm::vec3 position, glm::vec3 extent, NextMotionType motionType)
{
    return CreateBoxBody(position, glm::quat(1, 0, 0, 0), extent, motionType);
}

NextBodyID FJoltPhysicsBackend::CreateBoxBody(glm::vec3 position, glm::quat rotation, glm::vec3 extent,
                                             NextMotionType motionType)
{
    CompleteTick();
    BodyInterface &bodyInterface = context_->physicsSystem.GetBodyInterface();
    BodyID bodyId(-1);

    Vec3 halfExtent(extent.x * 0.5f, extent.y * 0.5f, extent.z * 0.5f);
    float convexRadius = ComputeBoxConvexRadius(halfExtent);

    Quat joltRot(rotation.x, rotation.y, rotation.z, rotation.w);
    BodyCreationSettings settings(new BoxShape(halfExtent, convexRadius), RVec3(position.x, position.y, position.z),
                                  joltRot, ToJoltMotionType(motionType), NextLayers::MOVING);
    if (motionType == NextMotionType::Dynamic)
    {
        ConfigureDynamicBoxSettings(settings);
    }
    else
    {
        settings.mFriction = 0.5f;
    }
    bodyId = bodyInterface.CreateAndAddBody(settings, EActivation::Activate);
    WakeDynamicBody(bodyInterface, bodyId);

    FNextPhysicsBody body { position, rotation, glm::vec3(0.0f), ENextBodyShape::Box,
                            FromJoltBodyID(bodyId), motionType };
    return AddBodyInternal(body);
}

NextBodyID FJoltPhysicsBackend::CreateMeshBody(const NextMeshShapeHandle& meshShape, glm::vec3 position,
                                              glm::quat rotation, glm::vec3 scale,
                                              NextMotionType motionType, NextObjectLayer layer)
{
    CompleteTick();
    BodyInterface &bodyInterface = context_->physicsSystem.GetBodyInterface();
    BodyID bodyId(-1);
    const auto joltMeshShape = std::dynamic_pointer_cast<const FJoltMeshShape>(meshShape);
    if (!joltMeshShape || !joltMeshShape->settings_)
    {
        return {};
    }
    const RefConst<MeshShapeSettings>& meshShapeSettings = joltMeshShape->settings_;

    // Cooking can drop every triangle (e.g. centimeter-scale meshes fall below Jolt's
    // degenerate-triangle threshold). Feeding such settings to CreateAndAddBody trips a
    // fatal assert, so validate here and skip the physics body instead.
    if (const Shape::ShapeResult cooked = meshShapeSettings->Create(); cooked.HasError())
    {
        SPDLOG_WARN("[Physics] mesh shape cooking failed ({}); skipping collision body",
                    cooked.GetError().c_str());
        return {};
    }
    
    const float epsilon = 0.001f;
    bool isUniformScale = (glm::abs(scale.x - 1.0f) < epsilon && 
                          glm::abs(scale.y - 1.0f) < epsilon && 
                          glm::abs(scale.z - 1.0f) < epsilon);

    BodyCreationSettings bodyCreation = isUniformScale ? BodyCreationSettings(meshShapeSettings,
    Vec3(position.x, position.y, position.z),Quat(rotation.x, rotation.y, rotation.z, rotation.w),
    ToJoltMotionType(motionType), layer): BodyCreationSettings(new ScaledShapeSettings(meshShapeSettings, Vec3(scale.x, scale.y, scale.z)),
        Vec3(position.x, position.y, position.z), Quat(rotation.x, rotation.y, rotation.z, rotation.w),
        ToJoltMotionType(motionType), layer);

    //bodyCreation.mRestitution = 0.05f;
    bodyCreation.mFriction = 0.5f;
    bodyCreation.mOverrideMassProperties = EOverrideMassProperties::CalculateInertia;
    bodyCreation.mMassPropertiesOverride.mMass = 1.0f;
    
    bodyId = bodyInterface.CreateAndAddBody(bodyCreation, EActivation::Activate);

    FNextPhysicsBody body { position, rotation, glm::vec3(0.0f), ENextBodyShape::Mesh,
                            FromJoltBodyID(bodyId), motionType };
    return AddBodyInternal(body);
}

NextBodyID FJoltPhysicsBackend::CreatePlaneBody(glm::vec3 position, glm::vec3 normal, NextMotionType motionType)
{
    CompleteTick();
    BodyInterface &bodyInterface = context_->physicsSystem.GetBodyInterface();
    BodyID bodyId(-1);

    // Next we can create a rigid body to serve as the floor, we make a large box
    // Create the settings for the collision volume (the shape).
    // Note that for simple shapes (like boxes) you can also directly construct a BoxShape.
    PlaneShapeSettings planeShapeSettings(Plane::sFromPointAndNormal(Vec3(position.x, position.y, position.z), Vec3(normal.x, normal.y, normal.z)));
    planeShapeSettings.SetEmbedded(); // A ref counted object on the stack (base class RefTarget) should be marked as such to prevent it from being freed when its reference count goes to 0.

    // Create the shape
    ShapeSettings::ShapeResult floorShapeResult = planeShapeSettings.Create();
    ShapeRefC floorShape = floorShapeResult.Get(); // We don't expect an error here, but you can check floor_shape_result for HasError() / GetError()

    // Create the settings for the body itself. Note that here you can also set other properties like the restitution / friction.
    BodyCreationSettings floorSettings(floorShape, RVec3(0,0,0), Quat::sIdentity(), EMotionType::Static, NextLayers::NON_MOVING);
    //floor_settings.mRestitution = 0.05f;
    floorSettings.mFriction = 0.5f;
    // Create the actual rigid body
    bodyId = bodyInterface.CreateAndAddBody(floorSettings, EActivation::DontActivate);

    FNextPhysicsBody body { position, glm::quat(1,0,0,0), glm::vec3(0.0f), ENextBodyShape::Box,
                            FromJoltBodyID(bodyId), NextMotionType::Static };
    return AddBodyInternal(body);
}

NextMeshShapeHandle FJoltPhysicsBackend::CreateMeshShape(Assets::Model& model)
{
    VertexList inVertices;
    IndexedTriangleList inTriangles;
    for ( auto& vertex : model.CPUVertices() )
    {
        inVertices.push_back( { vertex.Position.x, vertex.Position.y, vertex.Position.z} );
    }

    for ( int i = 0; i < model.CPUIndices().size(); i += 3 )
    {
        inTriangles.push_back( { model.CPUIndices()[i], model.CPUIndices()[i+1], model.CPUIndices()[i+2], 0 } );
    }

    PhysicsMaterialList materials;
    materials.push_back(new PhysicsMaterialSimple("Material " + ConvertToString(0), Color::sGetDistinctColor(0)));
    
    if (inTriangles.empty())
    {
        return nullptr;
    }
    return std::make_shared<FJoltMeshShape>(new MeshShapeSettings(inVertices, inTriangles, materials));
}

void FJoltPhysicsBackend::AddForceToBody(NextBodyID bodyID, const glm::vec3& force)
{
    CompleteTick();
    BodyInterface &bodyInterface = context_->physicsSystem.GetBodyInterface();

    bodyInterface.AddForce(ToJoltBodyID(bodyID), Vec3(force.x, force.y, force.z), EActivation::Activate);
}

void FJoltPhysicsBackend::MoveKinematicBody(NextBodyID bodyID, const glm::vec3& position,
                                           const glm::quat& rotation, float deltaSeconds)
{
    CompleteTick();
    BodyInterface &bodyInterface = context_->physicsSystem.GetBodyInterface();
    bodyInterface.MoveKinematic(ToJoltBodyID(bodyID), RVec3(position.x, position.y, position.z),
                               Quat(rotation.x, rotation.y, rotation.z, rotation.w), deltaSeconds);
    if (bodies_.contains(bodyID))
    {
        bodies_[bodyID].position = position;
        bodies_[bodyID].rotation = rotation;
    }
}

void FJoltPhysicsBackend::SetBodyTransform(NextBodyID bodyID, const glm::vec3& position,
                                          const glm::quat& rotation, bool resetVelocity)
{
    CompleteTick();
    if (bodyID.IsInvalid())
    {
        return;
    }

    BodyInterface &bodyInterface = context_->physicsSystem.GetBodyInterface();
    const BodyID joltBodyId = ToJoltBodyID(bodyID);
    bodyInterface.SetPositionAndRotationWhenChanged(
        joltBodyId,
        RVec3(position.x, position.y, position.z),
        Quat(rotation.x, rotation.y, rotation.z, rotation.w),
        EActivation::Activate);

    if (resetVelocity && bodyInterface.GetMotionType(joltBodyId) != EMotionType::Static)
    {
        bodyInterface.SetLinearAndAngularVelocity(joltBodyId, Vec3::sZero(), Vec3::sZero());
    }
    WakeDynamicBody(bodyInterface, joltBodyId);

    if (bodies_.contains(bodyID))
    {
        bodies_[bodyID].position = position;
        bodies_[bodyID].rotation = rotation;
        if (resetVelocity)
        {
            bodies_[bodyID].velocity = glm::vec3(0.0f);
        }
    }
}

void FJoltPhysicsBackend::SetBodyVelocity(NextBodyID bodyID, const glm::vec3& linearVelocity,
                                         const glm::vec3& angularVelocity)
{
    CompleteTick();
    if (bodyID.IsInvalid())
    {
        return;
    }

    BodyInterface &bodyInterface = context_->physicsSystem.GetBodyInterface();
    const BodyID joltBodyId = ToJoltBodyID(bodyID);
    if (bodyInterface.GetMotionType(joltBodyId) == EMotionType::Static)
    {
        return;
    }

    bodyInterface.SetLinearAndAngularVelocity(
        joltBodyId,
        Vec3(linearVelocity.x, linearVelocity.y, linearVelocity.z),
        Vec3(angularVelocity.x, angularVelocity.y, angularVelocity.z));
    WakeDynamicBody(bodyInterface, joltBodyId);

    if (bodies_.contains(bodyID))
    {
        bodies_[bodyID].velocity = linearVelocity;
    }
}

FNextPhysicsBody* FJoltPhysicsBackend::GetBody(NextBodyID bodyID)
{
    if ( bodies_.contains(bodyID) )
    {
        return &(bodies_[bodyID]);
    }
    return nullptr;
}

FNextPhysicsDebugState FJoltPhysicsBackend::GetBodyDebugState(NextBodyID bodyID) const
{
    const_cast<FJoltPhysicsBackend*>(this)->CompleteTick();
    FNextPhysicsDebugState state;
    if (!context_ || bodyID.IsInvalid())
    {
        return state;
    }

    BodyInterface &bodyInterface = context_->physicsSystem.GetBodyInterface();
    const BodyID joltBodyId = ToJoltBodyID(bodyID);
    if (!bodyInterface.IsAdded(joltBodyId))
    {
        return state;
    }

    state.motionType = FromJoltMotionType(bodyInterface.GetMotionType(joltBodyId));
    state.objectLayer = bodyInterface.GetObjectLayer(joltBodyId);
    state.isActive = bodyInterface.IsActive(joltBodyId);
    state.isValid = true;
    return state;
}

glm::vec4 FJoltPhysicsBackend::GetBodyDebugColor(NextBodyID bodyID) const
{
    const FNextPhysicsDebugState state = GetBodyDebugState(bodyID);
    return SelectDebugBodyColor(state.motionType, state.objectLayer, state.isActive, state.isValid);
}

void FJoltPhysicsBackend::RemoveBody(NextBodyID bodyID)
{
    CompleteTick();
    if (!context_ || bodyID.IsInvalid() || !bodies_.contains(bodyID))
    {
        return;
    }

    BodyInterface &bodyInterface = context_->physicsSystem.GetBodyInterface();
    const BodyID joltBodyId = ToJoltBodyID(bodyID);
    if (bodyInterface.IsAdded(joltBodyId))
    {
        bodyInterface.RemoveBody(joltBodyId);
    }
    bodyInterface.DestroyBody(joltBodyId);
    bodies_.erase(bodyID);
}

void FJoltPhysicsBackend::SetBodyActive(NextBodyID bodyID, bool active)
{
    CompleteTick();
    if (bodyID.IsInvalid()) return;

    BodyInterface &bodyInterface = context_->physicsSystem.GetBodyInterface();
    const BodyID joltBodyId = ToJoltBodyID(bodyID);
    bool isAdded = bodyInterface.IsAdded(joltBodyId);

    if (active && isAdded)
    {
        // Dynamic and kinematic bodies both belong in the moving broadphase.
        // Kinematic bodies in NON_MOVING can be moved, but they won't reliably push sleeping dynamic bodies.
        EMotionType mt = bodyInterface.GetMotionType(joltBodyId);
        ObjectLayer targetLayer = (mt == EMotionType::Static) ? NextLayers::NON_MOVING : NextLayers::MOVING;
        bodyInterface.SetObjectLayer(joltBodyId, targetLayer);
    }
    else if (!active && isAdded)
    {
        bodyInterface.SetObjectLayer(joltBodyId, NextLayers::HIDDEN);
    }
}

void FJoltPhysicsBackend::DrawDebugBodies() const
{
    const_cast<FJoltPhysicsBackend*>(this)->CompleteTick();
    if (!context_)
    {
        return;
    }

    BodyInterface &bodyInterface = context_->physicsSystem.GetBodyInterface();
    for (const auto& [bodyId, bodyInfo] : bodies_)
    {
        (void)bodyInfo;

        if (bodyId.IsInvalid())
        {
            continue;
        }

        const BodyID joltBodyId = ToJoltBodyID(bodyId);
        if (!bodyInterface.IsAdded(joltBodyId))
        {
            continue;
        }

        const TransformedShape transformedShape = bodyInterface.GetTransformedShape(joltBodyId);
        const AABox worldBounds = transformedShape.GetWorldSpaceBounds();
        if (!worldBounds.IsValid() || transformedShape.mShape == nullptr)
        {
            continue;
        }

        const FNextPhysicsDebugState debugState = GetBodyDebugState(bodyId);
        const glm::vec4 color =
            SelectDebugBodyColor(debugState.motionType, debugState.objectLayer, debugState.isActive, debugState.isValid);
        const AABox localBounds = transformedShape.mShape->GetLocalBounds();
        const glm::mat4 worldTransform = ToGlmMat4(transformedShape.GetWorldTransform().ToMat44());
        DrawAuxObb(worldTransform, ToGlmVec3(localBounds.mMin), ToGlmVec3(localBounds.mMax), color, 1.75f);

        const glm::vec3 localCenter = ToGlmVec3(localBounds.GetCenter());
        const glm::vec3 center = glm::vec3(worldTransform * glm::vec4(localCenter, 1.0f));
        Runtime::EngineHelper::DrawAuxPoint(center, color, 2.0f);
    }

    // VehicleConstraint wheels are ray/cylinder-cast constraints rather than rigid bodies.
    // Draw their actual solved poses, suspension segments and ground contacts explicitly.
    constexpr int wheelSegments = 16;
    for (const auto& [vehicleId, vehicle] : vehicles_)
    {
        (void)vehicleId;
        const VehicleConstraint& constraint = *vehicle->constraint;
        for (uint wheelIndex = 0; wheelIndex < constraint.GetWheels().size(); ++wheelIndex)
        {
            const Wheel* wheel = constraint.GetWheel(wheelIndex);
            const WheelSettings* settings = wheel->GetSettings();
            const RMat44 transform = constraint.GetWheelWorldTransform(wheelIndex, Vec3::sAxisZ(), Vec3::sAxisY());
            const RVec3 centerR = transform.GetTranslation();
            const glm::vec3 center(static_cast<float>(centerR.GetX()), static_cast<float>(centerR.GetY()),
                                   static_cast<float>(centerR.GetZ()));
            const Vec3 axisX = transform.GetAxisX();
            const Vec3 axisY = transform.GetAxisY();
            const glm::vec3 right(axisX.GetX(), axisX.GetY(), axisX.GetZ());
            const glm::vec3 up(axisY.GetX(), axisY.GetY(), axisY.GetZ());
            const glm::vec4 wheelColor = wheel->HasContact() ? glm::vec4(0.1f, 1.0f, 0.25f, 1.0f)
                                                                 : glm::vec4(1.0f, 0.2f, 0.15f, 1.0f);
            for (int segment = 0; segment < wheelSegments; ++segment)
            {
                const float a0 = glm::two_pi<float>() * static_cast<float>(segment) / wheelSegments;
                const float a1 = glm::two_pi<float>() * static_cast<float>(segment + 1) / wheelSegments;
                const glm::vec3 p0 = center + (right * std::cos(a0) + up * std::sin(a0)) * settings->mRadius;
                const glm::vec3 p1 = center + (right * std::cos(a1) + up * std::sin(a1)) * settings->mRadius;
                Runtime::EngineHelper::DrawAuxLine(p0, p1, wheelColor, 2.0f, false);
            }

            const RMat44 bodyTransform = constraint.GetVehicleBody()->GetWorldTransform();
            const RVec3 hardPointR = bodyTransform * settings->mPosition;
            const glm::vec3 hardPoint(static_cast<float>(hardPointR.GetX()), static_cast<float>(hardPointR.GetY()),
                                      static_cast<float>(hardPointR.GetZ()));
            Runtime::EngineHelper::DrawAuxLine(hardPoint, center, glm::vec4(1.0f, 0.8f, 0.1f, 1.0f), 2.0f, false);
            Runtime::EngineHelper::DrawAuxPoint(hardPoint, glm::vec4(1.0f, 0.8f, 0.1f, 1.0f), 3.0f);
            if (wheel->HasContact())
            {
                const RVec3 contactR = wheel->GetContactPosition();
                const glm::vec3 contact(static_cast<float>(contactR.GetX()), static_cast<float>(contactR.GetY()),
                                        static_cast<float>(contactR.GetZ()));
                Runtime::EngineHelper::DrawAuxLine(center, contact, glm::vec4(0.2f, 0.9f, 1.0f, 1.0f), 2.0f, false);
                Runtime::EngineHelper::DrawAuxPoint(contact, glm::vec4(0.2f, 0.9f, 1.0f, 1.0f), 4.0f);
            }
        }
    }
}

void FJoltPhysicsBackend::OnSceneStarted()
{
    CompleteTick();
    accumulatedTime_ = 0.0;
    updateCallCount_ = 0;
    simulatedStepCount_ = 0;
    lastUpdateErrorMask_ = 0;
    pendingBodyAddCount_ = 0;
    previousActiveRigidBodyCount_ = 0;
}

void FJoltPhysicsBackend::OnSceneDestroyed()
{
    CompleteTick();
    if (!context_)
    {
        return;
    }
    BodyInterface &bodyInterface = context_->physicsSystem.GetBodyInterface();
    for (auto& [id, vehicle] : vehicles_)
    {
        (void)id;
        context_->physicsSystem.RemoveStepListener(vehicle->constraint);
        context_->physicsSystem.RemoveConstraint(vehicle->constraint);
    }
    vehicles_.clear();
    
    for (auto& body : bodies_)
    {
        if (body.first.IsInvalid()) continue;
        const BodyID joltBodyId = ToJoltBodyID(body.first);
        bodyInterface.RemoveBody(joltBodyId);
        bodyInterface.DestroyBody(joltBodyId);
    }
    
    bodies_.clear();
}

NextVehicleID FJoltPhysicsBackend::CreateWheeledVehicle(const FNextVehicleSettings& settings)
{
    CompleteTick();
    if (!context_ || settings.wheels.empty()) return invalidNextVehicleId;
    BodyInterface& bi = context_->physicsSystem.GetBodyInterface();
    RefConst<Shape> chassis = new BoxShape(Vec3(settings.chassisHalfExtent.x, settings.chassisHalfExtent.y,
                                                settings.chassisHalfExtent.z));
    if (glm::dot(settings.centerOfMassOffset, settings.centerOfMassOffset) > 0.0f)
    {
        chassis = new OffsetCenterOfMassShape(chassis, Vec3(settings.centerOfMassOffset.x,
                                                            settings.centerOfMassOffset.y,
                                                            settings.centerOfMassOffset.z));
    }
    BodyCreationSettings bodySettings(chassis,
                                      RVec3(settings.initialPosition.x, settings.initialPosition.y, settings.initialPosition.z),
                                      Quat(settings.initialRotation.x, settings.initialRotation.y,
                                           settings.initialRotation.z, settings.initialRotation.w),
                                      EMotionType::Dynamic, NextLayers::MOVING);
    bodySettings.mOverrideMassProperties = EOverrideMassProperties::CalculateInertia;
    bodySettings.mMassPropertiesOverride.mMass = settings.mass;
    bodySettings.mLinearDamping = 0.08f;
    bodySettings.mAngularDamping = 0.25f;
    Body* body = bi.CreateBody(bodySettings);
    if (!body) return invalidNextVehicleId;
    bi.AddBody(body->GetID(), EActivation::Activate);

    VehicleConstraintSettings vehicleSettings;
    vehicleSettings.mUp = Vec3::sAxisY();
    vehicleSettings.mForward = Vec3::sAxisX();
    vehicleSettings.mMaxPitchRollAngle = DegreesToRadians(85.0f);
    auto* controller = new WheeledVehicleControllerSettings();
    const float maxTorque = settings.engine.maxTorque > 0.0f ? settings.engine.maxTorque : settings.maxEngineTorque;
    controller->mEngine.mMaxTorque = maxTorque;
    controller->mEngine.mMinRPM = settings.engine.minRPM;
    controller->mEngine.mMaxRPM = settings.engine.maxRPM;
    controller->mEngine.mInertia = settings.engine.inertia;
    controller->mEngine.mNormalizedTorque.Clear();
    for (const glm::vec2& point : settings.engine.normalizedTorque)
        controller->mEngine.mNormalizedTorque.AddPoint(point.x, point.y);
    controller->mEngine.mNormalizedTorque.Sort();
    controller->mTransmission.mMode = settings.transmission.automatic ? ETransmissionMode::Auto : ETransmissionMode::Manual;
    controller->mTransmission.mGearRatios.clear();
    for (float ratio : settings.transmission.gearRatios) controller->mTransmission.mGearRatios.push_back(ratio);
    controller->mTransmission.mReverseGearRatios = {settings.transmission.reverseRatio};
    controller->mTransmission.mShiftUpRPM = settings.transmission.shiftUpRPM;
    controller->mTransmission.mShiftDownRPM = settings.transmission.shiftDownRPM;
    controller->mTransmission.mClutchStrength = settings.transmission.clutchStrength;
    for (const auto& source : settings.wheels)
    {
        WheelSettingsWV* wheel = new WheelSettingsWV();
        wheel->mPosition = Vec3(source.position.x, source.position.y, source.position.z);
        wheel->mSuspensionDirection = -Vec3::sAxisY();
        wheel->mSteeringAxis = Vec3::sAxisY();
        wheel->mWheelUp = Vec3::sAxisY();
        wheel->mWheelForward = Vec3::sAxisX();
        wheel->mRadius = source.radius;
        wheel->mWidth = source.width;
        wheel->mSuspensionMinLength = source.suspensionMin;
        wheel->mSuspensionMaxLength = source.suspensionMax;
        wheel->mSuspensionPreloadLength = source.suspensionPreload;
        wheel->mSuspensionSpring = SpringSettings(ESpringMode::FrequencyAndDamping,
                                                  source.suspensionFrequency, source.suspensionDamping);
        wheel->mMaxSteerAngle = source.steered ? DegreesToRadians(settings.maxSteerAngleDeg) : 0.0f;
        wheel->mMaxHandBrakeTorque = source.steered ? 0.0f : 5000.0f;
        vehicleSettings.mWheels.push_back(Ref<WheelSettings>(wheel));
    }
    std::vector<bool> differentialDriven;
    for (size_t i = 0; i + 1 < settings.wheels.size(); i += 2)
    {
        VehicleDifferentialSettings diff;
        diff.mLeftWheel = static_cast<int>(i);
        diff.mRightWheel = static_cast<int>(i + 1);
        const bool driven = settings.wheels[i].driven || settings.wheels[i + 1].driven;
        differentialDriven.push_back(driven);
        diff.mEngineTorqueRatio = driven ? 1.0f : 0.0f;
        controller->mDifferentials.push_back(diff);
        VehicleAntiRollBar antiRoll;
        antiRoll.mLeftWheel = static_cast<int>(i);
        antiRoll.mRightWheel = static_cast<int>(i + 1);
        antiRoll.mStiffness = i == 0 ? settings.frontAntiRollStiffness : settings.rearAntiRollStiffness;
        if (antiRoll.mStiffness > 0.0f) vehicleSettings.mAntiRollBars.push_back(antiRoll);
    }
    const size_t drivenDifferentials = static_cast<size_t>(std::count(differentialDriven.begin(), differentialDriven.end(), true));
    if (drivenDifferentials > 0)
        for (auto& diff : controller->mDifferentials)
            if (diff.mEngineTorqueRatio > 0.0f) diff.mEngineTorqueRatio = 1.0f / static_cast<float>(drivenDifferentials);
    vehicleSettings.mController = controller;
    Ref<VehicleConstraint> constraint = new VehicleConstraint(*body, vehicleSettings);
    constraint->SetVehicleCollisionTester(new VehicleCollisionTesterCastCylinder(NextLayers::MOVING));
    const NextVehicleID id = nextVehicleID_++;
    auto data = std::make_unique<FVehicleData>();
    data->bodyID = FromJoltBodyID(body->GetID());
    data->constraint = constraint;
    data->frictionScales.resize(settings.wheels.size(), {1.0f, 1.0f});
    data->differentialDriven = std::move(differentialDriven);
    FVehicleData* raw = data.get();
    constraint->SetCombineFriction([raw](uint index, float& longitudinal, float& lateral, const Body&, const SubShapeID&)
    {
        if (index < raw->frictionScales.size())
        {
            longitudinal *= raw->frictionScales[index].first;
            lateral *= raw->frictionScales[index].second;
        }
    });
    context_->physicsSystem.AddConstraint(constraint);
    context_->physicsSystem.AddStepListener(constraint);
    FNextPhysicsBody info{settings.initialPosition, settings.initialRotation, {}, ENextBodyShape::Box,
                          data->bodyID, NextMotionType::Dynamic};
    bodies_[data->bodyID] = info;
    vehicles_[id] = std::move(data);
    return id;
}

void FJoltPhysicsBackend::RemoveVehicle(NextVehicleID id)
{
    CompleteTick();
    auto it = vehicles_.find(id); if (it == vehicles_.end()) return;
    const NextBodyID bodyId = it->second->bodyID;
    context_->physicsSystem.RemoveStepListener(it->second->constraint);
    context_->physicsSystem.RemoveConstraint(it->second->constraint);
    vehicles_.erase(it);
    RemoveBody(bodyId);
}

void FJoltPhysicsBackend::SetVehicleInput(NextVehicleID id, const FNextVehicleInput& input)
{
    CompleteTick();
    auto it = vehicles_.find(id); if (it == vehicles_.end()) return;
    it->second->lastThrottle = glm::clamp(input.throttle, -1.0f, 1.0f);
    static_cast<WheeledVehicleController*>(it->second->constraint->GetController())->SetDriverInput(
        glm::clamp(input.throttle, -1.0f, 1.0f), glm::clamp(input.steer, -1.0f, 1.0f),
        glm::clamp(input.brake, 0.0f, 1.0f), glm::clamp(input.handbrake, 0.0f, 1.0f));
    context_->physicsSystem.GetBodyInterface().ActivateBody(ToJoltBodyID(it->second->bodyID));
}

void FJoltPhysicsBackend::SetVehicleDiffLock(NextVehicleID id, bool locked)
{
    CompleteTick();
    auto it = vehicles_.find(id); if (it == vehicles_.end()) return;
    auto* controller = static_cast<WheeledVehicleController*>(it->second->constraint->GetController());
    for (auto& diff : controller->GetDifferentials()) diff.mLimitedSlipRatio = locked ? 1.02f : 1.4f;
    controller->SetDifferentialLimitedSlipRatio(locked ? 1.02f : 1.4f);
}

void FJoltPhysicsBackend::SetVehicleAllWheelDrive(NextVehicleID id, bool enabled)
{
    CompleteTick();
    auto it = vehicles_.find(id); if (it == vehicles_.end()) return;
    auto& diffs = static_cast<WheeledVehicleController*>(it->second->constraint->GetController())->GetDifferentials();
    const size_t active = enabled ? diffs.size() : static_cast<size_t>(std::count(it->second->differentialDriven.begin(),
                                                                                  it->second->differentialDriven.end(), true));
    if (active == 0) return;
    for (size_t index = 0; index < diffs.size(); ++index)
        diffs[index].mEngineTorqueRatio = (enabled || it->second->differentialDriven[index]) ? 1.0f / static_cast<float>(active) : 0.0f;
}

bool FJoltPhysicsBackend::GetVehicleTelemetry(NextVehicleID id, FNextVehicleTelemetry& telemetry) const
{
    const_cast<FJoltPhysicsBackend*>(this)->CompleteTick();
    auto it = vehicles_.find(id); if (it == vehicles_.end() || !context_) return false;
    const auto* controller = static_cast<const WheeledVehicleController*>(it->second->constraint->GetController());
    telemetry.gear = controller->GetTransmission().GetCurrentGear();
    telemetry.rpm = controller->GetEngine().GetCurrentRPM();
    telemetry.maxRPM = controller->GetEngine().mMaxRPM;
    telemetry.engineTorque = controller->GetEngine().GetTorque(std::abs(it->second->lastThrottle));
    const BodyInterface& bi = context_->physicsSystem.GetBodyInterface();
    const BodyID bodyID = ToJoltBodyID(it->second->bodyID);
    const Vec3 velocity = bi.GetLinearVelocity(bodyID);
    const Vec3 forward = bi.GetRotation(bodyID) * Vec3::sAxisX();
    telemetry.forwardSpeed = velocity.Dot(forward);
    telemetry.wheelSlip.clear(); telemetry.wheelContact.clear();
    for (const Wheel* wheel : it->second->constraint->GetWheels())
    {
        const auto* wheeled = static_cast<const WheelWV*>(wheel);
        telemetry.wheelSlip.push_back(wheeled->mLongitudinalSlip);
        telemetry.wheelContact.push_back(wheel->HasContact());
    }
    return true;
}

bool FJoltPhysicsBackend::GetVehicleBodyTransform(NextVehicleID id, glm::vec3& position, glm::quat& rotation)
{
    CompleteTick();
    auto it = vehicles_.find(id); if (it == vehicles_.end()) return false;
    BodyInterface& bi = context_->physicsSystem.GetBodyInterface();
    RVec3 p = bi.GetPosition(ToJoltBodyID(it->second->bodyID)); Quat q = bi.GetRotation(ToJoltBodyID(it->second->bodyID));
    position = {p.GetX(), p.GetY(), p.GetZ()}; rotation = glm::quat(q.GetW(), q.GetX(), q.GetY(), q.GetZ()); return true;
}

bool FJoltPhysicsBackend::GetVehicleWheelLocalTransform(NextVehicleID id, int wheel, glm::vec3& position, glm::quat& rotation)
{
    CompleteTick();
    auto it = vehicles_.find(id); if (it == vehicles_.end() || wheel < 0 || static_cast<uint>(wheel) >= it->second->constraint->GetWheels().size()) return false;
    Mat44 m = it->second->constraint->GetWheelLocalTransform(static_cast<uint>(wheel), Vec3::sAxisZ(), Vec3::sAxisY());
    Vec3 p = m.GetTranslation(); Quat q = m.GetQuaternion(); position = {p.GetX(), p.GetY(), p.GetZ()};
    rotation = glm::quat(q.GetW(), q.GetX(), q.GetY(), q.GetZ()); return true;
}

NextBodyID FJoltPhysicsBackend::GetVehicleWheelContactBody(NextVehicleID id, int wheel)
{
    CompleteTick();
    auto it = vehicles_.find(id); if (it == vehicles_.end() || wheel < 0 || static_cast<uint>(wheel) >= it->second->constraint->GetWheels().size()) return {};
    const Wheel* value = it->second->constraint->GetWheel(static_cast<uint>(wheel));
    return value->HasContact() ? FromJoltBodyID(value->GetContactBodyID()) : NextBodyID{};
}

void FJoltPhysicsBackend::SetVehicleWheelFrictionScale(NextVehicleID id, int wheel, float longitudinal, float lateral)
{
    CompleteTick();
    auto it = vehicles_.find(id); if (it != vehicles_.end() && wheel >= 0 && static_cast<size_t>(wheel) < it->second->frictionScales.size())
        it->second->frictionScales[wheel] = {std::max(0.0f, longitudinal), std::max(0.0f, lateral)};
}

void FJoltPhysicsBackend::SetVehicleBodyTransform(NextVehicleID id, const glm::vec3& p, const glm::quat& q)
{ auto it = vehicles_.find(id); if (it != vehicles_.end()) SetBodyTransform(it->second->bodyID, p, q, true); }

NextBodyID FJoltPhysicsBackend::GetVehicleBodyID(NextVehicleID id) const
{ auto it = vehicles_.find(id); return it == vehicles_.end() ? NextBodyID{} : it->second->bodyID; }

namespace
{
    RefConst<Shape> CreateFootAnchoredCharacterShape(float height, float radius)
    {
        const float cylinderHalfHeight = (height - 2.0f * radius) * 0.5f;
        RefConst<Shape> capsule = new CapsuleShape(std::max(cylinderHalfHeight, 0.01f), radius);
        return new RotatedTranslatedShape(Vec3(0, height * 0.5f, 0), Quat::sIdentity(), capsule);
    }

    class FJoltCharacterControllerBackend final : public INextCharacterControllerBackend
    {
    public:
        FJoltCharacterControllerBackend(PhysicsSystem& physicsSystem, TempAllocator& tempAllocator,
                                        const FCharacterControllerSettings& settings)
            : physicsSystem_(physicsSystem), tempAllocator_(tempAllocator), height_(settings.height),
              radius_(settings.radius), padding_(settings.padding)
        {
            RefConst<Shape> shape = CreateFootAnchoredCharacterShape(height_, radius_);

            CharacterVirtualSettings characterSettings;
            characterSettings.mShape = shape;
            characterSettings.mMaxSlopeAngle = DegreesToRadians(settings.maxSlopeAngle);
            characterSettings.mMaxStrength = settings.maxStrength;
            characterSettings.mCharacterPadding = settings.padding;
            characterSettings.mPenetrationRecoverySpeed = 1.0f;
            characterSettings.mPredictiveContactDistance = 0.1f;
            characterSettings.mSupportingVolume = Plane(Vec3::sAxisY(), -settings.radius);
            characterSettings.mMass = settings.mass;

            character_ = new CharacterVirtual(
                &characterSettings,
                RVec3(settings.initialPosition.x, settings.initialPosition.y, settings.initialPosition.z),
                Quat::sIdentity(), 0, &physicsSystem_);
            updateSettings_.mStickToFloorStepDown = Vec3(0, -settings.maxStepHeight, 0);
            updateSettings_.mWalkStairsStepUp = Vec3(0, settings.maxStepHeight, 0);
        }

        void Update(const glm::vec3& inputDirection, float speed, bool jump, float deltaSeconds) override
        {
            if (!character_)
            {
                return;
            }

            constexpr float gravity = -15.0f;
            constexpr float jumpSpeed = 6.0f;
            const bool onGround = character_->GetGroundState() == CharacterVirtual::EGroundState::OnGround;
            float verticalVelocity = velocity_.y;
            if (onGround)
            {
                verticalVelocity = jump ? jumpSpeed : 0.0f;
            }
            else
            {
                verticalVelocity += gravity * deltaSeconds;
            }

            const Vec3 newVelocity(inputDirection.x * speed, verticalVelocity, inputDirection.z * speed);
            character_->SetLinearVelocity(newVelocity);
            velocity_ = glm::vec3(newVelocity.GetX(), newVelocity.GetY(), newVelocity.GetZ());

            const BroadPhaseLayerFilter& broadPhaseFilter =
                physicsSystem_.GetDefaultBroadPhaseLayerFilter(NextLayers::MOVING);
            const ObjectLayerFilter& objectLayerFilter =
                physicsSystem_.GetDefaultLayerFilter(NextLayers::MOVING);
            character_->ExtendedUpdate(deltaSeconds, Vec3(0, gravity, 0), updateSettings_,
                                       broadPhaseFilter, objectLayerFilter, {}, {}, tempAllocator_);
        }

        bool TrySetHeight(float height) override
        {
            constexpr float kMinCylinderHeight = 0.02f;
            constexpr float kSameHeightEpsilon = 1.0e-4f;
            if (!character_ || !std::isfinite(height) || height < 2.0f * radius_ + kMinCylinderHeight)
            {
                return false;
            }
            if (std::abs(height - height_) <= kSameHeightEpsilon)
            {
                return true;
            }

            RefConst<Shape> shape = CreateFootAnchoredCharacterShape(height, radius_);
            const BroadPhaseLayerFilter& broadPhaseFilter =
                physicsSystem_.GetDefaultBroadPhaseLayerFilter(NextLayers::MOVING);
            const ObjectLayerFilter& objectLayerFilter =
                physicsSystem_.GetDefaultLayerFilter(NextLayers::MOVING);
            const float maxPenetrationDepth =
                height < height_ ? std::numeric_limits<float>::max() : std::max(padding_, 1.0e-3f);
            if (!character_->SetShape(shape, maxPenetrationDepth, broadPhaseFilter, objectLayerFilter, {}, {},
                                      tempAllocator_))
            {
                return false;
            }

            height_ = height;
            character_->RefreshContacts(broadPhaseFilter, objectLayerFilter, {}, {}, tempAllocator_);
            return true;
        }

        void SetPosition(const glm::vec3& position) override
        {
            if (!character_)
            {
                return;
            }
            character_->SetPosition(RVec3(position.x, position.y, position.z));
            character_->SetLinearVelocity(Vec3::sZero());
            velocity_ = glm::vec3(0.0f);

            const BroadPhaseLayerFilter& broadPhaseFilter =
                physicsSystem_.GetDefaultBroadPhaseLayerFilter(NextLayers::MOVING);
            const ObjectLayerFilter& objectLayerFilter =
                physicsSystem_.GetDefaultLayerFilter(NextLayers::MOVING);
            character_->RefreshContacts(broadPhaseFilter, objectLayerFilter, {}, {}, tempAllocator_);
        }

        glm::vec3 GetPosition() const override
        {
            if (!character_)
            {
                return glm::vec3(0.0f);
            }
            const RVec3 position = character_->GetPosition();
            return glm::vec3(static_cast<float>(position.GetX()),
                             static_cast<float>(position.GetY()),
                             static_cast<float>(position.GetZ()));
        }

        glm::vec3 GetLinearVelocity() const override { return velocity_; }

        ECharacterGroundState GetGroundState() const override
        {
            if (!character_)
            {
                return ECharacterGroundState::InAir;
            }
            switch (character_->GetGroundState())
            {
            case CharacterVirtual::EGroundState::OnGround: return ECharacterGroundState::OnGround;
            case CharacterVirtual::EGroundState::OnSteepGround: return ECharacterGroundState::OnSteepGround;
            case CharacterVirtual::EGroundState::NotSupported: return ECharacterGroundState::NotSupported;
            case CharacterVirtual::EGroundState::InAir: return ECharacterGroundState::InAir;
            }
            return ECharacterGroundState::InAir;
        }

        float GetHeight() const override { return height_; }
        bool IsValid() const override { return character_ != nullptr; }

    private:
        PhysicsSystem& physicsSystem_;
        TempAllocator& tempAllocator_;
        Ref<CharacterVirtual> character_;
        CharacterVirtual::ExtendedUpdateSettings updateSettings_;
        glm::vec3 velocity_{0.0f};
        float height_ = 0.0f;
        float radius_ = 0.0f;
        float padding_ = 0.0f;
    };
}

std::unique_ptr<INextCharacterControllerBackend> FJoltPhysicsBackend::CreateCharacterController(
    const FCharacterControllerSettings& settings)
{
    CompleteTick();
    if (!context_)
    {
        return nullptr;
    }
    return std::make_unique<FJoltCharacterControllerBackend>(
        context_->physicsSystem, context_->tempAllocator, settings);
}
