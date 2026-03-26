#pragma once
#include "Common/CoreMinimal.hpp"
#include "Utilities/Glm.hpp"

namespace Assets
{
    inline constexpr const char* kLDrawLibraryRootEntry = "assets/ldraw";
    inline constexpr const char* kLDrawShadowLibraryRootEntry = "assets/ldrawShadow";
    inline constexpr const char* kLDrawConnectivityRootEntry = "assets/ldrawConn";
    inline constexpr const char* kLDrawLibraryPakPath = "assets/paks/ldraw.pak";

    bool EnsureLDrawLibraryPakMounted();
    bool LoadLDrawTextResource(const std::string& pathOrEntry, std::string& outText);

    struct LDrawColor
    {
        int code;
        std::string name;
        glm::vec3 diffuse;      // linear RGB
        glm::vec3 edge;         // linear RGB
        glm::vec3 secondaryDiffuse; // linear RGB for glitter/speckle style finishes
        float alpha;            // 0-1 (1=opaque)
        float luminance;        // 0-100
        bool hasSecondaryDiffuse;
        enum class Finish { Solid, Chrome, Pearlescent, Rubber, MatteMetallic, Glitter, Speckle };
        Finish finish;
    };

    class LDrawColorTable
    {
    public:
        void Parse(const std::string& ldconfigPath);
        const LDrawColor* GetColor(int code) const;
        const std::unordered_map<int, LDrawColor>& AllColors() const { return colors_; }
        static glm::vec3 SrgbToLinear(glm::vec3 srgb);

    private:
        std::unordered_map<int, LDrawColor> colors_;
    };

    class LDrawFileResolver
    {
    public:
        void BuildIndex(const std::string& ldrawRoot);
        bool BuildIndexFromMountedRoot(const std::string& ldrawRootEntry);
        void SetWarnOnMissing(bool enable) { warnOnMissing_ = enable; }
        std::string Resolve(const std::string& filename) const;

    private:
        std::unordered_map<std::string, std::string> pathIndex_;  // lowercase -> resolved path or pak entry
        bool warnOnMissing_ = true;
    };
}
