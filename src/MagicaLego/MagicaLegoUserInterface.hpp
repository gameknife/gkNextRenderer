#pragma once

struct ImFont;
class MagicaLegoGameInstance;

class MagicaLegoUserInterface final
{
public:
	MagicaLegoUserInterface(MagicaLegoGameInstance* gameInstance);

	void OnInitUI();
	void DrawOpening();
	void OnRenderUI();

private:
	void DrawMainToolBar();
	void DrawLeftBar();
	void DrawRightBar();
	void DrawTimeline();
	
	MagicaLegoGameInstance* GetGameInstance() {return gameInstance_;}
	MagicaLegoGameInstance* gameInstance_;

	bool showLeftBar_ = false;
	bool showRightBar_ = false;
	bool showTimeline_ = false;

	ImFont* bigFont_ {};
	float openingTimer_ = 2.0f;
};

