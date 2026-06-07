#include "Engine/Runtime/Subsystems/NextPhysics.h"

#if WITH_PHYSIC
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
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/PlaneShape.h>
#include <Jolt/Physics/Collision/PhysicsMaterialSimple.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyActivationListener.h>
#include <Jolt/Physics/Collision/Shape/ScaledShape.h>
#endif

// STL includes
#include <iostream>
#include <cstdarg>
#include <thread>
#include <glm/ext.hpp>

#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/Utilities/NextEngineHelper.h"
#include "Engine/Assets/Core/Model.hpp"

#if WITH_PHYSIC

// Disable common warnings triggered by Jolt, you can use JPH_SUPPRESS_WARNING_PUSH / JPH_SUPPRESS_WARNING_POP to store and restore the warning state
JPH_SUPPRESS_WARNINGS

// All Jolt symbols are in the JPH namespace
using namespace JPH;

// If you want your code to compile using single or double precision write 0.0_r to get a Real value that compiles to double or float depending if JPH_DOUBLE_PRECISION is set or not.
using namespace JPH::literals;

namespace
{
	constexpr float kSleepVelocityThreshold = 0.01f;
	constexpr float kTimeBeforeSleep = 1.25f;
	constexpr float kDynamicBoxFriction = 0.22f;
	constexpr float kDynamicBoxGravityFactor = 1.15f;
	constexpr float kDynamicBoxLinearDamping = 0.025f;
	constexpr float kDynamicBoxAngularDamping = 0.02f;
	constexpr float kDynamicBoxInertiaMultiplier = 1.0f;

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
		case EMotionType::Static:
			return glm::vec4(0.35f, 0.65f, 1.0f, 1.0f);
		case EMotionType::Kinematic:
			return glm::vec4(0.2f, 0.95f, 0.95f, 1.0f);
		case EMotionType::Dynamic:
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
	virtual bool					ShouldCollide(ObjectLayer inObject1, ObjectLayer inObject2) const override
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

	virtual uint					GetNumBroadPhaseLayers() const override
	{
		return BroadPhaseLayers::numLayers;
	}

	virtual BroadPhaseLayer			GetBroadPhaseLayer(ObjectLayer inLayer) const override
	{
		JPH_ASSERT(inLayer < NextLayers::NUM_LAYERS);
		return mObjectToBroadPhase_[inLayer];
	}

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
	virtual const char *			GetBroadPhaseLayerName(BroadPhaseLayer inLayer) const override
	{
		switch ((BroadPhaseLayer::Type)inLayer)
		{
		case (BroadPhaseLayer::Type)BroadPhaseLayers::NON_MOVING:	return "NON_MOVING";
		case (BroadPhaseLayer::Type)BroadPhaseLayers::MOVING:		return "MOVING";
		case (BroadPhaseLayer::Type)BroadPhaseLayers::HIDDEN:		return "HIDDEN";
		default:													JPH_ASSERT(false); return "INVALID";
		}
	}
#endif // JPH_EXTERNAL_PROFILE || JPH_PROFILE_ENABLED

private:
	BroadPhaseLayer					mObjectToBroadPhase_[NextLayers::NUM_LAYERS];
};

/// Class that determines if an object layer can collide with a broadphase layer
class ObjectVsBroadPhaseLayerFilterImpl : public ObjectVsBroadPhaseLayerFilter
{
public:
	virtual bool				ShouldCollide(ObjectLayer inLayer1, BroadPhaseLayer inLayer2) const override
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
	virtual ValidateResult	OnContactValidate(const Body &inBody1, const Body &inBody2, RVec3Arg inBaseOffset, const CollideShapeResult &inCollisionResult) override
	{
		//cout << "Contact validate callback" << endl;

		// Allows you to ignore a contact before it is created (using layers to not make objects collide is cheaper!)
		return ValidateResult::AcceptAllContactsForThisBodyPair;
	}

	virtual void			OnContactAdded(const Body &inBody1, const Body &inBody2, const ContactManifold &inManifold, ContactSettings &ioSettings) override
	{
		//cout << "A contact was added" << endl;
	}

	virtual void			OnContactPersisted(const Body &inBody1, const Body &inBody2, const ContactManifold &inManifold, ContactSettings &ioSettings) override
	{
		//cout << "A contact was persisted" << endl;
	}

	virtual void			OnContactRemoved(const SubShapeIDPair &inSubShapePair) override
	{
		//cout << "A contact was removed" << endl;
	}
};

// An example activation listener
class MyBodyActivationListener : public BodyActivationListener
{
public:
	virtual void		OnBodyActivated(const BodyID &inBodyID, uint64 inBodyUserData) override
	{
		//cout << "A body got activated" << endl;
	}

	virtual void		OnBodyDeactivated(const BodyID &inBodyID, uint64 inBodyUserData) override
	{
		//cout << "A body went to sleep" << endl;
	}
};

struct FNextPhysicsContext
{
	FNextPhysicsContext():
		tempAllocator(10 * 1024 * 1024),
		jobSystem(cMaxPhysicsJobs, cMaxPhysicsBarriers, std::thread::hardware_concurrency() - 1)
	{
		// This is the max amount of rigid bodies that you can add to the physics system. If you try to add more you'll get an error.
		// Note: This value is low because this is a simple test. For a real project use something in the order of 65536.
		const uint cMaxBodies = 65535;//1024;

		// This determines how many mutexes to allocate to protect rigid bodies from concurrent access. Set it to 0 for the default settings.
		const uint cNumBodyMutexes = 0;

		// This is the max amount of body pairs that can be queued at any time (the broad phase will detect overlapping
		// body pairs based on their bounding boxes and will insert them into a queue for the narrowphase). If you make this buffer
		// too small the queue will fill up and the broad phase jobs will start to do narrow phase work. This is slightly less efficient.
		// Note: This value is low because this is a simple test. For a real project use something in the order of 65536.
		const uint cMaxBodyPairs = 65535;//1024;

		// This is the maximum size of the contact constraint buffer. If more contacts (collisions between bodies) are detected than this
		// number then these contacts will be ignored and bodies will start interpenetrating / fall through the world.
		// Note: This value is low because this is a simple test. For a real project use something in the order of 10240.
		const uint cMaxContactConstraints = 10240;//1024;
		
		physicsSystem.Init(cMaxBodies, cNumBodyMutexes, cMaxBodyPairs, cMaxContactConstraints, broadPhaseLayerInterface, objectVsBroadphaseLayerFilter, objectVsObjectLayerFilter);

		PhysicsSettings settings = physicsSystem.GetPhysicsSettings();
		settings.mPointVelocitySleepThreshold = kSleepVelocityThreshold;
		settings.mTimeBeforeSleep = kTimeBeforeSleep;
		physicsSystem.SetPhysicsSettings(settings);
		
		
		physicsSystem.SetBodyActivationListener(&bodyActivationListener);
		physicsSystem.SetContactListener(&contactListener);
	}

	~FNextPhysicsContext()
	{
	    
	}
	// We need a temp allocator for temporary allocations during the physics update. We're
	// pre-allocating 10 MB to avoid having to do allocations during the physics update.
	// B.t.w. 10 MB is way too much for this example but it is a typical value you can use.
	// If you don't want to pre-allocate you can also use TempAllocatorMalloc to fall back to
	// malloc / free.
	TempAllocatorImpl tempAllocator;

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

#else

struct FNextPhysicsContext {};

#endif

// Accessor functions for NextCharacterController to reach Jolt internals
#if WITH_PHYSIC
JPH::PhysicsSystem* GetJoltPhysicsSystem(NextPhysics* physics);
JPH::TempAllocator* GetJoltTempAllocator(NextPhysics* physics);
#endif

// These are defined after FNextPhysicsContext so the type is complete.
#if WITH_PHYSIC

static FNextPhysicsContext* GetPhysicsContext(NextPhysics* physics)
{
    // Access via the public interface: NextPhysics stores context_ as unique_ptr.
    // We use a small trick: NextCharacterController.cpp declares these as extern.
    // We implement them here where FNextPhysicsContext is fully defined.
    return nullptr; // placeholder, real impl below
}

#endif

// Real accessor implementations using a friend-like pattern:
// NextPhysics exposes its context_ pointer through these free functions.
// We store a static map from NextPhysics* -> FNextPhysicsContext* set during Start().
#include <unordered_map>
static std::unordered_map<NextPhysics*, FNextPhysicsContext*> sPhysicsContextMap;

#if WITH_PHYSIC
JPH::PhysicsSystem* GetJoltPhysicsSystem(NextPhysics* physics)
{
    auto it = sPhysicsContextMap.find(physics);
    if (it != sPhysicsContextMap.end() && it->second)
    {
        return &it->second->physicsSystem;
    }
    return nullptr;
}

JPH::TempAllocator* GetJoltTempAllocator(NextPhysics* physics)
{
    auto it = sPhysicsContextMap.find(physics);
    if (it != sPhysicsContextMap.end() && it->second)
    {
        return &it->second->tempAllocator;
    }
    return nullptr;
}
#else
// Stub implementations when physics is disabled
namespace JPH { class PhysicsSystem; class TempAllocator; }
JPH::PhysicsSystem* GetJoltPhysicsSystem(NextPhysics*) { return nullptr; }
JPH::TempAllocator* GetJoltTempAllocator(NextPhysics*) { return nullptr; }
#endif

NextPhysics::NextPhysics()
{
    
}

NextPhysics::~NextPhysics()
{
    
}

void NextPhysics::Start()
{
#if WITH_PHYSIC
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
	sPhysicsContextMap[this] = context_.get();

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
#endif
}

void NextPhysics::Tick(double deltaSeconds)
{
    if (paused_)
    {
        return;
    }

#if WITH_PHYSIC
	TimeElapsed += deltaSeconds;
	const float timeOffset = static_cast<float>( TimeElapsed - TimeSimulated );
	const float cDeltaTime = 1.0f / 60.0f;

	float shouldTick = timeOffset / cDeltaTime;

	if (shouldTick < 1.0f)
	{
		return;
	}
	
	// If you take larger steps than 1 / 60th of a second you need to do multiple collision steps in order to keep the simulation stable. Do 1 collision step per 1 / 60th of a second (round up).
	const int cCollisionSteps = glm::min(4, glm::max(1, static_cast<int>(glm::floor(timeOffset / cDeltaTime))));

	TimeSimulated += cDeltaTime * cCollisionSteps;

	// Update bodies
	BodyInterface &bodyInterface = context_->physicsSystem.GetBodyInterface();
	
	for (auto& body : bodies_)
	{
		if (body.second.motionType == EMotionType::Dynamic)
		{
			RVec3 pos = bodyInterface.GetPosition(body.first);
			RVec3 vel = bodyInterface.GetLinearVelocity(body.first);
		    Quat rot = bodyInterface.GetRotation(body.first);
			body.second.position = glm::vec3(pos.GetX(), pos.GetY(), pos.GetZ());
		    body.second.rotation = glm::quat(rot.GetW(), rot.GetX(), rot.GetY(), rot.GetZ());
			body.second.velocity = glm::vec3(vel.GetX(), vel.GetY(), vel.GetZ());

			if (vel.Length() > 0.001f)
			{
				NextEngine::GetInstance()->GetScene().MarkTransformDirty();
			}
		}
	}

	// Step the world
	context_->physicsSystem.Update(cDeltaTime, cCollisionSteps, &context_->tempAllocator, &context_->jobSystem);
#endif
}

void NextPhysics::SetPaused(bool paused)
{
    paused_ = paused;
}

FNextPhysicsBodyStats NextPhysics::GetBodyStats() const
{
    FNextPhysicsBodyStats stats{};
    stats.total = bodies_.size();

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

void NextPhysics::Stop()
{
	OnSceneDestroyed();
	
#if WITH_PHYSIC
	// Unregisters all types with the factory and cleans up the default material
	UnregisterTypes();

	// Destroy the factory
	delete Factory::sInstance;
	Factory::sInstance = nullptr;

	sPhysicsContextMap.erase(this);
	context_.reset();
#endif
}

NextBodyID NextPhysics::AddBodyInternal(FNextPhysicsBody& body, bool optimizeBroadPhase)
{
#if WITH_PHYSIC
	bodies_[body.bodyID] = body;
	if (optimizeBroadPhase) context_->physicsSystem.OptimizeBroadPhase();
	return body.bodyID;
#else
    return NextBodyID();
#endif
}

NextBodyID NextPhysics::CreateSphereBody(glm::vec3 position, float radius, NextMotionType motionType)
{
#if WITH_PHYSIC
	BodyInterface &bodyInterface = context_->physicsSystem.GetBodyInterface();

	// Now create a dynamic body to bounce on the floor
	// Note that this uses the shorthand version of creating and adding a body to the world
	BodyCreationSettings sphereSettings(new SphereShape(radius), RVec3(position.x, position.y, position.z), Quat::sIdentity(), motionType, NextLayers::MOVING);
	sphereSettings.mFriction = 0.5f;
	sphereSettings.mInertiaMultiplier = 2.0f;
	//sphere_settings.mRestitution = 0.05f;
	sphereSettings.mMotionQuality = EMotionQuality::LinearCast;
	BodyID bodyId = bodyInterface.CreateAndAddBody(sphereSettings, EActivation::Activate);

	// Now you can interact with the dynamic body, in this case we're going to give it a velocity.
	// (note that if we had used CreateBody then we could have set the velocity straight on the body before adding it to the physics system)
	//body_interface.SetLinearVelocity(body_id, Vec3(0.0f, -5.0f, 0.0f));
	FNextPhysicsBody body { position, glm::quat(1,0,0,0), glm::vec3(0.0f, 0.0f, 0.0f), ENextBodyShape::Box, bodyId, motionType };
	return AddBodyInternal(body, true);
#else
    return NextBodyID();
#endif
}

NextBodyID NextPhysics::CreateBoxBody(glm::vec3 position, glm::vec3 extent, NextMotionType motionType)
{
#if WITH_PHYSIC
	BodyInterface &bodyInterface = context_->physicsSystem.GetBodyInterface();
	BodyID bodyId(-1);

	Vec3 halfExtent(extent.x * 0.5f, extent.y * 0.5f, extent.z * 0.5f);
	float convexRadius = ComputeBoxConvexRadius(halfExtent);

	// Create the settings for the body itself. Note that here you can also set other properties like the restitution / friction.
	BodyCreationSettings floorSettings(new BoxShape(halfExtent, convexRadius), RVec3(position.x, position.y, position.z), Quat::sIdentity(), motionType, NextLayers::MOVING);
	//floorSettings.mRestitution = 0.05f;
	if (motionType == EMotionType::Dynamic)
	{
		ConfigureDynamicBoxSettings(floorSettings);
	}
	else
	{
		floorSettings.mFriction = 0.5f;
	}
	// Create the actual rigid body
	bodyId = bodyInterface.CreateAndAddBody(floorSettings, EActivation::Activate);
	WakeDynamicBody(bodyInterface, bodyId);

	FNextPhysicsBody body { position, glm::quat(1,0,0,0), glm::vec3(0.0f, 0.0f, 0.0f), ENextBodyShape::Box, bodyId, motionType };
	return AddBodyInternal(body, true);
#else
    return NextBodyID();
#endif
}

NextBodyID NextPhysics::CreateBoxBody(glm::vec3 position, glm::quat rotation, glm::vec3 extent, NextMotionType motionType)
{
#if WITH_PHYSIC
	BodyInterface &bodyInterface = context_->physicsSystem.GetBodyInterface();
	BodyID bodyId(-1);

	Vec3 halfExtent(extent.x * 0.5f, extent.y * 0.5f, extent.z * 0.5f);
	float convexRadius = ComputeBoxConvexRadius(halfExtent);

	Quat joltRot(rotation.x, rotation.y, rotation.z, rotation.w);
	BodyCreationSettings settings(new BoxShape(halfExtent, convexRadius), RVec3(position.x, position.y, position.z), joltRot, motionType, NextLayers::MOVING);
	if (motionType == EMotionType::Dynamic)
	{
		ConfigureDynamicBoxSettings(settings);
	}
	else
	{
		settings.mFriction = 0.5f;
	}
	bodyId = bodyInterface.CreateAndAddBody(settings, EActivation::Activate);
	WakeDynamicBody(bodyInterface, bodyId);

	FNextPhysicsBody body { position, rotation, glm::vec3(0.0f, 0.0f, 0.0f), ENextBodyShape::Box, bodyId, motionType };
	return AddBodyInternal(body, true);
#else
    return NextBodyID();
#endif
}

NextBodyID NextPhysics::CreateMeshBody(NextRefConst<NextMeshShapeSettings> meshShapeSettings, glm::vec3 position, glm::quat rotation, glm::vec3 scale, NextMotionType motionType, NextObjectLayer layer)
{
#if WITH_PHYSIC
	BodyInterface &bodyInterface = context_->physicsSystem.GetBodyInterface();
	BodyID bodyId(-1);
	
	const float epsilon = 0.001f;
	bool isUniformScale = (glm::abs(scale.x - 1.0f) < epsilon && 
						  glm::abs(scale.y - 1.0f) < epsilon && 
						  glm::abs(scale.z - 1.0f) < epsilon);

	BodyCreationSettings bodyCreation = isUniformScale ? BodyCreationSettings(meshShapeSettings,
	Vec3(position.x, position.y, position.z),Quat(rotation.x, rotation.y, rotation.z, rotation.w),
	motionType, layer): BodyCreationSettings(new ScaledShapeSettings(meshShapeSettings, Vec3(scale.x, scale.y, scale.z)),
		Vec3(position.x, position.y, position.z), Quat(rotation.x, rotation.y, rotation.z, rotation.w),
		motionType, layer);

	//bodyCreation.mRestitution = 0.05f;
	bodyCreation.mFriction = 0.5f;
	bodyCreation.mOverrideMassProperties = EOverrideMassProperties::CalculateInertia;
	bodyCreation.mMassPropertiesOverride.mMass = 1.0f;
	
	bodyId = bodyInterface.CreateAndAddBody(bodyCreation, EActivation::Activate);

	FNextPhysicsBody body { position, rotation, glm::vec3(0.0f, 0.0f, 0.0f), ENextBodyShape::Mesh, bodyId, motionType };
	return AddBodyInternal(body, false);
#else
    return NextBodyID();
#endif
}

NextBodyID NextPhysics::CreatePlaneBody(glm::vec3 position, glm::vec3 normal, NextMotionType motionType)
{
#if WITH_PHYSIC
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

	FNextPhysicsBody body { position, glm::quat(1,0,0,0),glm::vec3(0.0f, 0.0f, 0.0f), ENextBodyShape::Box, bodyId, EMotionType::Static };
	return AddBodyInternal(body, true);
#else
    return NextBodyID();
#endif
}

NextMeshShapeSettings* NextPhysics::CreateMeshShape(Assets::Model& model)
{
#if WITH_PHYSIC
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
	
	return new MeshShapeSettings(inVertices, inTriangles, materials);
#else
    return nullptr;
#endif
}

void NextPhysics::AddForceToBody(NextBodyID bodyID, const glm::vec3& force)
{
#if WITH_PHYSIC
	BodyInterface &bodyInterface = context_->physicsSystem.GetBodyInterface();

	bodyInterface.AddForce(bodyID, Vec3(force.x, force.y, force.z), EActivation::Activate); // Activate the body if it is sleeping
#endif
}

void NextPhysics::MoveKinematicBody(NextBodyID bodyID, const glm::vec3& position, const glm::quat& rotation, float deltaSeconds)
{
#if WITH_PHYSIC
	BodyInterface &bodyInterface = context_->physicsSystem.GetBodyInterface();
	bodyInterface.MoveKinematic(bodyID, RVec3(position.x, position.y, position.z), Quat(rotation.x, rotation.y, rotation.z, rotation.w), deltaSeconds);
	if (bodies_.contains(bodyID))
	{
		bodies_[bodyID].position = position;
		bodies_[bodyID].rotation = rotation;
	}
#endif
}

void NextPhysics::SetBodyTransform(NextBodyID bodyID, const glm::vec3& position, const glm::quat& rotation, bool resetVelocity)
{
#if WITH_PHYSIC
    if (bodyID.IsInvalid())
    {
        return;
    }

	BodyInterface &bodyInterface = context_->physicsSystem.GetBodyInterface();
    bodyInterface.SetPositionAndRotationWhenChanged(
        bodyID,
        RVec3(position.x, position.y, position.z),
        Quat(rotation.x, rotation.y, rotation.z, rotation.w),
        EActivation::Activate);

    if (resetVelocity && bodyInterface.GetMotionType(bodyID) != EMotionType::Static)
    {
        bodyInterface.SetLinearAndAngularVelocity(bodyID, Vec3::sZero(), Vec3::sZero());
    }
	WakeDynamicBody(bodyInterface, bodyID);

    if (bodies_.contains(bodyID))
    {
        bodies_[bodyID].position = position;
        bodies_[bodyID].rotation = rotation;
        if (resetVelocity)
        {
            bodies_[bodyID].velocity = glm::vec3(0.0f);
        }
    }
#else
    (void)bodyID;
    (void)position;
    (void)rotation;
    (void)resetVelocity;
#endif
}

void NextPhysics::SetBodyVelocity(NextBodyID bodyID, const glm::vec3& linearVelocity, const glm::vec3& angularVelocity)
{
#if WITH_PHYSIC
    if (bodyID.IsInvalid())
    {
        return;
    }

    BodyInterface &bodyInterface = context_->physicsSystem.GetBodyInterface();
    if (bodyInterface.GetMotionType(bodyID) == EMotionType::Static)
    {
        return;
    }

    bodyInterface.SetLinearAndAngularVelocity(
        bodyID,
        Vec3(linearVelocity.x, linearVelocity.y, linearVelocity.z),
        Vec3(angularVelocity.x, angularVelocity.y, angularVelocity.z));
	WakeDynamicBody(bodyInterface, bodyID);

    if (bodies_.contains(bodyID))
    {
        bodies_[bodyID].velocity = linearVelocity;
    }
#else
    (void)bodyID;
    (void)linearVelocity;
    (void)angularVelocity;
#endif
}

FNextPhysicsBody* NextPhysics::GetBody(NextBodyID bodyID)
{
#if WITH_PHYSIC
	if ( bodies_.contains(bodyID) )
	{
		return &(bodies_[bodyID]);
	}
#endif
	return nullptr;
}

FNextPhysicsDebugState NextPhysics::GetBodyDebugState(NextBodyID bodyID) const
{
	FNextPhysicsDebugState state;
#if WITH_PHYSIC
	if (!context_ || bodyID.IsInvalid())
	{
		return state;
	}

	BodyInterface &bodyInterface = context_->physicsSystem.GetBodyInterface();
	if (!bodyInterface.IsAdded(bodyID))
	{
		return state;
	}

	state.motionType = bodyInterface.GetMotionType(bodyID);
	state.objectLayer = bodyInterface.GetObjectLayer(bodyID);
	state.isActive = bodyInterface.IsActive(bodyID);
	state.isValid = true;
#else
	(void)bodyID;
#endif
	return state;
}

glm::vec4 NextPhysics::GetBodyDebugColor(NextBodyID bodyID) const
{
	const FNextPhysicsDebugState state = GetBodyDebugState(bodyID);
	return SelectDebugBodyColor(state.motionType, state.objectLayer, state.isActive, state.isValid);
}

void NextPhysics::RemoveBody(NextBodyID bodyID)
{
#if WITH_PHYSIC
    BodyInterface &bodyInterface = context_->physicsSystem.GetBodyInterface();
    bodyInterface.RemoveBody(bodyID);
    bodies_.erase(bodyID);
#endif
}

void NextPhysics::SetBodyActive(NextBodyID bodyID, bool active)
{
#if WITH_PHYSIC
    if (bodyID.IsInvalid()) return;

    BodyInterface &bodyInterface = context_->physicsSystem.GetBodyInterface();
    bool isAdded = bodyInterface.IsAdded(bodyID);

    if (active && isAdded)
    {
        // Dynamic and kinematic bodies both belong in the moving broadphase.
        // Kinematic bodies in NON_MOVING can be moved, but they won't reliably push sleeping dynamic bodies.
        EMotionType mt = bodyInterface.GetMotionType(bodyID);
        ObjectLayer targetLayer = (mt == EMotionType::Static) ? NextLayers::NON_MOVING : NextLayers::MOVING;
        bodyInterface.SetObjectLayer(bodyID, targetLayer);
    }
    else if (!active && isAdded)
    {
        bodyInterface.SetObjectLayer(bodyID, NextLayers::HIDDEN);
    }
#endif
}

void NextPhysics::DrawDebugBodies() const
{
#if WITH_PHYSIC
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

		if (!bodyInterface.IsAdded(bodyId))
		{
			continue;
		}

		const TransformedShape transformedShape = bodyInterface.GetTransformedShape(bodyId);
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
#endif
}

void NextPhysics::OnSceneStarted()
{
	TimeElapsed = 0;
	TimeSimulated = 0;
}

void NextPhysics::OnSceneDestroyed()
{
#if WITH_PHYSIC
	BodyInterface &bodyInterface = context_->physicsSystem.GetBodyInterface();
	
	for (auto& body : bodies_)
	{
		if (body.first.IsInvalid()) continue;
		bodyInterface.RemoveBody(body.first);
		bodyInterface.DestroyBody(body.first);
	}
	
	bodies_.clear();
#endif
}
