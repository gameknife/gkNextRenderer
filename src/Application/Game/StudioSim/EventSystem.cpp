#include "EventSystem.h"

#include "EmployeeSystem.h"
#include "OfficeMap.h"

#include <spdlog/spdlog.h>

namespace StudioSim
{
    EventSystem::EventSystem()
    {
        catalog_ = {
            {"competitor_launch", "竞争对手发布了新游戏", "竞品刚上线了一款类似的新游戏，口碑不错，老板很紧张", "紧张"},
            {"power_outage", "公司突然断电了", "整层楼断电，电脑全部关机，暂时没法在工位上工作", "无奈"},
            {"build_server_down", "版本服务器宕机了", "打包服务器挂了，出不了版本，工程师们急需排查", "焦头烂额"},
        };
    }

    const FEventDef* EventSystem::Find(const std::string& id) const
    {
        for (const auto& def : catalog_)
        {
            if (def.id == id)
            {
                return &def;
            }
        }
        return nullptr;
    }

    void EventSystem::Raise(const std::string& eventId, double gameMinutes, FWorldState& world,
                            std::vector<FEmployee>& employees, OfficeMap& office)
    {
        const FEventDef* def = Find(eventId);
        if (def == nullptr)
        {
            return;
        }

        FWorldEvent event;
        event.id = def->id;
        event.title = def->title;
        event.description = def->description;
        event.gameTimeRaised = gameMinutes;
        world.todaysEvents.push_back(event);
        world.globalMood = def->globalMood;

        // 部分事件让相关工位暂时不可用（硬约束：prompt 不再列出这些点位、决策也不会去）。
        if (eventId == "power_outage")
        {
            office.SetWorkable("desk", "", false); // 断电：所有工位
        }
        else if (eventId == "build_server_down")
        {
            office.SetWorkable("desk", "engineer", false); // 宕机：工程师工位
        }

        // 全员清当前 LLM 目标 + 立即重决策（插队），下一轮 prompt 会带上事件。
        for (auto& emp : employees)
        {
            emp.overrideTargetPoi.clear();
            emp.nextDecisionAt = gameMinutes;
        }

        int hh = 0, mm = 0;
        MinutesToHHMM(gameMinutes, hh, mm);
        SPDLOG_INFO("StudioSim/Event RAISED: {} at {:02d}:{:02d} (globalMood -> {})", def->title, hh, mm,
                    world.globalMood);
    }

    std::string EventSystem::BuildSummary(const FWorldState& world)
    {
        if (world.todaysEvents.empty())
        {
            return "暂无";
        }
        std::string summary;
        for (const auto& event : world.todaysEvents)
        {
            if (!summary.empty())
            {
                summary += "；";
            }
            summary += event.description;
        }
        return summary;
    }
}
