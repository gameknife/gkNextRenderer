#include <catch2/catch_all.hpp>
#include "Modules/GltfLoader/FSceneLoader.h"
#include "Engine/Assets/Core/Model.hpp"
#include "Engine/Assets/Data/Material.hpp"
#include <fstream>
#include <filesystem>

#include "Engine/Utilities/FileHelper.hpp"

TEST_CASE("Load glTF Skinning Data", "[Assets][glTF]")
{
    Utilities::Package::FPackageFileSystem pakSys(Utilities::Package::EPM_OsFile);
    
    std::string filename = "assets/models/rig.glb";
    
    Assets::EnvironmentSetting camera;
    std::vector<std::shared_ptr<Assets::Node>> nodes;
    std::vector<Assets::Model> models;
    std::vector<Assets::FMaterial> materials;
    std::vector<Assets::LightObject> lights;
    std::vector<Assets::AnimationTrack> tracks;
    std::vector<Assets::Skeleton> skeletons;
    
    bool result = Assets::FSceneLoader::LoadGLTFScene(filename, camera, nodes, models, materials, lights, tracks, skeletons);
    REQUIRE(result);
    REQUIRE(!models.empty());
    REQUIRE(models[0].CPUJoints().size() > 0);
    REQUIRE(models[0].CPUWeights().size() > 0);
}

#include "Engine/Runtime/Components/SkinnedMeshComponent.h"

TEST_CASE("SkinnedMeshComponent Animation Playback", "[Runtime][Animation]") {
    Assets::Skeleton skeleton;
    Assets::Joint joint;
    joint.Name = "Joint1";
    joint.ParentIndex = -1;
    joint.InverseBindMatrix = glm::mat4(1.0f);
    skeleton.Joints.push_back(joint);
    
    Runtime::SkinnedMeshComponent component(skeleton);
    
    std::vector<Assets::AnimationTrack> tracks;
    Assets::AnimationTrack track;
    track.AnimationName = "TestAnim";
    track.NodeName_ = "Joint1";
    track.Duration_ = 1.0f;
    
    // Add two keyframes for translation
    track.TranslationChannel.Keys.push_back({0.0f, glm::vec3(0,0,0)});
    track.TranslationChannel.Keys.push_back({1.0f, glm::vec3(1,0,0)});
    tracks.push_back(track);
    
    component.AddAnimations(tracks);
    
    SECTION("Play Animation") {
        component.PlayAnimation("TestAnim", false);
        REQUIRE(component.GetCurrentAnimationName() == "TestAnim");
        
        // Update to 0.5s
        component.Update(0.5f);
        auto matrices = component.GetJointMatrices();
        REQUIRE(matrices.size() == 1);
        // At 0.5s, translation should be (0.5, 0, 0)
        // Matrix should be translate(0.5, 0, 0) * IBM(identity)
        // Margin accommodates ozz 16-bit keyframe compression (~1e-4 worst case).
        REQUIRE(matrices[0][3][0] == Catch::Approx(0.5f).margin(0.001f));
    }

    SECTION("Looping Animation") {
        component.PlayAnimation("TestAnim", true);
        component.Update(1.5f); // Should loop to 0.5s
        auto matrices = component.GetJointMatrices();
        REQUIRE(matrices[0][3][0] == Catch::Approx(0.5f).margin(0.001f));
    }
}

TEST_CASE("SkinnedMeshComponent Animation CrossFade", "[Runtime][Animation]") {
    Assets::Skeleton skeleton;
    Assets::Joint joint;
    joint.Name = "Joint1";
    joint.ParentIndex = -1;
    joint.InverseBindMatrix = glm::mat4(1.0f);
    skeleton.Joints.push_back(joint);

    Runtime::SkinnedMeshComponent component(skeleton);

    std::vector<Assets::AnimationTrack> tracks;

    Assets::AnimationTrack idleTrack;
    idleTrack.AnimationName = "Idle";
    idleTrack.NodeName_ = "Joint1";
    idleTrack.Duration_ = 1.0f;
    idleTrack.TranslationChannel.Keys.push_back({0.0f, glm::vec3(0, 0, 0)});
    idleTrack.TranslationChannel.Keys.push_back({1.0f, glm::vec3(0, 0, 0)});
    tracks.push_back(idleTrack);

    Assets::AnimationTrack runTrack;
    runTrack.AnimationName = "Run";
    runTrack.NodeName_ = "Joint1";
    runTrack.Duration_ = 1.0f;
    runTrack.TranslationChannel.Keys.push_back({0.0f, glm::vec3(10, 0, 0)});
    runTrack.TranslationChannel.Keys.push_back({1.0f, glm::vec3(10, 0, 0)});
    tracks.push_back(runTrack);

    component.AddAnimations(tracks);

    component.PlayAnimation("Idle", true);
    component.Update(0.05f);
    REQUIRE(component.GetJointMatrices()[0][3][0] == Catch::Approx(0.0f));

    component.PlayAnimation("Run", true);
    component.Update(0.06f);
    const float blendedValue = component.GetJointMatrices()[0][3][0];
    REQUIRE(blendedValue > 0.0f);
    REQUIRE(blendedValue < 10.0f);

    component.Update(0.06f);
    REQUIRE(component.GetJointMatrices()[0][3][0] == Catch::Approx(10.0f));
}
