#pragma once
#include "Common/CoreMinimal.hpp"
#include "Runtime/Application.hpp"

#define MAGICALEGO_SAVE_VERSION 1

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

enum EBasePlane
{
	EBP_Big,
	EBP_Mid,
	EBP_Small,
};

struct FBasicBlock
{
	// just need modelIdNow
	int brushId_;
	int modelId_;
	int matType;
	glm::vec4 color;
	char name[128];
	char type[128];
};

struct FPlacedBlock
{
	glm::i16vec3 location; // 对应一个hash
	int modelId_; // 如果为-1，表示已经被挖掉了
};

class MagicaLegoUserInterface;

using FBasicBlockStack = std::vector<FBasicBlock>;
using FBasicBlockLibrary = std::map<std::string, FBasicBlockStack >;
using FPlacedBlockDatabase = std::unordered_map<uint32_t, FPlacedBlock>;

struct FMagicaLegoSave
{
	int version = MAGICALEGO_SAVE_VERSION;
	FBasicBlockStack brushs;
	std::vector<FPlacedBlock> records;

	void Save(std::string filename);
	void Load(std::string filename);
};

class MagicaLegoGameInstance : public NextGameInstanceBase
{
public:
	MagicaLegoGameInstance(Vulkan::WindowConfig& config, Options& options, NextRendererApplication* engine);
    ~MagicaLegoGameInstance() override = default;
	
    void OnInit() override {}
    void OnTick(double deltaSeconds) override;
    void OnDestroy() override {}
	bool OnRenderUI() override;
	void OnInitUI() override;
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

	FBasicBlockStack& GetBasicNodes() {return BasicNodes;}
	FBasicBlockLibrary& GetBasicNodeLibrary() {return BasicBlockTypeMap;}

	int ConvertBrushIdxToNextType(const std::string& prefix, int idx ) const;
	int GetCurrentBrushIdx() const {return currentBlockIdx_;}
	void SetCurrentBrushIdx(int idx) {currentBlockIdx_ = idx;}
	void TryChangeSelectionBrushIdx(int idx);

	// Handle
	glm::i16vec3 GetCurrentSeletionBlock() const {return lastSelectLocation_;}
	
	// Replay
	int GetCurrentStep() const {return currentPreviewStep;}
	int GetMaxStep() const {return std::max(0, static_cast<int>(BlockRecords.size()));}
	void SetPlayStep(int step);

	bool IsPlayReview() const {return playReview_;}
	void SetPlayReview(bool b) {playReview_ = b;}

	void DumpReplayStep(int step);

	NextRendererApplication& GetEngine() {return *engine_;}

	void PlaceDynamicBlock(FPlacedBlock Block);

	void SwitchBasePlane(EBasePlane Type);
	EBasePlane GetCurrentBasePlane() const {return currentBaseSize_;}

	glm::i16vec3 GetLastPlacedLocation() const {return lastPlacedLocation_;}

	void GeneratingThmubnail();
	
protected:
	void AddBlockGroup(std::string typeName);
	void AddBasicBlock(std::string blockName, std::string typeName);
	FBasicBlock* GetBasicBlock(uint32_t BlockIdx);

	void RebuildScene(FPlacedBlockDatabase& Source);
	void RebuildFromRecord(int timelapse);

	void CleanDynamicBlocks();

private:
	ELegoMode currentMode_;
	ECamMode currentCamMode_;
	EBasePlane currentBaseSize_;
	
	// base blocks library
	FBasicBlockStack BasicNodes;
	FBasicBlockLibrary BasicBlockTypeMap;
	
	int currentBlockIdx_ {};
	int currentPreviewStep {};

	// 起始的方块位置，之后的instance都是rebuild出来的
	uint32_t instanceCountBeforeDynamics_ {};

	bool playReview_ {};

	// 基础加速结构，location -> uint64_t，存储已经放置的方块
	FPlacedBlockDatabase BlocksDynamics;
	std::vector<FPlacedBlock> BlockRecords;
	
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
	std::vector<uint32_t> oneLinePlacedInstance_ {};
	glm::i16vec3 lastPlacedLocation_ {};
	glm::i16vec3 lastSelectLocation_ {};
	std::unique_ptr<class MagicaLegoUserInterface> UserInterface_;

	double previewWindowTimer_ {};
	double previewWindowElapsed_ {};
};

