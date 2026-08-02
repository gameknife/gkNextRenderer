#pragma once

#include <compare>
#include <cstdint>
#include <functional>
#include <memory>

class NextBodyID final
{
public:
    static constexpr uint32_t invalidValue = 0xffffffffu;
    static constexpr uint32_t maxBodyIndex = 0x7fffffu;

    constexpr NextBodyID() = default;
    explicit constexpr NextBodyID(uint32_t value) : value_(value) {}

    constexpr bool IsInvalid() const { return value_ == invalidValue; }
    constexpr uint32_t GetIndex() const { return value_ & maxBodyIndex; }
    constexpr uint32_t Value() const { return value_; }

    auto operator<=>(const NextBodyID&) const = default;

private:
    uint32_t value_ = invalidValue;
};

enum class NextMotionType : uint8_t
{
    Static,
    Kinematic,
    Dynamic,
};

using NextObjectLayer = uint16_t;

class NextMeshShape
{
public:
    virtual ~NextMeshShape() = default;
};

using NextMeshShapeHandle = std::shared_ptr<const NextMeshShape>;

namespace Runtime
{
    enum class ENodeMobility
    {
        Static,
        Dynamic,
        Kinematic
    };
}

namespace NextLayers {
    static constexpr NextObjectLayer NON_MOVING = 0;
    static constexpr NextObjectLayer MOVING = 1;
    static constexpr NextObjectLayer HIDDEN = 2;
    static constexpr NextObjectLayer NUM_LAYERS = 3;
}

template <>
struct std::hash<NextBodyID>
{
    size_t operator()(const NextBodyID& bodyId) const noexcept
    {
        return std::hash<uint32_t>{}(bodyId.Value());
    }
};
