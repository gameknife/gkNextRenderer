#include "Runtime/Scene/SceneBuilder.h"

#include "Assets/Core/Node.h"
#include "Assets/Core/Scene.hpp"
#include "Assets/Data/Material.hpp"
#include "Runtime/Components/RenderComponent.h"

namespace
{
    Assets::FMaterial CreateLambertianMaterial(const glm::vec3& color)
    {
        return {Assets::Material::Lambertian(glm::clamp(color, glm::vec3(0.0f), glm::vec3(1.0f)))};
    }

    Assets::FMaterial CreateDiffuseLightMaterial(const glm::vec3& color, float intensity)
    {
        const glm::vec3 clampedColor = glm::clamp(color, glm::vec3(0.0f), glm::vec3(1.0f));
        return {Assets::Material::DiffuseLight(clampedColor * std::max(0.0f, intensity))};
    }
}

namespace SceneBuilder
{
    uint32_t AddLambertianMaterial(std::vector<Assets::FMaterial>& materials, const glm::vec3& color)
    {
        materials.push_back(CreateLambertianMaterial(color));
        return static_cast<uint32_t>(materials.size() - 1);
    }

    uint32_t AddDiffuseLightMaterial(std::vector<Assets::FMaterial>& materials, const glm::vec3& color, float intensity)
    {
        materials.push_back(CreateDiffuseLightMaterial(color, intensity));
        return static_cast<uint32_t>(materials.size() - 1);
    }

    uint32_t AddLambertianMaterialToScene(Assets::Scene& scene, const glm::vec3& color)
    {
        return scene.AddMaterial(CreateLambertianMaterial(color));
    }

    uint32_t AddDiffuseLightMaterialToScene(Assets::Scene& scene, const glm::vec3& color, float intensity)
    {
        return scene.AddMaterial(CreateDiffuseLightMaterial(color, intensity));
    }

    std::shared_ptr<Assets::Node> CreateRenderNode(
        std::string_view name,
        const glm::vec3& translation,
        const glm::vec3& scale,
        uint32_t instanceId,
        uint32_t modelId,
        uint32_t materialId,
        bool visible,
        const glm::quat& rotation,
        bool rayCastVisible)
    {
        std::array<uint32_t, 16> materialIds{};
        materialIds[0] = materialId;
        return CreateRenderNode(name, translation, scale, instanceId, modelId, materialIds, visible, rotation, rayCastVisible);
    }

    std::shared_ptr<Assets::Node> CreateRenderNode(
        std::string_view name,
        const glm::vec3& translation,
        const glm::vec3& scale,
        uint32_t instanceId,
        uint32_t modelId,
        const std::array<uint32_t, 16>& materialIds,
        bool visible,
        const glm::quat& rotation,
        bool rayCastVisible)
    {
        auto node = Assets::Node::CreateNode(std::string(name), translation, rotation, scale, instanceId);
        auto renderComponent = std::make_shared<Runtime::RenderComponent>();
        renderComponent->SetModelId(modelId);
        renderComponent->SetMaterials(materialIds);
        renderComponent->SetVisible(visible);
        renderComponent->SetRayCastVisible(rayCastVisible);
        node->AddComponent(renderComponent);
        return node;
    }
}
