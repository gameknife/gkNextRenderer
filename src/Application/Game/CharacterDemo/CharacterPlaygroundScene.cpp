#include "CharacterPlaygroundScene.hpp"
#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Assets/Data/Material.hpp"
#include "Engine/Assets/Core/Model.hpp"
#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Assets/Data/Skeleton.hpp"
#include "Engine/Assets/Loaders/FProcModel.hpp"
#include "Engine/Assets/Loaders/LoaderRegistry.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/Scene/SceneBuilder.hpp"
#include "Engine/Runtime/Subsystems/NextPhysics.hpp"
#include "Engine/Runtime/Components/PhysicsComponent.hpp"
#include "Engine/Runtime/Components/SkinnedMeshComponent.hpp"
#include "Engine/Utilities/FileHelper.hpp"

#include <functional>
#include <random>
#include <cmath>

#include <spdlog/spdlog.h>

using namespace glm;

using Assets::Material;
using Assets::Model;

namespace
{
    void CharacterPlayground(Assets::EnvironmentSetting& cameraInit,
                             std::vector<std::shared_ptr<Assets::Node>>& nodes,
                             std::vector<Assets::Model>& models,
                             std::vector<Assets::FMaterial>& materials,
                             std::vector<Assets::LightObject>& lights,
                             std::vector<Assets::AnimationTrack>& tracks)
    {
        // Camera: slightly elevated, looking at origin
        Assets::Camera defaultCam;
        defaultCam.name = "PlaygroundCam";
        defaultCam.ModelView = lookAt(vec3(0, 3, 10), vec3(0, 1, 0), vec3(0, 1, 0));
        defaultCam.FieldOfView = 60;
        defaultCam.Aperture = 0;
        defaultCam.FocalDistance = 10;

        cameraInit.cameras.push_back(defaultCam);
        cameraInit.ControlSpeed = 5.0f;
        cameraInit.GammaCorrection = true;
        cameraInit.HasSky = true;
        cameraInit.HasSun = false;
        cameraInit.SunIntensity = 200.0f;
        cameraInit.SkyIntensity = 50.0f;

        // -- Ground material --
        uint32_t matBase = static_cast<uint32_t>(materials.size());
        materials.push_back({Material::Mixture(vec3(0.5f, 0.5f, 0.5f), 0.5f), "ground"});

        // -- Ground plane: large flat box (procedural, 100x100) --
        const float groundHalfSize = 50.0f;
        const float groundThickness = 0.5f;
        models.push_back(Assets::FProcModel::CreateBox(
            vec3(-groundHalfSize, -groundThickness, -groundHalfSize),
            vec3(groundHalfSize, 0.0f, groundHalfSize)));
        uint32_t groundModelId = static_cast<uint32_t>(models.size() - 1);

        nodes.push_back(Assets::SceneBuilder::CreateRenderNode("Ground",
                                                               vec3(0, 0, 0),
                                                               vec3(1),
                                                               static_cast<uint32_t>(nodes.size()),
                                                               groundModelId,
                                                               matBase));
    }
}

void RegisterCharacterPlaygroundScene()
{
    Assets::FLoaderRegistry::Get().RegisterProcScene("CharacterPlayground.proc", CharacterPlayground);
}
