#pragma once
#include "Common/CoreMinimal.hpp"
#include "Utilities/Glm.hpp"
#include "Assets/Loaders/FLDrawConfig.h"

namespace Assets
{
    constexpr int kLDrawColorInherit = -1;

    struct LDrawFace
    {
        glm::vec3 vertices[4];
        int vertexCount;        // 3 or 4
        int colorCode;          // actual color code, or kLDrawColorInherit
    };

    struct BFCState
    {
        bool certified = false;
        bool localCull = true;
        bool windingCCW = true;
        bool invertNext = false;
    };

    struct LDrawPartTemplate
    {
        std::string filename;
        std::vector<LDrawFace> faces;
    };

    class LDrawParser
    {
    public:
        LDrawParser(LDrawColorTable& colors, LDrawFileResolver& resolver);

        LDrawPartTemplate ParseFile(const std::string& filepath);
        void ClearCache();

    private:
        void ParseFileRecursive(const std::string& filepath, int parentColor,
                                const glm::mat4& transform, BFCState bfc,
                                std::vector<LDrawFace>& outFaces);

        int ResolveColor(int faceColor, int parentColor) const;

        LDrawColorTable& colors_;
        LDrawFileResolver& resolver_;
        std::unordered_map<std::string, LDrawPartTemplate> templateCache_;
    };
}
