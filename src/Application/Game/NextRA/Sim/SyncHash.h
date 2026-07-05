#pragma once

#include "Sim/SimWorld.h"

#include <cstdint>

namespace NextRA::Sim
{
    uint64_t ComputeSyncHash(const FSimWorld& world);
}
