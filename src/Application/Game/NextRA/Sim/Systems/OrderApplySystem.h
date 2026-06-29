#pragma once

#include "Net/Order.h"
#include "Sim/PathfindGrid.h"
#include "Sim/SimWorld.h"

#include <span>

namespace NextRA::Sim
{
    void ApplyOrders(FSimWorld& world, const FPathfindGrid& grid, std::span<const Net::FOrder> orders);
}
