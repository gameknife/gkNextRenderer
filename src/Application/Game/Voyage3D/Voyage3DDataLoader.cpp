#include "Voyage3DDataLoader.hpp"

#include "Engine/Utilities/Exception.hpp"
#include "Engine/Utilities/FileHelper.hpp"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <vector>

namespace
{
    [[noreturn]] void FailJson(const std::string& path, const std::string& message)
    {
        SPDLOG_ERROR("[Voyage3D] JSON error in {}: {}", path, message);
        Throw(std::runtime_error(fmt::format("Voyage3D JSON error in {}: {}", path, message)));
    }

    nlohmann::json ReadDocument(const std::string& path)
    {
        std::vector<uint8_t> data;
        if (!Utilities::Package::FPackageFileSystem::GetInstance().LoadFile(path, data))
        {
            FailJson(path, "file not found");
        }

        try
        {
            return nlohmann::json::parse(data.begin(), data.end());
        }
        catch (const std::exception& exception)
        {
            FailJson(path, exception.what());
        }
    }

    const nlohmann::json& RequireField(const nlohmann::json& object, const char* key, const std::string& path)
    {
        if (!object.contains(key))
        {
            FailJson(path, fmt::format("missing field '{}'", key));
        }
        return object.at(key);
    }

    std::string RequireString(const nlohmann::json& object, const char* key, const std::string& path)
    {
        const auto& value = RequireField(object, key, path);
        if (!value.is_string())
        {
            FailJson(path, fmt::format("'{}' must be string", key));
        }
        return value.get<std::string>();
    }

    int RequireInt(const nlohmann::json& object, const char* key, const std::string& path)
    {
        const auto& value = RequireField(object, key, path);
        if (!value.is_number_integer())
        {
            FailJson(path, fmt::format("'{}' must be integer", key));
        }
        return value.get<int>();
    }

    float RequireFloat(const nlohmann::json& object, const char* key, const std::string& path)
    {
        const auto& value = RequireField(object, key, path);
        if (!value.is_number())
        {
            FailJson(path, fmt::format("'{}' must be number", key));
        }
        return value.get<float>();
    }

    glm::vec3 RequireVec3(const nlohmann::json& object, const char* key, const std::string& path)
    {
        const auto& value = RequireField(object, key, path);
        if (!value.is_array() || value.size() != 3)
        {
            FailJson(path, fmt::format("'{}' must be [x,y,z]", key));
        }
        return glm::vec3(value.at(0).get<float>(), value.at(1).get<float>(), value.at(2).get<float>());
    }

    std::vector<std::string> RequireStringArray(const nlohmann::json& object, const char* key, const std::string& path)
    {
        const auto& value = RequireField(object, key, path);
        if (!value.is_array())
        {
            FailJson(path, fmt::format("'{}' must be string array", key));
        }

        std::vector<std::string> strings;
        strings.reserve(value.size());
        for (const auto& entry : value)
        {
            if (!entry.is_string())
            {
                FailJson(path, fmt::format("'{}' must contain only strings", key));
            }
            strings.push_back(entry.get<std::string>());
        }
        return strings;
    }
}

namespace Voyage3D
{
    std::vector<FPortDef> LoadPorts(const std::string& path)
    {
        const nlohmann::json document = ReadDocument(path);
        const auto& portsJson = RequireField(document, "ports", path);
        if (!portsJson.is_array())
        {
            FailJson(path, "'ports' must be an array");
        }

        std::vector<FPortDef> ports;
        ports.reserve(portsJson.size());
        for (const auto& entry : portsJson)
        {
            FPortDef port;
            port.id = RequireString(entry, "id", path);
            port.name = RequireString(entry, "name", path);
            port.lon = RequireFloat(entry, "lon", path);
            port.lat = RequireFloat(entry, "lat", path);
            port.nation = RequireString(entry, "nation", path);
            port.specialtyId = RequireString(entry, "specialty", path);
            port.color = RequireVec3(entry, "color", path);
            ports.push_back(port);
        }
        return ports;
    }

    std::vector<FGoodsDef> LoadGoods(const std::string& path)
    {
        const nlohmann::json document = ReadDocument(path);
        const auto& goodsJson = RequireField(document, "goods", path);
        if (!goodsJson.is_array())
        {
            FailJson(path, "'goods' must be an array");
        }

        std::vector<FGoodsDef> goods;
        goods.reserve(goodsJson.size());
        for (const auto& entry : goodsJson)
        {
            FGoodsDef good;
            good.id = RequireString(entry, "id", path);
            good.name = RequireString(entry, "name", path);
            good.basePrice = RequireInt(entry, "basePrice", path);
            good.supplyPorts = RequireStringArray(entry, "supplyPorts", path);
            good.demandPorts = RequireStringArray(entry, "demandPorts", path);
            goods.push_back(good);
        }
        return goods;
    }

    std::vector<FShipDef> LoadShips(const std::string& path)
    {
        const nlohmann::json document = ReadDocument(path);
        const auto& shipsJson = RequireField(document, "ships", path);
        if (!shipsJson.is_array())
        {
            FailJson(path, "'ships' must be an array");
        }

        std::vector<FShipDef> ships;
        ships.reserve(shipsJson.size());
        for (const auto& entry : shipsJson)
        {
            FShipDef ship;
            ship.id = RequireString(entry, "id", path);
            ship.name = RequireString(entry, "name", path);
            ship.hp = RequireInt(entry, "hp", path);
            ship.cargoMax = RequireInt(entry, "cargoMax", path);
            ship.cannonCount = RequireInt(entry, "cannonCount", path);
            ship.speedKnots = RequireFloat(entry, "speedKnots", path);
            ship.price = RequireInt(entry, "price", path);
            ship.size = RequireVec3(entry, "size", path);
            ship.sailHeight = RequireFloat(entry, "sailHeight", path);
            ships.push_back(ship);
        }
        return ships;
    }

    std::vector<FLandmassBlock> LoadLandmass(const std::string& path)
    {
        const nlohmann::json document = ReadDocument(path);
        const auto& blocksJson = RequireField(document, "blocks", path);
        if (!blocksJson.is_array())
        {
            FailJson(path, "'blocks' must be an array");
        }

        std::vector<FLandmassBlock> blocks;
        blocks.reserve(blocksJson.size());
        for (const auto& entry : blocksJson)
        {
            FLandmassBlock block;
            block.name = RequireString(entry, "name", path);
            block.min = RequireVec3(entry, "min", path);
            block.max = RequireVec3(entry, "max", path);
            block.color = RequireVec3(entry, "color", path);
            blocks.push_back(block);
        }
        return blocks;
    }

    std::vector<FEventDef> LoadEvents(const std::string& path)
    {
        const nlohmann::json document = ReadDocument(path);
        const auto& eventsJson = RequireField(document, "events", path);
        if (!eventsJson.is_array())
        {
            FailJson(path, "'events' must be an array");
        }

        std::vector<FEventDef> events;
        events.reserve(eventsJson.size());
        for (const auto& entry : eventsJson)
        {
            FEventDef event;
            event.id = RequireString(entry, "id", path);
            event.weight = RequireInt(entry, "weight", path);
            event.effect = RequireString(entry, "effect", path);
            event.enemyShip = entry.value("enemyShip", std::string());
            event.rewardGold = entry.value("rewardGold", 0);
            event.messageKey = entry.value("messageKey", std::string());
            event.rewardCargoId = entry.value("rewardCargoId", std::string());
            event.rewardCargoQty = entry.value("rewardCargoQty", 0);
            event.hpDamage = entry.value("hpDamage", 0);
            event.speedDebuffMs = entry.value("speedDebuffMs", 0);
            events.push_back(event);
        }
        return events;
    }

    std::map<std::string, std::string> LoadPortLore(const std::string& path)
    {
        const nlohmann::json document = ReadDocument(path);
        const auto& loreJson = RequireField(document, "lore", path);
        if (!loreJson.is_object())
        {
            FailJson(path, "'lore' must be an object");
        }

        std::map<std::string, std::string> lore;
        for (const auto& [id, text] : loreJson.items())
        {
            if (!text.is_string())
            {
                FailJson(path, fmt::format("lore '{}' must be string", id));
            }
            lore[id] = text.get<std::string>();
        }
        return lore;
    }
}
