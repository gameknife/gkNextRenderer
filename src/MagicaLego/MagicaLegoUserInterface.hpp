#pragma once

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
	void DrawMainToolBar();
	void DrawLeftBar();
	void DrawRightBar();
	void DrawTimeline();
	void DrawOpening();
	void DrawIndicator();
	void DrawStatusBar();
	
	MagicaLegoGameInstance* GetGameInstance() {return gameInstance_;}
	MagicaLegoGameInstance* gameInstance_;

	bool showLeftBar_ = false;
	bool showRightBar_ = false;
	bool showTimeline_ = false;

	ImFont* bigFont_ {};
	float openingTimer_ = 2.0f;

	EIntroStep introStep_ = EIS_Entry;
};

