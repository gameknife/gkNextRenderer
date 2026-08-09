#pragma once

#include "CitySolSimTypes.h"

#include "Engine/Assets/AssetsFwd.hpp"
#include "Gameplay/Sim/CharacterPool.h"

#include <vector>

namespace CitySolSim
{
    class CitizenSystem
    {
    public:
        void InjectAssets(std::vector<Assets::Model>& models, std::vector<Assets::FMaterial>& materials);
        void OnSceneLoaded(Assets::Scene& scene, double gameMinutes);
        void Tick(float deltaSeconds, float simulationSpeed, double gameMinutes, double dayMinutes,
                  int dayIndex, Assets::Scene& scene);
        void Clear();

        const std::vector<FCitizen>& Citizens() const { return citizens_; }
        const FCitizen* FindById(int id) const;
        const NextGameplay::Sim::FSimCharacter* CharacterFor(const FCitizen& citizen) const;
        glm::vec3 PositionOf(const FCitizen& citizen) const;
        int MovingCount() const;

    private:
        ECitizenActivity DesiredActivity(const FCitizen& citizen, double dayMinutes) const;
        void StartActivity(FCitizen& citizen, ECitizenActivity activity, double gameMinutes, int dayIndex);

        NextGameplay::Sim::FCharacterPool characterPool_;
        std::vector<FCitizen> citizens_;
    };
}
