#pragma once

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


namespace NextLayers {
    static constexpr NextObjectLayer NON_MOVING = 0;
    static constexpr NextObjectLayer MOVING = 1;
    static constexpr NextObjectLayer HIDDEN = 2;
    static constexpr NextObjectLayer NUM_LAYERS = 3;
}
