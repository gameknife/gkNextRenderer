#pragma once
#include "Common/CoreMinimal.hpp"

struct ImFont;
class MagicaLegoGameInstance;

enum EIntroStep : uint8_t
{
	EIS_Entry,
	EIS_Opening,
	EIS_GuideBuild,
	EIS_Finish,
	EIS_InGame,
};

enum EUILayout : uint8_t
{
	EULUT_LeftBar = 0x1,
	EULUT_RightBar = 0x2,
	EULUT_Timeline = 0x4,
	EULUT_TitleBar = 0x8,
	EULUT_LayoutIndicator = 0x10,
};

class MagicaLegoUserInterface final
{
public:
	MagicaLegoUserInterface(MagicaLegoGameInstance* gameInstance);

	void OnInitUI();
	void OnRenderUI();
	void OnSceneLoaded();

private:
	void DrawTitleBar();
	void DrawMainToolBar();
	void DrawLeftBar();
	void DrawRightBar();
	void DrawTimeline();
	void DrawOpening() const;

	void DrawWaiting();
	void DrawNotify();

	void DrawWatermark();
	void DrawVersionWatermark();

	void DrawHUD();

	void DrawHelp();

	void RecordTimeline(bool autoRotate);

	void ShowNotify(const std::string& text, std::function<void()> callback = nullptr);
	
	MagicaLegoGameInstance* GetGameInstance() const {return gameInstance_;}
	MagicaLegoGameInstance* gameInstance_;

	void DirectSetLayout(uint32_t layout);
	void PushLayout(uint32_t layout);
	void PopLayout();
	uint32_t uiStatus_ {};
	std::vector<uint32_t> uiStatusStack_ {};

	bool capture_ {};
	bool waiting_ {};
	std::string waitingText_ {};

	bool notify_ {};
	float notifyTimer_ {};
	std::string notifyText_ {};
	std::function<void()> notifyCallback_ {};

	ImFont* bigFont_ {};
	ImFont* boldFont_ {};
	float openingTimer_ = 2.0f;

	bool showHelp_ {};

	EIntroStep introStep_ = EIntroStep::EIS_Entry;

	bool qualityMode_ {};
};

