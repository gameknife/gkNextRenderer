#pragma once
#include "Assets/Core/Model.hpp"
#include "Assets/Data/Skeleton.hpp"
#include "Assets/Loaders/FLDrawTypes.h"

#include <unordered_map>

namespace Assets
{
    struct FMaterial;
    class Node;

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
        static int32_t GetLastLoadTotalSteps();

    private:
        static PartModelInfo BuildPartModel(
            const LDrawPartTemplate& tmpl,
            std::vector<Model>& models,
            const LDrawLoadOptions& options);
    };
}
