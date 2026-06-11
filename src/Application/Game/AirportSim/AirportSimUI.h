#pragma once

#include "AirportSimTypes.h"

#include <glm/glm.hpp>

namespace AirportSim
{
    class AgentSystem;
    class AirportMap;
    class DecisionScheduler;
    class FlightBoard;
    class QueueSystem;
    class TimeSystem;

    // 全 ImGui 的气泡/HUD/调试面板（§7.5）。世界坐标 → 屏幕投影画头顶气泡与名牌。
    class AirportSimUI
    {
    public:
        struct FState
        {
            bool showDebugPanel = false; // F8
            bool showOverlay = true;     // 名牌+气泡
            bool showPoiMarkers = false;
            bool llmEnabled = true;
            int  followAgentId = -1;     // -1 = 自由观察
        };

        void Draw(const glm::mat4& viewProjection, double gameMinutes, TimeSystem& time, const FlightBoard& flights,
                  AgentSystem& agents, const AirportMap& map, const QueueSystem& queues,
                  const DecisionScheduler& scheduler, bool llmConnected);

        FState& State() { return state_; }
        const FState& State() const { return state_; }

    private:
        void DrawHud(TimeSystem& time, const FlightBoard& flights, const AgentSystem& agents,
                     const DecisionScheduler& scheduler, bool llmConnected);
        void DrawFlightBoardHud(const FlightBoard& flights);
        void DrawDebugPanel(double gameMinutes, TimeSystem& time, AgentSystem& agents, const QueueSystem& queues,
                            const DecisionScheduler& scheduler);
        void DrawWorldOverlay(const glm::mat4& viewProjection, double gameMinutes, const AgentSystem& agents,
                              const AirportMap& map, const glm::vec3& cameraEye);

        FState state_;
        glm::vec3 cameraEye_{0.0f};

    public:
        void SetCameraEye(const glm::vec3& eye) { cameraEye_ = eye; }
    };
}
