#pragma once

#include "Gameplay/Sim/AnchorMap.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

// Shared StudioSim data model. Keep deterministic simulation state independent from LLM response text.
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

    using FPointOfInterest = NextGameplay::Sim::FAnchorPoi;

    // In-game day phases. UI and simulation transitions consume this enum directly.
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

    enum class EProjectStage
    {
        Planning,
        Production,
        Polish,
        Done
    };

    inline const char* ProjectStageName(EProjectStage stage)
    {
        switch (stage)
        {
        case EProjectStage::Planning:   return "Planning";
        case EProjectStage::Production: return "Production";
        case EProjectStage::Polish:     return "Polish";
        case EProjectStage::Done:       return "Done";
        default:                        return "?";
        }
    }

    enum class EGameGenre
    {
        RPG,
        Action,
        Simulation,
        Puzzle,
        Shooter,
        Adventure,
        Unknown
    };

    inline const char* GameGenreName(EGameGenre genre)
    {
        switch (genre)
        {
        case EGameGenre::RPG:        return "RPG";
        case EGameGenre::Action:     return "Action";
        case EGameGenre::Simulation: return "Simulation";
        case EGameGenre::Puzzle:     return "Puzzle";
        case EGameGenre::Shooter:    return "Shooter";
        case EGameGenre::Adventure:  return "Adventure";
        default:                     return "Unknown";
        }
    }

    enum class EGameTheme
    {
        Fantasy,
        SciFi,
        Sports,
        Romance,
        Horror,
        Daily,
        Unknown
    };

    inline const char* GameThemeName(EGameTheme theme)
    {
        switch (theme)
        {
        case EGameTheme::Fantasy: return "Fantasy";
        case EGameTheme::SciFi:   return "SciFi";
        case EGameTheme::Sports:  return "Sports";
        case EGameTheme::Romance: return "Romance";
        case EGameTheme::Horror:  return "Horror";
        case EGameTheme::Daily:   return "Daily";
        default:                  return "Unknown";
        }
    }

    enum class EProjectSizeTier
    {
        Small,
        Standard,
        Big
    };

    inline const char* ProjectSizeTierName(EProjectSizeTier tier)
    {
        switch (tier)
        {
        case EProjectSizeTier::Small:    return "Small";
        case EProjectSizeTier::Standard: return "Standard";
        case EProjectSizeTier::Big:      return "Big";
        default:                         return "Standard";
        }
    }

    struct FProjectMeters
    {
        float tech = 0.0f;
        float design = 0.0f;
        float art = 0.0f;
        float polish = 0.0f;
    };

    struct FProjectState
    {
        EProjectStage stage = EProjectStage::Planning;
        FProjectMeters meters;
        FProjectMeters targetMeters;
        int bugCount = 0;
        int bugsFixed = 0;
        float overallProgress = 0.0f;
        bool shipped = false;
    };

    struct FHighlight
    {
        std::string text;
        std::string meter;
        bool achieved = false;
    };

    struct FGameProject
    {
        std::string name;
        EGameGenre genre = EGameGenre::Unknown;
        EGameTheme theme = EGameTheme::Unknown;
        std::vector<FHighlight> highlights;
        float comboFit = 1.0f;

        int plannedDays = 7;
        int elapsedDays = 0;
        int64_t budget = 0;

        FProjectState production;

        float quality = 0.0f;
        int reviewScore = 0;
        std::vector<int> reviewerScores;
        std::vector<std::string> reviewQuotes;
        int64_t unitsSold = 0;
        int64_t revenue = 0;
        int64_t cost = 0;
        int64_t profit = 0;
        bool launched = false;
    };

    struct FCompanyState
    {
        int64_t funds = 50000;
        std::vector<FGameProject> shipped;
        int projectIndex = 0;
    };

    struct FWorkOutput
    {
        std::string meter;
        float amount = 0.0f;
        bool foundBug = false;
        bool fixedBug = false;
    };

    // 玩家注入的事件。
    struct FWorldEvent
    {
        std::string id;
        std::string title;
        std::string description;
        double      gameTimeRaised = 0.0;
    };

    // 全局世界状态：模拟时钟 + 阶段 + 当日事件。时钟只在 Working 推进。
    struct FWorldState
    {
        EDayPhase phase = EDayPhase::Working;
        int       dayIndex = 0;
        double    gameClockMinutes = 9.0 * 60.0; // 09:00
        float     timeScale = 5.0f;              // 1 真实秒 = N 游戏分钟（运行时 slider 可调）
        bool      paused = false;
        std::string globalMood;                  // 事件带来的全局氛围
        std::vector<FWorldEvent> todaysEvents;   // 今日已注入的事件
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

    // LLM 决策 JSON 解析后的结构。
    struct FDecisionResult
    {
        std::string action;        // WORK / REST / TALK / MEETING / IDLE
        std::string targetPoi;
        std::string targetEmployee; // TALK 的对象同事
        std::string dialogue;
        EMood       mood = EMood::Calm;
        int         durationMinutes = 30;
        bool        valid = false;
    };

    // 晨会候选目标（LLM 给出的一项）。
    struct FGoalOption
    {
        std::string title;
        std::string description;
    };

    // 今日团队目标（玩家选定或自定义）。
    struct FDailyGoal
    {
        std::string title;
        std::string description;
        std::string source;        // "llm" / "player" / "fallback"
        std::string category;      // "ship_demo" / "fix_crash" / "brainstorm"
        FProjectMeters targetMeters;
        bool        set = false;
    };

    struct FMeetingLine
    {
        std::string speaker;
        std::string text;
    };

    enum class EGatheringKind
    {
        Meeting,
        Pantry
    };

    enum class EGatheringState
    {
        Forming,
        Talking,
        Deciding,
        Dispersing
    };

    struct FGroupDecision
    {
        std::string summary;
        std::string focusMeter;
        // 可选改派：员工名 -> 新的今日重点（LLM 群体决策产出，采纳后写入 todayTask）。
        std::vector<std::pair<std::string, std::string>> reassign;
        bool valid = false;
        bool accepted = false;
        bool rejected = false;
    };

    struct FGathering
    {
        int id = -1;
        EGatheringKind kind = EGatheringKind::Meeting;
        EGatheringState state = EGatheringState::Forming;
        std::string topic;
        std::string anchorCategory;
        std::vector<size_t> participants;
        double startGameMinutes = 0.0;
        double endGameMinutes = 0.0;
        double elapsedRealSeconds = 0.0;
        double nextLineRealSeconds = 1.0;
        size_t nextLineIndex = 0;
        std::vector<FMeetingLine> lines;
        FGroupDecision decision;
        bool awaitingConfirm = false;
    };
}
