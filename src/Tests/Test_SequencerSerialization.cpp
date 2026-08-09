#include <catch2/catch_all.hpp>

#include "TestCommon.hpp"
#include "Application/Editor/gkNextEditor/Core/SceneSavePolicy.hpp"
#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Assets/Core/Scene.hpp"
#include "Modules/GltfLoader/FSceneLoader.h"
#include "Modules/SceneExport/FSceneSaver.h"

#include <chrono>
#include <filesystem>

TEST_CASE("Sequencer scenes can be saved as GLB or glTF", "[Unit][Editor][Sequencer][SceneExport]")
{
    CHECK(Editor::IsWritableGltfScenePath("sequence.glb"));
    CHECK(Editor::IsWritableGltfScenePath("sequence.gltf"));
    CHECK(Editor::IsWritableGltfScenePath("sequence.GLTF"));
    CHECK_FALSE(Editor::IsWritableGltfScenePath("sequence.fbx"));
    CHECK(std::filesystem::path(Editor::NormalizeSaveAsScenePath("sequence")).extension() == ".glb");
    CHECK(std::filesystem::path(Editor::NormalizeSaveAsScenePath("sequence", ".gltf")).extension() == ".gltf");
}

TEST_CASE_METHOD(EngineTestFixture, "Sequencer tracks round-trip through glTF scene export",
                 "[GPU][Integration][Animation][Sequencer][SceneExport]")
{
    Assets::Scene& scene = engine_->GetScene();
    auto node = Assets::Node::CreateNode(
        "RoundTripTarget", glm::vec3(0.0f), glm::quat(1, 0, 0, 0), glm::vec3(1.0f), scene.GenerateInstanceId());
    scene.AddNode(node);

    Assets::AnimationTrack transformTrack;
    transformTrack.AnimationName = "RoundTrip";
    transformTrack.NodeName_ = node->GetName();
    transformTrack.Duration_ = 3.0f;
    transformTrack.TranslationChannel.Keys = {
        {0.0f, glm::vec3(1.0f, 2.0f, 3.0f)},
        {3.0f, glm::vec3(4.0f, 5.0f, 6.0f)},
    };
    transformTrack.RotationChannel.Keys = {
        {0.0f, glm::quat(1.0f, 0.0f, 0.0f, 0.0f)},
        {3.0f, glm::normalize(glm::quat(0.8f, 0.0f, 0.6f, 0.0f))},
    };

    Assets::AnimationTrack environmentTrack;
    environmentTrack.AnimationName = "SkyRoundTrip";
    environmentTrack.NodeName_ = "Environment";
    environmentTrack.Target_ = Assets::AnimationTrack::Target::Environment;
    environmentTrack.Duration_ = 3.0f;
    environmentTrack.PlaySpeed_ = 0.75f;
    environmentTrack.SunIntensityChannel.Keys = {{0.0f, 150.0f}, {3.0f, 750.0f}};
    environmentTrack.SkyColorChannel.Keys = {
        {0.0f, glm::vec3(0.1f, 0.2f, 0.3f)},
        {3.0f, glm::vec3(0.6f, 0.7f, 0.8f)},
    };
    scene.Tracks() = {transformTrack, environmentTrack};

    const std::string suffix = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const std::filesystem::path output = std::filesystem::temp_directory_path() /
        ("gk_sequencer_roundtrip_" + suffix + ".gltf");

    REQUIRE(Assets::FSceneSaver::SaveGLTFScene(output.string(), scene));
    std::vector<Assets::AnimationTrack> loadedTracks;
    REQUIRE(Assets::FSceneLoader::LoadAnimationTracks(output.string(), loadedTracks));
    std::error_code removeError;
    std::filesystem::remove(output, removeError);

    const auto transform = std::ranges::find_if(loadedTracks, [](const Assets::AnimationTrack& track)
    {
        return track.AnimationName == "RoundTrip" && track.NodeName_ == "RoundTripTarget";
    });
    REQUIRE(transform != loadedTracks.end());
    REQUIRE(transform->TranslationChannel.Keys.size() == 2);
    REQUIRE(transform->RotationChannel.Keys.size() == 2);
    CHECK(transform->Duration_ == Catch::Approx(3.0f));
    CHECK(transform->TranslationChannel.Keys[1].Value.z == Catch::Approx(6.0f));
    CHECK(transform->RotationChannel.Keys[1].Value.y == Catch::Approx(0.6f).margin(0.0001f));

    const auto environment = std::ranges::find_if(loadedTracks, [](const Assets::AnimationTrack& track)
    {
        return track.Target_ == Assets::AnimationTrack::Target::Environment &&
            track.AnimationName == "SkyRoundTrip";
    });
    REQUIRE(environment != loadedTracks.end());
    CHECK(environment->Duration_ == Catch::Approx(3.0f));
    CHECK(environment->PlaySpeed_ == Catch::Approx(0.75f));
    REQUIRE(environment->SunIntensityChannel.Keys.size() == 2);
    REQUIRE(environment->SkyColorChannel.Keys.size() == 2);
    CHECK(environment->SunIntensityChannel.Keys[1].Value == Catch::Approx(750.0f));
    CHECK(environment->SkyColorChannel.Keys[1].Value.b == Catch::Approx(0.8f));

    scene.Tracks().clear();
    scene.RemoveNodeByInstanceId(node->GetInstanceId());
}
