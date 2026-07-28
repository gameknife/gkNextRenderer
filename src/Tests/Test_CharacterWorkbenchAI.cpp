#include <catch2/catch_test_macros.hpp>

#include "CharacterWorkbench.hpp"

using namespace ScadLibrary;

TEST_CASE("Character workbench rebuilds rig assets after adding a clip",
          "[AI][ScadLibrary][Rig]")
{
    Assets::FRigAsset asset;
    asset.bones.push_back({.name = "bone_root"});
    FCharacterWorkbench workbench;
    std::string error;
    REQUIRE(workbench.CaptureRig("test_character.scad", asset, error));

    int clipIndex = -1;
    REQUIRE(workbench.CreateClip("wave", false, clipIndex, error));
    REQUIRE(clipIndex == 0);
    FEditableRigChannel channel;
    channel.bone = 0;
    channel.type = EEditableRigChannel::Rotation;
    channel.keys = {{0.0f, {0.0f, 0.0f, -20.0f}}, {1.0f, {0.0f, 0.0f, 20.0f}}};
    workbench.Clips()[clipIndex].channels.push_back(std::move(channel));
    workbench.CommitRigEdit();

    REQUIRE(workbench.ApplyToAsset(asset, error));
    REQUIRE(asset.clips.size() == 1);
    REQUIRE(asset.clips[0].name == "wave");
    REQUIRE_FALSE(asset.clips[0].loop);
    REQUIRE(asset.clips[0].duration == 1.0f);
    REQUIRE(asset.clips[0].channels.size() == 1);
    REQUIRE(asset.clips[0].channels[0].rotation.Keys.size() == 2);
}
