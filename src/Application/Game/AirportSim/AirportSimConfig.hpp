#pragma once

#include "AirportSimTypes.h"

#include <glm/glm.hpp>

// AirportSim 全部数值配置（照 CharacterDemoConfig 惯例：纯 C++ 常量，无外部文件）。
// 坐标均为引擎世界系：scad(x,y,z) → world(x, z, −y)，地面顶面 y=0.15。
namespace AirportSim::Config
{
    // ---- 时钟（§4.1）：一律游戏分钟 ----
    constexpr double kDayStartMinutes = 5.0 * 60.0;  // 模拟从 05:00 开始
    constexpr float  kDefaultTimeScale = 2.0f;       // 1 真实秒 = 2 游戏分钟（1 日 = 12 真实分钟）
    constexpr float  kMinTimeScale = 0.5f;
    constexpr float  kMaxTimeScale = 16.0f;          // 8× 默认速度

    // ---- 日夜光照（§4.2）----
    constexpr float kDaySunIntensity = 500.0f;
    constexpr float kDaySkyIntensity = 100.0f;
    constexpr float kNightSkyFraction = 0.15f;

    // ---- 班次（§3.1）----
    constexpr double kMorningShiftStart = 5.5 * 60.0;   // 05:30
    constexpr double kMorningShiftEnd = 13.5 * 60.0;    // 13:30
    constexpr double kEveningShiftEnd = 21.5 * 60.0;    // 21:30
    constexpr double kAllDayShiftStart = 6.5 * 60.0;    // 06:30
    constexpr double kNightShiftStart = 21.0 * 60.0;    // 21:00
    constexpr double kNightShiftEnd = 6.0 * 60.0;       // 06:00（次日）
    constexpr double kCommuteLeadMinutes = 20.0;        // 班前 20 分钟生成
    constexpr double kHandoverGraceMinutes = 30.0;      // 接班者迟迟未到时旧员工最多多等 30 分钟

    // ---- 航班表（§4.3）----
    constexpr int    kMinFlightsPerDay = 8;
    constexpr int    kMaxFlightsPerDay = 12;
    constexpr double kFirstDeparture = 7.0 * 60.0;
    constexpr double kLastDeparture = 21.0 * 60.0;
    constexpr double kSameGateSpacingMinutes = 90.0;
    constexpr int    kMinPaxPerFlight = 6;
    constexpr int    kMaxPaxPerFlight = 10;
    constexpr double kCheckinOpenLead = 120.0;  // 起飞前 120 分钟开值机
    constexpr double kBoardingLead = 30.0;
    constexpr double kFinalCallLead = 10.0;
    // 旅客在起飞前 240~160 分钟生成。动态值机/安检在拥堵时可能耗时 40~70 分钟，
    // 该窗口可让大多数旅客在登机前保留 90~150 分钟空侧消费或候机时间。
    constexpr double kSpawnWindowStart = 240.0;
    constexpr double kSpawnWindowEnd = 160.0;

    // ---- 旅客（§3.2）----
    constexpr int   kMaxConcurrentPassengers = 24;
    constexpr float kKioskFraction = 0.30f;     // 30% 走自助值机
    constexpr float kPassengerGroupFraction = 0.60f;
    constexpr int   kMaxPassengerGroupSize = 4;
    constexpr float kGroupSpawnSpacing = 0.65f;
    constexpr float kGroupTargetSpacing = 0.55f;
    constexpr double kGroupDiscussionCooldownMinutes = 28.0;
    // 1 游戏日 = 12 真实分钟（时间压缩 120×），步速也按压缩感放大，
    // 否则横穿航站楼要吃掉 ~90 游戏分钟，旅客赶不上登机。
    constexpr float kBaseWalkSpeed = 4.2f;      // m/s
    constexpr float kWalkSpeedJitter = 0.15f;   // ±15%
    constexpr double kForceToGateMinutes = 5.0; // 距开始登机不足 5 分钟时强制收敛去 gate

    // ---- 服务计时（游戏分钟）----
    // 服务时间从旅客真正到达服务位后开始。实际耗时还会叠加设施状态、拥堵和旅客复杂度波动。
    constexpr double kCheckinServiceMin = 9.0,   kCheckinServiceMax = 18.0;
    constexpr double kKioskServiceMin = 4.0,     kKioskServiceMax = 8.0;
    constexpr double kSecurityServiceMin = 5.0,  kSecurityServiceMax = 10.0;
    constexpr double kGateServiceMin = 0.5,     kGateServiceMax = 1.5;
    constexpr double kFacilityDelayMultiplierMin = 0.85;
    constexpr double kFacilityDelayMultiplierMax = 1.45;
    constexpr double kPassengerDelayMultiplierMin = 0.90;
    constexpr double kPassengerDelayMultiplierMax = 1.35;
    constexpr double kCongestionDelayPerWaitingPassenger = 0.04;
    constexpr double kCongestionDelayMultiplierMax = 1.28;
    constexpr double kServiceIncidentChance = 0.12;
    constexpr double kServiceIncidentMultiplierMin = 1.35;
    constexpr double kServiceIncidentMultiplierMax = 1.90;
    constexpr double kShopUseMin = 3.0,         kShopUseMax = 8.0;
    constexpr double kToiletUseMin = 1.0,       kToiletUseMax = 2.5;
    constexpr double kVendingUseMin = 0.8,      kVendingUseMax = 1.5;
    constexpr double kSitMin = 10.0,            kSitMax = 25.0;

    // ---- 排队（§7.3）----
    constexpr float kQueueFirstSlotOffset = 0.9f;
    constexpr float kQueueSlotSpacing = 0.8f;
    constexpr int   kQueueMaxSlots = 8;
    constexpr int   kLongQueueThreshold = 6;    // 触发"抱怨"决策时刻

    // ---- 移动（§7.2）----
    constexpr float kNavCellSize = 0.45f;
    constexpr float kAgentRadius = 0.28f;
    constexpr float kSeparationRadius = 0.6f;
    constexpr float kSeparationStrength = 1.2f;
    constexpr float kGroundY = 0.15f;

    // ---- 决策（§5.3）----
    constexpr double kDecisionCooldownMinutes = 20.0;
    constexpr double kBubbleDurationMinutes = 10.0;   // 游戏分钟（默认速度约 5 真实秒）
    constexpr double kPerceptionIntervalSeconds = 0.5;
    constexpr float  kNeighborRadius = 3.0f;
    constexpr int    kMaxVisibleBubbles = 8;

    // ---- 陆侧生成/消失点（引擎世界系；scad: 停车场(-19,-25.7) / 公交(0,-20) / 出租(14,-20)）----
    constexpr glm::vec3 kSpawnPoints[3] = {
        {-19.0f, kGroundY, 25.7f},
        {0.0f,   kGroundY, 20.0f},
        {14.0f,  kGroundY, 20.0f},
    };

    // ---- POI 服务点偏移（沿 frontDir，米）----
    constexpr float kCheckinServiceOffset = 1.4f;  // 柜台前
    constexpr float kCounterStandOffset = -0.9f;   // 员工站柜台后
    constexpr float kSecurityEntryOffset = 1.2f;   // 通道南口
    constexpr float kSecurityPassDepth = 3.0f;     // 南进北出纵深
    constexpr float kGateServiceOffset = 1.5f;
    constexpr float kGenericUseOffset = 0.9f;
    constexpr float kSeatSpacing = 0.8f;           // 4 联座座距
    constexpr float kSeatFrontOffset = 0.35f;

    // ---- 员工 roster（§3.1）----
    struct FStaffDef
    {
        const char* name;
        EAgentRole  role;
        const char* post;       // 岗位 POI（GateAgent 动态调度，填 staff 办公室）
        EShift      shift;
        const char* personality;
        glm::vec3   color;
    };

    inline const FStaffDef kStaffRoster[] = {
        {"陈柜员", EAgentRole::Checkin,  "checkin_01", EShift::Morning, "麻利、嗓门大",     {0.20f, 0.45f, 0.80f}},
        {"林柜员", EAgentRole::Checkin,  "checkin_02", EShift::Morning, "细心、爱聊天",     {0.20f, 0.45f, 0.80f}},
        {"赵柜员", EAgentRole::Checkin,  "checkin_03", EShift::Evening, "沉稳、不苟言笑",   {0.20f, 0.45f, 0.80f}},
        {"钱柜员", EAgentRole::Checkin,  "checkin_04", EShift::Evening, "新手、容易紧张",   {0.20f, 0.45f, 0.80f}},
        {"王安检", EAgentRole::Security, "security_01", EShift::Morning, "严格、一丝不苟",  {0.85f, 0.30f, 0.25f}},
        {"李安检", EAgentRole::Security, "security_02", EShift::Morning, "幽默、爱开玩笑",  {0.85f, 0.30f, 0.25f}},
        {"张安检", EAgentRole::Security, "security_03", EShift::Evening, "老练、眼尖",      {0.85f, 0.30f, 0.25f}},
        {"刘安检", EAgentRole::Security, "security_04", EShift::Evening, "话少、动作快",    {0.85f, 0.30f, 0.25f}},
        {"周登机", EAgentRole::GateAgent, "staff_01",  EShift::Morning, "嗓音甜、有耐心",   {0.30f, 0.70f, 0.55f}},
        {"吴登机", EAgentRole::GateAgent, "staff_02",  EShift::Evening, "干练、走路带风",   {0.30f, 0.70f, 0.55f}},
        {"郑问询", EAgentRole::Info,     "info_01",    EShift::Morning, "热心、百事通",     {0.90f, 0.65f, 0.20f}},
        {"咖啡师", EAgentRole::Clerk,    "cafe_01",    EShift::AllDay,  "文艺、慢性子",     {0.55f, 0.35f, 0.20f}},
        {"汉堡哥", EAgentRole::Clerk,    "food_01",    EShift::AllDay,  "风风火火、爱吆喝", {0.80f, 0.40f, 0.20f}},
        {"便利妹", EAgentRole::Clerk,    "shop_01",    EShift::AllDay,  "机灵、记性好",     {0.45f, 0.65f, 0.30f}},
        {"书店姐", EAgentRole::Clerk,    "book_01",    EShift::AllDay,  "安静、爱推荐书",   {0.45f, 0.50f, 0.75f}},
        {"礼品叔", EAgentRole::Clerk,    "gift_01",    EShift::AllDay,  "健谈、会砍价",     {0.20f, 0.60f, 0.55f}},
        {"夜保洁", EAgentRole::Cleaner,  "staff_01",   EShift::Night,   "勤快、自言自语",   {0.60f, 0.60f, 0.60f}},
        {"老保安", EAgentRole::Guard,    "staff_02",   EShift::Night,   "警觉、爱哼歌",     {0.25f, 0.30f, 0.40f}},
    };
    constexpr int kStaffCount = static_cast<int>(sizeof(kStaffRoster) / sizeof(kStaffRoster[0]));

    // 旅客配色盘（随机循环取用）。
    inline const glm::vec3 kPassengerPalette[] = {
        {0.85f, 0.55f, 0.30f}, {0.40f, 0.60f, 0.85f}, {0.65f, 0.80f, 0.40f}, {0.80f, 0.45f, 0.60f},
        {0.50f, 0.75f, 0.70f}, {0.90f, 0.75f, 0.35f}, {0.60f, 0.50f, 0.80f}, {0.75f, 0.60f, 0.50f},
    };
    constexpr int kPassengerPaletteCount = static_cast<int>(sizeof(kPassengerPalette) / sizeof(kPassengerPalette[0]));

    inline const char* kPassengerNames[] = {
        "张先生", "李女士", "王同学", "赵阿姨", "陈大爷", "杨小姐", "黄先生", "周女士",
        "吴老板", "徐医生", "孙教授", "马师傅", "朱小哥", "胡大姐", "郭叔叔", "何同学",
        "高经理", "林姑娘", "罗先生", "宋女士", "谢同学", "唐阿姨", "韩先生", "冯小姐",
    };
    constexpr int kPassengerNameCount = static_cast<int>(sizeof(kPassengerNames) / sizeof(kPassengerNames[0]));

    inline const char* kPersonalities[] = {"急躁", "悠闲", "健谈"};
    constexpr int kPersonalityCount = 3;

    constexpr int kMaxAgents = kStaffCount + kMaxConcurrentPassengers; // 视觉池上限
}
