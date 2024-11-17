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
	
	MagicaLegoGameInstance* GetGameInstance() {return gameInstance_;}
	MagicaLegoGameInstance* gameInstance_;

	void DirectSetLayout(uint32_t layout);
	void PushLayout(uint32_t layout);
	void PopLayout();
	uint32_t uiStatus_ {};
	std::vector<uint32_t> uiStatusStack_ {};

	ImFont* bigFont_ {};
	ImFont* boldFont_ {};
	float openingTimer_ = 2.0f;

	EIntroStep introStep_ = EIS_Entry;
};

