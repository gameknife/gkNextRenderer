#pragma once

#include "Common/CoreMinimal.hpp"

#include "Assets/Core/Node.h"
#include "Assets/Data/Material.hpp"
#include "Runtime/Components/RenderComponent.h"

namespace Brotato3DUtil
{
    inline constexpr const char* EnemiesConfigPath = "assets/configs/brotato3d/enemies.json";
    inline constexpr const char* WeaponsConfigPath = "assets/configs/brotato3d/weapons.json";
    inline constexpr const char* UpgradesConfigPath = "assets/configs/brotato3d/upgrades.json";
    inline constexpr const char* WavesConfigPath = "assets/configs/brotato3d/waves.json";
    inline constexpr const char* ShopItemsConfigPath = "assets/configs/brotato3d/shop_items.json";
    inline constexpr const char* ItemsConfigPath = "assets/configs/brotato3d/items.json";
    inline constexpr const char* CharactersConfigPath = "assets/configs/brotato3d/characters.json";
    inline constexpr float ArenaHalfWidth = 12.0f;
    inline constexpr float ArenaHalfDepth = 8.0f;
    inline constexpr float PlayerBaseSpeed = 5.0f;
    inline constexpr float PickupBaseRadius = 1.6f;
    inline constexpr size_t MaxWeaponSlots = 6;
    inline const glm::vec3 HiddenPosition(0.0f, -100.0f, 0.0f);

    inline uint32_t AddLambertMaterial(std::vector<Assets::FMaterial>& materials, const glm::vec3& color)
    {
        materials.push_back({Assets::Material::Lambertian(glm::clamp(color, glm::vec3(0.0f), glm::vec3(1.0f)))});
        return static_cast<uint32_t>(materials.size() - 1);
    }

    inline std::shared_ptr<Assets::Node> CreateRenderNode(const std::string& name,
                                                          const glm::vec3& translation,
                                                          const glm::vec3& scale,
                                                          uint32_t instanceId,
                                                          uint32_t modelId,
                                                          uint32_t materialId,
                                                          bool visible = true)
    {
        auto node = Assets::Node::CreateNode(name, translation, glm::quat(1.0f, 0.0f, 0.0f, 0.0f), scale, instanceId);
        auto renderComponent = std::make_shared<Runtime::RenderComponent>();
        renderComponent->SetModelId(modelId);
        renderComponent->SetMaterial({materialId});
        renderComponent->SetVisible(visible);
        node->AddComponent(renderComponent);
        return node;
    }

    inline glm::vec3 ClampToArena(const glm::vec3& pos, float radius)
    {
        return glm::vec3(std::clamp(pos.x, -ArenaHalfWidth + radius, ArenaHalfWidth - radius), pos.y,
                         std::clamp(pos.z, -ArenaHalfDepth + radius, ArenaHalfDepth - radius));
    }

    inline float DistanceXZ(const glm::vec3& a, const glm::vec3& b)
    {
        const glm::vec2 delta(a.x - b.x, a.z - b.z);
        return glm::length(delta);
    }

    inline glm::vec3 RotateY(const glm::vec3& dir, float radians)
    {
        const float c = std::cos(radians);
        const float s = std::sin(radians);
        return glm::normalize(glm::vec3(dir.x * c - dir.z * s, 0.0f, dir.x * s + dir.z * c));
    }

    inline std::string LightMaterialKey(const glm::vec3& color)
    {
        const glm::ivec3 quantized =
            glm::ivec3(glm::round(glm::clamp(color, glm::vec3(0.0f), glm::vec3(1.0f)) * 255.0f));
        return fmt::format("{}_{}_{}", quantized.r, quantized.g, quantized.b);
    }
}
