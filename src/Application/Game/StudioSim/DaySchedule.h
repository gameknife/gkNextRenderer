#pragma once

#include <string>

// 脚本化日程 —— LLM 不可用时的确定性 fallback 层。
// 所有员工共用一条简单日程模板（工位为主 + 午休/下午茶去茶水间）。
// LLM 决策可覆盖这里的目标；解析或超时失败时回退到本日程。
namespace StudioSim
{
    // 返回该员工此刻按脚本日程应该所在的 POI 名。
    inline std::string ScheduledPoi(const std::string& homeDeskPoi, double gameMinutes)
    {
        const double hour = gameMinutes / 60.0;
        if (hour >= 12.0 && hour < 13.0) return "pantry_01"; // 午休
        if (hour >= 15.5 && hour < 16.0) return "pantry_01"; // 下午茶
        return homeDeskPoi;                                  // 其余时间在工位
    }
}
