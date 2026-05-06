#pragma once

#include "Application/Flappy/FlappyCommon.hpp"

namespace Flappy
{
    FGameplayConfig LoadGameplayConfig(const std::string& path = "assets/configs/flappy/gameplay.json");
    FReplayConfig LoadReplayConfig(const std::string& path = "assets/configs/flappy/replay.json");
}
