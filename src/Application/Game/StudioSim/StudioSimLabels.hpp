#pragma once

#include "StudioSimTypes.h"

namespace StudioSim
{
    enum class ELabelTextStyle
    {
        Short,
        Prompt,
    };

    inline const char* ProjectStageLabelZh(EProjectStage stage)
    {
        switch (stage)
        {
        case EProjectStage::Planning:   return "企划";
        case EProjectStage::Production: return "生产";
        case EProjectStage::Polish:     return "打磨";
        case EProjectStage::Done:       return "完成";
        default:                        return "?";
        }
    }

    inline const char* GameGenreLabelZh(EGameGenre genre, ELabelTextStyle style = ELabelTextStyle::Short)
    {
        switch (genre)
        {
        case EGameGenre::RPG:        return "RPG";
        case EGameGenre::Action:     return "动作";
        case EGameGenre::Simulation: return style == ELabelTextStyle::Prompt ? "模拟经营" : "模拟";
        case EGameGenre::Puzzle:     return "解谜";
        case EGameGenre::Shooter:    return "射击";
        case EGameGenre::Adventure:  return "冒险";
        default:                     return "未知";
        }
    }

    inline const char* GameThemeLabelZh(EGameTheme theme, ELabelTextStyle style = ELabelTextStyle::Short)
    {
        switch (theme)
        {
        case EGameTheme::Fantasy: return "奇幻";
        case EGameTheme::SciFi:   return "科幻";
        case EGameTheme::Sports:  return style == ELabelTextStyle::Prompt ? "体育" : "运动";
        case EGameTheme::Romance: return "恋爱";
        case EGameTheme::Horror:  return "恐怖";
        case EGameTheme::Daily:   return "日常";
        default:                  return "未知";
        }
    }
}
