#pragma once
#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Utilities/Glm.hpp"
#include "Modules/LDrawLoader/FLDrawConfig.h"

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
        bool fileWindingCCW = true;
        bool orientationInverted = false;
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
        LDrawPartTemplate ParseFileDirectGeometry(const std::string& filepath);
        void ClearCache();

        void SetMPDSubfiles(std::unordered_map<std::string, std::string> subfiles);
        void ClearMPDSubfiles();
        bool HasMPDSubfile(const std::string& name) const;

    private:
        void ParseStream(std::istream& input, int parentColor,
                         const glm::mat4& transform, BFCState bfc, bool expandSubfiles,
                         std::vector<LDrawFace>& outFaces,
                         std::vector<std::string>* dependencyFiles);
        LDrawPartTemplate ParseFileImpl(const std::string& filepath, bool expandSubfiles);
        void AppendTemplateInstance(
            const LDrawPartTemplate& tmpl,
            int parentColor,
            const glm::mat4& transform,
            bool invertWinding,
            std::vector<LDrawFace>& outFaces) const;

        int ResolveColor(int faceColor, int parentColor) const;

        LDrawColorTable& colors_;
        LDrawFileResolver& resolver_;
        std::unordered_map<std::string, LDrawPartTemplate> templateCache_;
        std::unordered_map<std::string, LDrawPartTemplate> directTemplateCache_;
        std::unordered_map<std::string, std::vector<std::string>> templateDependencies_;
        std::unordered_map<std::string, std::vector<std::string>> directTemplateDependencies_;
        std::unordered_map<std::string, std::string> mpdSubfiles_;
    };
}
