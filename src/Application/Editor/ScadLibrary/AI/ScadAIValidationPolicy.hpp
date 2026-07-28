#pragma once

#include <cstddef>

namespace ScadLibrary::AI
{
    struct FScadAIValidationPolicy
    {
        static constexpr size_t maxOperations = 128;
        static constexpr size_t maxSourceBytes = 2 * 1024 * 1024;
        static constexpr size_t maxHistoryMessages = 24;
        static constexpr size_t maxHistoryBytes = 128 * 1024;
        static constexpr size_t maxRigChannels = 256;
        static constexpr size_t maxRigKeysPerChannel = 1024;
        static constexpr size_t maxTerrainFeatures = 512;
        static constexpr size_t maxTerrainRules = 512;
        // Reasoning/high-quality models can spend more than two minutes before
        // emitting the first structured token. Keep this below the provider
        // transport's ten-minute hard timeout.
        static constexpr int advancedModelDeadlineMs = 300000;
        static constexpr int repairBudget = 1;
    };
} // namespace ScadLibrary::AI
