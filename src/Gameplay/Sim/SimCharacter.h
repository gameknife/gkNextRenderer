#pragma once

#include "Gameplay/AI/PathFollower.h"
#include "Gameplay/Sim/SimVisual.h"

#include <memory>
#include <vector>

namespace NextGameplay::Sim
{
    struct FSimCharacter
    {
        int id = -1;
        bool active = false;
        glm::vec3 position{0.0f};
        float yaw = 0.0f;
        float speed = 1.8f;
        NextGameplay::FPathFollower follower;
        bool moving = false;
        glm::vec3 moveTarget{0.0f};
        std::vector<glm::vec3> scriptWaypoints;
        EAnimHint anim = EAnimHint::Idle;
        std::unique_ptr<ISimVisual> visual;
    };
}
