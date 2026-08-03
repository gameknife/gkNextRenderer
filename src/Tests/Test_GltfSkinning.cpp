#include <catch2/catch_all.hpp>
#include "Modules/GltfLoader/FSceneLoader.h"
#include "Engine/Assets/Core/LightObject.hpp"
#include "Engine/Assets/Core/Model.hpp"
#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Runtime/Components/LightComponent.hpp"
#include "Engine/Assets/Data/Material.hpp"
#include <chrono>
#include <fstream>
#include <filesystem>

#include "Engine/Utilities/FileHelper.hpp"

namespace
{
    class ScopedGltfFile
    {
    public:
        ScopedGltfFile()
        {
            const auto suffix = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
            root_ = std::filesystem::temp_directory_path() / ("gk_area_light_" + suffix);
            std::filesystem::create_directories(root_);
        }

        ~ScopedGltfFile()
        {
            std::error_code error;
            std::filesystem::remove_all(root_, error);
        }

        std::filesystem::path Write(const std::string& contents) const
        {
            const std::filesystem::path path = root_ / "area_light.gltf";
            std::ofstream stream(path);
            REQUIRE(stream.is_open());
            stream << contents;
            return path;
        }

    private:
        std::filesystem::path root_;
    };
}

TEST_CASE("glTF area light stays bound to its node transform", "[Assets][glTF][AreaLight]")
{
    Utilities::Package::FPackageFileSystem pakSys(Utilities::Package::EPM_OsFile);
    ScopedGltfFile fixture;
    const std::filesystem::path filename = fixture.Write(R"json({
        "asset":{"version":"2.0"},
        "scene":0,
        "scenes":[{"nodes":[0]}],
        "nodes":[
            {"name":"parent","translation":[10,2,3],"children":[1]},
            {"name":"area","translation":[1,0,0],"mesh":0,"extras":{"arealight":1}}
        ],
        "meshes":[{"primitives":[
            {"attributes":{"POSITION":0,"NORMAL":1},"indices":2,"material":0},
            {"attributes":{"POSITION":0,"NORMAL":1},"indices":2,"material":1}
        ]}],
        "materials":[
            {"name":"emitter","emissiveFactor":[1,1,1],"pbrMetallicRoughness":{"roughnessFactor":0.5}},
            {"name":"frame","pbrMetallicRoughness":{"baseColorFactor":[0.5,0.5,0.5,1],"roughnessFactor":0.5}}
        ],
        "buffers":[{"byteLength":108,"uri":"data:application/octet-stream;base64,PQpXvgAAAABI4fo+PQpXvgAAAABI4fq+PQpXPgAAAABI4fq+PQpXPgAAAABI4fo+AAAAAAAAgL8AAAAAAAAAAAAAgL8AAAAAAAAAAAAAgL8AAAAAAAAAAAAAgL8AAAAAAAABAAIAAAACAAMA"}],
        "bufferViews":[
            {"buffer":0,"byteOffset":0,"byteLength":48,"target":34962},
            {"buffer":0,"byteOffset":48,"byteLength":48,"target":34962},
            {"buffer":0,"byteOffset":96,"byteLength":12,"target":34963}
        ],
        "accessors":[
            {"bufferView":0,"componentType":5126,"count":4,"type":"VEC3","min":[-0.21,0,-0.49],"max":[0.21,0,0.49]},
            {"bufferView":1,"componentType":5126,"count":4,"type":"VEC3"},
            {"bufferView":2,"componentType":5123,"count":6,"type":"SCALAR"}
        ]
    })json");

    Assets::EnvironmentSetting camera;
    std::vector<std::shared_ptr<Assets::Node>> nodes;
    std::vector<Assets::Model> models;
    std::vector<Assets::FMaterial> materials;
    std::vector<Assets::LightObject> lights;
    std::vector<Assets::AnimationTrack> tracks;
    std::vector<Assets::Skeleton> skeletons;

    REQUIRE(Assets::FSceneLoader::LoadGLTFScene(filename.string(), camera, nodes, models, materials, lights,
                                                tracks, skeletons));
    CHECK(lights.empty());
    const auto areaNode = std::find_if(nodes.begin(), nodes.end(), [](const auto& node)
    {
        return node && node->GetName() == "area";
    });
    REQUIRE(areaNode != nodes.end());
    const auto lightComponent = (*areaNode)->GetComponent<Runtime::LightComponent>();
    REQUIRE(lightComponent);
    REQUIRE(lightComponent->Lights().size() == 1);
    const Assets::LightObject& light = lightComponent->Lights().front();

    const Assets::LightObject worldLight =
        Assets::LightObjects::Transform(light, (*areaNode)->WorldTransform());
    CHECK(light.lightMatIdx == 0);
    CHECK(worldLight.p0.x == Catch::Approx(10.79f));
    CHECK(worldLight.p0.y == Catch::Approx(2.0f));
    CHECK(worldLight.p0.z == Catch::Approx(3.49f));
    CHECK(worldLight.normal_area.x == Catch::Approx(0.0f).margin(1.0e-6f));
    CHECK(worldLight.normal_area.y == Catch::Approx(-1.0f).margin(1.0e-6f));
    CHECK(worldLight.normal_area.z == Catch::Approx(0.0f).margin(1.0e-6f));
    CHECK(worldLight.normal_area.w == Catch::Approx(0.4116f).margin(1.0e-5f));
}

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

TEST_CASE("glTF cameras use node camera indices and standard FOV", "[Assets][glTF][Camera]")
{
    Utilities::Package::FPackageFileSystem pakSys(Utilities::Package::EPM_OsFile);
    ScopedGltfFile fixture;
    const std::filesystem::path filename = fixture.Write(R"json({
        "asset":{"version":"2.0"},
        "scene":0,
        "scenes":[{"nodes":[0,1]}],
        "nodes":[
            {"name":"second","camera":1},
            {"name":"first","camera":0}
        ],
        "cameras":[
            {"name":"camera-zero","type":"perspective","perspective":{"yfov":0.5,"znear":0.1,"zfar":100.0}},
            {"name":"camera-one","type":"perspective","perspective":{"yfov":1.0,"znear":0.2,"zfar":200.0}},
            {"name":"unused-camera","type":"perspective","perspective":{"yfov":1.5,"znear":0.3,"zfar":300.0}}
        ]
    })json");

    Assets::EnvironmentSetting camera;
    std::vector<std::shared_ptr<Assets::Node>> nodes;
    std::vector<Assets::Model> models;
    std::vector<Assets::FMaterial> materials;
    std::vector<Assets::LightObject> lights;
    std::vector<Assets::AnimationTrack> tracks;
    std::vector<Assets::Skeleton> skeletons;

    REQUIRE(Assets::FSceneLoader::LoadGLTFScene(filename.string(), camera, nodes, models, materials, lights,
                                                tracks, skeletons));
    REQUIRE(camera.cameras.size() == 2);
    CHECK(camera.cameras[0].name == "1 second");
    CHECK(camera.cameras[0].FieldOfView == Catch::Approx(glm::degrees(1.0f)));
    CHECK(camera.cameras[0].NearPlane == Catch::Approx(0.2f));
    CHECK(camera.cameras[0].FarPlane == Catch::Approx(200.0f));
    CHECK(camera.cameras[1].name == "0 first");
    CHECK(camera.cameras[1].FieldOfView == Catch::Approx(glm::degrees(0.5f)));
}

TEST_CASE("glTF scene extras restore environment animation tracks", "[Assets][glTF][Animation][Sequencer]")
{
    Utilities::Package::FPackageFileSystem pakSys(Utilities::Package::EPM_OsFile);
    ScopedGltfFile fixture;
    const std::filesystem::path filename = fixture.Write(R"json({
        "asset":{"version":"2.0"},
        "scene":0,
        "scenes":[{
            "nodes":[0],
            "extras":{"gkEnvironmentTracks":[{
                "name":"Daylight",
                "duration":4.0,
                "playSpeed":0.5,
                "sunRotation":[{"time":4.0,"value":1.0},{"time":0.0,"value":-1.0}],
                "sunElevation":[{"time":0.0,"value":0.2}],
                "skyRotation":[],
                "sunIntensity":[{"time":0.0,"value":100.0},{"time":4.0,"value":900.0}],
                "skyIntensity":[],
                "sunColor":[{"time":0.0,"value":[1.0,0.5,0.25]}],
                "skyColor":[{"time":4.0,"value":[0.2,0.4,0.8]}]
            }]}
        }],
        "nodes":[{"name":"root"}]
    })json");

    Assets::EnvironmentSetting camera;
    std::vector<std::shared_ptr<Assets::Node>> nodes;
    std::vector<Assets::Model> models;
    std::vector<Assets::FMaterial> materials;
    std::vector<Assets::LightObject> lights;
    std::vector<Assets::AnimationTrack> tracks;
    std::vector<Assets::Skeleton> skeletons;

    REQUIRE(Assets::FSceneLoader::LoadGLTFScene(filename.string(), camera, nodes, models, materials, lights,
                                                tracks, skeletons));
    REQUIRE(tracks.size() == 1);
    const Assets::AnimationTrack& track = tracks.front();
    CHECK(track.Target_ == Assets::AnimationTrack::Target::Environment);
    CHECK(track.AnimationName == "Daylight");
    CHECK(track.Duration_ == Catch::Approx(4.0f));
    CHECK(track.PlaySpeed_ == Catch::Approx(0.5f));
    REQUIRE(track.SunRotationChannel.Keys.size() == 2);
    CHECK(track.SunRotationChannel.Keys[0].Time == Catch::Approx(0.0f));
    CHECK(track.SunRotationChannel.Keys[1].Time == Catch::Approx(4.0f));
    CHECK(track.SunColorChannel.Keys.front().Value.g == Catch::Approx(0.5f));
    CHECK(track.SkyColorChannel.Keys.front().Value.b == Catch::Approx(0.8f));
}

#include "Engine/Runtime/Components/SkinnedMeshComponent.hpp"

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
