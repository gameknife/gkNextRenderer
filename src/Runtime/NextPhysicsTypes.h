#pragma once

#if WITH_PHYSIC
#include <Jolt/Jolt.h>
#include <Jolt/Core/Reference.h>
#include <Jolt/Physics/Body/BodyID.h>
#include <Jolt/Physics/Body/MotionType.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>

using NextBodyID = JPH::BodyID;
using NextMotionType = JPH::EMotionType;
using NextObjectLayer = JPH::ObjectLayer;
using NextMeshShapeSettings = JPH::MeshShapeSettings;

template <typename T>
using NextRefConst = JPH::RefConst<T>;

#else

#include <cstdint>
#include <functional>

struct NextBodyID {
    uint32_t id = 0xFFFFFFFF;
    bool IsInvalid() const { return id == 0xFFFFFFFF; }
    bool operator==(const NextBodyID& other) const { return id == other.id; }
    bool operator!=(const NextBodyID& other) const { return id != other.id; }
};

namespace std {
    template<> struct hash<NextBodyID> {
        size_t operator()(const NextBodyID& b) const { return hash<uint32_t>{}(b.id); }
    };
}

enum class NextMotionType {
    Static,
    Kinematic,
    Dynamic
};

using NextObjectLayer = uint16_t;

class NextMeshShapeSettings {};

template <typename T>
class NextRefConst {
public:
    NextRefConst(T* ptr = nullptr) : ptr_(ptr) {}
    T* GetPtr() const { return ptr_; }
    operator bool() const { return ptr_ != nullptr; }
    T* operator->() const { return ptr_; }
    bool operator==(const NextRefConst& other) const { return ptr_ == other.ptr_; }
    bool operator!=(const NextRefConst& other) const { return ptr_ != other.ptr_; }
private:
    T* ptr_;
};

#endif

namespace NextLayers {
    static constexpr NextObjectLayer NON_MOVING = 0;
    static constexpr NextObjectLayer MOVING = 1;
    static constexpr NextObjectLayer HIDDEN = 2;
    static constexpr NextObjectLayer NUM_LAYERS = 3;
}
