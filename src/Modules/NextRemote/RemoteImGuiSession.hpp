#pragma once

#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Runtime/GameInstance.hpp"
#include "Modules/NextRemote/CloudInputRouter.hpp"

#include <imgui.h>

#include <string>
#include <vector>

class NextEngine;

namespace Runtime::Remote
{
    class FRemoteImGuiSession final
    {
    public:
        FRemoteImGuiSession(NextEngine& engine, std::string sessionId);
        ~FRemoteImGuiSession();

        FRemoteImGuiSession(const FRemoteImGuiSession&) = delete;
        FRemoteImGuiSession& operator=(const FRemoteImGuiSession&) = delete;

        void HandleInputEvents(const std::vector<FCloudInputEvent>& events, VkExtent2D extent);
        ImDrawData* BuildDrawData(VkExtent2D extent, const Assets::Camera& camera);

        bool WantsCaptureKeyboard() const { return wantsCaptureKeyboard_; }
        bool WantsCaptureMouse() const { return wantsCaptureMouse_; }
        const std::string& SessionId() const { return sessionId_; }

    private:
        class FContextScope final
        {
        public:
            explicit FContextScope(ImGuiContext* context);
            ~FContextScope();

            FContextScope(const FContextScope&) = delete;
            FContextScope& operator=(const FContextScope&) = delete;

        private:
            ImGuiContext* previousContext_ = nullptr;
        };

        void ConfigureContext();
        void AddKeyEvent(const FCloudInputEvent& event);
        void AddMouseMoveEvent(const FCloudInputEvent& event, VkExtent2D extent);
        void AddMouseButtonEvent(const FCloudInputEvent& event, VkExtent2D extent);
        void AddWheelEvent(const FCloudInputEvent& event);
        void AddTextEvent(const FCloudInputEvent& event);

        NextEngine& engine_;
        std::string sessionId_;
        ImGuiContext* context_ = nullptr;
        bool wantsCaptureKeyboard_ = false;
        bool wantsCaptureMouse_ = false;
    };
}
