#pragma once

#include "Runtime/Application.hpp"

enum ELegoMode
{
	ELM_Dig,
	ELM_Place,
	ELM_Select,
};

enum ECamMode
{
	ECM_Pan,
	ECM_Orbit,
	ECM_AutoFocus,
};

struct FBasicBlock
{
	// just need modelIdNow
	int modelId_;
	int matType;
	glm::vec4 color;
};

struct FPlacedBlock
{
	glm::ivec3 location; // 对应一个hash
	int modelId_; // 如果为-1，表示已经被挖掉了
};

class MagicaLegoUserInterface;

class MagicaLegoGameInstance : public NextGameInstanceBase
{
public:
	MagicaLegoGameInstance(Vulkan::WindowConfig& config, Options& options, NextRendererApplication* engine);
    ~MagicaLegoGameInstance() override = default;
	
    void OnInit() override {}
    void OnTick() override;
    void OnDestroy() override {}
	bool OnRenderUI() override;
    void OnRayHitResponse(Assets::RayCastResult& result) override;

	bool OverrideModelView(glm::mat4& OutMatrix) const override;

	void OnSceneLoaded() override;
	void OnSceneUnloaded() override;
	
    bool OnKey(int key, int scancode, int action, int mods) override;
    bool OnCursorPosition(double xpos, double ypos) override;
    bool OnMouseButton(int button, int action, int mods) override;

	void CleanUp();
	void SaveRecord(std::string filename);
	void LoadRecord(std::string filename);

	ELegoMode GetBuildMode() const {return currentMode_;}
	ECamMode GetCameraMode() const {return currentCamMode_;}

	void SetBuildMode(ELegoMode mode);
	void SetCameraMode(ECamMode mode);

	std::vector<FBasicBlock>& GetBasicNodes() {return BasicNodes;}

	int GetCurrentBrushIdx() const {return currentBlockIdx_;}
	void SetCurrentBrushIdx(int idx) {currentBlockIdx_ = idx;}

	int GetCurrentStep() const {return currentPreviewStep;}
	int GetMaxStep() const {return static_cast<int>(BlockRecords.size());}
	void SetPlayStep(int step);

	bool IsPlayReview() const {return playReview_;}
	void SetPlayReview(bool b) {playReview_ = b;}
	
protected:
	void AddBasicBlock(std::string blockName);
	FBasicBlock* GetBasicBlock(uint32_t BlockIdx);

	void PlaceDynamicBlock(FPlacedBlock Block);
	
	void RebuildScene(std::unordered_map<uint64_t, FPlacedBlock>& Source);
	void RebuildFromRecord(int timelapse);

private:
	ELegoMode currentMode_;
	ECamMode currentCamMode_;
	// 基础的方块
	std::vector<FBasicBlock> BasicNodes;

	int currentBlockIdx_ {};

	int currentPreviewStep {};

	// 起始的方块位置，之后的instance都是rebuild出来的
	uint32_t instanceCountBeforeDynamics_ {};

	bool playReview_ {};

	// 基础加速结构，location -> uint64_t，存储已经放置的方块
	std::unordered_map<uint64_t, FPlacedBlock> BlocksDynamics;
	std::vector<uint64_t> hashByInstance;

	std::vector<FPlacedBlock> BlockRecords;

	NextRendererApplication& GetEngine() {return *engine_;}
	NextRendererApplication* engine_;

	bool resetMouse_ {};
	
	glm::dvec2 mousePos_ {};

	glm::vec3 realCameraCenter_ {};
	glm::vec3 cameraCenter_ {};
	mutable glm::vec3 panForward_ {};
	mutable glm::vec3 panLeft_ {};
	float cameraRotX_ {};
	float cameraRotY_ {};
	float cameraMultiplier_ {};

	bool bMouseLeftDown_ {};
	int lastDownFrameNum_ {};
	std::vector<uint64_t> oneLinePlacedInstance_ {};
	glm::ivec3 lastPlacedLocation_ {};

	std::unique_ptr<class MagicaLegoUserInterface> UserInterface_;
};

