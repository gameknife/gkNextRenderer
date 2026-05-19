#pragma once

#include "Engine/Assets/Data/Vertex.hpp"
#include "Engine/Assets/Loaders/FLDrawParser.h"
#include "Engine/Assets/Loaders/FLDrawTypes.h"

namespace Assets
{
    struct LDrawBuiltGeometry
    {
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
        std::vector<int> sectionColors;
    };

    class FLDrawGeometry
    {
    public:
        static LDrawBuiltGeometry BuildPartGeometry(const LDrawPartTemplate& tmpl,
                                                    const LDrawLoadOptions& options = {});
    };
}
