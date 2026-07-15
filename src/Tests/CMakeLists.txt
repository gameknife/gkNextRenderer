set(unitTestSources
    TestCommon.cpp
    Test_PhysicsSync.cpp
    Test_GpuTimerLogic.cpp
    Test_GltfSkinning.cpp
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
    Test_SimKit.cpp
    Test_ScadRig.cpp
    Test_RigAnimator.cpp
    Test_RenderComponent.cpp
    Test_Brotato3DPcg.cpp
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
    Test_GnbAIClient.cpp
    Test_ScadPromptContext.cpp
    Test_ScadSessionStore.cpp
    Test_Rgb9e5Pack.cpp
    Test_UpscalerTypes.cpp
    Test_SogLoader.cpp
    Test_TerminalBlitter.cpp
    Test_PhysicsComponent.cpp
    ${GK_SOURCE_ROOT}/Application/Editor/ScadStudio/ScadOutline.cpp
    ${GK_SOURCE_ROOT}/Application/Editor/ScadStudio/ScadPromptContext.cpp
    ${GK_SOURCE_ROOT}/Application/Editor/ScadStudio/ScadSessionStore.cpp
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
    ${GK_SOURCE_ROOT}/Application/Game/Brotato3D/Brotato3DPcgGenerator.cpp
)

find_package(Catch2 3 REQUIRED)
add_executable(gkNextUnitTests ${unitTestSources})

gk_apply_target_defaults(gkNextUnitTests)
set_target_properties(gkNextUnitTests PROPERTIES FOLDER "Tests")
add_dependencies(gkNextUnitTests Assets)
target_include_directories(gkNextUnitTests PRIVATE
    ${GK_SOURCE_ROOT}/Application/Game/NextRA
)
target_link_libraries(gkNextUnitTests PRIVATE
    gkNextEngine
    NextGameplay
    Catch2::Catch2WithMain
    NextAI
)
set(unitTestRuntimeModules
    DevTools
    NextPhysics
    GltfLoader
    LDrawLoader
    ScadLoader
    SplatLoader
)
if(TARGET NextTui)
    list(APPEND unitTestRuntimeModules NextTui)
endif()
gk_target_runtime_modules(gkNextUnitTests MODULES ${unitTestRuntimeModules})
