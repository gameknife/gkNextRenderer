#pragma once
#include "Engine/Assets/AssetsFwd.hpp"
#include "Engine/Assets/Core/Model.hpp"
#include "Engine/Assets/Data/Skeleton.hpp"
#include "Modules/LDrawLoader/FLDrawTypes.h"

#include <unordered_map>

namespace Assets
{
    struct PartModelInfo
    {
        uint32_t modelIdx;
        std::vector<int> sectionColors;
    };

    struct LDrawPartTemplate;

    class FLDrawLoader
    {
    public:
        static bool LoadLDrawScene(
            const std::string& filename,
            EnvironmentSetting& cameraInit,
            std::vector<std::shared_ptr<Node>>& nodes,
            std::vector<Model>& models,
            std::vector<FMaterial>& materials,
            std::vector<LightObject>& lights,
            std::vector<AnimationTrack>& tracks,
            std::vector<Skeleton>& skeletons,
            const LDrawLoadOptions& options = {});

        // Step data from last LoadLDrawScene call
        // Maps node instanceId -> build step index (0-based)
        static const std::unordered_map<uint32_t, int32_t>& GetLastLoadStepMap();
        static const std::unordered_map<uint32_t, std::string>& GetLastLoadPartFileMap();
        static int32_t GetLastLoadTotalSteps();
        static bool GetLastLoadIsFreeBuild();

    private:
        static PartModelInfo BuildPartModel(
            const LDrawPartTemplate& tmpl,
            std::vector<Model>& models,
            const LDrawLoadOptions& options);
    };
}
