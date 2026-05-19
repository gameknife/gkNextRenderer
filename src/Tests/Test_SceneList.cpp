#include <catch2/catch_all.hpp>

#include "Engine/Runtime/Scene/SceneList.hpp"

TEST_CASE("SceneList recognizes supported scene extensions", "[Unit][SceneList]")
{
    CHECK(SceneList::IsSupportedSceneExtension(".glb"));
    CHECK(SceneList::IsSupportedSceneExtension(".gltf"));
    CHECK(SceneList::IsSupportedSceneExtension(".ldr"));
    CHECK(SceneList::IsSupportedSceneExtension(".mpd"));

    CHECK(SceneList::IsSupportedSceneExtension(".GLB"));
    CHECK(SceneList::IsSupportedSceneExtension(".LDR"));

    CHECK_FALSE(SceneList::IsSupportedSceneExtension(".hdr"));
    CHECK_FALSE(SceneList::IsSupportedSceneExtension(".png"));
    CHECK_FALSE(SceneList::IsSupportedSceneExtension(""));
}
