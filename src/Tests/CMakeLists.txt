set(unitTestSources
    TestCommon.cpp
    Test_PhysicsSync.cpp
    Test_Bindless3DTexture.cpp
    Test_Atmosphere.cpp
    Test_BindlessSlotRegistry.cpp
    Test_GltfSkinning.cpp
    Test_FileHelperTrace.cpp
    Test_SequencerSerialization.cpp
    Test_LightObject.cpp
    Test_CameraFocus.cpp
    Test_HdrTextureCache.cpp
    Test_ComponentSystem.cpp
    Test_GameplayComponents.cpp
    Test_LDrawConfig.cpp
    Test_LDrawGeometry.cpp
    Test_LDrawLoader.cpp
    Test_LDrawParser.cpp
    Test_LDrawConnectivity.cpp
    Test_CVarSystem.cpp
    Test_SceneList.cpp
    Test_SceneReference.cpp
    Test_ScadLoader.cpp
    Test_ScadTerrain.cpp
    Test_ScadLibraryTerrainProcess.cpp
    Test_TerrainWalkable.cpp
    Test_SimKit.cpp
    Test_ScadRig.cpp
    Test_RigAnimator.cpp
    Test_RenderComponent.cpp
    Test_ImAnimIntegration.cpp
    Test_BrickPlayerSnapLogic.cpp
    Test_BrickBuildPlan.cpp
    Test_ProductAIStructuredContracts.cpp
    Test_ProductAIArtifactFixtures.cpp
    Test_MagicaLegoStyle.cpp
    Test_MagicaLegoScript.cpp
    Test_MagicaLegoPlacementRules.cpp
    Test_FlappyRng.cpp
    Test_NextRAFixed.cpp
    Test_NextTotalwarCamera.cpp
    Test_NextTotalwarFormation.cpp
    Test_NextTotalwarCombat.cpp
    Test_NextTotalwarProductization.cpp
    Test_NextDayzProductization.cpp
    Test_GnbAIClient.cpp
    Test_ScadPromptContext.cpp
    Test_ScadSessionStore.cpp
    Test_ScadAIController.cpp
    Test_ScadAIAdapters.cpp
    Test_ScadSourceIndex.cpp
    Test_ScadStudioSessionImporter.cpp
    Test_CharacterWorkbenchAI.cpp
    Test_Rgb9e5Pack.cpp
    Test_UpscalerTypes.cpp
    Test_RendererChoices.cpp
    Test_UiFoundation.cpp
    Test_SogLoader.cpp
    Test_TerminalBlitter.cpp
    Test_PhysicsComponent.cpp
    Test_CPUAccelerationSnapshot.cpp
    Test_AmbientBakeScheduler.cpp
    ${GK_SOURCE_ROOT}/Application/Editor/ScadStudio/ScadOutline.cpp
    ${GK_SOURCE_ROOT}/Application/Editor/ScadStudio/ScadPromptContext.cpp
    ${GK_SOURCE_ROOT}/Application/Editor/ScadStudio/ScadSessionStore.cpp
    ${GK_SOURCE_ROOT}/Application/Editor/ScadLibrary/TerrainProcessDocument.cpp
    ${GK_SOURCE_ROOT}/Application/Editor/ScadLibrary/CharacterWorkbench.cpp
    ${GK_SOURCE_ROOT}/Application/Editor/ScadLibrary/AI/ScadAIContracts.cpp
    ${GK_SOURCE_ROOT}/Application/Editor/ScadLibrary/AI/ScadAIController.cpp
    ${GK_SOURCE_ROOT}/Application/Editor/ScadLibrary/AI/ScadAIHistoryStore.cpp
    ${GK_SOURCE_ROOT}/Application/Editor/ScadLibrary/AI/ScadStudioSessionImporter.cpp
    ${GK_SOURCE_ROOT}/Application/Editor/ScadLibrary/AI/Adapters/KitModuleAIAdapter.cpp
    ${GK_SOURCE_ROOT}/Application/Editor/ScadLibrary/AI/Adapters/RigClipAIAdapter.cpp
    ${GK_SOURCE_ROOT}/Application/Editor/ScadLibrary/AI/Adapters/SceneObjectsAIAdapter.cpp
    ${GK_SOURCE_ROOT}/Application/Editor/ScadLibrary/AI/Adapters/SceneSourceAIAdapter.cpp
    ${GK_SOURCE_ROOT}/Application/Editor/ScadLibrary/AI/Adapters/TerrainProcessAIAdapter.cpp
    ${GK_SOURCE_ROOT}/Application/Game/BrickPlayer/BrickPlayerSnapLogic.cpp
    ${GK_SOURCE_ROOT}/Application/Game/BrickPlayer/BrickBuildPlan.cpp
    ${GK_SOURCE_ROOT}/Application/Game/MagicaLego/MagicaLegoStyle.cpp
    ${GK_SOURCE_ROOT}/Application/Game/MagicaLego/MagicaLegoScriptParser.cpp
    ${GK_SOURCE_ROOT}/Application/Game/MagicaLego/MagicaLegoPlacementRules.cpp
    ${GK_SOURCE_ROOT}/Application/Game/NextRA/Net/LoopbackTransport.cpp
    ${GK_SOURCE_ROOT}/Application/Game/NextRA/Net/Order.cpp
    ${GK_SOURCE_ROOT}/Application/Game/NextRA/Net/OrderManager.cpp
    ${GK_SOURCE_ROOT}/Application/Game/NextRA/Net/Replay.cpp
    ${GK_SOURCE_ROOT}/Application/Game/NextRA/Sim/OccupancyGrid.cpp
    ${GK_SOURCE_ROOT}/Application/Game/NextRA/Sim/PathfindGrid.cpp
    ${GK_SOURCE_ROOT}/Application/Game/NextRA/Sim/SimWorld.cpp
    ${GK_SOURCE_ROOT}/Application/Game/NextRA/Sim/SyncHash.cpp
    ${GK_SOURCE_ROOT}/Application/Game/NextRA/Sim/Systems/OrderApplySystem.cpp
    ${GK_SOURCE_ROOT}/Application/Game/NextTotalwar/Battle/FormationLayout.cpp
    ${GK_SOURCE_ROOT}/Application/Game/NextTotalwar/Battle/CombatModel.cpp
    ${GK_SOURCE_ROOT}/Application/Game/NextTotalwar/Battle/CombatGrid.cpp
    ${GK_SOURCE_ROOT}/Application/Game/NextTotalwar/Battle/CombatSystem.cpp
    ${GK_SOURCE_ROOT}/Application/Game/NextTotalwar/Battle/BattleSession.cpp
    ${GK_SOURCE_ROOT}/Application/Game/NextTotalwar/Battle/BattleOrderSystem.cpp
    ${GK_SOURCE_ROOT}/Application/Game/NextTotalwar/Battle/MoraleSystem.cpp
    ${GK_SOURCE_ROOT}/Application/Game/NextTotalwar/Battle/RangedCombatSystem.cpp
    ${GK_SOURCE_ROOT}/Application/Game/NextTotalwar/AI/CommanderAI.cpp
    ${GK_SOURCE_ROOT}/Application/Game/NextTotalwar/Data/BattleData.cpp
    ${GK_SOURCE_ROOT}/Application/Game/NextTotalwar/Render/BattleCamera.cpp
    ${GK_SOURCE_ROOT}/Application/Game/NextDayz/Data/ItemDefs.cpp
    ${GK_SOURCE_ROOT}/Application/Game/NextDayz/Inventory/Inventory.cpp
    ${GK_SOURCE_ROOT}/Application/Game/NextDayz/Player/PlayerActionController.cpp
    ${GK_SOURCE_ROOT}/Application/Game/NextDayz/Player/SurvivalSystem.cpp
    ${GK_SOURCE_ROOT}/Application/Game/NextDayz/World/LootDirector.cpp
    ${GK_SOURCE_ROOT}/Application/Game/NextDayz/Combat/NoiseSystem.cpp
    ${GK_SOURCE_ROOT}/Application/Game/NextDayz/Combat/CombatSystem.cpp
    ${GK_SOURCE_ROOT}/Application/Game/NextDayz/Zombies/ZombieSystem.cpp
    ${GK_SOURCE_ROOT}/Application/Game/NextDayz/World/ZombieSpawnDirector.cpp
    ${GK_SOURCE_ROOT}/Application/Game/NextDayz/Zombies/ZombieVisualPool.cpp
)

if(GK_DOTNET_ENABLED)
    list(APPEND unitTestSources Test_DotNetEngineApi.cpp)
    list(APPEND unitTestSources Test_ReflectionManifest.cpp)
endif()

find_package(Catch2 3 REQUIRED)
add_executable(gkNextUnitTests ${unitTestSources})

gk_apply_target_defaults(gkNextUnitTests)
set_target_properties(gkNextUnitTests PROPERTIES FOLDER "Tests")
# The reflection manifest test reads a committed source file, and the tests are launched from
# whatever directory the caller happens to be in.
target_compile_definitions(gkNextUnitTests PRIVATE GK_NEXT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_dependencies(gkNextUnitTests Assets)
find_package(nlohmann_json CONFIG REQUIRED)

target_include_directories(gkNextUnitTests PRIVATE
    ${GK_SOURCE_ROOT}/Application/Game/NextRA
    ${GK_SOURCE_ROOT}/Application/Editor/ScadStudio
    ${GK_SOURCE_ROOT}/Application/Editor/ScadLibrary
    ${GK_SOURCE_ROOT}/Application/Game/BrickPlayer
    ${GK_SOURCE_ROOT}/Application/Game/MagicaLego
    ${GK_SOURCE_ROOT}/Application/Game/NextTotalwar
    ${GK_SOURCE_ROOT}/Application/Game/NextDayz
)
target_link_libraries(gkNextUnitTests PRIVATE
    gkNextEngine
    NextGameplay
    Catch2::Catch2WithMain
    NextAI
    nlohmann_json::nlohmann_json
    fmt::fmt
)
set(unitTestRuntimeModules
    DevTools
    NextPhysics
    GltfLoader
    SceneExport
    LDrawLoader
    ScadLoader
    SplatLoader
)
if(TARGET NextTui)
    list(APPEND unitTestRuntimeModules NextTui)
endif()
if(TARGET NextDotNet)
    list(APPEND unitTestRuntimeModules NextDotNet)
endif()
gk_target_runtime_modules(gkNextUnitTests MODULES ${unitTestRuntimeModules})

if(TARGET NextDotNet)
    # The tests exercise the native binding table, not the managed runtime, so they host no C#.
    gk_dotnet_stub_game(gkNextUnitTests)
endif()
