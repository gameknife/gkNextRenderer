#include "TrafficSystem.h"

#include "CitySolSimConfig.hpp"

#include <algorithm>
#include <cmath>

#include <fmt/format.h>
#include <glm/gtc/quaternion.hpp>

#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Assets/Loaders/FProcModel.hpp"
#include "Engine/Runtime/Components/PhysicsComponent.hpp"
#include "Engine/Runtime/Scene/SceneBuilder.hpp"

namespace CitySolSim
{
    namespace
    {
        std::array<glm::vec3, 4> BuildTrafficLoop(int vehicleIndex, bool clockwise)
        {
            const int leftIndex = (vehicleIndex * 3) % 8;
            const int rightIndex = std::min(10, leftIndex + 2 + vehicleIndex % 3);
            const int topIndex = (vehicleIndex * 3 + vehicleIndex / 7) % 5;
            const int bottomIndex = std::min(6, topIndex + 1 + (vehicleIndex / 3) % 2);
            const float left = Config::kRoadX[static_cast<size_t>(leftIndex)];
            const float right = Config::kRoadX[static_cast<size_t>(rightIndex)];
            const float top = Config::kRoadZ[static_cast<size_t>(topIndex)];
            const float bottom = Config::kRoadZ[static_cast<size_t>(bottomIndex)];

            if (clockwise)
            {
                return {{
                    {left - 2.0f, Config::kVehicleGroundY, top + 2.0f},
                    {right + 2.0f, Config::kVehicleGroundY, top + 2.0f},
                    {right + 2.0f, Config::kVehicleGroundY, bottom - 2.0f},
                    {left - 2.0f, Config::kVehicleGroundY, bottom - 2.0f},
                }};
            }
            return {{
                {right - 2.0f, Config::kVehicleGroundY, top - 2.0f},
                {left + 2.0f, Config::kVehicleGroundY, top - 2.0f},
                {left + 2.0f, Config::kVehicleGroundY, bottom + 2.0f},
                {right - 2.0f, Config::kVehicleGroundY, bottom + 2.0f},
            }};
        }

        EVehicleKind KindForSlot(int slot)
        {
            if (slot % 11 == 0) return EVehicleKind::Bus;
            if (slot % 7 == 0) return EVehicleKind::Delivery;
            if (slot % 5 == 0) return EVehicleKind::Taxi;
            return EVehicleKind::Car;
        }

        glm::vec3 VehicleScale(EVehicleKind kind)
        {
            switch (kind)
            {
            case EVehicleKind::Bus: return {1.12f, 1.55f, 1.70f};
            case EVehicleKind::Delivery: return {1.08f, 1.45f, 1.35f};
            case EVehicleKind::Taxi: return {0.96f, 1.0f, 1.0f};
            case EVehicleKind::Car:
            default: return {1.0f, 1.0f, 1.0f};
            }
        }
    }

    void TrafficSystem::InjectAssets(std::vector<std::shared_ptr<Assets::Node>>& nodes,
                                     std::vector<Assets::Model>& models,
                                     std::vector<Assets::FMaterial>& materials)
    {
        vehicles_.clear();
        vehicles_.reserve(Config::kVehicleCount);

        models.push_back(Assets::FProcModel::CreateBox({-0.82f, 0.0f, -1.75f}, {0.82f, 0.55f, 1.75f}));
        const uint32_t bodyModel = static_cast<uint32_t>(models.size() - 1);
        models.push_back(Assets::FProcModel::CreateBox({-0.66f, 0.55f, -0.86f}, {0.66f, 1.10f, 0.92f}));
        const uint32_t cabinModel = static_cast<uint32_t>(models.size() - 1);
        models.push_back(Assets::FProcModel::CreateBox({-0.12f, 0.42f, -1.78f}, {0.12f, 0.72f, -1.74f}));
        const uint32_t lampModel = static_cast<uint32_t>(models.size() - 1);

        const std::array<glm::vec3, 8> colors = {{
            {0.84f, 0.18f, 0.14f}, {0.16f, 0.42f, 0.78f}, {0.93f, 0.72f, 0.12f}, {0.90f, 0.90f, 0.87f},
            {0.18f, 0.62f, 0.50f}, {0.54f, 0.28f, 0.68f}, {0.14f, 0.17f, 0.20f}, {0.86f, 0.43f, 0.18f},
        }};
        std::array<uint32_t, 8> bodyMaterials{};
        for (size_t i = 0; i < colors.size(); ++i)
        {
            bodyMaterials[i] = Assets::SceneBuilder::AddLambertianMaterial(materials, colors[i]);
        }
        const uint32_t glassMaterial = Assets::SceneBuilder::AddLambertianMaterial(materials, {0.12f, 0.24f, 0.34f});
        const uint32_t lampMaterial = Assets::SceneBuilder::AddDiffuseLightMaterial(materials, {1.0f, 0.86f, 0.55f}, 3.5f);

        for (int slot = 0; slot < Config::kVehicleCount; ++slot)
        {
            FVehicle vehicle;
            vehicle.id = slot + 1;
            vehicle.kind = KindForSlot(slot);
            vehicle.speed = vehicle.kind == EVehicleKind::Bus ? 8.5f :
                            vehicle.kind == EVehicleKind::Delivery ? 9.5f : 11.0f + static_cast<float>(slot % 4);
            vehicle.route = BuildTrafficLoop(slot, slot % 2 == 0);
            const float routeFraction = static_cast<float>((slot * 37) % 100) / 100.0f;
            vehicle.position = glm::mix(vehicle.route[0], vehicle.route[1], routeFraction);
            vehicle.routeTarget = 1;

            vehicle.rootNode = Assets::Node::CreateNode(
                fmt::format("city_vehicle_{:02d}", slot), vehicle.position,
                glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f), static_cast<uint32_t>(nodes.size()));
            auto physics = std::make_shared<Runtime::PhysicsComponent>();
            physics->SetMobility(Runtime::ENodeMobility::Dynamic);
            vehicle.rootNode->AddComponent(physics);
            nodes.push_back(vehicle.rootNode);

            const glm::vec3 scale = VehicleScale(vehicle.kind);
            auto body = Assets::SceneBuilder::CreateRenderNode(
                fmt::format("city_vehicle_{:02d}_body", slot), glm::vec3(0.0f), scale,
                static_cast<uint32_t>(nodes.size()), bodyModel,
                bodyMaterials[static_cast<size_t>(slot) % bodyMaterials.size()]);
            body->SetParent(vehicle.rootNode);
            nodes.push_back(body);

            auto cabin = Assets::SceneBuilder::CreateRenderNode(
                fmt::format("city_vehicle_{:02d}_cabin", slot), glm::vec3(0.0f), scale,
                static_cast<uint32_t>(nodes.size()), cabinModel, glassMaterial);
            cabin->SetParent(vehicle.rootNode);
            nodes.push_back(cabin);

            auto headlights = Assets::SceneBuilder::CreateRenderNode(
                fmt::format("city_vehicle_{:02d}_lights", slot), glm::vec3(0.0f), scale,
                static_cast<uint32_t>(nodes.size()), lampModel, lampMaterial, true,
                glm::quat(1.0f, 0.0f, 0.0f, 0.0f), false);
            headlights->SetParent(vehicle.rootNode);
            nodes.push_back(headlights);

            vehicles_.push_back(std::move(vehicle));
        }
    }

    void TrafficSystem::Tick(float deltaSeconds, float simulationSpeed, Assets::Scene& scene)
    {
        const float dt = std::max(0.0f, deltaSeconds) * std::max(0.0f, simulationSpeed);
        for (FVehicle& vehicle : vehicles_)
        {
            if (vehicle.waitSeconds > 0.0f)
            {
                vehicle.waitSeconds = std::max(0.0f, vehicle.waitSeconds - deltaSeconds);
                continue;
            }

            float travel = vehicle.speed * dt;
            while (travel > 0.0f)
            {
                const glm::vec3 target = vehicle.route[vehicle.routeTarget];
                glm::vec3 delta = target - vehicle.position;
                delta.y = 0.0f;
                const float distance = glm::length(delta);
                if (distance <= 0.02f)
                {
                    vehicle.position = target;
                    vehicle.routeTarget = (vehicle.routeTarget + 1) % vehicle.route.size();
                    if ((vehicle.id + static_cast<int>(vehicle.routeTarget)) % 5 == 0)
                    {
                        vehicle.waitSeconds = 0.22f;
                        break;
                    }
                    continue;
                }

                const glm::vec3 direction = delta / distance;
                const float step = std::min(travel, distance);
                vehicle.position += direction * step;
                vehicle.distanceMeters += static_cast<double>(step);
                vehicle.yaw = std::atan2(direction.x, direction.z);
                travel -= step;
            }

            if (vehicle.rootNode)
            {
                vehicle.rootNode->SetTranslation(vehicle.position);
                vehicle.rootNode->SetRotation(glm::angleAxis(vehicle.yaw, glm::vec3(0.0f, 1.0f, 0.0f)));
                vehicle.rootNode->RecalcTransform(true);
            }
        }
        scene.MarkTransformDirty();
    }

    void TrafficSystem::Clear()
    {
        vehicles_.clear();
    }

    const FVehicle* TrafficSystem::FindById(int id) const
    {
        const auto it = std::find_if(vehicles_.begin(), vehicles_.end(),
                                     [id](const FVehicle& vehicle) { return vehicle.id == id; });
        return it == vehicles_.end() ? nullptr : &*it;
    }

    double TrafficSystem::TotalDistanceKm() const
    {
        double meters = 0.0;
        for (const FVehicle& vehicle : vehicles_)
        {
            meters += vehicle.distanceMeters;
        }
        return meters / 1000.0;
    }
}
