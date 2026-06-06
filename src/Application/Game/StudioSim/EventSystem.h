#pragma once

#include "StudioSimTypes.h"

#include <string>
#include <vector>

namespace StudioSim
{
    struct FEmployee;
    class OfficeMap;

    struct FEventDef
    {
        std::string id;
        std::string title;
        std::string description;
        std::string globalMood;
    };

    // 玩家事件系统（见计划 §11）。事件被框定为"对今日目标的冲击"：注入后写入世界状态、
    // 改全局氛围，并让全员清掉当前 LLM 目标、插队立即重决策；事件文本进决策 prompt，
    // 由 LLM 自行反应（断电→离开工位、宕机→工程师救火……）而非硬编码规则。
    class EventSystem
    {
    public:
        EventSystem();

        const std::vector<FEventDef>& Catalog() const { return catalog_; }

        void Raise(const std::string& eventId, double gameMinutes, FWorldState& world,
                   std::vector<FEmployee>& employees, OfficeMap& office);

        // 拼出喂进决策 prompt 的当日事件摘要（无事件时返回 "暂无"）。
        static std::string BuildSummary(const FWorldState& world);

    private:
        const FEventDef* Find(const std::string& id) const;

        std::vector<FEventDef> catalog_;
    };
}
