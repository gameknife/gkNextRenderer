#include "CitizenSystem.h"

#include "CitySolSimConfig.hpp"

#include <algorithm>
#include <cmath>

#include <spdlog/spdlog.h>

#include "Engine/Assets/Core/Scene.hpp"

namespace CitySolSim
{
    void CitizenSystem::InjectAssets(std::vector<Assets::Model>& models,
                                     std::vector<Assets::FMaterial>& materials)
    {
        NextGameplay::Sim::FCharacterPoolConfig config;
        config.poolCapacity = Config::kCitizenCount;
        config.navCellSize = 2.0f;
        config.agentRadius = 0.30f;
        config.separationRadius = 0.85f;
        config.separationStrength = 1.4f;
        config.groundY = Config::kGroundY;
        config.parkedPosition = {0.0f, -100.0f, 0.0f};
        config.useRig = true;
        config.rigPath = Config::kCitizenRigPath;
        config.rigVisual.baseWalkSpeed = Config::kBaseWalkSpeed;
        config.nodeNamePrefix = "city_citizen";
        config.slotTints.reserve(Config::kCitizenCount);
        for (int slot = 0; slot < Config::kCitizenCount; ++slot)
        {
            config.slotTints.push_back(Config::kCitizenColors[static_cast<size_t>(slot) % Config::kCitizenColors.size()]);
        }
        characterPool_.Configure(config);
        characterPool_.InjectAssets(models, materials);
    }

    void CitizenSystem::OnSceneLoaded(Assets::Scene& scene, double gameMinutes)
    {
        characterPool_.OnSceneLoaded(scene);
        citizens_.clear();
        citizens_.reserve(Config::kCitizenCount);
        for (int slot = 0; slot < Config::kCitizenCount; ++slot)
        {
            const size_t homeIndex = static_cast<size_t>((slot * 5 + slot / 3) % Config::kHomes.size());
            const size_t workIndex = static_cast<size_t>((slot * 7 + 2) % Config::kWorkplaces.size());
            const size_t leisureIndex = static_cast<size_t>((slot * 3 + 1) % Config::kLeisure.size());
            const glm::vec3 spawn = Config::kHomes[homeIndex].position +
                                    glm::vec3(static_cast<float>(slot % 3 - 1) * 0.9f, 0.0f,
                                              static_cast<float>((slot / 3) % 2) * 0.75f);
            auto* character = characterPool_.Acquire(
                slot, spawn, Config::kCitizenColors[static_cast<size_t>(slot) % Config::kCitizenColors.size()]);
            if (character == nullptr)
            {
                continue;
            }
            character->speed = Config::kBaseWalkSpeed * (0.88f + static_cast<float>(slot % 7) * 0.035f);

            FCitizen citizen;
            citizen.id = slot + 1;
            citizen.poolSlot = slot;
            citizen.name = Config::kCitizenNames[static_cast<size_t>(slot) % Config::kCitizenNames.size()];
            citizen.homeIndex = homeIndex;
            citizen.workIndex = workIndex;
            citizen.leisureIndex = leisureIndex;
            citizen.destination = Config::kHomes[homeIndex].label;
            citizen.nextDecisionAt = gameMinutes;
            citizens_.push_back(std::move(citizen));
        }
        const double dayMinutes = std::fmod(gameMinutes, 24.0 * 60.0);
        const int dayIndex = static_cast<int>(std::floor(gameMinutes / (24.0 * 60.0)));
        for (FCitizen& citizen : citizens_)
        {
            const ECitizenActivity initialActivity = DesiredActivity(citizen, dayMinutes);
            if (initialActivity != ECitizenActivity::Home)
            {
                StartActivity(citizen, initialActivity, gameMinutes, dayIndex);
            }
        }
        SPDLOG_INFO("CitySolSim/Citizens: {} ScadRig citizens spawned, NavGrid {}x{}",
                    citizens_.size(), characterPool_.NavGrid().GetWidth(), characterPool_.NavGrid().GetHeight());
    }

    ECitizenActivity CitizenSystem::DesiredActivity(const FCitizen& citizen, double dayMinutes) const
    {
        const double staggered = std::fmod(dayMinutes + static_cast<double>((citizen.id % 7) - 3) * 5.0 + 1440.0, 1440.0);
        const double hour = staggered / 60.0;
        if (hour < 6.5 || hour >= 22.0) return ECitizenActivity::Home;
        if (hour >= 12.0 && hour < 13.2) return ECitizenActivity::Lunch;
        if (hour >= 8.0 && hour < 17.3) return ECitizenActivity::Work;
        return ECitizenActivity::Leisure;
    }

    void CitizenSystem::StartActivity(FCitizen& citizen, ECitizenActivity activity,
                                      double gameMinutes, int dayIndex)
    {
        auto& characters = characterPool_.Characters();
        if (citizen.poolSlot < 0 || static_cast<size_t>(citizen.poolSlot) >= characters.size())
        {
            return;
        }
        NextGameplay::Sim::FSimCharacter& character = characters[static_cast<size_t>(citizen.poolSlot)];
        glm::vec3 target{0.0f};
        switch (activity)
        {
        case ECitizenActivity::Home:
            target = Config::kHomes[citizen.homeIndex].position;
            citizen.destination = Config::kHomes[citizen.homeIndex].label;
            break;
        case ECitizenActivity::Work:
            target = Config::kWorkplaces[citizen.workIndex].position;
            citizen.destination = Config::kWorkplaces[citizen.workIndex].label;
            break;
        case ECitizenActivity::Lunch:
        case ECitizenActivity::Leisure:
            citizen.leisureIndex = (citizen.leisureIndex + static_cast<size_t>(dayIndex + citizen.id)) % Config::kLeisure.size();
            target = Config::kLeisure[citizen.leisureIndex].position;
            citizen.destination = Config::kLeisure[citizen.leisureIndex].label;
            break;
        }
        target += glm::vec3(static_cast<float>((citizen.id % 3) - 1) * 0.8f, 0.0f,
                            static_cast<float>(((citizen.id / 3) % 3) - 1) * 0.65f);
        citizen.commuteMeters += glm::distance(character.position, target);
        citizen.activity = activity;
        citizen.nextDecisionAt = gameMinutes + (activity == ECitizenActivity::Leisure ? 55.0 : 90.0);
        characterPool_.MoveTo(character, target);
    }

    void CitizenSystem::Tick(float deltaSeconds, float simulationSpeed, double gameMinutes,
                             double dayMinutes, int dayIndex, Assets::Scene& scene)
    {
        for (FCitizen& citizen : citizens_)
        {
            const ECitizenActivity wanted = DesiredActivity(citizen, dayMinutes);
            if (wanted != citizen.activity)
            {
                StartActivity(citizen, wanted, gameMinutes, dayIndex);
                continue;
            }

            auto& character = characterPool_.Characters()[static_cast<size_t>(citizen.poolSlot)];
            if (!character.moving && gameMinutes >= citizen.nextDecisionAt &&
                (wanted == ECitizenActivity::Leisure || wanted == ECitizenActivity::Lunch))
            {
                StartActivity(citizen, wanted, gameMinutes, dayIndex);
            }
            else if (!character.moving)
            {
                character.anim = wanted == ECitizenActivity::Work
                                     ? NextGameplay::Sim::EAnimHint::Work
                                     : NextGameplay::Sim::EAnimHint::Idle;
            }
        }
        characterPool_.Tick(deltaSeconds * simulationSpeed, scene);
    }

    void CitizenSystem::Clear()
    {
        citizens_.clear();
        characterPool_.Clear();
    }

    const FCitizen* CitizenSystem::FindById(int id) const
    {
        const auto it = std::find_if(citizens_.begin(), citizens_.end(),
                                     [id](const FCitizen& citizen) { return citizen.id == id; });
        return it == citizens_.end() ? nullptr : &*it;
    }

    const NextGameplay::Sim::FSimCharacter* CitizenSystem::CharacterFor(const FCitizen& citizen) const
    {
        const auto& characters = characterPool_.Characters();
        if (citizen.poolSlot < 0 || static_cast<size_t>(citizen.poolSlot) >= characters.size())
        {
            return nullptr;
        }
        return &characters[static_cast<size_t>(citizen.poolSlot)];
    }

    glm::vec3 CitizenSystem::PositionOf(const FCitizen& citizen) const
    {
        const auto* character = CharacterFor(citizen);
        return character != nullptr ? character->position : glm::vec3(0.0f);
    }

    int CitizenSystem::MovingCount() const
    {
        int count = 0;
        for (const auto& character : characterPool_.Characters())
        {
            count += character.active && character.moving ? 1 : 0;
        }
        return count;
    }
}
