#pragma once
#include "Common/CoreMinimal.hpp"
#include "Runtime/Application.hpp"
#include "Utilities/FileHelper.hpp"

#define MAGICALEGO_SAVE_VERSION 1

enum class ELegoMode : uint8_t
{
	ELM_Dig,
	ELM_Place,
	ELM_Select,
};

enum class ECamMode : uint8_t
{
	ECM_Pan,
	ECM_Orbit,
	ECM_AutoFocus,
};

enum class EBasePlane : uint8_t
{
	EBP_Big,
	EBP_Mid,
	EBP_Small,
};

enum class EOrientation : uint8_t
{
	EO_North,
	EO_East,
	EO_South,
	EO_West,
};

static constexpr glm::mat4 GetOrientationMatrix(EOrientation orientation)
{
	switch(orientation)
	{
	case EOrientation::EO_North: return glm::mat4(1.0f);
	case EOrientation::EO_East: return glm::rotate(glm::mat4(1.0f), -glm::pi<float>() * 0.5f, glm::vec3(0,1,0));
	case EOrientation::EO_South: return glm::rotate(glm::mat4(1.0f), -glm::pi<float>() * 1.0f, glm::vec3(0,1,0));
	case EOrientation::EO_West: return glm::rotate(glm::mat4(1.0f), -glm::pi<float>() * 1.5f, glm::vec3(0,1,0));
	}
	return glm::mat4(1.0f);
};

template <> class fmt::formatter<EOrientation> {
public:
	constexpr auto parse (format_parse_context& ctx) { return ctx.begin(); }
	template <typename Context>
	constexpr auto format (EOrientation const& foo, Context& ctx) const {
		std::string result;
		switch(foo)
		{
			case EOrientation::EO_North: result = "NORTH"; break;
			case EOrientation::EO_East: result = "EAST"; break;
			case EOrientation::EO_South: result = "SOUTH"; break;
			case EOrientation::EO_West: result = "WEST"; break;
		}
		return format_to(ctx.out(), "{}", result); 
	}
};

struct FBasicBlock
{
	// just need modelIdNow
	int16_t brushId_;
	int modelId_;
	int matType;
	glm::vec4 color;
	char name[128];
	char type[128];
};

// 基础record，12byte, 带有多个reserved字段，方便后续扩展
struct FPlacedBlock
{
	glm::i16vec3 location; // 对应一个hash
	EOrientation orientation;
	uint8_t reserved0;
	int16_t modelId_; // 如果为-1，表示已经被挖掉了
	uint8_t reserved1;
	uint8_t reserved2;
	bool operator == (const FPlacedBlock& Other) const
	{
		return location == Other.location && modelId_ == Other.modelId_;
	}
};

class MagicaLegoUserInterface;

using FBasicBlockIndicatorLibrary = std::map<std::string, std::tuple<glm::vec3, glm::vec3>>;
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
	
    void OnInit() override;
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
	int16_t GetCurrentBrushIdx() const {return currentBlockIdx_;}
	void SetCurrentBrushIdx(int16_t idx) {currentBlockIdx_ = idx;}
	void TryChangeSelectionBrushIdx(int16_t idx);

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

	EOrientation GetCurrentOrientation() const {return currentOrientation_;}
	void ChangeOrientation() {currentOrientation_ = static_cast<EOrientation>((static_cast<uint8_t>(currentOrientation_) + 1) % 4);}

	void PlayNextBGM();
	bool IsBGMPaused();
	void PauseBGM(bool pause);
	std::string GetCurrentBGMName();
	
protected:
	void AddBlockGroup(std::string typeName);
	void AddBasicBlock(std::string blockName, std::string typeName);
	FBasicBlock* GetBasicBlock(uint32_t BlockIdx);

	void RebuildScene(FPlacedBlockDatabase& Source, uint32_t newhash);
	void RebuildFromRecord(int timelapse);

	void CleanDynamicBlocks();

private:
	ELegoMode currentMode_ {};
	ECamMode currentCamMode_ {};
	EBasePlane currentBaseSize_ {};
	EOrientation currentOrientation_ {};
	
	// base blocks library
	FBasicBlockStack BasicNodes;
	FBasicBlockLibrary BasicBlockTypeMap;
	FBasicBlockIndicatorLibrary BasicNodeIndicatorMap;
	
	int16_t currentBlockIdx_ {};
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
	float cameraArm_ {};
	float cameraMultiplier_ {};

	bool bMouseLeftDown_ {};
	int lastDownFrameNum_ {};
	std::vector<uint32_t> oneLinePlacedInstance_ {};
	glm::i16vec3 lastPlacedLocation_ {};
	glm::i16vec3 lastSelectLocation_ {};
	glm::i16vec3 lastHoverLocation_ {};
	std::unique_ptr<MagicaLegoUserInterface> UserInterface_;

	double previewWindowTimer_ {};
	double previewWindowElapsed_ {};

	double indicatorTimer_ {};
	bool indicatorDrawRequest_ {};
	glm::vec3 indicatorMinTarget_ {};
	glm::vec3 indicatorMaxTarget_ {};
	glm::vec3 indicatorMinCurrent_ {};
	glm::vec3 indicatorMaxCurrent_ {};

	uint32_t currentBGM_ = 0;
	std::vector< std::tuple<std::string, std::string> > bgmArray_;
};

