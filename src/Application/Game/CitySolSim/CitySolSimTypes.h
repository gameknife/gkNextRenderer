#pragma once

#include <array>
#include <memory>
#include <string>

#include <glm/glm.hpp>

namespace Assets
{
    class Node;
}

namespace CitySolSim
{
    enum class EVehicleKind
    {
        Car,
        Taxi,
        Bus,
        Delivery,
    };

    inline const char* VehicleKindLabel(EVehicleKind kind)
    {
        switch (kind)
        {
        case EVehicleKind::Taxi: return "出租车";
        case EVehicleKind::Bus: return "公交车";
        case EVehicleKind::Delivery: return "物流车";
        case EVehicleKind::Car:
        default: return "小汽车";
        }
    }

    enum class ECitizenActivity
    {
        Home,
        Work,
        Lunch,
        Leisure,
    };

    inline const char* CitizenActivityLabel(ECitizenActivity activity)
    {
        switch (activity)
        {
        case ECitizenActivity::Home: return "在家休息";
        case ECitizenActivity::Work: return "上班";
        case ECitizenActivity::Lunch: return "午餐";
        case ECitizenActivity::Leisure: return "休闲活动";
        default: return "活动中";
        }
    }

    struct FVehicle
    {
        int id = -1;
        EVehicleKind kind = EVehicleKind::Car;
        glm::vec3 position{0.0f};
        float yaw = 0.0f;
        float speed = 12.0f;
        std::array<glm::vec3, 4> route{};
        size_t routeTarget = 1;
        float waitSeconds = 0.0f;
        double distanceMeters = 0.0;
        std::shared_ptr<Assets::Node> rootNode;
    };

    struct FCitizen
    {
        int id = -1;
        int poolSlot = -1;
        std::string name;
        ECitizenActivity activity = ECitizenActivity::Home;
        size_t homeIndex = 0;
        size_t workIndex = 0;
        size_t leisureIndex = 0;
        std::string destination;
        double nextDecisionAt = 0.0;
        double commuteMeters = 0.0;
    };
}
