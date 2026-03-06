#pragma once
#include "Common/CoreMinimal.hpp"
#include "Utilities/Glm.hpp"

namespace Assets
{
    struct LDrawColor
    {
        int code;
        std::string name;
        glm::vec3 diffuse;      // linear RGB
        glm::vec3 edge;         // linear RGB
        float alpha;            // 0-1 (1=opaque)
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
        std::string Resolve(const std::string& filename) const;

    private:
        std::string ldrawRoot_;
        std::unordered_map<std::string, std::string> pathIndex_;  // lowercase -> actual path
    };
}
