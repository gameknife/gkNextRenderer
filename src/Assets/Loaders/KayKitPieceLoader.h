#pragma once
#include "Common/CoreMinimal.hpp"
#include "Assets/Core/Model.hpp"

namespace Assets
{
    struct FMaterial;

    struct KayKitPiece
    {
        std::string name;
        uint32_t modelId;
        uint32_t materialId;
        glm::vec3 aabbMin;
        glm::vec3 aabbMax;
    };

    class KayKitPieceLoader
    {
    public:
        // Load a single piece from the KayKit Platformer Pack.
        // Returns the piece index in the internal catalog, or -1 on failure.
        int LoadPiece(const std::string& pieceName,
                      const std::string& color,
                      std::vector<Model>& models,
                      std::vector<FMaterial>& materials);

        const KayKitPiece& GetPiece(int index) const { return pieces_[index]; }
        int GetPieceCount() const { return static_cast<int>(pieces_.size()); }

        // Collision helpers derived from AABB
        glm::vec3 GetCollisionExtent(int index) const;
        glm::vec3 GetCollisionOffset(int index) const;

    private:
        std::vector<KayKitPiece> pieces_;
        std::unordered_map<std::string, uint32_t> colorToMaterialId_;

        static std::string BuildPath(const std::string& pieceName, const std::string& color);
    };
}
