#pragma once

#include "KitCatalog.hpp"

#include "Engine/Assets/AssetsFwd.hpp"
#include "Engine/Assets/Data/RigAsset.hpp"
#include "Gameplay/Rig/RigInstance.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace ScadLibrary
{
    // Slot-based character assembler over the kit_char parts library: pick one
    // module per body slot (+ optional hair/hat/accessories and colors) and
    // generate a ScadRig-conformant character .scad (standard skeleton pivots
    // via ch_pivot_*(), clips via ch_clip_*()).
    class FCharacterDesigner
    {
    public:
        // Rebuilds the per-slot option lists from the kit_char kit (null = none
        // found); selections reset to the first option of each slot.
        void SetKit(const FKitInfo* kit);
        bool HasKit() const { return hasKit_; }

        const std::vector<FKitModuleInfo>& Heads() const { return heads_; }
        const std::vector<FKitModuleInfo>& Hairs() const { return hairs_; }
        const std::vector<FKitModuleInfo>& Hats() const { return hats_; }
        const std::vector<FKitModuleInfo>& Torsos() const { return torsos_; }
        const std::vector<FKitModuleInfo>& Arms() const { return arms_; }
        const std::vector<FKitModuleInfo>& Legs() const { return legs_; }
        const std::vector<FKitModuleInfo>& Accessories() const { return accs_; }

        // Selections (indices into the lists above; hair/hat -1 = none).
        int head = 0;
        int hair = 0;
        int hat = -1;
        int torso = 0;
        int arm = 0;
        int leg = 0;
        std::vector<uint8_t> accEnabled; // aligned with Accessories()

        float skinColor[3] = {0.92f, 0.76f, 0.62f};
        float hairColor[3] = {0.20f, 0.14f, 0.10f};

        // Emits the character .scad source; kitUsePath lands in `use <...>`
        // (absolute for workspace previews, ../lib/kit_char.scad for exports).
        std::string BuildSource(const std::string& kitUsePath) const;

    private:
        bool hasKit_ = false;
        std::vector<FKitModuleInfo> heads_;
        std::vector<FKitModuleInfo> hairs_;
        std::vector<FKitModuleInfo> hats_;
        std::vector<FKitModuleInfo> torsos_;
        std::vector<FKitModuleInfo> arms_;
        std::vector<FKitModuleInfo> legs_;
        std::vector<FKitModuleInfo> accs_;
    };

    // Single-instance rig preview for the designer viewport: loads the rig on
    // the CPU, injects part models/materials during the stage-scene rebuild,
    // instantiates the bone/part node tree once the scene is live and drives
    // the animator every tick (mirrors the CharacterPool injection contract).
    class FRigPreview
    {
    public:
        bool LoadRig(const std::string& scadPathAbs, std::string& outError,
                     std::vector<std::string>* outWarnings);

        // Only an active preview injects/instantiates; the browser and the
        // compose bench must deactivate before loading their own scenes.
        void SetActive(bool active) { active_ = active; }
        bool Active() const { return active_; }
        bool HasRig() const { return hasRig_; }
        const Assets::FRigAsset& Asset() const { return asset_; }

        void SetTint(const glm::vec3& tint); // live when the scene is bound
        void PlayClip(const std::string& clip); // "" = bind pose
        const std::string& CurrentClip() const { return clip_; }

        // Engine hooks, forwarded by ScadLibraryGameInstance.
        void InjectAssets(std::vector<Assets::Model>& models,
                          std::vector<Assets::FMaterial>& materials);
        void OnSceneLoaded(Assets::Scene& scene);
        void OnSceneUnloaded();
        void Tick(double deltaSeconds);

    private:
        bool active_ = false;
        bool hasRig_ = false;
        Assets::FRigAsset asset_;
        glm::vec3 tint_{0.30f, 0.52f, 0.75f};
        std::string clip_ = "idle";

        // Per-scene state (reset on unload).
        bool injected_ = false;
        std::vector<uint32_t> partModelIds_;
        std::vector<std::array<uint32_t, 16>> partMaterialIds_;
        uint32_t tintMaterialId_ = 0;
        Assets::Scene* scene_ = nullptr;
        NextGameplay::FRigAnimator animator_;
        bool animatorBound_ = false;
    };
}
