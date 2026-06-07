#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>

// StudioSim 共享数据模型（见 docs/StudioSim-MVP-Plan.md §7）。
// 随里程碑增量扩充；M1 只需职位枚举 + 功能点位 POI。
namespace StudioSim
{
    enum class ERole
    {
        Engineer,
        Artist,
        Designer,
        ProducerPM,
        QA,
        Boss,
        Unknown
    };

    inline const char* RoleName(ERole role)
    {
        switch (role)
        {
        case ERole::Engineer:   return "engineer";
        case ERole::Artist:     return "artist";
        case ERole::Designer:   return "designer";
        case ERole::ProducerPM: return "pm";
        case ERole::QA:         return "qa";
        case ERole::Boss:       return "boss";
        default:                return "unknown";
        }
    }

    inline ERole RoleFromString(const std::string& s)
    {
        if (s == "engineer") return ERole::Engineer;
        if (s == "artist")   return ERole::Artist;
        if (s == "designer") return ERole::Designer;
        if (s == "pm")       return ERole::ProducerPM;
        if (s == "qa")       return ERole::QA;
        if (s == "boss")     return ERole::Boss;
        return ERole::Unknown;
    }

    // 从 SCAD 具名节点解析出的功能点位锚点。
    struct FPointOfInterest
    {
        std::string name;       // 节点名，如 "desk_engineer_01"
        std::string category;   // "desk" / "meet" / "pantry" / "lounge"
        ERole       roleTag = ERole::Unknown; // desk 专属职位标签
        glm::vec3   worldPos{0.0f};
        uint32_t    nodeId = 0;
        bool        workable = true;   // 断电/宕机等事件会置 false（M6）
        uint32_t    occupiedBy = 0;    // 占用员工序号，0 = 空（M2+）
    };

    // 一天的阶段机（见计划 §10）。M3 主要跑 Working；Briefing/Review 在 M5 接 LLM 目标。
    enum class EDayPhase
    {
        Briefing,
        Working,
        Review
    };

    inline const char* DayPhaseName(EDayPhase phase)
    {
        switch (phase)
        {
        case EDayPhase::Briefing: return "Briefing";
        case EDayPhase::Working:  return "Working";
        case EDayPhase::Review:   return "Review";
        default:                  return "?";
        }
    }

    // 玩家注入的事件（见计划 §11）。
    struct FWorldEvent
    {
        std::string id;
        std::string title;
        std::string description;
        double      gameTimeRaised = 0.0;
    };

    // 全局世界状态：模拟时钟 + 阶段 + 当日事件。时钟只在 Working 推进（见计划 §5.2）。
    struct FWorldState
    {
        EDayPhase phase = EDayPhase::Working; // M3 直接从 Working 开始
        int       dayIndex = 0;
        double    gameClockMinutes = 9.0 * 60.0; // 09:00
        float     timeScale = 5.0f;              // 1 真实秒 = N 游戏分钟（运行时 slider 可调）
        bool      paused = false;
        std::string globalMood;                  // M6：事件带来的全局氛围
        std::vector<FWorldEvent> todaysEvents;   // M6：今日已注入的事件
    };

    inline void MinutesToHHMM(double minutes, int& outHour, int& outMinute)
    {
        const int total = static_cast<int>(minutes);
        outHour = (total / 60) % 24;
        outMinute = total % 60;
    }

    enum class EMood
    {
        Calm,
        Focused,
        Stressed,
        Excited,
        Bored,
        Panicked
    };

    inline const char* MoodName(EMood mood)
    {
        switch (mood)
        {
        case EMood::Calm:     return "calm";
        case EMood::Focused:  return "focused";
        case EMood::Stressed: return "stressed";
        case EMood::Excited:  return "excited";
        case EMood::Bored:    return "bored";
        case EMood::Panicked: return "panicked";
        default:              return "calm";
        }
    }

    inline EMood MoodFromString(const std::string& s)
    {
        if (s == "focused")  return EMood::Focused;
        if (s == "stressed") return EMood::Stressed;
        if (s == "excited")  return EMood::Excited;
        if (s == "bored")    return EMood::Bored;
        if (s == "panicked") return EMood::Panicked;
        return EMood::Calm;
    }

    // LLM 决策 JSON 解析后的结构（见计划 §8）。
    struct FDecisionResult
    {
        std::string action;        // WORK / REST / TALK / MEETING / IDLE
        std::string targetPoi;
        std::string targetEmployee; // M7：TALK 的对象同事
        std::string dialogue;
        EMood       mood = EMood::Calm;
        int         durationMinutes = 30;
        bool        valid = false;
    };

    // 晨会候选目标（LLM 给的一项，见计划 §10.2）。
    struct FGoalOption
    {
        std::string title;
        std::string description;
    };

    // 今日团队目标（玩家选定或自定义，见计划 §10）。
    struct FDailyGoal
    {
        std::string title;
        std::string description;
        std::string source;        // "llm" / "player" / "fallback"
        bool        set = false;
    };

    struct FMeetingLine
    {
        std::string speaker;
        std::string text;
    };
}
