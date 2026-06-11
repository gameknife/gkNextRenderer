#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>

// AirportSim 共享数据模型（见 docs/AirportSim-MVP-Plan.md §2/§4/§5）。
namespace AirportSim
{
    enum class EAgentRole
    {
        Passenger,
        Checkin,
        Security,
        GateAgent,
        Info,
        Clerk,
        Cleaner,
        Guard
    };

    inline const char* RoleName(EAgentRole role)
    {
        switch (role)
        {
        case EAgentRole::Passenger: return "passenger";
        case EAgentRole::Checkin:   return "checkin";
        case EAgentRole::Security:  return "security";
        case EAgentRole::GateAgent: return "gate";
        case EAgentRole::Info:      return "info";
        case EAgentRole::Clerk:     return "clerk";
        case EAgentRole::Cleaner:   return "cleaner";
        case EAgentRole::Guard:     return "guard";
        default:                    return "?";
        }
    }

    inline const char* RoleLabelZh(EAgentRole role)
    {
        switch (role)
        {
        case EAgentRole::Passenger: return "旅客";
        case EAgentRole::Checkin:   return "值机员";
        case EAgentRole::Security:  return "安检员";
        case EAgentRole::GateAgent: return "登机口职员";
        case EAgentRole::Info:      return "问询员";
        case EAgentRole::Clerk:     return "店员";
        case EAgentRole::Cleaner:   return "保洁";
        case EAgentRole::Guard:     return "保安";
        default:                    return "?";
        }
    }

    enum class EMood
    {
        Neutral,
        Happy,
        Tired,
        Annoyed,
        Excited,
        Anxious
    };

    inline const char* MoodName(EMood mood)
    {
        switch (mood)
        {
        case EMood::Neutral: return "neutral";
        case EMood::Happy:   return "happy";
        case EMood::Tired:   return "tired";
        case EMood::Annoyed: return "annoyed";
        case EMood::Excited: return "excited";
        case EMood::Anxious: return "anxious";
        default:             return "neutral";
        }
    }

    inline EMood MoodFromString(const std::string& s)
    {
        if (s == "happy")   return EMood::Happy;
        if (s == "tired")   return EMood::Tired;
        if (s == "annoyed") return EMood::Annoyed;
        if (s == "excited") return EMood::Excited;
        if (s == "anxious") return EMood::Anxious;
        return EMood::Neutral;
    }

    // mood 表情符号（头顶气泡/名牌用）。
    inline const char* MoodIcon(EMood mood)
    {
        switch (mood)
        {
        case EMood::Happy:   return "(^_^)";
        case EMood::Tired:   return "(=_=)";
        case EMood::Annoyed: return "(>_<)";
        case EMood::Excited: return "(!!)";
        case EMood::Anxious: return "(?!)";
        default:             return "";
        }
    }

    // 视觉层动画提示（§3.3）。GeometryVisual 用简单姿态变化表达。
    enum class EAgentAnimHint
    {
        Idle,
        Walk,
        Sit,
        Work
    };

    enum class EFlightState
    {
        Scheduled,
        CheckinOpen,
        Boarding,
        Final,
        Departed
    };

    inline const char* FlightStateName(EFlightState state)
    {
        switch (state)
        {
        case EFlightState::Scheduled:   return "Scheduled";
        case EFlightState::CheckinOpen: return "Checkin";
        case EFlightState::Boarding:    return "Boarding";
        case EFlightState::Final:       return "Final";
        case EFlightState::Departed:    return "Departed";
        default:                        return "?";
        }
    }

    // 旅客旅程状态机（§5.1，Layer 0 刚性主线）。
    enum class EPassengerState
    {
        ToEntrance,
        ToKiosk,
        UseKiosk,
        QueueCheckin,
        ToSecurity,
        QueueSecurity,
        PassSecurity,
        AirsideIdle,   // 决策点：下一个活动由 Layer 1 选择
        AirsideWalk,   // 去某个消费/休息 POI 的路上
        AirsideUse,    // 在 POI 处停留（含坐下）
        ToGate,
        QueueGate,
        Despawned
    };

    inline const char* PassengerStateName(EPassengerState state)
    {
        switch (state)
        {
        case EPassengerState::ToEntrance:    return "ToEntrance";
        case EPassengerState::ToKiosk:       return "ToKiosk";
        case EPassengerState::UseKiosk:      return "UseKiosk";
        case EPassengerState::QueueCheckin:  return "QueueCheckin";
        case EPassengerState::ToSecurity:    return "ToSecurity";
        case EPassengerState::QueueSecurity: return "QueueSecurity";
        case EPassengerState::PassSecurity:  return "PassSecurity";
        case EPassengerState::AirsideIdle:   return "AirsideIdle";
        case EPassengerState::AirsideWalk:   return "AirsideWalk";
        case EPassengerState::AirsideUse:    return "AirsideUse";
        case EPassengerState::ToGate:        return "ToGate";
        case EPassengerState::QueueGate:     return "QueueGate";
        case EPassengerState::Despawned:     return "Despawned";
        default:                             return "?";
        }
    }

    // 员工日程状态机（§5.2）。
    enum class EStaffState
    {
        Commute,   // 停车场/公交 → entrance
        ToPost,    // entrance → 岗位
        OnDuty,
        Patrol,    // 保洁/保安巡回
        OffDuty,   // 离岗走向场外
        Despawned
    };

    inline const char* StaffStateName(EStaffState state)
    {
        switch (state)
        {
        case EStaffState::Commute:   return "Commute";
        case EStaffState::ToPost:    return "ToPost";
        case EStaffState::OnDuty:    return "OnDuty";
        case EStaffState::Patrol:    return "Patrol";
        case EStaffState::OffDuty:   return "OffDuty";
        case EStaffState::Despawned: return "Despawned";
        default:                     return "?";
        }
    }

    enum class EShift
    {
        Morning, // 05:30–13:30
        Evening, // 13:30–21:30
        AllDay,  // 06:30–21:30（店员简化单班）
        Night    // 21:00–06:00（跨午夜）
    };

    inline const char* ShiftName(EShift shift)
    {
        switch (shift)
        {
        case EShift::Morning: return "早班";
        case EShift::Evening: return "晚班";
        case EShift::AllDay:  return "全天";
        case EShift::Night:   return "夜班";
        default:              return "?";
        }
    }

    // 从 SCAD 具名节点解析出的功能点位锚点（§2.2）。
    struct FPointOfInterest
    {
        std::string name;       // 节点名，如 "checkin_01"
        std::string category;   // 前缀，如 "checkin"
        glm::vec3   worldPos{0.0f};
        glm::vec3   frontDir{0.0f, 0.0f, 1.0f}; // 世界系 front（scad 局部 -y）
        uint32_t    nodeId = 0;
        int         occupiedBy = -1;        // 占用 agent id，-1 = 空
        int         seatOccupied[4] = {-1, -1, -1, -1}; // wait 类 4 联座占用
    };

    // 离港航班（§4.3）。
    struct FFlight
    {
        std::string number;      // "GK101"
        std::string gatePoi;     // "gate_03"
        double      departMinutes = 0.0; // 当日游戏分钟
        int         paxTotal = 8;
        int         paxSpawned = 0;
        int         paxBoarded = 0;
        EFlightState state = EFlightState::Scheduled;
        int         colorIdx = 0;
    };

    // LLM 决策 JSON（§5.3 动作 schema）。
    struct FDecisionResult
    {
        std::string action;   // goto / use_poi / say_to / emote / idle
        std::string target;   // POI 名或 agent 名
        std::string say;      // ≤20 字气泡台词
        EMood       mood = EMood::Neutral;
        bool        valid = false;
    };

    inline void MinutesToHHMM(double minutes, int& outHour, int& outMinute)
    {
        const int total = static_cast<int>(minutes);
        outHour = (total / 60) % 24;
        outMinute = total % 60;
    }
}
