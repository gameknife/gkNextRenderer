#include <catch2/catch_all.hpp>

#include "Engine/Runtime/Scene/SceneList.hpp"
#include "Modules/LDrawLoader/LDrawModule.hpp"
#include "Modules/ScadLoader/ScadModule.hpp"

TEST_CASE("Runtime::Scene::SceneList recognizes supported scene extensions", "[Unit][Runtime::Scene::SceneList]")
{
    // glTF is built into the core; other formats come from registered modules.
    Modules::LDraw::Register();
    Modules::Scad::Register();

    CHECK(Runtime::Scene::SceneList::IsSupportedSceneExtension(".glb"));
    CHECK(Runtime::Scene::SceneList::IsSupportedSceneExtension(".gltf"));
    CHECK(Runtime::Scene::SceneList::IsSupportedSceneExtension(".ldr"));
    CHECK(Runtime::Scene::SceneList::IsSupportedSceneExtension(".mpd"));
    CHECK(Runtime::Scene::SceneList::IsSupportedSceneExtension(".scad"));

    CHECK(Runtime::Scene::SceneList::IsSupportedSceneExtension(".GLB"));
    CHECK(Runtime::Scene::SceneList::IsSupportedSceneExtension(".LDR"));

    CHECK_FALSE(Runtime::Scene::SceneList::IsSupportedSceneExtension(".hdr"));
    CHECK_FALSE(Runtime::Scene::SceneList::IsSupportedSceneExtension(".png"));
    CHECK_FALSE(Runtime::Scene::SceneList::IsSupportedSceneExtension(""));
}
