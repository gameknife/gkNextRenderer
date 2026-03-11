#pragma once
#include "Assets/Core/Model.hpp"
#include "Assets/Data/Skeleton.hpp"
#include "Assets/Loaders/FLDrawTypes.h"

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

    private:
        static PartModelInfo BuildPartModel(
            const LDrawPartTemplate& tmpl,
            std::vector<Model>& models,
            const LDrawLoadOptions& options);
    };
}
