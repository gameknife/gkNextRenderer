#pragma once
#include "Common/CoreMinimal.hpp"

struct ImFont;
class MagicaLegoGameInstance;

enum EIntroStep
{
	EIS_Entry,
	EIS_Opening,
	EIS_GuideBuild,
	EIS_Finish,

	EIS_InGame,
};

enum EUILayout
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
	void DrawOpening();
	void DrawIndicator();
	void DrawStatusBar();

	void DrawWaiting();
	void DrawNotify();

	void DrawWatermark();

	void DrawHUD();

	void RecordTimeline();
	
	MagicaLegoGameInstance* GetGameInstance() {return gameInstance_;}
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

	EIntroStep introStep_ = EIS_Entry;
};

