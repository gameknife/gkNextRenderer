#include "MagicaLegoUserInterface.hpp"

#include "MagicaLegoStyle.hpp"
#include <fmt/chrono.h>
#include <im_anim.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>
#include <spdlog/spdlog.h>
#include <cstring>
#include <utility>

#include "MagicaLegoAIService.hpp"
#include "MagicaLegoCommands.hpp"
#include "MagicaLegoGameInstance.hpp"
#include "Runtime/Editor/UserInterface.hpp"
#include "Runtime/Platform/PlatformCommon.h"
#include "ThirdParty/fontawesome/IconsFontAwesome6.h"
#include "Utilities/FileHelper.hpp"
#include "Utilities/ImGui.hpp"
#include "Utilities/Localization.hpp"

namespace 
{
    constexpr float titlebarSize = 40;
    constexpr float titlebarControlSize = titlebarSize * 3;
    constexpr float iconSize = 64;
    constexpr float palateSize = 46;
    constexpr float buttonSize = 36;
    constexpr float buildBarWidth = 240;
    constexpr float sideBarWidth = 300;
    constexpr float shortcutSize = 10;

    constexpr int panelFlags =
    ImGuiWindowFlags_NoTitleBar |
    ImGuiWindowFlags_NoCollapse |
    ImGuiWindowFlags_NoMove |
    ImGuiWindowFlags_NoResize |
    ImGuiWindowFlags_NoSavedSettings;

    static ImGuiID MakeAnimId(const char* label)
    {
        return ImHashStr(label, std::strlen(label), 0);
    }
    
    // Helper to calculate visual rect
    static void GetZoomedRect(ImVec2 p_start, ImVec2 standardSize, float hoverFactor, float zoomScale, ImVec2& p_min, ImVec2& p_max)
    {
        ImVec2 zoomedSize = standardSize;
        zoomedSize.x *= (1.0f + hoverFactor * zoomScale);
        zoomedSize.y *= (1.0f + hoverFactor * zoomScale);
        ImVec2 offset = (zoomedSize - standardSize) * 0.5f;
        p_min = p_start - offset;
        p_max = p_min + zoomedSize;
    }

    static bool SelectButton(const char* label, const char* shortcut, bool selected, const char* tooltip)
    {
        ImGui::PushID(shortcut);
        ImGuiID id = ImGui::GetID("Anim");
        float dt = ImGui::GetIO().DeltaTime;
        ImGuiStyle& style = ImGui::GetStyle();

        ImGui::BeginGroup();
        
        ImVec2 p_start = ImGui::GetCursorScreenPos();
        ImVec2 standardSize(iconSize, iconSize);
        
        bool clicked = ImGui::InvisibleButton("##Hit", standardSize);
        bool hovered = ImGui::IsItemHovered();
        BUTTON_TOOLTIP( LOCTEXT(tooltip) )
        ImGui::PopID();
        
        float hoverFactor = iam_tween_float(id, 0, hovered ? 1.0f : 0.0f, 0.1f, {iam_ease_out_cubic}, iam_policy_crossfade, dt);
        float selectFactor = iam_tween_float(id, 1, selected ? 1.0f : 0.0f, 0.2f, {iam_ease_out_back}, iam_policy_crossfade, dt);

        ImVec2 p_min, p_max;
        GetZoomedRect(p_start, standardSize, hoverFactor, 0.05f, p_min, p_max);
        
        ImU32 bgCol = ImGui::GetColorU32(ImGuiCol_Button);
        if (selected || selectFactor > 0.01f)
        {
             ImVec4 col = ImVec4(0.4f, 0.6f, 0.8f, selectFactor);
             if (hoverFactor > 0.01f) {
                 col.x += 0.1f * hoverFactor;
                 col.y += 0.1f * hoverFactor;
                 col.z += 0.1f * hoverFactor;
             }
             bgCol = ImGui::ColorConvertFloat4ToU32(col);
        }
        else if (hovered)
        {
             bgCol = ImGui::GetColorU32(ImGuiCol_ButtonHovered);
        }
        
        ImGui::GetWindowDrawList()->AddRectFilled(p_min, p_max, bgCol, style.FrameRounding);
        
        ImVec2 textSize = ImGui::CalcTextSize(label);
        ImVec2 textPos = p_min + (p_max - p_min - textSize) * 0.5f;
        ImGui::GetWindowDrawList()->AddText(textPos, ImGui::GetColorU32(ImGuiCol_Text), label);
        
        ImVec2 cursor = Utilities::UI::TextCentered(shortcut, iconSize);
        ImGui::GetForegroundDrawList()->AddRect(cursor - ImVec2(shortcutSize, shortcutSize), cursor + ImVec2(shortcutSize, shortcutSize), IM_COL32(255, 255, 255, (int)(128 * (1.0f + hoverFactor))), 4.0f);
        
        ImGui::EndGroup();
        return clicked;
    }

    static bool MaterialButton(const FBasicBlock& block, ImTextureID texId, float windowWidth, bool selected)
    {
        ImGuiStyle& style = ImGui::GetStyle();
        ImGuiID id = ImGui::GetID((void*)(intptr_t)block.modelId_);
        float dt = ImGui::GetIO().DeltaTime;

        ImGui::BeginGroup();
        
        ImVec2 p_start = ImGui::GetCursorScreenPos();
        ImVec2 standardSize(palateSize, palateSize);
        
        ImGui::PushID(static_cast<int>(block.modelId_));
        bool clicked = ImGui::InvisibleButton("##Hit", standardSize);
        ImGui::PopID();
        
        bool hovered = ImGui::IsItemHovered();
        
        float hoverFactor = iam_tween_float(id, 0, hovered ? 1.0f : 0.0f, 0.1f, {iam_ease_out_cubic}, iam_policy_crossfade, dt);
        float selectFactor = iam_tween_float(id, 1, selected ? 1.0f : 0.0f, 0.2f, {iam_ease_out_back}, iam_policy_crossfade, dt);

        ImVec2 p_min, p_max;
        GetZoomedRect(p_start, standardSize, hoverFactor, 0.1f, p_min, p_max);
        
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        
        if (selected || selectFactor > 0.01f)
        {
            ImU32 borderCol = ImGui::ColorConvertFloat4ToU32(ImVec4(0.6f, 0.85f, 1.0f, selectFactor));
            drawList->AddRect(p_min, p_max, borderCol, 0.0f, 0, 4.0f * selectFactor);
        }

        if(texId != 0) drawList->AddImage(texId, p_min, p_max);
        
        Utilities::UI::TextCentered(block.name, palateSize);
        
        ImGui::EndGroup();
        
        float lastButtonX2 = ImGui::GetItemRectMax().x;
        float nextButtonX2 = lastButtonX2 + style.ItemSpacing.x + palateSize; 
        if (nextButtonX2 < windowWidth) ImGui::SameLine();
        
        return clicked;
    }
}

MagicaLegoUserInterface::MagicaLegoUserInterface(MagicaLegoGameInstance* gameInstance): gameInstance_(gameInstance)
{
    // set the uiStatus_ 1,2,3 bit to show the left, right, timeline
    DirectSetLayout(EULUT_TitleBar | EULUT_LayoutIndicator | EULUT_LeftBar | EULUT_RightBar);

    // Initialize command executor
    commandExecutor_ = std::make_unique<MagicaLego::FCommandExecutor>(gameInstance);

    // Initialize AI service
    aiService_ = std::make_unique<MagicaLego::FAIService>(gameInstance);
}

MagicaLegoUserInterface::~MagicaLegoUserInterface() = default;

void MagicaLegoUserInterface::OnInitUI()
{
    MagicaLego::Style::ApplyStyle();

    std::vector<uint8_t> tmpData;
    
    if (bigFont_ == nullptr)
    {
        ImFontGlyphRangesBuilder builder;
        builder.AddText("MagicaLego");
        const ImWchar* glyphRange = ImGui::GetIO().Fonts->GetGlyphRangesDefault();
        if (Utilities::Package::FPackageFileSystem::GetInstance().LoadFile("assets/fonts/Roboto-BoldCondensed.ttf", tmpData))
        {
            void* dataSrc = IM_ALLOC(tmpData.size());
            std::memcpy(dataSrc, tmpData.data(), tmpData.size());
            bigFont_ = ImGui::GetIO().Fonts->AddFontFromMemoryTTF(dataSrc, int(tmpData.size()), 72, nullptr, glyphRange);
        }
    }

    if (boldFont_ == nullptr)
    {
        ImFontGlyphRangesBuilder builder;
        const ImWchar* glyphRange = ImGui::GetIO().Fonts->GetGlyphRangesDefault();
        if (Utilities::Package::FPackageFileSystem::GetInstance().LoadFile("assets/fonts/Roboto-BoldCondensed.ttf", tmpData))
        {
            void* dataSrc = IM_ALLOC(tmpData.size());
            std::memcpy(dataSrc, tmpData.data(), tmpData.size());
            boldFont_ = ImGui::GetIO().Fonts->AddFontFromMemoryTTF(dataSrc, int(tmpData.size()), 20, nullptr, glyphRange);
        }
    }

    introStep_ = EIS_Entry;
    openingTimer_ = 1.0f;
}

void MagicaLegoUserInterface::OnSceneLoaded()
{
    if (!std::filesystem::exists(Utilities::FileHelper::GetPlatformFilePath("bin/record.txt")))
    {
        // write the record.txt
        std::ofstream recordFile(Utilities::FileHelper::GetPlatformFilePath("bin/record.txt"));
        // write timestamp
        recordFile << fmt::format("{}", std::time(nullptr));
        recordFile.close();

        // start intro
        GetGameInstance()->GetEngine().AddTimerTask(1.0, [this]() -> bool
        {
            introStep_ = static_cast<EIntroStep>(static_cast<int>(introStep_) + 1);
            openingTimer_ = 1.0f;

            if (introStep_ == EIS_Finish)
            {
                return true;
            }
            return false;
        });
    }
    else
    {
        openingTimer_ = 1.0f;
        introStep_ = EIS_Opening;
        GetGameInstance()->GetEngine().AddTimerTask(1.0, [this]() -> bool
        {
            introStep_ = EIS_InGame;
            return true;
        });
    }
}

void MagicaLegoUserInterface::DrawTitleBar()
{
    float dt = GetGameInstance()->GetEngine().GetDeltaSeconds();
    bool isOpen = (uiStatus_ & EULUT_TitleBar) != 0;
    float currentAnim = iam_tween_float(MakeAnimId("MagicaLego.TitleBarAlpha"), 0, isOpen ? 1.0f : 0.0f, 0.3f, {iam_ease_out_cubic}, iam_policy_crossfade, dt, 0.0f);

    if (currentAnim <= 0.0f && !isOpen) return;

    // 获取窗口的大小
    ImVec2 windowSize = ImGui::GetMainViewport()->Size;
    auto bgColor = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
    bgColor.w = currentAnim * 0.6f;
    
    float animOffset = (1.0f - currentAnim) * (titlebarSize + 10.0f);
    
    ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0, -animOffset), ImVec2(windowSize.x, titlebarSize - animOffset), ImGui::ColorConvertFloat4ToU32(bgColor));

    ImGui::PushFont(boldFont_);

    auto textSize = ImGui::CalcTextSize("MagicaLEGO");
    ImGui::GetForegroundDrawList()->AddText(ImVec2((windowSize.x - textSize.x) * 0.5f, (titlebarSize - textSize.y) * 0.5f - animOffset), IM_COL32(255, 255, 255, (int)(255 * currentAnim)), "MagicaLEGO");

    ImGui::PopFont();

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));

    ImGui::SetNextWindowPos(ImVec2(windowSize.x - titlebarControlSize, -animOffset), ImGuiCond_Always, ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(titlebarControlSize, titlebarSize));
    ImGui::SetNextWindowBgAlpha(0.0f);

    ImGui::Begin("TitleBarRight", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground);
    
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, currentAnim);

    if (ImGui::Button(ICON_FA_MINUS, ImVec2(titlebarSize, titlebarSize)))
    {
        GetGameInstance()->GetEngine().RequestMinimize();
    }
    ImGui::SameLine();
    if (ImGui::Button(GetGameInstance()->GetEngine().IsMaximumed() ? ICON_FA_WINDOW_RESTORE : ICON_FA_SQUARE, ImVec2(titlebarSize, titlebarSize)))
    {
        GetGameInstance()->GetEngine().ToggleMaximize();
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_XMARK, ImVec2(titlebarSize, titlebarSize)))
    {
        GetGameInstance()->GetEngine().RequestClose();
    }
    ImGui::PopStyleVar();
    ImGui::End();

    ImGui::SetNextWindowPos(ImVec2(0, -animOffset), ImGuiCond_Always, ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(titlebarSize * 18, titlebarSize));
    ImGui::SetNextWindowBgAlpha(0.0f);

    ImGui::Begin("TitleBarLeft", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground);
    
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, currentAnim);
    
    if (ImGui::Button(ICON_FA_GITHUB, ImVec2(titlebarSize, titlebarSize)))
    {
        NextRenderer::OSCommand("https://github.com/gameknife/gkNextRenderer");
    }
    BUTTON_TOOLTIP(LOCTEXT("Open Project Page in OS Browser"))
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_TWITTER, ImVec2(titlebarSize, titlebarSize)))
    {
        NextRenderer::OSCommand("https://x.com/gKNIFE_");
    }
    BUTTON_TOOLTIP(LOCTEXT("Open Twitter Page in OS Browser"))
    ImGui::SameLine();
    ImGui::GetForegroundDrawList()->AddLine(ImGui::GetCursorPos() + ImVec2(4, titlebarSize / 2 - 5), ImGui::GetCursorPos() + ImVec2(4, titlebarSize / 2 + 5), IM_COL32(255, 255, 255, 160), 2.0f);
    ImGui::Dummy(ImVec2(10, 10));
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_CAMERA, ImVec2(titlebarSize, titlebarSize)))
    {
        static int counter = 0;
        counter = 0;
        // hide and request screenshot then un-hide
        GetGameInstance()->GetEngine().AddTickedTask([this](double deltaSeconds)-> bool
        {
            counter++;
            switch (counter)
            {
            case 1:
                PushLayout(0x0);
                GetGameInstance()->GetEngine().GetUserSettings().TemporalFrames = 512;
                waiting_ = true;
                waitingText_ = "Rendering...";
                capture_ = true;
                GetGameInstance()->SetCapturing(true);
                return false;
            case 512:
                waiting_ = false;
                return false;
            case 513:
                {
                    std::string localPath = Utilities::FileHelper::GetPlatformFilePath("screenshots");
                    Utilities::FileHelper::EnsureDirectoryExists(Utilities::FileHelper::GetAbsolutePath(localPath));
                    auto time = std::time(nullptr);
                    std::string filename = fmt::format("shot_{:%Y-%m-%d-%H-%M-%S}", *std::localtime(&time));
                    GetGameInstance()->GetEngine().RequestScreenShot(localPath + "/" + filename);
                }
                return false;
            case 514:
                PopLayout();
                GetGameInstance()->GetEngine().GetUserSettings().TemporalFrames = 8;
                counter = 0;
                capture_ = false;
                GetGameInstance()->SetCapturing(false);
                ShowNotify("Screenshot captured, open in explorer?", []()-> void
                {
                    std::string localPath = Utilities::FileHelper::GetPlatformFilePath("screenshots");
                    auto absPath = Utilities::FileHelper::GetAbsolutePath(localPath);
                    Utilities::FileHelper::EnsureDirectoryExists(absPath);

                    NextRenderer::OSCommand(absPath.string().c_str());
                });
                return true;
            default:
                return false;
            }
        });
    }
    BUTTON_TOOLTIP(LOCTEXT("Take a Screenshot into the screenshots folder"))
    ImGui::SameLine();
    std::string recordAText = ICON_FA_VIDEO " ";
    recordAText += LOCTEXT("auto");
    if (ImGui::Button(recordAText.c_str(), ImVec2(titlebarSize * 2, titlebarSize)))
    {
        RecordTimeline(true);
    }
    BUTTON_TOOLTIP(LOCTEXT("Record build timeline video, auto rotate"))
    ImGui::SameLine();
    std::string recordMText = ICON_FA_VIDEO " ";
    recordMText += LOCTEXT("manual");
    if (ImGui::Button(recordMText.c_str(), ImVec2(titlebarSize * 2, titlebarSize)))
    {
        RecordTimeline(false);
    }
    BUTTON_TOOLTIP(LOCTEXT("Record build timeline video, manual rotate"))
    ImGui::SameLine();
    ImGui::GetForegroundDrawList()->AddLine(ImGui::GetCursorPos() + ImVec2(4, titlebarSize / 2 - 5), ImGui::GetCursorPos() + ImVec2(4, titlebarSize / 2 + 5), IM_COL32(255, 255, 255, 160), 2.0f);
    ImGui::Dummy(ImVec2(10, 10));
    ImGui::SameLine();
    bool paused = GetGameInstance()->IsBGMPaused();
    if (ImGui::Button(paused ? ICON_FA_VOLUME_LOW : ICON_FA_VOLUME_XMARK, ImVec2(titlebarSize, titlebarSize)))
    {
        paused ? GetGameInstance()->PauseBGM(false) : GetGameInstance()->PauseBGM(true);
    }
    BUTTON_TOOLTIP(LOCTEXT("Toggle BGM"))
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_SHUFFLE, ImVec2(titlebarSize, titlebarSize)))
    {
        GetGameInstance()->PlayNextBGM();
    }
    BUTTON_TOOLTIP(LOCTEXT("Shuffle BGM"))
    ImGui::SameLine();
    ImGui::GetForegroundDrawList()->AddLine(ImGui::GetCursorPos() + ImVec2(4, titlebarSize / 2 - 5), ImGui::GetCursorPos() + ImVec2(4, titlebarSize / 2 + 5), IM_COL32(255, 255, 255, 160), 2.0f);
    ImGui::Dummy(ImVec2(10, 10));
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_QUESTION, ImVec2(titlebarSize, titlebarSize)))
    {
        showHelp_ = !showHelp_;
        GetGameInstance()->SetCapturing(showHelp_ || capture_);
    }
    BUTTON_TOOLTIP(LOCTEXT("Show Help"))

    ImGui::PopStyleVar();
    ImGui::End();

    ImGui::PopStyleColor();
    ImGui::PopStyleVar(4);
}

void MagicaLegoUserInterface::DrawOpening() const 
{
    if (introStep_ >= EIS_GuideBuild)
    {
        return;
    }
    // 获取窗口的大小
    ImVec2 windowSize = ImGui::GetMainViewport()->Size;

    // 设置背景颜色
    ImVec4 bgColor = ImVec4(0.1f, 0.1f, 0.1f, openingTimer_);
    ImVec4 fgColor = ImVec4(1.0f, 1.0f, 1.0f, openingTimer_);
    ImGui::GetForegroundDrawList()->AddRectFilled(ImVec2(0, 0), windowSize, ImGui::ColorConvertFloat4ToU32(bgColor));

    ImGui::PushFont(bigFont_);

    // 设置文字格式
    auto textSize = ImGui::CalcTextSize("for my son's MAGICALEGO");
    auto textPos = windowSize * 0.5f - textSize * 0.5f;
    ImGui::GetForegroundDrawList()->AddText(ImGui::GetFont(), ImGui::GetFontSize(), textPos, ImGui::ColorConvertFloat4ToU32(fgColor), "for my son's MAGICALEGO");

    ImGui::PopFont();
}

void MagicaLegoUserInterface::OnRenderUI()
{
    iam_update_begin_frame();
    // static int frameCount = 0;
    // if (++frameCount % 600 == 0) iam_gc();

    if (uiStatus_ & EULUT_TitleBar)
    {
        DrawTitleBar();
    }
    // TotalSwitch
    DrawMainToolBar();

    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0);
    
    DrawLeftBar();
    DrawRightBar();
    
    ImGui::PopStyleVar(1);

    DrawTimeline();
    
    switch (introStep_)
    {
    case EIS_Entry:
        DrawOpening();
        break;
    case EIS_Opening:
        openingTimer_ = openingTimer_ - GetGameInstance()->GetEngine().GetDeltaSeconds();
        DrawOpening();
        break;
    case EIS_GuideBuild:
        {
            openingTimer_ = openingTimer_ - GetGameInstance()->GetEngine().GetDeltaSeconds();
            auto screenSize = ImGui::GetMainViewport()->Size;
            auto lerpedPos = glm::mix(glm::vec2(screenSize.x * 0.5, screenSize.y * 0.5), glm::vec2(screenSize.x * 0.6, screenSize.y * 0.75), openingTimer_);
           // glfwSetCursorPos(GetGameInstance()->GetEngine().GetRenderer().Window().Handle(), lerpedPos.x, lerpedPos.y);
        }
        break;
    case EIS_Finish:
        GetGameInstance()->PlaceDynamicBlock({glm::i16vec3(0, 0, 0), EOrientation::EO_North, 0, 12, 0, 0});
        introStep_ = EIS_InGame;
        break;
    case EIS_InGame:
        // Draw Nothing
        break;
    }

    if (waiting_)
    {
        DrawWaiting();
    }

    if (capture_)
    {
        DrawWatermark();
    }
    else
    {
        DrawHUD();
    }

    if (showHelp_)
    {
        DrawHelp();
    }

    if (notify_)
    {
        ImGui::OpenPopup("Notify");
        notify_ = false;
    }

    DrawNotify();

    DrawVersionWatermark();
}

void MagicaLegoUserInterface::DrawWaiting()
{
    ImGui::OpenPopup("Waiting");
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Waiting", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings))
    {
        ImGui::TextUnformatted(waitingText_.c_str());
        ImGui::EndPopup();
    }
}

void MagicaLegoUserInterface::DrawNotify()
{
    float dt = GetGameInstance()->GetEngine().GetDeltaSeconds();
    bool isOpen = ImGui::IsPopupOpen("Notify");
    float currentAnim = iam_tween_float(MakeAnimId("MagicaLego.NotifyAnim"), 0, isOpen ? 1.0f : 0.0f, 0.4f, {iam_ease_out_back, 1.5f}, iam_policy_crossfade, dt, 0.0f);

    if (currentAnim <= 0.0f && !isOpen) return;

    const ImVec2 viewportSize = ImGui::GetMainViewport()->Size;
    float offsetY = (1.0f - currentAnim) * 150.0f;
    
    ImGui::SetNextWindowPos(viewportSize - ImVec2(20, 20 + offsetY), ImGuiCond_Always, ImVec2(1.f, 1.f));
    ImGui::SetNextWindowBgAlpha(currentAnim * 0.9f);

    if (ImGui::BeginPopup("Notify", ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings))
    {
        ImGui::TextUnformatted(notifyText_.c_str());
        ImGui::Separator();
        if (ImGui::Button("Ok")) { ImGui::CloseCurrentPopup(); }
        ImGui::SameLine();
        if (ImGui::Button("Open"))
        {
            ImGui::CloseCurrentPopup();
            notifyCallback_();
        }
        ImGui::EndPopup();
    }
}

void MagicaLegoUserInterface::DrawWatermark()
{
    const ImVec2 viewportSize = ImGui::GetMainViewport()->Size;
    ImGui::SetNextWindowPos(ImVec2(30, viewportSize.y - 30), ImGuiCond_Always, ImVec2(0, 1));
    ImGui::SetNextWindowBgAlpha(0.0f);

    ImGui::Begin("Watermark", nullptr, panelFlags | ImGuiWindowFlags_NoBackground);
    ImGui::PushFont(bigFont_);
    ImGui::TextUnformatted("MagicaLEGO");
    ImGui::PopFont();
    ImGui::Text("%s %s", ICON_FA_GITHUB, "https://github.com/gameknife/gkNextEngine");
    ImGui::End();
}

void MagicaLegoUserInterface::DrawVersionWatermark()
{
    const ImVec2 viewportSize = ImGui::GetMainViewport()->Size;
    ImGui::SetNextWindowPos(ImVec2(viewportSize.x / 2, viewportSize.y), ImGuiCond_Always, ImVec2(0.5, 1));
    ImGui::SetNextWindowBgAlpha(0.0f);

    ImGui::Begin("VersionWatermark", nullptr, panelFlags | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoBackground);
    //auto Size = ImGui::CalcTextSize(NextRenderer::GetBuildVersion().c_str());
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 0.2f));
    ImGui::TextUnformatted(NextRenderer::GetBuildVersion().c_str());
    ImGui::PopStyleColor();
    ImGui::End();
}

void MagicaLegoUserInterface::DrawHUD()
{
    const ImVec2 viewportSize = ImGui::GetMainViewport()->Size;
    ImGui::SetNextWindowPos(ImVec2(buildBarWidth + 30, viewportSize.y - 30), ImGuiCond_Always, ImVec2(0, 1));
    ImGui::SetNextWindowSize(ImVec2(0,0));
    ImGui::SetNextWindowBgAlpha(0.0f);

    ImGui::Begin("HUD", nullptr, panelFlags | ImGuiWindowFlags_NoBackground);
    ImGui::PushFont(bigFont_);
    auto time = std::time(nullptr);
    ImGui::TextUnformatted(fmt::format("{:%H:%M}", *std::localtime(&time)).c_str());
    ImGui::PopFont();
    ImGui::Text("%s %d", ICON_FA_SHOE_PRINTS, GetGameInstance()->GetMaxStep());
    if (!GetGameInstance()->IsBGMPaused())
    {
        ImGui::Text("%s %s", ICON_FA_MUSIC, GetGameInstance()->GetCurrentBGMName().c_str());
    }

    ImGui::End();
}

void MagicaLegoUserInterface::DrawHelp()
{
    // forbid other input
    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always, ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImGui::GetMainViewport()->Size);
    ImGui::SetNextWindowBgAlpha(0.0f);
    ImGui::Begin("Help", nullptr, panelFlags | ImGuiWindowFlags_NoBackground);

    ImVec2 windowSize = ImGui::GetMainViewport()->Size;

    ImGui::GetForegroundDrawList()->AddRectFilled(ImVec2(0, 0), windowSize, IM_COL32(80, 80, 80, 200));

    {
        auto textSize = ImGui::CalcTextSize(LOCTEXT("LMB: Build/Rotate | RMB: Pan | Wheel: Zoom"));
        auto textPos = windowSize * 0.5f - textSize * 0.5f;
        ImGui::GetForegroundDrawList()->AddText(ImGui::GetFont(), ImGui::GetFontSize(), textPos, IM_COL32(255, 255, 255, 255), LOCTEXT("LMB: Build/Rotate | RMB: Pan | Wheel: Zoom"));
    }

    {
        ImGui::GetForegroundDrawList()->AddLine(ImVec2(buildBarWidth, 180), ImVec2(100, 60), IM_COL32(255, 255, 255, 255));
        ImGui::GetForegroundDrawList()->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(buildBarWidth, 200), IM_COL32(255, 255, 255, 255), LOCTEXT("Function Bar, hover to get help."));
    }


    {
        auto textSize = ImGui::CalcTextSize(LOCTEXT("Layout Bar, toggle Left/Right/Bottom Panel."));
        ImGui::GetForegroundDrawList()->AddLine(ImVec2(windowSize.x * 0.5f, 180), ImVec2(windowSize.x * 0.5f, 80), IM_COL32(255, 255, 255, 255));
        ImGui::GetForegroundDrawList()->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2((windowSize.x - textSize.x) * 0.5f, 200), IM_COL32(255, 255, 255, 255),
                                                LOCTEXT("Layout Bar, toggle Left/Right/Bottom Panel."));
    }

    {
        ImGui::GetForegroundDrawList()->AddLine(ImVec2(buildBarWidth + 40, windowSize.y * 0.6f), ImVec2(buildBarWidth, windowSize.y * 0.5f), IM_COL32(255, 255, 255, 255));
        ImGui::GetForegroundDrawList()->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(buildBarWidth + 50, windowSize.y * 0.6f), IM_COL32(255, 255, 255, 255),
                                                LOCTEXT("Build Bar, switch build / camera / base mode. Handle file save."));
    }

    {
        auto textSize = ImGui::CalcTextSize(LOCTEXT("Brush Bar, switch Block, select Color to build."));
        ImGui::GetForegroundDrawList()->AddLine(ImVec2(windowSize.x - sideBarWidth, windowSize.y * 0.5f), ImVec2(windowSize.x - sideBarWidth - 40, windowSize.y * 0.6f), IM_COL32(255, 255, 255, 255));
        ImGui::GetForegroundDrawList()->AddText(ImGui::GetFont(), ImGui::GetFontSize(),
                                                ImVec2(windowSize.x - sideBarWidth - textSize.x - 50, windowSize.y * 0.6f), IM_COL32(255, 255, 255, 255),
                                                LOCTEXT("Brush Bar, switch Block, select Color to build."));
    }
    // if hit anywhere, close the help
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right))
    {
        showHelp_ = false;
        GetGameInstance()->SetCapturing(showHelp_ || capture_);
    }

    ImGui::End();
}

void MagicaLegoUserInterface::RecordTimeline(bool autoRotate)
{
    auto maxStep = GetGameInstance()->GetMaxStep(); // add 5 step to stop the final still
    // round to latest 360 degree
    maxStep = maxStep + (autoRotate ? 360 : 30);
    std::string localPath = Utilities::FileHelper::GetPlatformFilePath("captures");
    std::string localTempPath = Utilities::FileHelper::GetPlatformFilePath("temps");
    Utilities::FileHelper::EnsureDirectoryExists(Utilities::FileHelper::GetAbsolutePath(localPath));
    Utilities::FileHelper::EnsureDirectoryExists(Utilities::FileHelper::GetAbsolutePath(localTempPath));
    auto time = std::time(nullptr);
    std::string filename = fmt::format("{}/magicalLego_{:%Y-%m-%d-%H-%M-%S}", localPath, *std::localtime(&time));
    PushLayout(0x0);
    capture_ = true;
    GetGameInstance()->SetCapturing(true);

    static int count = 0;
    count = 0;
    GetGameInstance()->GetEngine().AddTickedTask([this, maxStep, localTempPath, filename, autoRotate](double deltaSeconds)-> bool
    {
        int framePerStep = 4;
        count++;

        if (count > maxStep * framePerStep)
        {
            GetGameInstance()->SetCapturing(false);
            capture_ = false;
            waiting_ = false;
            PopLayout();
            // sleep os for a while
            int result = NextRenderer::OSProcess(fmt::format("ffmpeg -framerate 30 -i {}/video_%d.jpg -c:v libx264 -pix_fmt yuv420p {}.mp4", localTempPath, filename).c_str());
            if (result != 0)
            {
                SPDLOG_ERROR("ffmpeg process failed with exit code {}", result);
            }

            // delete all *.jpg using std::filesystem
            std::filesystem::remove_all(localTempPath);

            ShowNotify("Video recorded, open in explorer?", []()-> void
            {
                std::string localPath = Utilities::FileHelper::GetPlatformFilePath("captures");
                auto absPath = Utilities::FileHelper::GetAbsolutePath(localPath);
                Utilities::FileHelper::EnsureDirectoryExists(absPath);
                NextRenderer::OSCommand(absPath.string().c_str());
            });

            return true;
        }
        if (count > maxStep * framePerStep - framePerStep + 1)
        {
            waiting_ = true;
            waitingText_ = "Generating Video...";
            return false;
        }
        int step = count / framePerStep;
        GetGameInstance()->SetPlayStep(step);

        if (count % framePerStep == 0)
        {
            if (autoRotate)
            {
                GetGameInstance()->GetCameraRotX() += 1.0f;
            }
            std::string stepfilename = fmt::format("video_{}", step);
            GetGameInstance()->GetEngine().RequestScreenShot(localTempPath + "/" + stepfilename);
        }

        return false;
    });
}

void MagicaLegoUserInterface::ShowNotify(const std::string& text, std::function<void()> callback)
{
    notify_ = true;
    notifyText_ = text;
    notifyCallback_ = std::move(callback);
}

void MagicaLegoUserInterface::DirectSetLayout(uint32_t layout)
{
    uiStatus_ = layout;
    uiStatusStack_.clear();
    uiStatusStack_.push_back(layout);
}

void MagicaLegoUserInterface::PushLayout(uint32_t layout)
{
    uiStatusStack_.push_back(layout);
    uiStatus_ = layout;
}

void MagicaLegoUserInterface::PopLayout()
{
    uiStatusStack_.pop_back();
    uiStatus_ = uiStatusStack_.back();
}

void MagicaLegoUserInterface::DrawMainToolBar()
{
    float dt = GetGameInstance()->GetEngine().GetDeltaSeconds();
    bool isOpen = (uiStatus_ & EULUT_LayoutIndicator) != 0;
    float targetAlpha = isOpen ? 1.0f : 0.0f;
    float currentAlpha = iam_tween_float(MakeAnimId("MagicaLego.MainToolBarAlpha"), 0, targetAlpha, 0.25f, {iam_ease_out_cubic}, iam_policy_crossfade, dt, 0.0f);

    if (currentAlpha <= 0.0f && !isOpen) return;

    const float padding = 20.0f;
    const ImVec2 viewportSize = ImGui::GetMainViewport()->Size;

    const float animOffset = (1.0f - currentAlpha) * (titlebarSize + padding + 50.0f);
    const ImVec2 pos = ImVec2(viewportSize.x * 0.5f, titlebarSize + padding - animOffset);
    constexpr ImVec2 posPivot = ImVec2(0.5f, 0.0f);

    ImGui::SetNextWindowPos(pos, ImGuiCond_Always, posPivot);
    ImGui::SetNextWindowSize(ImVec2(0, 0));
    ImGui::SetNextWindowBgAlpha(currentAlpha * 0.6f);

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    if (ImGui::Begin("MainToolBar", nullptr, panelFlags))
    {
        if (ImGui::Button(ICON_FA_SQUARE_CARET_LEFT, ImVec2(buttonSize, buttonSize)))
        {
            DirectSetLayout(uiStatus_ ^ EULUT_LeftBar);
        }
        BUTTON_TOOLTIP("Open Build Window")
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_SQUARE_CARET_DOWN, ImVec2(buttonSize, buttonSize)))
        {
            DirectSetLayout(uiStatus_ ^ EULUT_Timeline);
        }
        BUTTON_TOOLTIP("Open Timeline Window")
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_SQUARE_CARET_RIGHT, ImVec2(buttonSize, buttonSize)))
        {
            DirectSetLayout(uiStatus_ ^ EULUT_RightBar);
        }
        BUTTON_TOOLTIP("Open Brush Window")
    }
    ImGui::PopStyleColor();

    ImGui::End();
}

void MagicaLegoUserInterface::DrawLeftBar()
{
    float dt = GetGameInstance()->GetEngine().GetDeltaSeconds();
    bool isOpen = (uiStatus_ & EULUT_LeftBar) != 0;
    float targetAlpha = isOpen ? 1.0f : 0.0f;
    float currentAlpha = iam_tween_float(MakeAnimId("MagicaLego.LeftBarAlpha"), 0, targetAlpha, 0.3f, {iam_ease_out_cubic}, iam_policy_crossfade, dt, 0.0f);

    if (currentAlpha <= 0.0f && !isOpen) return;

    const float padding = 20.0f;
    const ImVec2 viewportSize = ImGui::GetMainViewport()->Size;
    
    const float animOffset = (1.0f - currentAlpha) * (buildBarWidth + padding + 50.0f);
    const ImVec2 pos = ImVec2(padding - animOffset, titlebarSize + padding);
    constexpr ImVec2 posPivot = ImVec2(0.0f, 0.0f);
    
    ImGui::SetNextWindowPos(pos, ImGuiCond_Always, posPivot);
    ImGui::SetNextWindowSize(ImVec2(buildBarWidth, viewportSize.y - titlebarSize - padding * 2));
    ImGui::SetNextWindowBgAlpha(currentAlpha * 0.6f);
    
    if (ImGui::Begin("Place & Dig", nullptr, panelFlags))
    {
        ImGui::SeparatorText(LOCTEXT("Mode"));
        if (SelectButton(ICON_FA_PERSON_DIGGING, "Q", GetGameInstance()->GetBuildMode() == ELegoMode::ELM_Dig, "Dig a block"))
        {
            GetGameInstance()->SetBuildMode(ELegoMode::ELM_Dig);
        }
        ImGui::SameLine();
        if (SelectButton(ICON_FA_CUBES_STACKED, "W", GetGameInstance()->GetBuildMode() == ELegoMode::ELM_Place, "Place a block with selected brush"))
        {
            GetGameInstance()->SetBuildMode(ELegoMode::ELM_Place);
        }
        ImGui::SameLine();
        if (SelectButton(ICON_FA_HAND_POINTER, "E", GetGameInstance()->GetBuildMode() == ELegoMode::ELM_Select, "Select a block to modify"))
        {
            GetGameInstance()->SetBuildMode(ELegoMode::ELM_Select);
        }
        ImGui::SeparatorText(LOCTEXT("Base"));
        if (SelectButton(ICON_FA_L, "1", GetGameInstance()->GetCurrentBasePlane() == EBasePlane::EBP_Big, "Switch base plane big"))
        {
            GetGameInstance()->SwitchBasePlane(EBasePlane::EBP_Big);
        }
        ImGui::SameLine();
        if (SelectButton(ICON_FA_M, "2", GetGameInstance()->GetCurrentBasePlane() == EBasePlane::EBP_Mid, "Switch base plane middle"))
        {
            GetGameInstance()->SwitchBasePlane(EBasePlane::EBP_Mid);
        }
        ImGui::SameLine();
        if (SelectButton(ICON_FA_S, "3", GetGameInstance()->GetCurrentBasePlane() == EBasePlane::EBP_Small, "Switch base plane small"))
        {
            GetGameInstance()->SwitchBasePlane(EBasePlane::EBP_Small);
        }
        ImGui::SeparatorText(LOCTEXT("Orientation"));
        if (SelectButton(fmt::format("{}", GetGameInstance()->GetCurrentOrientation()).c_str(), "R", false, "Rotate current block"))
        {
            GetGameInstance()->ChangeOrientation();
        }

        ImGui::Dummy(ImVec2(0, 20));
        ImGui::SeparatorText(LOCTEXT("Light"));

        ImGui::Checkbox(LOCTEXT("Sun"), &GetGameInstance()->GetEngine().GetScene().GetEnvSettings().HasSun);
        if (GetGameInstance()->GetEngine().GetScene().GetEnvSettings().HasSun)
        {
            ImGui::SliderFloat(LOCTEXT("Dir"), &GetGameInstance()->GetEngine().GetScene().GetEnvSettings().SunRotation, 0, 2);
        }
        ImGui::SliderInt(LOCTEXT("Sky"), &GetGameInstance()->GetEngine().GetScene().GetEnvSettings().SkyIdx, 0, 10);
        ImGui::SliderFloat(LOCTEXT("Lum"), &GetGameInstance()->GetEngine().GetScene().GetEnvSettings().SkyIntensity, 0, 100);

        ImGui::Dummy(ImVec2(0, 20));

        ImGui::SeparatorText(LOCTEXT("Files"));
        static std::string selectedFilename = "";
        static std::string newFilename = "magicalego";
        if (ImGui::BeginListBox("##listbox 2", ImVec2(-FLT_MIN, 8 * ImGui::GetTextLineHeightWithSpacing())))
        {
            std::string path = Utilities::FileHelper::GetPlatformFilePath("assets/legos/");
            Utilities::FileHelper::EnsureDirectoryExists(path);
            for (const auto& entry : std::filesystem::directory_iterator(path))
            {
                if (entry.path().extension() != ".mls")
                {
                    continue;
                }
                std::string filename = entry.path().filename().string();
                filename = filename.substr(0, filename.size() - 4);

                bool isSelected = (selectedFilename == filename);
                ImGuiSelectableFlags flags = (selectedFilename == filename) ? ImGuiSelectableFlags_Highlight : 0;
                if (ImGui::Selectable(filename.c_str(), isSelected, flags))
                {
                    selectedFilename = filename;
                    newFilename = filename;
                }
                if (isSelected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndListBox();
        }

        if (ImGui::Button(ICON_FA_FILE, ImVec2(buttonSize, buttonSize)))
        {
            GetGameInstance()->CleanUp();
        }
        BUTTON_TOOLTIP(LOCTEXT("New scene"))
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_FOLDER_OPEN, ImVec2(buttonSize, buttonSize)))
        {
            GetGameInstance()->LoadRecord(selectedFilename);
        }
        BUTTON_TOOLTIP(LOCTEXT("Open select scene"))
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_FLOPPY_DISK, ImVec2(buttonSize, buttonSize)))
        {
            if (selectedFilename != "") GetGameInstance()->SaveRecord(selectedFilename);
        }
        BUTTON_TOOLTIP(LOCTEXT("Save current scene"))
        ImGui::SameLine();
        // 模态新保存
        if (ImGui::Button(ICON_FA_DOWNLOAD, ImVec2(buttonSize, buttonSize)))
            ImGui::OpenPopup(ICON_FA_DOWNLOAD);
        BUTTON_TOOLTIP(LOCTEXT("Save as..."))

        ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

        if (ImGui::BeginPopupModal(ICON_FA_DOWNLOAD, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("Input new filename here.\nDO NOT need extension.");
            ImGui::Separator();

            ImGui::InputText("##newscenename", &newFilename);

            if (ImGui::Button("Save", ImVec2(120, 0)))
            {
                GetGameInstance()->SaveRecord(newFilename);
                ImGui::CloseCurrentPopup();
            }
            ImGui::SetItemDefaultFocus();
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0)))
            {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        DrawAISection();
        DrawConsole();
    }
    ImGui::End();

    DrawScriptLoadPopup();
}

void MagicaLegoUserInterface::DrawRightBar()
{
    float dt = GetGameInstance()->GetEngine().GetDeltaSeconds();
    bool isOpen = (uiStatus_ & EULUT_RightBar) != 0;
    float targetAlpha = isOpen ? 1.0f : 0.0f;
    float currentAlpha = iam_tween_float(MakeAnimId("MagicaLego.RightBarAlpha"), 0, targetAlpha, 0.3f, {iam_ease_out_cubic}, iam_policy_crossfade, dt, 0.0f);

    if (currentAlpha <= 0.0f && !isOpen) return;

    const float padding = 20.0f;
    const ImVec2 viewportSize = ImGui::GetMainViewport()->Size;
    
    const float animOffset = (1.0f - currentAlpha) * (sideBarWidth + padding + 50.0f);
    const ImVec2 pos = ImVec2(viewportSize.x - padding + animOffset, titlebarSize + padding);
    constexpr ImVec2 posPivot = ImVec2(1.0f, 0.0f);

    ImGui::SetNextWindowPos(pos, ImGuiCond_Always, posPivot);
    ImGui::SetNextWindowSize(ImVec2(sideBarWidth, viewportSize.y - titlebarSize - padding * 2));
    ImGui::SetNextWindowBgAlpha(currentAlpha * 0.6f);

    if (ImGui::Begin("Color Pallete", nullptr, panelFlags))
    {
        std::vector<const char*> types;
        static int currentType = 0;

        auto& basicNodeLibrary = GetGameInstance()->GetBasicNodeLibrary();
        if (basicNodeLibrary.size() > 0)
        {
            for (auto& type : basicNodeLibrary)
            {
                types.push_back(type.first.c_str());
            }

            ImGui::SeparatorText(LOCTEXT("Blocks"));
            ImGui::Dummy(ImVec2(0, 5));

            ImGui::SetNextItemWidth(sideBarWidth - 46);
            if (ImGui::Combo("##Type", &currentType, types.data(), static_cast<int>(types.size())))
            {
                if (auto newIdx = GetGameInstance()->ConvertBrushIdxToNextType(types[currentType], GetGameInstance()->GetCurrentBrushIdx()))
                {
                    GetGameInstance()->SetCurrentBrushIdx(newIdx);
                }
            }
            ImGui::Dummy(ImVec2(0, 5));

            float windowWidth = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
            auto& basicBlocks = basicNodeLibrary[types[currentType]];

            for (auto& block : basicBlocks)
            {
                std::string filename = fmt::format("assets/textures/thumb/thumb_{}_{}.jpg", block.type, block.name);
                ImTextureID id = (ImTextureID)(intptr_t)GetGameInstance()->GetEngine().GetUserInterface()->RequestImTextureByName(filename);
                if (MaterialButton(block, id, windowWidth, GetGameInstance()->GetCurrentBrushIdx() == block.brushId_))
                {
                    GetGameInstance()->SetCurrentBrushIdx(block.brushId_);
                    GetGameInstance()->TryChangeSelectionBrushIdx(block.brushId_);
                }
            }
        }
    }
    ImGui::End();
}

void MagicaLegoUserInterface::DrawTimeline()
{
    float dt = GetGameInstance()->GetEngine().GetDeltaSeconds();
    bool isOpen = (uiStatus_ & EULUT_Timeline) != 0;
    float targetAlpha = isOpen ? 1.0f : 0.0f;
    float currentAlpha = iam_tween_float(MakeAnimId("MagicaLego.TimelineAlpha"), 0, targetAlpha, 0.3f, {iam_ease_out_cubic}, iam_policy_crossfade, dt, 0.0f);

    if (currentAlpha <= 0.0f && !isOpen) return;

    const float padding = 20.0f;
    const ImVec2 viewportSize = ImGui::GetMainViewport()->Size;
    
    const float animOffset = (1.0f - currentAlpha) * (150.0f);
    const ImVec2 pos = ImVec2(viewportSize.x * 0.5f, viewportSize.y - padding + animOffset);
    constexpr ImVec2 posPivot = ImVec2(0.5f, 1.0f);
    const float width = viewportSize.x - (sideBarWidth + padding) * 2 - padding * 2;
    ImGuiStyle& style = ImGui::GetStyle();

    ImGui::SetNextWindowPos(pos, ImGuiCond_Always, posPivot);
    ImGui::SetNextWindowSize(ImVec2(width, 90));
    ImGui::SetNextWindowBgAlpha(currentAlpha * 0.6f);

    if (ImGui::Begin("Timeline", nullptr, panelFlags))
    {
        ImGui::PushItemWidth(width - 20);
        int step = GetGameInstance()->GetCurrentStep();
        if (ImGui::SliderInt("##Timeline", &step, 0, GetGameInstance()->GetMaxStep()))
        {
            GetGameInstance()->SetPlayStep(step);
        }
        ImGui::PopItemWidth();

        ImVec2 nextLinePos = ImGui::GetCursorPos();
        ImGui::SetCursorPos(nextLinePos + ImVec2(width * 0.5f - (style.ItemSpacing.x * 4 + buttonSize * 3) * 0.5f, 0));
        ImGui::BeginGroup();
        if (ImGui::Button(ICON_FA_BACKWARD_STEP, ImVec2(buttonSize, buttonSize)))
        {
            GetGameInstance()->SetPlayStep(step - 1);
        }
        ImGui::SameLine();
        if (ImGui::Button(GetGameInstance()->IsPlayReview() ? ICON_FA_PAUSE : ICON_FA_PLAY, ImVec2(buttonSize, buttonSize)))
        {
            GetGameInstance()->SetPlayReview(!GetGameInstance()->IsPlayReview());
        }
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_FORWARD_STEP, ImVec2(buttonSize, buttonSize)))
        {
            GetGameInstance()->SetPlayStep(step + 1);
        }
        ImGui::EndGroup();

        ImGui::SetCursorPos(nextLinePos + ImVec2(width - (style.ItemSpacing.x * 4 + buttonSize * 3) * 0.5f, 0));
        ImGui::BeginGroup();
        if (ImGui::Button(ICON_FA_CLOCK_ROTATE_LEFT, ImVec2(buttonSize, buttonSize)))
        {
            GetGameInstance()->DumpReplayStep(step);
        }
        ImGui::EndGroup();
    }
    ImGui::End();
}

void MagicaLegoUserInterface::DrawConsole()
{
    ImGui::Dummy(ImVec2(0, 10));
    ImGui::SeparatorText(LOCTEXT("Console"));

    // Output area
    float outputHeight = 6 * ImGui::GetTextLineHeightWithSpacing();
    if (ImGui::BeginChild("ConsoleOutput", ImVec2(-FLT_MIN, outputHeight), ImGuiChildFlags_Borders))
    {
        for (const auto& line : consoleOutput_)
        {
            // Color code based on prefix
            if (line.find("[OK]") == 0)
            {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 1.0f, 0.4f, 1.0f));
            }
            else if (line.find("[FAIL]") == 0 || line.find("Error") != std::string::npos)
            {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
            }
            else if (line.find("[SKIP]") == 0)
            {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 0.4f, 1.0f));
            }
            else if (line.find(">") == 0)
            {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.8f, 1.0f, 1.0f));
            }
            else
            {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.9f, 0.9f, 1.0f));
            }

            ImGui::TextWrapped("%s", line.c_str());
            ImGui::PopStyleColor();
        }

        if (scrollToBottom_)
        {
            ImGui::SetScrollHereY(1.0f);
            scrollToBottom_ = false;
        }
    }
    ImGui::EndChild();

    // Input line
    ImGui::PushItemWidth(buildBarWidth - buttonSize - 16);

    ImGuiInputTextFlags inputFlags = ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackHistory;

    bool executeCommand = false;
    if (ImGui::InputText("##ConsoleInput", &consoleInput_, inputFlags,
        [](ImGuiInputTextCallbackData* data) -> int
        {
            auto* ui = static_cast<MagicaLegoUserInterface*>(data->UserData);
            if (data->EventFlag == ImGuiInputTextFlags_CallbackHistory)
            {
                std::string historyCmd;
                if (data->EventKey == ImGuiKey_UpArrow)
                {
                    historyCmd = ui->commandExecutor_->GetHistoryPrev();
                }
                else if (data->EventKey == ImGuiKey_DownArrow)
                {
                    historyCmd = ui->commandExecutor_->GetHistoryNext();
                }

                if (!historyCmd.empty())
                {
                    data->DeleteChars(0, data->BufTextLen);
                    data->InsertChars(0, historyCmd.c_str());
                }
            }
            return 0;
        }, this))
    {
        executeCommand = true;
    }
    ImGui::PopItemWidth();

    ImGui::SameLine();

    if (ImGui::Button(ICON_FA_PLAY, ImVec2(buttonSize, 0)) || executeCommand)
    {
        if (!consoleInput_.empty())
        {
            consoleOutput_.push_back("> " + consoleInput_);

            auto result = commandExecutor_->ExecuteCommand(consoleInput_);

            if (!result.message.empty())
            {
                consoleOutput_.push_back(result.message);
            }

            for (const auto& line : result.output)
            {
                consoleOutput_.push_back("  " + line);
            }

            // Keep output limited to last 100 lines
            while (consoleOutput_.size() > 100)
            {
                consoleOutput_.erase(consoleOutput_.begin());
            }

            consoleInput_.clear();
            scrollToBottom_ = true;
        }
    }
    BUTTON_TOOLTIP(LOCTEXT("Execute command"))

    // Script load button
    std::string loadScriptLabel = std::string(ICON_FA_FILE_CODE) + " " + LOCTEXT("Load Script");
    if (ImGui::Button(loadScriptLabel.c_str(), ImVec2(-FLT_MIN, 0)))
    {
        showScriptPopup_ = true;
    }
    BUTTON_TOOLTIP(LOCTEXT("Load and execute a .mlscript file"))
}

void MagicaLegoUserInterface::DrawScriptLoadPopup()
{
    if (showScriptPopup_)
    {
        ImGui::OpenPopup("LoadScript");
        showScriptPopup_ = false;
    }

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    if (ImGui::BeginPopupModal("LoadScript", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text(LOCTEXT("Select a script file to execute:"));
        ImGui::Separator();

        // List script files
        std::string scriptsPath = Utilities::FileHelper::GetPlatformFilePath("assets/scripts/");
        Utilities::FileHelper::EnsureDirectoryExists(scriptsPath);

        if (ImGui::BeginListBox("##scriptlist", ImVec2(300, 8 * ImGui::GetTextLineHeightWithSpacing())))
        {
            try
            {
                for (const auto& entry : std::filesystem::directory_iterator(scriptsPath))
                {
                    if (entry.path().extension() == ".mlscript")
                    {
                        std::string filename = entry.path().filename().string();
                        bool isSelected = (selectedScript_ == filename);

                        if (ImGui::Selectable(filename.c_str(), isSelected))
                        {
                            selectedScript_ = filename;
                        }

                        if (isSelected)
                        {
                            ImGui::SetItemDefaultFocus();
                        }
                    }
                }
            }
            catch (const std::exception&)
            {
                ImGui::TextDisabled("No scripts found");
            }
            ImGui::EndListBox();
        }

        if (ImGui::Button(LOCTEXT("Execute"), ImVec2(120, 0)))
        {
            if (!selectedScript_.empty())
            {
                std::string fullPath = scriptsPath + selectedScript_;
                consoleOutput_.push_back("> [Script] " + selectedScript_);

                auto result = commandExecutor_->ExecuteScript(fullPath);

                if (!result.message.empty())
                {
                    consoleOutput_.push_back(result.message);
                }

                for (const auto& line : result.output)
                {
                    consoleOutput_.push_back("  " + line);
                }

                scrollToBottom_ = true;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SetItemDefaultFocus();
        ImGui::SameLine();
        if (ImGui::Button(LOCTEXT("Cancel"), ImVec2(120, 0)))
        {
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

void MagicaLegoUserInterface::DrawAISection()
{
    ImGui::Dummy(ImVec2(0, 10));
    ImGui::SeparatorText(LOCTEXT("AI Assistant"));

    // Check for pending AI results
    if (aiService_ && aiService_->HasPendingResult())
    {
        auto response = aiService_->GetPendingResult();
        aiGenerating_ = false;

        if (response.success)
        {
            consoleOutput_.push_back("> [AI] Generated script:");

            // Execute the generated script
            auto result = commandExecutor_->ExecuteScriptText(response.script);

            if (!result.message.empty())
            {
                consoleOutput_.push_back(result.message);
            }

            for (const auto& line : result.output)
            {
                consoleOutput_.push_back("  " + line);
            }

            scrollToBottom_ = true;
        }
        else
        {
            consoleOutput_.push_back(fmt::format("> [AI] Error: {}", response.message));
            scrollToBottom_ = true;
        }
    }

    // Status indicator
    if (aiService_)
    {
        auto status = aiService_->GetStatus();
        ImVec4 statusColor;
        const char* statusIcon;

        switch (status)
        {
        case MagicaLego::EAIStatus::NotConfigured:
            statusColor = ImVec4(1.0f, 0.8f, 0.2f, 1.0f);
            statusIcon = ICON_FA_TRIANGLE_EXCLAMATION;
            break;
        case MagicaLego::EAIStatus::Ready:
            statusColor = ImVec4(0.4f, 1.0f, 0.4f, 1.0f);
            statusIcon = ICON_FA_ROBOT;
            break;
        case MagicaLego::EAIStatus::Generating:
            statusColor = ImVec4(0.4f, 0.8f, 1.0f, 1.0f);
            statusIcon = ICON_FA_SPINNER;
            break;
        case MagicaLego::EAIStatus::Error:
            statusColor = ImVec4(1.0f, 0.4f, 0.4f, 1.0f);
            statusIcon = ICON_FA_CIRCLE_XMARK;
            break;
        }

        ImGui::PushStyleColor(ImGuiCol_Text, statusColor);
        ImGui::Text("%s %s", statusIcon, aiService_->GetStatusMessage().c_str());
        ImGui::PopStyleColor();
    }
    else
    {
        ImGui::TextDisabled("AI service unavailable");
    }

    // Input and generate button
    bool canGenerate = aiService_ && aiService_->IsConfigured() && !aiGenerating_;

    if (!canGenerate)
    {
        ImGui::BeginDisabled();
    }

    ImGui::PushItemWidth(buildBarWidth - buttonSize - 16);

    bool executeGenerate = false;
    if (ImGui::InputTextMultiline("##AIInput", &aiInput_,
        ImVec2(-FLT_MIN, 3 * ImGui::GetTextLineHeightWithSpacing()),
        ImGuiInputTextFlags_CtrlEnterForNewLine))
    {
    }

    // Check for Ctrl+Enter to generate
    if (ImGui::IsItemFocused() && ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Enter))
    {
        executeGenerate = true;
    }

    ImGui::PopItemWidth();

    std::string generateLabel = aiGenerating_
        ? (std::string(ICON_FA_SPINNER) + " " + LOCTEXT("Generating..."))
        : (std::string(ICON_FA_ROBOT) + " " + LOCTEXT("Generate"));

    if (ImGui::Button(generateLabel.c_str(), ImVec2(-FLT_MIN, 0)) || executeGenerate)
    {
        if (!aiInput_.empty() && canGenerate)
        {
            aiGenerating_ = true;
            consoleOutput_.push_back(fmt::format("> [AI] Request: {}", aiInput_));
            scrollToBottom_ = true;

            std::string prompt = aiInput_;
            aiService_->GenerateScriptAsync(prompt, nullptr);
        }
    }

    if (!canGenerate)
    {
        ImGui::EndDisabled();
    }

    BUTTON_TOOLTIP(LOCTEXT("Use AI to generate building script from natural language (Ctrl+Enter)"))
}
