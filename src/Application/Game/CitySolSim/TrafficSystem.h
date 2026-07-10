#pragma once

#include "CitySolSimTypes.h"

#include "Engine/Assets/AssetsFwd.hpp"

#include <memory>
#include <vector>

namespace CitySolSim
{
    class TrafficSystem
    {
    public:
        void InjectAssets(std::vector<std::shared_ptr<Assets::Node>>& nodes,
                          std::vector<Assets::Model>& models,
                          std::vector<Assets::FMaterial>& materials);
        void Tick(float deltaSeconds, float simulationSpeed, Assets::Scene& scene);
        void Clear();

        const std::vector<FVehicle>& Vehicles() const { return vehicles_; }
        const FVehicle* FindById(int id) const;
        double TotalDistanceKm() const;

    private:
        std::vector<FVehicle> vehicles_;
    };
}
