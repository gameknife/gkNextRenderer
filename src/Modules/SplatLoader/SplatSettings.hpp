#pragma once

#include "Engine/Common/CoreMinimal.hpp"

class NextEngine;

namespace Modules::Splat
{
    struct FSplatSettings
    {
        uint32_t bucketCount = 4096, maxCount = 0;
        bool sortCache = true;
        float sigma = 3.0f;
        bool forceAA = true;
        float aaStrength = 0.5f;
        bool proxyEnable = true;
        uint32_t proxyGridMax = 64, proxyBrickSize = 8;
        float proxySigma = 2.5f, proxyIsoThreshold = 0.35f, proxySimplifyRatio = 0.0f;
        bool shadowEnable = true, rayOcclusionEnable = true, proxyDebugVisible = false;
        bool receiveLighting = true;
        float lightingStrength = 0.35f;
        int proxyDebug = 0;
        bool visible = true;
    };

    std::shared_ptr<FSplatSettings> GetSettings(NextEngine& engine);
    std::shared_ptr<const FSplatSettings> GetSettings(const NextEngine& engine);
    void InstallSettings(NextEngine& engine);
}
