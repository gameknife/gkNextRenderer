#include "Application/Game/NextAstrobot/UI/AstroHud.hpp"

#include <algorithm>

#include <fmt/format.h>
#include <imgui.h>

namespace NextAstrobot::AstroHud
{
    namespace
    {
        constexpr ImU32 kInk = IM_COL32(250, 250, 252, 235);
        constexpr ImU32 kShadow = IM_COL32(10, 12, 20, 190);
        constexpr ImU32 kAccent = IM_COL32(88, 172, 255, 250);

        std::string FormatClock(double seconds)
        {
            const int total = static_cast<int>(seconds);
            return fmt::format("{:02d}:{:02d}", total / 60, total % 60);
        }

        void ShadowText(ImDrawList& drawList, const ImVec2& position, ImU32 color, const char* text, float scale)
        {
            const float fontSize = ImGui::GetFontSize() * scale;
            ImFont* font = ImGui::GetFont();
            drawList.AddText(font, fontSize, ImVec2(position.x + 2.0f, position.y + 2.0f), kShadow, text);
            drawList.AddText(font, fontSize, position, color, text);
        }

        void DrawCentered(ImDrawList& drawList, const ImVec2& screen, float y, ImU32 color, const std::string& text,
                          float scale)
        {
            const float fontSize = ImGui::GetFontSize() * scale;
            const ImVec2 size = ImGui::GetFont()->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, text.c_str());
            ShadowText(drawList, ImVec2((screen.x - size.x) * 0.5f, y), color, text.c_str(), scale);
        }

        void DrawPlayingHud(ImDrawList& drawList, const ImVec2& screen, const FHudContext& context)
        {
            const FRunStats& stats = *context.stats;
            float y = 24.0f;
            ShadowText(drawList, ImVec2(28.0f, y), kInk, fmt::format("COINS  {}", stats.coins).c_str(), 1.6f);
            y += 34.0f;
            ShadowText(drawList, ImVec2(28.0f, y), kInk,
                       fmt::format("PUZZLE {} / {}", stats.puzzles, stats.puzzlesTotal).c_str(), 1.2f);
            y += 26.0f;
            ShadowText(drawList, ImVec2(28.0f, y), kInk,
                       fmt::format("RESCUE {} / {}", stats.rescued, stats.rescuedTotal).c_str(), 1.2f);

            const std::string clock = FormatClock(stats.elapsedSeconds);
            const float clockSize = ImGui::GetFontSize() * 1.4f;
            const ImVec2 clockExtent = ImGui::GetFont()->CalcTextSizeA(clockSize, FLT_MAX, 0.0f, clock.c_str());
            ShadowText(drawList, ImVec2(screen.x - clockExtent.x - 28.0f, 24.0f), kInk, clock.c_str(), 1.4f);

            if (!context.toast.empty() && context.toastAlpha > 0.01f)
            {
                const auto alpha = static_cast<int>(std::clamp(context.toastAlpha, 0.0f, 1.0f) * 255.0f);
                DrawCentered(drawList, screen, screen.y * 0.30f, IM_COL32(255, 226, 120, alpha), context.toast, 1.8f);
            }
        }

        void DrawResult(ImDrawList& drawList, const ImVec2& screen, const FHudContext& context)
        {
            drawList.AddRectFilled(ImVec2(0.0f, 0.0f), screen, IM_COL32(8, 10, 18, 195));
            const FRunStats& stats = *context.stats;
            float y = screen.y * 0.24f;
            DrawCentered(drawList, screen, y, kAccent, "LEVEL COMPLETE", 3.0f);
            y += 80.0f;
            DrawCentered(drawList, screen, y, kInk, fmt::format("Coins      {} / {}", stats.coins, stats.coinsTotal),
                         1.6f);
            y += 36.0f;
            DrawCentered(drawList, screen, y, kInk,
                         fmt::format("Puzzles    {} / {}", stats.puzzles, stats.puzzlesTotal), 1.6f);
            y += 36.0f;
            DrawCentered(drawList, screen, y, kInk,
                         fmt::format("Rescued    {} / {}", stats.rescued, stats.rescuedTotal), 1.6f);
            y += 36.0f;
            DrawCentered(drawList, screen, y, kInk, fmt::format("Deaths     {}", stats.deaths), 1.6f);
            y += 36.0f;
            DrawCentered(drawList, screen, y, kInk, fmt::format("Time       {}", FormatClock(stats.elapsedSeconds)),
                         1.6f);
            y += 64.0f;
            DrawCentered(drawList, screen, y, kInk, "[R] Replay      [Esc] Quit", 1.3f);
        }
    }

    bool Draw(const FHudContext& context)
    {
        if (!context.stats)
        {
            return false;
        }

        ImDrawList* drawList = ImGui::GetBackgroundDrawList();
        if (!drawList)
        {
            return false;
        }
        const ImVec2 screen = ImGui::GetIO().DisplaySize;

        switch (context.state)
        {
        case ELevelState::Title:
            // A band that fades out at both edges keeps the title readable without
            // flattening whatever the level's overview camera is framing.
            {
                constexpr ImU32 kBandCore = IM_COL32(6, 8, 16, 140);
                constexpr ImU32 kBandEdge = IM_COL32(6, 8, 16, 0);
                const float top = screen.y * 0.30f;
                const float middle = screen.y * 0.44f;
                const float bottom = screen.y * 0.62f;
                drawList->AddRectFilledMultiColor(ImVec2(0.0f, top), ImVec2(screen.x, middle), kBandEdge, kBandEdge,
                                                  kBandCore, kBandCore);
                drawList->AddRectFilledMultiColor(ImVec2(0.0f, middle), ImVec2(screen.x, bottom), kBandCore, kBandCore,
                                                  kBandEdge, kBandEdge);
            }
            DrawCentered(*drawList, screen, screen.y * 0.36f, kAccent, context.levelName, 3.4f);
            DrawCentered(*drawList, screen, screen.y * 0.50f, kInk, "Press Space to start", 1.6f);
            DrawCentered(*drawList, screen, screen.y * 0.56f, kInk, "WASD run   Space jump / hold to hover   X punch",
                         1.1f);
            break;
        case ELevelState::Intro:
            DrawCentered(*drawList, screen, screen.y * 0.88f, kInk, "Press any key to skip", 1.2f);
            break;
        case ELevelState::Paused:
            drawList->AddRectFilled(ImVec2(0.0f, 0.0f), screen, IM_COL32(8, 10, 18, 150));
            DrawCentered(*drawList, screen, screen.y * 0.42f, kAccent, "PAUSED", 3.0f);
            DrawCentered(*drawList, screen, screen.y * 0.54f, kInk, "[Esc] Resume", 1.4f);
            break;
        case ELevelState::Result:
            DrawResult(*drawList, screen, context);
            break;
        case ELevelState::Playing:
        case ELevelState::Dead:
        case ELevelState::Goal:
            DrawPlayingHud(*drawList, screen, context);
            if (context.deathFade > 0.001f)
            {
                const auto alpha = static_cast<int>(std::clamp(context.deathFade, 0.0f, 1.0f) * 255.0f);
                drawList->AddRectFilled(ImVec2(0.0f, 0.0f), screen, IM_COL32(0, 0, 0, alpha));
            }
            if (context.state == ELevelState::Goal)
            {
                DrawCentered(*drawList, screen, screen.y * 0.40f, kAccent, "GOAL!", 3.2f);
            }
            break;
        }

        if (context.showDebug)
        {
            ImGui::SetNextWindowPos(ImVec2(screen.x - 300.0f, 90.0f), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(280.0f, 0.0f), ImGuiCond_FirstUseEver);
            if (ImGui::Begin("Astro Debug", nullptr, ImGuiWindowFlags_NoFocusOnAppearing))
            {
                ImGui::Text("state       %s", LevelStateName(context.state));
                ImGui::Text("locomotion  %s", context.locomotion.c_str());
                ImGui::Text("position    %.2f %.2f %.2f", context.playerX, context.playerY, context.playerZ);
                ImGui::Text("onGround    %s", context.onGround ? "yes" : "no");
                ImGui::Text("checkpoint  %d", context.checkpoint);
                ImGui::Text("mechanisms  %d", context.mechanismCount);
                ImGui::Text("enemies     %d", context.enemiesAlive);
            }
            ImGui::End();
        }
        return false;
    }
}
