#include "Brotato3DUI.hpp"

#include "Brotato3DAudio.hpp"
#include "Brotato3DGameInstance.hpp"
#include "Engine/Runtime/Subsystems/NextLocalization.h"
#include "Engine/Runtime/UI/RmlUiSystem.hpp"

#if GK_WITH_RMLUI
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#endif

#include <algorithm>
#include <cmath>
#include <fmt/format.h>
#include <sstream>

namespace
{
    bool GSettingsOpen = false;
    bool GHelpOpen = false;
    std::string GSubmittedBody;
#if GK_WITH_RMLUI
    Rml::Element* GSubmittedRoot = nullptr;
#endif
    bool GBindEventsThisFrame = false;

    std::string Escape(std::string_view text)
    {
        std::string result;
        result.reserve(text.size());
        for (char ch : text)
        {
            switch (ch)
            {
            case '&':
                result += "&amp;";
                break;
            case '<':
                result += "&lt;";
                break;
            case '>':
                result += "&gt;";
                break;
            case '"':
                result += "&quot;";
                break;
            case '\'':
                result += "&#39;";
                break;
            default:
                result += ch;
                break;
            }
        }
        return result;
    }

    std::string Trim(std::string_view text)
    {
        const size_t start = text.find_first_not_of(" \t\r\n");
        if (start == std::string_view::npos)
        {
            return {};
        }
        const size_t end = text.find_last_not_of(" \t\r\n");
        return std::string(text.substr(start, end - start + 1));
    }

    std::string MarkdownInlineToRml(std::string_view text)
    {
        std::string result;
        size_t cursor = 0;
        bool strongOpen = false;
        while (cursor < text.size())
        {
            if (cursor + 1 < text.size() && text[cursor] == '*' && text[cursor + 1] == '*')
            {
                result += strongOpen ? "</strong>" : "<strong>";
                strongOpen = !strongOpen;
                cursor += 2;
                continue;
            }

            const size_t nextStrong = text.find("**", cursor);
            const std::string_view segment = nextStrong == std::string_view::npos ?
                text.substr(cursor) :
                text.substr(cursor, nextStrong - cursor);
            result += Escape(segment);
            if (nextStrong == std::string_view::npos)
            {
                break;
            }
            cursor = nextStrong;
        }

        if (strongOpen)
        {
            result += "</strong>";
        }
        return result;
    }

    std::string Tr(const Brotato3DGameInstance& gameInstance, const std::string& key, const std::string& fallback)
    {
        if (const NextLocalization* localization = gameInstance.GetEngine().GetLocalization())
        {
            return localization->Get(key, fallback);
        }
        return fallback;
    }

    template <typename... Args>
    std::string TrFormat(
        const Brotato3DGameInstance& gameInstance,
        const std::string& key,
        const std::string& fallback,
        Args&&... args)
    {
        return fmt::format(fmt::runtime(Tr(gameInstance, key, fallback)), std::forward<Args>(args)...);
    }

    std::string FormatTime(float seconds)
    {
        const int total = std::max(0, static_cast<int>(std::ceil(seconds)));
        return fmt::format("{:02}:{:02}", total / 60, total % 60);
    }

    std::string Button(const std::string& id, const std::string& label, const std::string& className = "")
    {
        return fmt::format(
            "<button id=\"{}\" class=\"{}\">{}</button>",
            Escape(id),
            Escape(className),
            Escape(label));
    }

    std::string StatLine(const std::string& label, const std::string& value)
    {
        return fmt::format(
            "<div class=\"stat-line\"><span>{}</span><strong>{}</strong></div>",
            Escape(label),
            Escape(value));
    }

    std::string RarityClass(const std::string& rarity)
    {
        if (rarity == "rare")
        {
            return " rare";
        }
        if (rarity == "uncommon")
        {
            return " uncommon";
        }
        return "";
    }

    std::string StatDisplayName(const Brotato3DGameInstance& gameInstance, const std::string& statKey)
    {
        if (statKey == "maxHpFlat")
        {
            return Tr(gameInstance, "character.hp", "生命");
        }
        if (statKey == "moveSpeedPct")
        {
            return Tr(gameInstance, "character.move", "移速");
        }
        if (statKey == "atkSpeedPct")
        {
            return Tr(gameInstance, "stat.attack_speed", "攻速");
        }
        if (statKey == "critChancePct")
        {
            return Tr(gameInstance, "character.crit", "暴击");
        }
        if (statKey == "rangePct")
        {
            return Tr(gameInstance, "character.range", "射程");
        }
        if (statKey == "damagePct" || statKey == "damageFlat")
        {
            return Tr(gameInstance, "character.damage", "伤害");
        }
        if (statKey == "healPct")
        {
            return Tr(gameInstance, "stat.heal", "治疗");
        }
        if (statKey == "critMultiplier")
        {
            return Tr(gameInstance, "stat.crit_damage", "暴伤");
        }
        return statKey;
    }

    std::string FormatStatValue(const std::string& statKey, float value)
    {
        if (statKey.ends_with("Pct") || statKey == "healPct")
        {
            return fmt::format("{:+.0f}%", value * 100.0f);
        }
        if (statKey == "critMultiplier")
        {
            return fmt::format("{:+.0f}%", value * 100.0f);
        }
        return fmt::format("{:+.0f}", value);
    }

#if GK_WITH_RMLUI
    constexpr const char* kBrotatoDocumentId = "brotato3d-ui";

    const char* BrotatoDocument()
    {
        return R"RML(
<rml>
<head>
<title>Brotato3D</title>
<style>
body {
    width: 100%;
    height: 100%;
    margin: 0;
    font-family: "Droid Sans Fallback";
    color: #f5f1dc;
}
strong, h1, h2, h3 {
    font-weight: normal;
}
strong {
    color: #ffe09a;
}
#root {
    width: 100%;
    height: 100%;
}
.screen {
    position: absolute;
    left: 0;
    top: 0;
    width: 100%;
    height: 100%;
}
.dim {
    background-color: rgba(7, 10, 16, 0.70);
}
.menu-panel {
    position: absolute;
    left: 7%;
    top: 9%;
    width: 430px;
    padding: 28px;
    background-color: rgba(14, 18, 25, 0.90);
    border: 3px #211710;
}
.title {
    font-size: 58px;
    color: #ffd060;
    margin-bottom: 10px;
}
.subtitle {
    color: #b9c4d8;
    font-size: 17px;
    margin-bottom: 24px;
}
button {
    display: block;
    width: 100%;
    height: 42px;
    margin-top: 10px;
    padding: 8px 12px;
    font-size: 18px;
    color: #1b1108;
    background-color: #f0b342;
    border: 2px #24160c;
}
button:hover {
    background-color: #ffd063;
}
button.secondary {
    background-color: #8fb7c7;
}
button.danger {
    background-color: #df6660;
}
.hud-top {
    position: absolute;
    left: 24px;
    top: 18px;
    width: 360px;
}
.hud-card, .modal, .card, .offer {
    background-color: rgba(12, 15, 21, 0.86);
    border: 2px #1d2430;
    padding: 14px;
}
.bar {
    width: 100%;
    height: 18px;
    margin-top: 6px;
    background-color: #202735;
    border: 1px #000000;
}
.bar-fill {
    height: 100%;
    background-color: #d74640;
}
.xp .bar-fill {
    background-color: #55b9e8;
}
.hud-right {
    position: absolute;
    right: 24px;
    top: 18px;
    width: 280px;
}
.stat-line {
    display: flex;
    justify-content: space-between;
    margin-bottom: 6px;
    font-size: 16px;
}
.stat-line span {
    color: #aeb9c8;
}
.stat-line strong {
    color: #ffe09a;
}
.banner {
    position: absolute;
    left: 31%;
    top: 12%;
    width: 38%;
    text-align: center;
    font-size: 30px;
    color: #ffe27a;
    padding: 16px;
    background-color: rgba(16, 20, 30, 0.78);
    border: 2px #e0b045;
}
.modal {
    position: absolute;
    left: 50%;
    top: 50%;
    width: 560px;
    margin-left: -280px;
    margin-top: -250px;
}
.modal.wide {
    width: 820px;
    margin-left: -410px;
}
.modal.help {
    width: 700px;
    margin-left: -350px;
    margin-top: -300px;
}
.modal h1, .modal h2 {
    margin: 0 0 14px 0;
    color: #ffd060;
}
.markdown {
    color: #e7dfc3;
    font-size: 17px;
    line-height: 1.35;
}
.markdown h2 {
    margin: 14px 0 8px 0;
    color: #ffd060;
    font-size: 24px;
}
.markdown p {
    margin: 0 0 10px 0;
}
.markdown .md-list {
    margin: 4px 0 12px 0;
}
.markdown .md-li {
    margin: 0 0 7px 0;
    padding-left: 18px;
}
.row {
    display: flex;
    gap: 12px;
}
.card, .offer {
    flex: 1;
    min-height: 150px;
}
.card h3, .offer h3 {
    margin: 0 0 8px 0;
    color: #f6d680;
}
.muted {
    color: #aeb9c8;
}
.rare {
    border-color: #8a55c4;
}
.uncommon {
    border-color: #4dad71;
}
.selected {
    border-color: #ffd060;
    background-color: rgba(60, 47, 22, 0.88);
}
.footer {
    margin-top: 16px;
}
.center {
    text-align: center;
}
.settings-row {
    display: flex;
    gap: 10px;
    margin-top: 10px;
}
.settings-row button {
    flex: 1;
}
</style>
</head>
<body>
<div id="root"></div>
</body>
</rml>
)RML";
    }

    NextUI::RmlUiSystem* Rml(Brotato3DGameInstance& gameInstance)
    {
        return gameInstance.GetEngine().GetRmlUi();
    }

    Rml::Element* Root(Brotato3DGameInstance& gameInstance)
    {
        NextUI::RmlUiSystem* rml = Rml(gameInstance);
        if (!rml || !rml->IsAvailable())
        {
            return nullptr;
        }

        rml->EnsureDocument(kBrotatoDocumentId, BrotatoDocument());
        return rml->GetElementById(kBrotatoDocumentId, "root");
    }

    void Submit(Brotato3DGameInstance& gameInstance, const std::string& rmlBody)
    {
        GBindEventsThisFrame = false;
        NextUI::RmlUiSystem* rml = Rml(gameInstance);
        Rml::Element* root = Root(gameInstance);
        if (!rml || !root)
        {
            return;
        }

        if (root == GSubmittedRoot && rmlBody == GSubmittedBody)
        {
            return;
        }

        rml->ClearEventListeners();
        root->SetInnerRML(rmlBody);
        GSubmittedRoot = root;
        GSubmittedBody = rmlBody;
        GBindEventsThisFrame = true;
    }

    void Click(Brotato3DGameInstance& gameInstance, const std::string& id, std::function<void()> callback)
    {
        if (GBindEventsThisFrame)
        {
            if (NextUI::RmlUiSystem* rml = Rml(gameInstance))
            {
                rml->ListenClick(kBrotatoDocumentId, id, std::move(callback));
            }
        }
    }

    std::string MarkdownToRml(std::string_view markdown)
    {
        std::istringstream input{std::string(markdown)};
        std::ostringstream html;
        std::string line;
        bool inList = false;

        auto closeList = [&]()
        {
            if (inList)
            {
                html << "</div>";
                inList = false;
            }
        };

        while (std::getline(input, line))
        {
            const std::string trimmed = Trim(line);
            if (trimmed.empty())
            {
                closeList();
                continue;
            }

            if (trimmed.starts_with("## "))
            {
                closeList();
                html << "<h2>" << MarkdownInlineToRml(std::string_view(trimmed).substr(3)) << "</h2>";
                continue;
            }

            if (trimmed.starts_with("# "))
            {
                closeList();
                html << "<h2>" << MarkdownInlineToRml(std::string_view(trimmed).substr(2)) << "</h2>";
                continue;
            }

            if (trimmed.starts_with("- "))
            {
                if (!inList)
                {
                    html << "<div class=\"md-list\">";
                    inList = true;
                }
                html << "<div class=\"md-li\">- " << MarkdownInlineToRml(std::string_view(trimmed).substr(2)) << "</div>";
                continue;
            }

            closeList();
            html << "<p>" << MarkdownInlineToRml(trimmed) << "</p>";
        }

        closeList();
        return html.str();
    }

    std::string HelpMarkdown()
    {
        return R"MD(
# Brotato3D 规则

## 目标
- 在竞技场中活过一波又一波敌人。
- 击败敌人获得材料和经验，尽量撑到最终波次。
- 每波结束后用材料购物，强化下一波的生存能力。

## 操作
- **WASD** 移动角色。
- **鼠标** 控制视角与瞄准方向。
- **冲刺** 用来拉开距离、穿过危险区域，冷却后会恢复次数。

## 成长
- 升级时选择一张属性卡，提升生命、伤害、暴击、攻速或移速。
- 商店里可以买武器、被动道具和属性强化。
- 同名武器可以合成更高阶武器，伤害和节奏都会提升。

## 提示
- 前期优先保证生命和移动空间。
- 材料不要一次花光，留一点给关键刷新。
- 被包围时先冲刺脱离，再回头清怪。
)MD";
    }

    std::string HelpOverlay()
    {
        if (!GHelpOpen)
        {
            return {};
        }

        std::ostringstream html;
        html << "<div class=\"screen dim\"></div><div class=\"modal help\">";
        html << "<div class=\"markdown\">" << MarkdownToRml(HelpMarkdown()) << "</div>";
        html << "<div class=\"footer\">" << Button("help_close", "关闭") << "</div>";
        html << "</div>";
        return html.str();
    }

    std::string SettingsOverlay(Brotato3DGameInstance& gameInstance)
    {
        if (!GSettingsOpen)
        {
            return {};
        }

        std::ostringstream html;
        html << "<div class=\"screen dim\"></div><div class=\"modal\">";
        html << "<h2>" << Escape(Tr(gameInstance, "settings.title", "设置")) << "</h2>";
        html << StatLine("SFX", fmt::format("{:.0f}%", Brotato3D::SfxVolume * 100.0f));
        html << "<div class=\"settings-row\">" << Button("sfx_down", "-") << Button("sfx_up", "+") << "</div>";
        html << StatLine("Music", fmt::format("{:.0f}%", Brotato3D::MusicVolume * 100.0f));
        html << "<div class=\"settings-row\">" << Button("music_down", "-") << Button("music_up", "+") << "</div>";
        html << Button("toggle_shake", Brotato3D::ScreenShakeEnabled ? "Screen shake: ON" : "Screen shake: OFF", "secondary");
        html << Button("toggle_hp", Brotato3D::ShowEnemyHpBars ? "Enemy HP bars: ON" : "Enemy HP bars: OFF", "secondary");
        html << Button("settings_close", Tr(gameInstance, "settings.apply", "应用并关闭"));
        html << "</div>";
        return html.str();
    }

    void BindSettings(Brotato3DGameInstance& gameInstance)
    {
        Click(gameInstance, "settings_close", []() { GSettingsOpen = false; });
        Click(gameInstance, "sfx_down", []()
        {
            Brotato3D::SfxVolume = std::clamp(Brotato3D::SfxVolume - 0.1f, 0.0f, 1.0f);
        });
        Click(gameInstance, "sfx_up", []()
        {
            Brotato3D::SfxVolume = std::clamp(Brotato3D::SfxVolume + 0.1f, 0.0f, 1.0f);
        });
        Click(gameInstance, "music_down", []()
        {
            Brotato3D::MusicVolume = std::clamp(Brotato3D::MusicVolume - 0.1f, 0.0f, 1.0f);
            Brotato3D::RefreshBgmVolume();
        });
        Click(gameInstance, "music_up", []()
        {
            Brotato3D::MusicVolume = std::clamp(Brotato3D::MusicVolume + 0.1f, 0.0f, 1.0f);
            Brotato3D::RefreshBgmVolume();
        });
        Click(gameInstance, "toggle_shake", []() { Brotato3D::ScreenShakeEnabled = !Brotato3D::ScreenShakeEnabled; });
        Click(gameInstance, "toggle_hp", []() { Brotato3D::ShowEnemyHpBars = !Brotato3D::ShowEnemyHpBars; });
    }

    void BindCharacterButtons(Brotato3DGameInstance& gameInstance)
    {
        const auto& characters = gameInstance.GetCharacterDefs();
        for (size_t index = 0; index < characters.size(); ++index)
        {
            const std::string id = characters[index].id;
            Click(gameInstance, fmt::format("character_{}", index), [&gameInstance, id]() { gameInstance.SelectCharacter(id); });
        }

        const auto& arenas = gameInstance.GetArenaDefs();
        for (size_t index = 0; index < arenas.size(); ++index)
        {
            const std::string id = arenas[index].id;
            Click(gameInstance, fmt::format("arena_{}", index), [&gameInstance, id]() { gameInstance.SelectArena(id); });
        }
    }
#endif
}

namespace Brotato3D
{
    void RenderMainMenu(Brotato3DGameInstance& gameInstance)
    {
#if GK_WITH_RMLUI
        std::ostringstream html;
        html << "<div class=\"screen dim\"></div><div class=\"menu-panel\">";
        html << "<div class=\"title\">Brotato3D</div>";
        html << "<div class=\"subtitle\">Survive the arena, build your loadout, and push one more wave.</div>";
        html << Button("start", Tr(gameInstance, "main.start", "开始"));
        html << Button("help", "帮助", "secondary");
        html << Button("settings", Tr(gameInstance, "main.settings", "设置"), "secondary");
        html << Button("exit", Tr(gameInstance, "main.exit", "退出"), "danger");
        const Brotato3D::FBestRecord& best = gameInstance.GetBestRecord();
        html << "<div class=\"footer\">";
        html << StatLine(Tr(gameInstance, "best.wins", "通关次数"), std::to_string(best.totalWins));
        html << StatLine(Tr(gameInstance, "best.kills", "累计击杀"), std::to_string(best.totalKills));
        html << "</div></div>";
        html << SettingsOverlay(gameInstance);
        html << HelpOverlay();
        Submit(gameInstance, html.str());

        Click(gameInstance, "start", [&gameInstance]()
        {
            GHelpOpen = false;
            GSettingsOpen = false;
            gameInstance.GoToCharacterSelect();
        });
        Click(gameInstance, "help", []()
        {
            GHelpOpen = true;
            GSettingsOpen = false;
        });
        Click(gameInstance, "settings", []()
        {
            GSettingsOpen = true;
            GHelpOpen = false;
        });
        Click(gameInstance, "help_close", []() { GHelpOpen = false; });
        Click(gameInstance, "exit", [&gameInstance]() { gameInstance.ExitGame(); });
        BindSettings(gameInstance);
#else
        (void)gameInstance;
#endif
    }

    void RenderCharacterSelect(Brotato3DGameInstance& gameInstance)
    {
#if GK_WITH_RMLUI
        std::ostringstream html;
        html << "<div class=\"screen dim\"></div><div class=\"modal wide\">";
        html << "<h1>" << Escape(Tr(gameInstance, "character.select", "选择角色")) << "</h1>";
        html << "<div class=\"row\">";
        const auto& characters = gameInstance.GetCharacterDefs();
        for (size_t index = 0; index < characters.size(); ++index)
        {
            const auto& character = characters[index];
            const bool selected = character.id == gameInstance.GetSelectedCharacterId();
            html << fmt::format("<div class=\"card{}\">", selected ? " selected" : "");
            html << "<h3>" << Escape(Tr(gameInstance, "character." + character.id + ".name", character.name)) << "</h3>";
            html << "<p class=\"muted\">" << Escape(Tr(gameInstance, "character." + character.id + ".tagline", character.tagline)) << "</p>";
            html << Button(fmt::format("character_{}", index), selected ? "Selected" : "Select", "secondary");
            html << "</div>";
        }
        html << "</div><h2>" << Escape(Tr(gameInstance, "arena.select", "选择场地")) << "</h2><div class=\"row\">";
        const auto& arenas = gameInstance.GetArenaDefs();
        for (size_t index = 0; index < arenas.size(); ++index)
        {
            const auto& arena = arenas[index];
            const bool selected = arena.id == gameInstance.GetSelectedArenaId();
            html << fmt::format("<div class=\"card{}\">", selected ? " selected" : "");
            html << "<h3>" << Escape(Tr(gameInstance, "arena." + arena.id + ".name", arena.name)) << "</h3>";
            html << "<p class=\"muted\">"
                 << Escape(fmt::format("{:.0f} x {:.0f}", arena.halfExtent.x * 2.0f, arena.halfExtent.y * 2.0f))
                 << "</p>";
            html << Button(fmt::format("arena_{}", index), selected ? "Selected" : "Select", "secondary");
            html << "</div>";
        }
        html << "</div><div class=\"footer row\">";
        html << Button("run", Tr(gameInstance, "main.start", "开始"));
        html << Button("back", Tr(gameInstance, "pause.menu", "回主菜单"), "secondary");
        html << "</div></div>";
        Submit(gameInstance, html.str());

        BindCharacterButtons(gameInstance);
        Click(gameInstance, "run", [&gameInstance]() { gameInstance.StartNewRun(); });
        Click(gameInstance, "back", [&gameInstance]() { gameInstance.GoToMainMenu(); });
#else
        (void)gameInstance;
#endif
    }

    void RenderHUD(Brotato3DGameInstance& gameInstance)
    {
#if GK_WITH_RMLUI
        const auto& player = gameInstance.GetPlayer();
        const float hpRatio = player.maxHp > 0 ? std::clamp(static_cast<float>(player.currentHp) / player.maxHp, 0.0f, 1.0f) : 0.0f;
        const int xpToNext = std::max(1, gameInstance.GetXpToNextLevel());
        const float xpRatio = std::clamp(static_cast<float>(player.currentXp) / xpToNext, 0.0f, 1.0f);

        std::ostringstream html;
        html << "<div class=\"hud-top\"><div class=\"hud-card\">";
        html << StatLine(Tr(gameInstance, "character.hp", "生命"), fmt::format("{} / {}", player.currentHp, player.maxHp));
        html << fmt::format("<div class=\"bar\"><div class=\"bar-fill\" style=\"width: {:.1f}%;\"></div></div>", hpRatio * 100.0f);
        html << StatLine("XP", fmt::format("{} / {}", player.currentXp, xpToNext));
        html << fmt::format("<div class=\"bar xp\"><div class=\"bar-fill\" style=\"width: {:.1f}%;\"></div></div>", xpRatio * 100.0f);
        html << "</div></div>";

        html << "<div class=\"hud-right\"><div class=\"hud-card\">";
        html << StatLine(Tr(gameInstance, "hud.wave", "波次"),
                         fmt::format("{}/{}", gameInstance.GetWaveSystem().GetCurrentWaveIndex() + 1,
                                     gameInstance.GetWaveSystem().GetWaveCount()));
        html << StatLine(Tr(gameInstance, "result.time", "时间"), FormatTime(gameInstance.GetRunElapsedSec()));
        html << StatLine(Tr(gameInstance, "hud.materials", "材料"), std::to_string(player.materials));
        html << StatLine(Tr(gameInstance, "result.kills", "击杀"), std::to_string(gameInstance.GetKillCount()));
        html << StatLine("Dash", fmt::format("{} / {}", gameInstance.GetDashCharges(), gameInstance.GetDashMaxCharges()));
        html << "<div class=\"muted\">Weapons</div>";
        for (const auto& weapon : gameInstance.GetWeapons())
        {
            html << "<div class=\"stat-line\"><span>" << Escape(weapon.weaponId) << "</span><strong>T" << weapon.tier << "</strong></div>";
        }
        html << "<div class=\"muted\">Items</div>";
        for (const std::string& itemId : gameInstance.GetOwnedItemIds())
        {
            if (const Brotato3D::FItemDef* item = gameInstance.GetItemDef(itemId))
            {
                html << "<div class=\"stat-line\"><span>" << Escape(Tr(gameInstance, "item." + item->id + ".name", item->name))
                     << "</span><strong>" << Escape(item->rarity) << "</strong></div>";
            }
        }
        html << "</div></div>";

        if (gameInstance.GetWaveBannerMs() > 0.0f)
        {
            html << "<div class=\"banner\">" << Escape(gameInstance.GetWaveBannerText()) << "</div>";
        }
        if (gameInstance.GetWeaponMergeBannerMs() > 0.0f)
        {
            html << "<div class=\"banner\" style=\"top: 22%;\">" << Escape(gameInstance.GetWeaponMergeBannerText()) << "</div>";
        }
        Submit(gameInstance, html.str());
#else
        (void)gameInstance;
#endif
    }

    void RenderPauseModal(Brotato3DGameInstance& gameInstance)
    {
#if GK_WITH_RMLUI
        std::ostringstream html;
        html << "<div class=\"screen dim\"></div><div class=\"modal center\">";
        html << "<h1>" << Escape(Tr(gameInstance, "pause.title", "游戏暂停")) << "</h1>";
        html << Button("resume", Tr(gameInstance, "pause.resume", "继续"));
        html << Button("pause_settings", Tr(gameInstance, "main.settings", "设置"), "secondary");
        html << Button("restart", Tr(gameInstance, "pause.restart", "重新开始（同角色）"), "secondary");
        html << Button("menu", Tr(gameInstance, "pause.menu", "退出到主菜单"), "danger");
        html << "</div>" << SettingsOverlay(gameInstance);
        Submit(gameInstance, html.str());

        Click(gameInstance, "resume", [&gameInstance]() { gameInstance.ResumeGame(); });
        Click(gameInstance, "pause_settings", []() { GSettingsOpen = true; });
        Click(gameInstance, "restart", [&gameInstance]() { gameInstance.StartNewRun(); });
        Click(gameInstance, "menu", [&gameInstance]() { gameInstance.GoToMainMenu(); });
        BindSettings(gameInstance);
#else
        (void)gameInstance;
#endif
    }

    void RenderSettingsModal(Brotato3DGameInstance& gameInstance)
    {
#if GK_WITH_RMLUI
        GSettingsOpen = true;
        Submit(gameInstance, SettingsOverlay(gameInstance));
        BindSettings(gameInstance);
#else
        (void)gameInstance;
#endif
    }

    void RenderUpgradeModal(Brotato3DGameInstance& gameInstance)
    {
#if GK_WITH_RMLUI
        std::ostringstream html;
        html << "<div class=\"screen dim\"></div><div class=\"modal wide\"><h1>"
             << Escape(Tr(gameInstance, "upgrade.title", "选择升级")) << "</h1><div class=\"row\">";
        const auto& choices = gameInstance.GetCurrentUpgradeChoices();
        for (size_t index = 0; index < choices.size(); ++index)
        {
            const auto& choice = choices[index];
            html << "<div class=\"card\">";
            html << "<h3>" << Escape(StatDisplayName(gameInstance, choice.stat)) << "</h3>";
            html << "<p class=\"muted\">" << Escape(FormatStatValue(choice.stat, choice.delta)) << "</p>";
            html << Button(fmt::format("upgrade_{}", index), Tr(gameInstance, "upgrade.pick", "选择"));
            html << "</div>";
        }
        html << "</div></div>";
        Submit(gameInstance, html.str());
        for (size_t index = 0; index < choices.size(); ++index)
        {
            Click(gameInstance, fmt::format("upgrade_{}", index), [&gameInstance, index]() { gameInstance.SelectUpgrade(index); });
        }
#else
        (void)gameInstance;
#endif
    }

    void RenderShopModal(Brotato3DGameInstance& gameInstance)
    {
#if GK_WITH_RMLUI
        const auto& offers = gameInstance.GetShopOffers();
        std::ostringstream html;
        html << "<div class=\"screen dim\"></div><div class=\"modal wide\"><h1>"
             << Escape(Tr(gameInstance, "shop.title", "商店")) << "</h1>";
        html << StatLine(Tr(gameInstance, "hud.materials", "材料"), std::to_string(gameInstance.GetPlayer().materials));
        html << "<div class=\"row\">";
        for (size_t index = 0; index < offers.size(); ++index)
        {
            const auto& offer = offers[index];
            std::string name = offer.id;
            std::string desc = offer.description;
            if (offer.isPassiveItem)
            {
                name = Tr(gameInstance, "item." + offer.id + ".name", offer.name);
                desc = Tr(gameInstance, "item." + offer.id + ".desc", offer.description);
            }
            else if (offer.isWeaponCard)
            {
                name = offer.weaponId;
                desc = Tr(gameInstance, "shop.weapon", "武器");
            }
            else
            {
                name = StatDisplayName(gameInstance, offer.stat);
                desc = FormatStatValue(offer.stat, offer.delta);
            }

            html << fmt::format("<div class=\"offer{}\">", RarityClass(offer.rarity));
            html << "<h3>" << Escape(name) << "</h3>";
            html << "<p class=\"muted\">" << Escape(desc) << "</p>";
            html << StatLine(Tr(gameInstance, "shop.cost", "价格"), std::to_string(offer.cost));
            html << Button(fmt::format("buy_{}", index),
                           gameInstance.CanBuyShopOffer(index) ? Tr(gameInstance, "shop.buy", "购买") :
                                                                 gameInstance.GetShopOfferUnavailableReason(index));
            html << "</div>";
        }
        html << "</div><div class=\"footer row\">";
        html << Button("reroll", TrFormat(gameInstance, "shop.reroll", "刷新（{0}）", gameInstance.GetRerollCost()), "secondary");
        html << Button("next_wave", Tr(gameInstance, "shop.next_wave", "下一波"));
        html << "</div></div>";
        Submit(gameInstance, html.str());

        for (size_t index = 0; index < offers.size(); ++index)
        {
            Click(gameInstance, fmt::format("buy_{}", index), [&gameInstance, index]()
            {
                if (gameInstance.CanBuyShopOffer(index))
                {
                    gameInstance.BuyShopItem(index);
                }
            });
        }
        Click(gameInstance, "reroll", [&gameInstance]() { gameInstance.RerollShop(); });
        Click(gameInstance, "next_wave", [&gameInstance]() { gameInstance.ContinueFromShop(); });
#else
        (void)gameInstance;
#endif
    }

    void RenderResultModal(Brotato3DGameInstance& gameInstance)
    {
#if GK_WITH_RMLUI
        const bool defeated = gameInstance.IsPlayerDead();
        std::ostringstream html;
        html << "<div class=\"screen dim\"></div><div class=\"modal\">";
        html << "<h1>" << Escape(defeated ? Tr(gameInstance, "result.defeat", "失败") :
                                      Tr(gameInstance, "result.victory", "胜利")) << "</h1>";
        html << StatLine(Tr(gameInstance, "result.time", "用时"), FormatTime(gameInstance.GetRunElapsedSec()));
        html << StatLine(Tr(gameInstance, "result.level", "最高等级"), std::to_string(gameInstance.GetPlayer().level));
        html << StatLine(Tr(gameInstance, "result.kills", "击杀"), std::to_string(gameInstance.GetKillCount()));
        html << StatLine(Tr(gameInstance, "result.materials", "获得材料"), std::to_string(gameInstance.GetTotalMaterialsGained()));
        html << "<h2>" << Escape(Tr(gameInstance, "best.title", "最佳记录")) << "</h2>";
        const Brotato3D::FBestRecord& best = gameInstance.GetBestRecord();
        html << StatLine(Tr(gameInstance, "best.wins", "通关次数"), std::to_string(best.totalWins));
        html << StatLine(Tr(gameInstance, "best.kills", "累计击杀"), std::to_string(best.totalKills));
        html << "<div class=\"footer row\">";
        html << Button("result_restart", Tr(gameInstance, "result.restart", "再来一局（同角色）"));
        html << Button("result_menu", Tr(gameInstance, "result.menu", "回主菜单"), "secondary");
        html << "</div></div>";
        Submit(gameInstance, html.str());

        Click(gameInstance, "result_restart", [&gameInstance]() { gameInstance.RestartGame(); });
        Click(gameInstance, "result_menu", [&gameInstance]() { gameInstance.GoToMainMenu(); });
#else
        (void)gameInstance;
#endif
    }
}
