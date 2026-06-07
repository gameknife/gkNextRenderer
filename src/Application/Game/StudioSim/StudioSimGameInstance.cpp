#include "StudioSimGameInstance.hpp"

#include <fmt/format.h>
#include <imgui.h>
#include <spdlog/spdlog.h>
#include <glm/gtc/matrix_transform.hpp>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_keycode.h>

#include <initializer_list>

#include "Engine/Assets/Core/Model.hpp"
#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Options.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/Scene/SceneList.hpp"
#include "Engine/Runtime/Subsystems/AIService.hpp"

#include <nlohmann/json.hpp>

namespace
{
    // Fixed isometric-ish overhead camera shared by the render override and the
    // debug overlay so projected anchors line up with the rendered scene.
    constexpr float kOfficeFov = 50.0f;

    glm::mat4 OfficeViewMatrix()
    {
        return glm::lookAt(glm::vec3(0.0f, 18.0f, 18.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    }

    ImU32 CategoryColor(const std::string& category)
    {
        if (category == "desk")   return IM_COL32(230, 150, 60, 255);
        if (category == "meet")   return IM_COL32(70, 140, 230, 255);
        if (category == "pantry") return IM_COL32(220, 200, 80, 255);
        if (category == "lounge") return IM_COL32(190, 120, 210, 255);
        return IM_COL32(200, 200, 200, 255);
    }

    ImU32 ColorToImU32(const glm::vec3& c)
    {
        return IM_COL32(static_cast<int>(c.r * 255.0f), static_cast<int>(c.g * 255.0f), static_cast<int>(c.b * 255.0f),
                        255);
    }

    bool ProjectWorld(const glm::mat4& viewProjection, const ImVec2& vpPos, const ImVec2& vpSize,
                      const glm::vec3& world, ImVec2& outScreen)
    {
        const glm::vec4 clip = viewProjection * glm::vec4(world, 1.0f);
        if (clip.w <= 0.0f)
        {
            return false;
        }
        const glm::vec3 ndc = glm::vec3(clip) / clip.w;
        if (ndc.x < -1.2f || ndc.x > 1.2f || ndc.y < -1.2f || ndc.y > 1.2f)
        {
            return false;
        }
        outScreen = ImVec2(vpPos.x + (ndc.x * 0.5f + 0.5f) * vpSize.x, vpPos.y + (-ndc.y * 0.5f + 0.5f) * vpSize.y);
        return true;
    }

    bool ContainsAny(const std::string& text, std::initializer_list<const char*> keywords)
    {
        for (const char* keyword : keywords)
        {
            if (text.find(keyword) != std::string::npos)
            {
                return true;
            }
        }
        return false;
    }

    bool GoalNeedsMeeting(const StudioSim::FDailyGoal& goal)
    {
        const std::string text = goal.title + " " + goal.description;
        return ContainsAny(text, {"头脑风暴", "脑暴", "会议", "讨论", "评审", "规划", "计划", "重排", "协调",
                                  "决策", "复盘", "brainstorm", "meeting", "review", "plan"});
    }

    bool EventNeedsMeeting(const std::string& eventId)
    {
        return eventId == "competitor_launch" || eventId == "power_outage" || eventId == "build_server_down";
    }

    std::string MeetingTopicForEvent(const std::string& eventId)
    {
        if (eventId == "competitor_launch") return "竞品发布后是否调整今日目标";
        if (eventId == "power_outage") return "断电期间如何继续推进目标";
        if (eventId == "build_server_down") return "版本服务器宕机后的救火分工";
        return "临时群体决策";
    }

    std::vector<StudioSim::FMeetingLine> BuildFallbackMeetingLines(const std::vector<StudioSim::FEmployee>& employees,
                                                                    const std::string& topic)
    {
        std::vector<StudioSim::FMeetingLine> lines;
        for (const auto& emp : employees)
        {
            if (emp.role == StudioSim::ERole::ProducerPM)
            {
                lines.push_back({emp.displayName, "先定优先级"});
            }
            else if (emp.role == StudioSim::ERole::Engineer)
            {
                lines.push_back({emp.displayName, "我评估技术风险"});
            }
            else if (emp.role == StudioSim::ERole::Designer)
            {
                lines.push_back({emp.displayName, "玩法目标要收敛"});
            }
            else if (emp.role == StudioSim::ERole::Artist)
            {
                lines.push_back({emp.displayName, "美术量要砍一刀"});
            }
            else if (emp.role == StudioSim::ERole::QA)
            {
                lines.push_back({emp.displayName, "测试范围要明确"});
            }
        }
        if (lines.empty())
        {
            lines.push_back({"Team", topic});
        }
        return lines;
    }

    std::vector<StudioSim::FMeetingLine> ParseMeetingLines(const std::string& text)
    {
        std::vector<StudioSim::FMeetingLine> lines;
        const size_t open = text.find('[');
        const size_t close = text.rfind(']');
        if (open == std::string::npos || close == std::string::npos || close <= open)
        {
            return lines;
        }

        try
        {
            const nlohmann::json json = nlohmann::json::parse(text.substr(open, close - open + 1));
            for (const auto& item : json)
            {
                StudioSim::FMeetingLine line;
                line.speaker = item.value("speaker", std::string());
                line.text = item.value("line", std::string());
                if (!line.speaker.empty() && !line.text.empty())
                {
                    lines.push_back(std::move(line));
                }
                if (lines.size() >= 10)
                {
                    break;
                }
            }
        }
        catch (...)
        {
            lines.clear();
        }
        return lines;
    }
}

// Each game executable provides this factory; DesktopMain.cpp binds it at link time.
std::unique_ptr<NextGameInstanceBase> CreateGameInstance(Vulkan::WindowConfig& config, Runtime::Config::Options& options,
                                                         NextEngine* engine)
{
    return std::make_unique<StudioSimGameInstance>(config, options, engine);
}

StudioSimGameInstance::StudioSimGameInstance(Vulkan::WindowConfig& config, Runtime::Config::Options& options,
                                             NextEngine* engine)
    : NextGameInstanceBase(config, options, engine)
{
    options.QuickJSEntry = "assets/scripts/studiosim_entry.js";
    ConfigureWindow(config, options, "StudioSim", 1280, 720, false);
}

void StudioSimGameInstance::OnInit()
{
    // NavGrid is built from the scene CPU BVH, so keep CPU mesh data alive.
    GOption->KeepCPUMeshData = true;

    std::string initialScene = "assets/scad/office.scad";
    if (!GOption->SceneName.empty())
    {
        initialScene = GOption->SceneName;
    }

    SPDLOG_INFO("StudioSim: loading scene '{}'", initialScene);
    GetEngine().RequestLoadScene({.filename = initialScene});

    // M4 self-test: switch to the local LLM and fire one async probe to confirm the
    // engine -> llama-server link before wiring up the decision scheduler.
    // Prefer the local llama-server for employee decisions.
    if (auto* ai = GetEngine().GetAIService())
    {
        const bool ok = ai->SwitchProvider(NextAI::EAIProviderType::LocalLlama);
        SPDLOG_INFO("StudioSim: SwitchProvider(LocalLlama) -> {} (provider='{}')", ok, ai->GetProviderName());
    }
}

void StudioSimGameInstance::BeforeSceneRebuild(std::vector<std::shared_ptr<Assets::Node>>& /*nodes*/,
                                               std::vector<Assets::Model>& models,
                                               std::vector<Assets::FMaterial>& materials,
                                               std::vector<Assets::LightObject>& /*lights*/,
                                               std::vector<Assets::AnimationTrack>& /*tracks*/)
{
    employeeSystem_.InjectAssets(models, materials);
}

void StudioSimGameInstance::OnTick(double deltaSeconds)
{
    if (!sceneReady_)
    {
        return;
    }

    if (worldState_.phase == StudioSim::EDayPhase::Briefing)
    {
        // 晨会：等 LLM 给目标 → 玩家选/自定义 → 分解 → 开工（时钟暂停在 09:00）。
        goalSystem_.Tick(GetEngine().GetAIService(), employeeSystem_.EmployeesMutable());
        if (goalSystem_.IsActive())
        {
            worldState_.phase = StudioSim::EDayPhase::Working;
            SPDLOG_INFO("StudioSim: goal set, entering Working");
            if (!goalMeetingStarted_ && GoalNeedsMeeting(goalSystem_.Goal()))
            {
                goalMeetingStarted_ = true;
                StartMeeting(fmt::format("围绕今日目标「{}」做群体决策", goalSystem_.Goal().title), 35.0);
            }
        }
    }
    else if (worldState_.phase == StudioSim::EDayPhase::Working && !worldState_.paused)
    {
        worldState_.gameClockMinutes += deltaSeconds * worldState_.timeScale;
        if (worldState_.gameClockMinutes >= 18.0 * 60.0)
        {
            worldState_.gameClockMinutes = 18.0 * 60.0;
            worldState_.phase = StudioSim::EDayPhase::Review;
            SPDLOG_INFO("StudioSim: day {} ended -> Review (goal was '{}')", worldState_.dayIndex,
                        goalSystem_.Goal().title);
            goalSystem_.Summarize(GetEngine().GetAIService(), employeeSystem_.EmployeesMutable());
        }

        TickMeeting(deltaSeconds);
        if (!meeting_.active)
        {
            scheduler_.Tick(worldState_.gameClockMinutes, goalSystem_.Goal(),
                            StudioSim::EventSystem::BuildSummary(worldState_), GetEngine().GetAIService(),
                            employeeSystem_.EmployeesMutable(), officeMap_);
        }
    }
    else if (worldState_.phase == StudioSim::EDayPhase::Review)
    {
        // 收尾：排空 LLM 结算总结。
        goalSystem_.Tick(GetEngine().GetAIService(), employeeSystem_.EmployeesMutable());
    }

    employeeSystem_.Tick(static_cast<float>(deltaSeconds), worldState_.gameClockMinutes, worldState_.paused,
                         GetEngine().GetScene(), officeMap_);
}

void StudioSimGameInstance::OnDestroy()
{
}

void StudioSimGameInstance::OnSceneLoaded()
{
    Assets::Scene& scene = GetEngine().GetScene();
    sceneNodeCount_ = scene.Nodes().size();
    officeMap_.BuildFromScene(scene);
    employeeSystem_.OnSceneLoaded(scene, officeMap_);
    worldState_ = StudioSim::FWorldState{};
    worldState_.phase = StudioSim::EDayPhase::Briefing; // 晨会先定今日目标
    scheduler_.Reset();
    goalSystem_.Reset();
    goalSystem_.BeginDay(GetEngine().GetAIService());
    goalMeetingStarted_ = false;
    sceneReady_ = true;
    SPDLOG_INFO("StudioSim: scene loaded ({} nodes, {} POIs, {} employees)", sceneNodeCount_, officeMap_.Count(),
                employeeSystem_.Count());
}

void StudioSimGameInstance::OnSceneUnloaded()
{
    sceneReady_ = false;
    sceneNodeCount_ = 0;
    employeeSystem_.Clear();
    officeMap_.Clear();
}

void StudioSimGameInstance::StartNextDay()
{
    const int nextDay = worldState_.dayIndex + 1;
    const float timeScale = worldState_.timeScale;

    worldState_ = StudioSim::FWorldState{};
    worldState_.dayIndex = nextDay;
    worldState_.timeScale = timeScale;
    worldState_.phase = StudioSim::EDayPhase::Briefing;
    worldState_.paused = false;

    officeMap_.ResetWorkable();
    for (auto& emp : employeeSystem_.EmployeesMutable())
    {
        emp.todayTask.clear();
        emp.targetPoi.clear();
        emp.overrideTargetPoi.clear();
        emp.overrideUntilMinutes = 0.0;
        emp.bubbleText.clear();
        emp.pendingFrom.clear();
        emp.pendingText.clear();
        emp.mood = StudioSim::EMood::Calm;
        emp.decisionPending = false;
        emp.nextDecisionAt = 0.0;
    }

    scheduler_.Reset();
    goalSystem_.Reset();
    goalSystem_.BeginDay(GetEngine().GetAIService());
    customGoalBuf_[0] = '\0';
    goalMeetingStarted_ = false;
    meeting_ = FMeetingRuntime{};
    {
        std::lock_guard<std::mutex> lock(meetingMutex_);
        ++meetingGeneration_;
        pendingMeetingLines_.clear();
    }

    SPDLOG_INFO("StudioSim: starting day {}", worldState_.dayIndex);
}

void StudioSimGameInstance::StartMeeting(const std::string& topic, double durationMinutes)
{
    const auto seats = officeMap_.PointsOfCategory("meet");
    if (seats.empty() || employeeSystem_.EmployeesMutable().empty())
    {
        SPDLOG_WARN("StudioSim/Meeting: cannot start '{}', no meeting seats or employees", topic);
        return;
    }

    scheduler_.Reset();
    {
        std::lock_guard<std::mutex> lock(meetingMutex_);
        ++meetingGeneration_;
        pendingMeetingLines_.clear();
    }

    meeting_ = FMeetingRuntime{};
    meeting_.active = true;
    meeting_.topic = topic;
    meeting_.endGameMinutes = worldState_.gameClockMinutes + durationMinutes;
    meeting_.nextLineRealSeconds = 1.0;
    meeting_.lines = BuildFallbackMeetingLines(employeeSystem_.Employees(), topic);

    auto& employees = employeeSystem_.EmployeesMutable();
    for (size_t i = 0; i < employees.size(); ++i)
    {
        auto& emp = employees[i];
        emp.targetPoi.clear();
        emp.overrideTargetPoi = seats[i % seats.size()]->name;
        emp.overrideUntilMinutes = meeting_.endGameMinutes;
        emp.bubbleText = "去会议室";
        emp.pendingFrom.clear();
        emp.pendingText.clear();
        emp.decisionPending = false;
        emp.nextDecisionAt = meeting_.endGameMinutes + 1.0;
    }

    if (auto* ai = GetEngine().GetAIService())
    {
        std::string attendees;
        for (const auto& emp : employeeSystem_.Employees())
        {
            attendees += fmt::format("{}({}) ", emp.displayName, StudioSim::RoleName(emp.role));
        }

        const std::string prompt = fmt::format(
            "你是游戏工作室会议编剧。会议主题：{}。\n"
            "今日目标：{}（{}）。当日事件：{}。\n"
            "参会者：{}。\n"
            "生成一段多人群聊会议记录，6到10句。每句必须由参会者之一发言，围绕是否调整计划、谁负责什么。"
            "只输出JSON数组，不要解释：[{{\"speaker\":\"Alice\",\"line\":\"一句不超过16字\"}}]",
            topic, goalSystem_.Goal().set ? goalSystem_.Goal().title : "未定",
            goalSystem_.Goal().set ? goalSystem_.Goal().description : "", StudioSim::EventSystem::BuildSummary(worldState_),
            attendees);

        uint64_t generation = 0;
        {
            std::lock_guard<std::mutex> lock(meetingMutex_);
            generation = meetingGeneration_;
        }

        ai->GenerateTextAsync(prompt,
                              [this, generation](NextAI::FAIResponse response)
                              {
                                  auto lines = ParseMeetingLines(response.success ? response.text : std::string());
                                  if (lines.empty())
                                  {
                                      return;
                                  }

                                  std::lock_guard<std::mutex> lock(meetingMutex_);
                                  if (generation != meetingGeneration_)
                                  {
                                      return;
                                  }
                                  pendingMeetingLines_ = std::move(lines);
                              });
    }

    SPDLOG_INFO("StudioSim/Meeting started: '{}' ({} employees, {:.0f} game minutes)", topic, employees.size(),
                durationMinutes);
}

void StudioSimGameInstance::TickMeeting(double deltaSeconds)
{
    {
        std::lock_guard<std::mutex> lock(meetingMutex_);
        if (!pendingMeetingLines_.empty())
        {
            meeting_.lines = std::move(pendingMeetingLines_);
            pendingMeetingLines_.clear();
            meeting_.nextLineIndex = 0;
            meeting_.elapsedRealSeconds = 0.0;
            meeting_.nextLineRealSeconds = 1.0;
        }
    }

    if (!meeting_.active)
    {
        return;
    }

    if (worldState_.gameClockMinutes >= meeting_.endGameMinutes)
    {
        meeting_.active = false;
        for (auto& emp : employeeSystem_.EmployeesMutable())
        {
            emp.overrideTargetPoi.clear();
            emp.overrideUntilMinutes = 0.0;
            emp.bubbleText.clear();
            emp.nextDecisionAt = worldState_.gameClockMinutes;
        }
        SPDLOG_INFO("StudioSim/Meeting ended: '{}'", meeting_.topic);
        return;
    }

    meeting_.elapsedRealSeconds += deltaSeconds;
    if (meeting_.lines.empty() || meeting_.elapsedRealSeconds < meeting_.nextLineRealSeconds)
    {
        return;
    }

    const StudioSim::FMeetingLine& line = meeting_.lines[meeting_.nextLineIndex % meeting_.lines.size()];
    for (auto& emp : employeeSystem_.EmployeesMutable())
    {
        if (emp.displayName == line.speaker)
        {
            emp.bubbleText = line.text;
            emp.mood = StudioSim::EMood::Focused;
            break;
        }
    }
    meeting_.nextLineIndex++;
    meeting_.nextLineRealSeconds += 3.0;
}

void StudioSimGameInstance::RaiseEventAndMaybeStartMeeting(const std::string& eventId)
{
    eventSystem_.Raise(eventId, worldState_.gameClockMinutes, worldState_, employeeSystem_.EmployeesMutable(),
                       officeMap_);
    if (worldState_.phase == StudioSim::EDayPhase::Working && EventNeedsMeeting(eventId))
    {
        StartMeeting(MeetingTopicForEvent(eventId), 30.0);
    }
}

bool StudioSimGameInstance::OverrideRenderCamera(Assets::Camera& outRenderCamera) const
{
    outRenderCamera.ModelView = OfficeViewMatrix();
    outRenderCamera.FieldOfView = kOfficeFov;
    return true;
}

void StudioSimGameInstance::DrawWorldOverlay() const
{
    const ImVec2 vpSize = ImGui::GetMainViewport()->Size;
    const ImVec2 vpPos = ImGui::GetMainViewport()->Pos;
    if (vpSize.x <= 1.0f || vpSize.y <= 1.0f)
    {
        return;
    }

    const float aspect = vpSize.x / vpSize.y;
    const glm::mat4 viewProjection =
        glm::perspective(glm::radians(kOfficeFov), aspect, 0.05f, 2000.0f) * OfficeViewMatrix();

    auto* drawList = ImGui::GetForegroundDrawList();
    ImVec2 screen;

    // POI anchors.
    for (const auto& poi : officeMap_.Points())
    {
        if (ProjectWorld(viewProjection, vpPos, vpSize, poi.worldPos + glm::vec3(0.0f, 0.9f, 0.0f), screen))
        {
            const ImU32 color = CategoryColor(poi.category);
            drawList->AddCircleFilled(screen, 4.0f, color);
            drawList->AddText(ImVec2(screen.x + 6.0f, screen.y - 6.0f), color, poi.name.c_str());
        }
    }

    // Employee name tags floating above each agent.
    for (const auto& emp : employeeSystem_.Employees())
    {
        if (ProjectWorld(viewProjection, vpPos, vpSize, emp.position + glm::vec3(0.0f, 2.0f, 0.0f), screen))
        {
            const ImU32 color = ColorToImU32(emp.color);
            drawList->AddText(ImVec2(screen.x - 12.0f, screen.y - 8.0f), color, emp.displayName.c_str());
            const char* bubble = emp.decisionPending ? "..." : emp.bubbleText.c_str();
            if (bubble != nullptr && bubble[0] != '\0')
            {
                constexpr float bubbleMaxWidth = 240.0f;
                const ImVec2 padding(8.0f, 5.0f);
                const ImVec2 textSize = ImGui::CalcTextSize(bubble, nullptr, false, bubbleMaxWidth);
                const ImVec2 textPos(screen.x - textSize.x * 0.5f, screen.y + 8.0f);
                const ImVec2 bubbleMin(textPos.x - padding.x, textPos.y - padding.y);
                const ImVec2 bubbleMax(textPos.x + textSize.x + padding.x, textPos.y + textSize.y + padding.y);
                drawList->AddRectFilled(bubbleMin, bubbleMax, IM_COL32(20, 24, 28, 220), 6.0f);
                drawList->AddRect(bubbleMin, bubbleMax, IM_COL32(255, 255, 255, 70), 6.0f);
                drawList->AddText(ImGui::GetFont(), ImGui::GetFontSize(), textPos, IM_COL32(255, 255, 255, 245),
                                  bubble, nullptr, bubbleMaxWidth);
            }
        }
    }
}

bool StudioSimGameInstance::OnRenderUI()
{
    ImGui::Begin("StudioSim");
    ImGui::Text("StudioSim MVP");
    {
        int hh = 0, mm = 0;
        StudioSim::MinutesToHHMM(worldState_.gameClockMinutes, hh, mm);
        ImGui::Text("Day %d | %s | %02d:%02d", worldState_.dayIndex, StudioSim::DayPhaseName(worldState_.phase), hh, mm);
        ImGui::TextWrapped("Goal: %s", goalSystem_.Goal().set ? goalSystem_.Goal().title.c_str() : "(briefing...)");
        if (worldState_.phase == StudioSim::EDayPhase::Review && !goalSystem_.Summary().empty())
        {
            ImGui::TextWrapped("Review: %s", goalSystem_.Summary().c_str());
        }
        if (worldState_.phase == StudioSim::EDayPhase::Review)
        {
            if (ImGui::Button("Next day"))
            {
                StartNextDay();
            }
        }
        ImGui::SliderFloat("Time scale (min/s)", &worldState_.timeScale, 1.0f, 240.0f);
        ImGui::Checkbox("Pause", &worldState_.paused);
        ImGui::Separator();
    }
    if (worldState_.phase == StudioSim::EDayPhase::Briefing &&
        goalSystem_.State() == StudioSim::GoalSystem::EState::AwaitingChoice)
    {
        ImGui::Text("Morning briefing - pick today's goal:");
        const auto& options = goalSystem_.Options();
        for (size_t i = 0; i < options.size(); ++i)
        {
            if (ImGui::Button(options[i].title.c_str()))
            {
                goalSystem_.ChooseGoal(static_cast<int>(i), GetEngine().GetAIService(),
                                       employeeSystem_.EmployeesMutable());
            }
            ImGui::SameLine();
            ImGui::TextDisabled("%s", options[i].description.c_str());
        }
        ImGui::InputText("##customgoal", customGoalBuf_, sizeof(customGoalBuf_));
        ImGui::SameLine();
        if (ImGui::Button("Use custom") && customGoalBuf_[0] != '\0')
        {
            goalSystem_.ChooseCustom(customGoalBuf_, GetEngine().GetAIService(),
                                     employeeSystem_.EmployeesMutable());
        }
        ImGui::Separator();
    }
    ImGui::Text("scene ready: %s | nav: %s", sceneReady_ ? "yes" : "no", employeeSystem_.NavReady() ? "ok" : "no");
    ImGui::Text("LLM: %s | decisions: %d", scheduler_.InFlight() ? "thinking..." : "idle", scheduler_.DecisionsMade());
    if (meeting_.active)
    {
        ImGui::TextWrapped("Meeting: %s", meeting_.topic.c_str());
    }
    ImGui::Text("nodes: %zu | POIs: %zu | employees: %zu", sceneNodeCount_, officeMap_.Count(),
                employeeSystem_.Count());
    ImGui::Checkbox("Show overlay", &showOverlay_);
    ImGui::Separator();
    for (const auto& emp : employeeSystem_.Employees())
    {
        ImGui::TextColored(ImColor(ColorToImU32(emp.color)), "%-7s [%-9s] %-9s %-16s %s", emp.displayName.c_str(),
                           StudioSim::RoleName(emp.role), StudioSim::MoodName(emp.mood),
                           emp.targetPoi.empty() ? "(idle)" : emp.targetPoi.c_str(), emp.bubbleText.c_str());
    }
    ImGui::Separator();
    ImGui::Text("Inject event (key 1/2/3):");
    for (const auto& def : eventSystem_.Catalog())
    {
        if (ImGui::Button(def.title.c_str()))
        {
            RaiseEventAndMaybeStartMeeting(def.id);
        }
    }
    if (!worldState_.todaysEvents.empty())
    {
        ImGui::Text("Mood: %s | Today's events:", worldState_.globalMood.c_str());
        for (const auto& ev : worldState_.todaysEvents)
        {
            ImGui::BulletText("%s", ev.title.c_str());
        }
    }
    ImGui::End();

    if (sceneReady_ && showOverlay_)
    {
        DrawWorldOverlay();
    }
    return true;
}

bool StudioSimGameInstance::OnKey(SDL_Event& event)
{
    if (event.type != SDL_EVENT_KEY_DOWN || !sceneReady_)
    {
        return false;
    }
    int index = -1;
    if (event.key.key == SDLK_1) index = 0;
    else if (event.key.key == SDLK_2) index = 1;
    else if (event.key.key == SDLK_3) index = 2;
    if (index >= 0 && index < static_cast<int>(eventSystem_.Catalog().size()))
    {
        RaiseEventAndMaybeStartMeeting(eventSystem_.Catalog()[static_cast<size_t>(index)].id);
        return true;
    }
    return false;
}
