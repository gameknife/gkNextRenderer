#include "Runtime/Application.hpp"

enum ELegoMode
{
	ELM_Dig,
	ELM_Place,
	ELM_Select,
};
class MagicaLegoGameInstance : public NextGameInstanceBase
{
public:
	MagicaLegoGameInstance(Vulkan::WindowConfig& config, NextRendererApplication* engine);
    ~MagicaLegoGameInstance() {}
	
    void OnInit() override {}
    void OnTick() override {}
    void OnDestroy() override {}
	bool OnRenderUI() override;
    void OnRayHitResponse(Assets::RayCastResult& result) override;
	
    bool OnKey(int key, int scancode, int action, int mods) override {return false;}
    bool OnCursorPosition(double xpos, double ypos) override {return false;}
    bool OnMouseButton(int button, int action, int mods) override {return false;}
	
private:
	NextRendererApplication& GetEngine() {return *engine_;}
	NextRendererApplication* engine_;

	ELegoMode currentMode_;
	
	void DrawLeftBar();
	void DrawRightBar();
};
