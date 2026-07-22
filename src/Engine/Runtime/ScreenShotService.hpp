#pragma once

#include "Engine/Common/CoreMinimal.hpp"

#include <cstdint>
#include <functional>
#include <string>

class NextEngine;

namespace Runtime
{
    class FScreenShotService final
    {
    public:
        struct FRequest
        {
            std::string tag;
            uint32_t accumulateFrames = 0;
            bool includeUi = false;
            std::function<void(const std::string& path)> onCompleted;
        };

        explicit FScreenShotService(NextEngine& engine);

        // Requests the standard full-output screenshot. Returns false if another
        // screenshot is already queued or being saved.
        bool Request(FRequest request = {});

        bool IsBusy() const;
        std::string GetDirectory() const;
        void EnsureDirectory() const;

    private:
        std::string BuildFilename(const std::string& tag);

        NextEngine& engine_;
        bool requestPending_ = false;
        std::string lastStem_;
        uint32_t stemSequence_ = 0;
    };
} // namespace Runtime
