#include "GeoTerrainSet.h"

#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Runtime/Components/TerrainComponent.hpp"

#include <algorithm>
#include <limits>

namespace NextWorldTravel
{
    namespace
    {
        glm::vec2 CentreOf(const Runtime::TerrainComponent& terrain)
        {
            glm::vec2 lo(0.0f);
            glm::vec2 hi(0.0f);
            if (!terrain.WorldBoundsXZ(lo, hi))
            {
                return glm::vec2(0.0f);
            }
            return (lo + hi) * 0.5f;
        }
    } // namespace

    void FGeoTerrainSet::Clear()
    {
        terrains_.clear();
        primary_ = nullptr;
        worldMin_ = glm::vec2(0.0f);
        worldMax_ = glm::vec2(0.0f);
    }

    void FGeoTerrainSet::Collect(Assets::Scene& scene)
    {
        Clear();
        glm::vec2 lo(std::numeric_limits<float>::max());
        glm::vec2 hi(std::numeric_limits<float>::lowest());
        for (const std::shared_ptr<Assets::Node>& node : scene.Nodes())
        {
            auto* component = node->GetComponent<Runtime::TerrainComponent>();
            if (component == nullptr || !component->HasData())
            {
                continue;
            }
            terrains_.push_back(component);
            glm::vec2 partLo(0.0f);
            glm::vec2 partHi(0.0f);
            if (component->WorldBoundsXZ(partLo, partHi))
            {
                lo = glm::min(lo, partLo);
                hi = glm::max(hi, partHi);
            }
        }
        if (terrains_.empty())
        {
            return;
        }
        worldMin_ = lo;
        worldMax_ = hi;
        primary_ = At(0.0f, 0.0f);
    }

    const Runtime::TerrainComponent* FGeoTerrainSet::At(float worldX, float worldZ) const
    {
        if (terrains_.empty())
        {
            return nullptr;
        }
        for (const Runtime::TerrainComponent* terrain : terrains_)
        {
            if (terrain->ContainsWorld(worldX, worldZ))
            {
                return terrain;
            }
        }
        // Outside the area entirely: answer with the nearest part rather than
        // nothing, so a query just past the rim still gets ground instead of
        // the caller's fallback height.
        const Runtime::TerrainComponent* nearest = terrains_.front();
        float best = std::numeric_limits<float>::max();
        for (const Runtime::TerrainComponent* terrain : terrains_)
        {
            const glm::vec2 delta = CentreOf(*terrain) - glm::vec2(worldX, worldZ);
            const float distance = glm::dot(delta, delta);
            if (distance < best)
            {
                best = distance;
                nearest = terrain;
            }
        }
        return nearest;
    }

    float FGeoTerrainSet::SampleHeight(float worldX, float worldZ) const
    {
        const Runtime::TerrainComponent* terrain = At(worldX, worldZ);
        return terrain != nullptr ? terrain->SampleHeight(worldX, worldZ) : 0.0f;
    }

    bool FGeoTerrainSet::IsWater(float worldX, float worldZ) const
    {
        const Runtime::TerrainComponent* terrain = At(worldX, worldZ);
        return terrain != nullptr && terrain->IsWater(worldX, worldZ);
    }

    float FGeoTerrainSet::GetSizeX() const
    {
        return primary_ != nullptr ? primary_->GetSizeX() : 0.0f;
    }

    float FGeoTerrainSet::GetSizeY() const
    {
        return primary_ != nullptr ? primary_->GetSizeY() : 0.0f;
    }

    int FGeoTerrainSet::GetCellsX() const
    {
        return primary_ != nullptr ? primary_->GetCellsX() : 0;
    }

    int FGeoTerrainSet::GetCellsY() const
    {
        return primary_ != nullptr ? primary_->GetCellsY() : 0;
    }
} // namespace NextWorldTravel
