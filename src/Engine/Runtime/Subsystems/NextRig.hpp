#pragma once

#include "Engine/Assets/AssetsFwd.hpp"
#include "Engine/Assets/Core/Model.hpp"
#include "Engine/Common/CoreMinimal.hpp"

#include <glm/glm.hpp>

/// Rigid-body character rigs (ScadRig) as the engine sees them: opaque pools and instances.
///
/// Declared here and implemented in NextGameplay for the same reason NextPhysics is — the engine
/// owns the frame and the scene, someone else owns the domain. The rig loader lives in a module
/// and the animator lives in the gameplay layer, neither of which the engine may depend on; what
/// the engine does need is a place to tick them and a handle its consumers can share.
///
/// The one consumer that could not exist without this is C# gameplay: the script bindings hold no
/// gameplay types, so `Rig.*` in managed code is this interface and nothing else.
///
/// Ids are opaque and never reused within a scene. Zero is "none" for both pools and instances, so
/// a default-initialised handle is not a live rig.
class NextRig
{
public:
    static constexpr uint32_t kInvalidId = 0;

    virtual ~NextRig() = default;

    /// Loads `rigPath` (a ScadRig .scad file) and reserves `capacity` instances of it, injecting
    /// one copy of every part model per instance.
    ///
    /// Only valid while a scene is being built: `models` and `materials` are that scene's, and the
    /// GPU-driven primitive buffer is sized once from them — an instance that was not declared
    /// here cannot be conjured later. Returns kInvalidId when the rig fails to load, having
    /// injected nothing.
    virtual uint32_t DeclarePool(const std::string& rigPath,
                                 int32_t capacity,
                                 std::vector<Assets::Model>& models,
                                 std::vector<Assets::FMaterial>& materials) = 0;

    /// Instantiates the declared pools into the committed scene. Nothing can be acquired before it.
    virtual void OnSceneLoaded(Assets::Scene& scene) = 0;

    /// Drops every pool and instance. The scene owns the nodes and is tearing them down itself;
    /// this exists so the next scene does not inherit handles into a world that no longer exists.
    virtual void Clear() = 0;

    /// Advances every live animator. Called once per frame by the engine.
    virtual void Tick(float deltaSeconds) = 0;

    /// Takes a free instance from a pool, or kInvalidId when the pool is full. `tint` recolours the
    /// rig's tintable sections — the ROLECOLOR placeholder in the asset.
    virtual uint32_t Acquire(uint32_t poolId, const glm::vec3& position, float yawRadians,
                             const glm::vec3& tint) = 0;
    virtual void Release(uint32_t instanceId) = 0;
    virtual bool IsAlive(uint32_t instanceId) const = 0;

    /// Yaw is a rotation about +Y in radians, measured the way atan2(x, z) reports it, so a
    /// character faces its movement direction with SetTransform(p, atan2(dx, dz)).
    virtual void SetTransform(uint32_t instanceId, const glm::vec3& position, float yawRadians) = 0;
    virtual void SetVisible(uint32_t instanceId, bool visible) = 0;

    /// Starts a clip with a crossfade. Re-playing the current clip is a no-op, so this is safe to
    /// call every frame from a state machine — which is how gameplay code actually uses it.
    virtual void PlayClip(uint32_t instanceId, const std::string& clip, float fadeSeconds) = 0;
    virtual void SetPlaySpeed(uint32_t instanceId, float speed) = 0;

    /// Node id of one bone, for hanging something off it (a muzzle flash, a held item). Negative
    /// when the instance or the bone does not exist.
    virtual int32_t GetBoneNodeId(uint32_t instanceId, const std::string& boneName) const = 0;
    /// Node id of the instance's world node — the one its position and yaw are written to.
    virtual int32_t GetRootNodeId(uint32_t instanceId) const = 0;

    /// Whether a pool's asset has a clip by that name. A rig with no such clip animates to its
    /// bind pose, which reads as a broken character rather than as a typo, so a game that picks
    /// clip names at runtime should ask.
    virtual bool HasClip(uint32_t poolId, const std::string& clip) const = 0;
};
