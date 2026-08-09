#pragma once

#include "Engine/Common/CoreMinimal.hpp"

#include <imgui.h>

namespace NextUI
{
    enum class EUiTextureLifetime;

    struct FUiTextureHandle
    {
        ImTextureID textureId = 0;
        ImVec2 pixelSize{0.0f, 0.0f};
        bool valid = false;
    };

    class FUiTextureResolver final
    {
    public:
        FUiTextureHandle Request(const std::string& path,
                                 bool srgb,
                                 EUiTextureLifetime lifetime,
                                 const std::function<ImTextureID(const std::string&)>& resolveByName);

    private:
        std::unordered_set<std::string> loadRequests_;
        std::unordered_map<std::string, ImVec2> pixelSizeCache_;
    };
}
