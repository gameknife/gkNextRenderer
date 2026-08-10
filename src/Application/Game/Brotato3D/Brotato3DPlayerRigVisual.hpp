#pragma once

#include <array>
#include <memory>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "Engine/Assets/Data/RigAsset.hpp"
#include "Gameplay/Rig/RigInstance.h"

namespace Assets
{
    class Node;
    class Scene;
    struct FMaterial;
    class Model;
}

namespace Brotato3D
{
    class FPlayerRigVisual
    {
    public:
        bool LoadRig(const std::string& scadPath);
        void InjectAssets(std::vector<::Assets::Model>& models,
                          std::vector<::Assets::FMaterial>& materials,
                          uint32_t defaultTintMaterialId);
        void OnSceneLoaded(::Assets::Scene& scene);
        void OnSceneUnloaded();

        void SetVisible(bool visible);
        void SetTintMaterial(uint32_t materialId);
        void ResetFacing(const glm::vec3& direction);
        void Update(const glm::vec3& feetPosition,
                    const glm::vec3& movementVelocity,
                    const glm::vec3& aimDirection,
                    float deltaSeconds);

    private:
        ::Assets::Node* BoneNode(std::string_view boneName);
        void ApplyTintMaterials();

        ::Assets::FRigAsset asset_{};
        bool hasRig_ = false;
        bool injected_ = false;
        bool bound_ = false;
        bool visible_ = false;
        bool walking_ = false;

        std::vector<uint32_t> partModelIds_;
        std::vector<std::array<uint32_t, 16>> partMaterialIds_;
        std::vector<::Assets::Node*> partNodes_;
        uint32_t tintMaterialId_ = 0;

        ::Assets::Scene* scene_ = nullptr;
        std::shared_ptr<::Assets::Node> worldNode_;
        std::vector<::Assets::Node*> boneNodes_;
        NextGameplay::FRigAnimator animator_;
        glm::vec3 lowerFacingDir_{0.0f, 0.0f, -1.0f};
    };
}
