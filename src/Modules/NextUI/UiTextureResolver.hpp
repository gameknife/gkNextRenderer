#pragma once

#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Runtime/Interface/UserInterface.hpp"

#include <imgui.h>

namespace NextUI
{
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
