#pragma once

// ============================================================================
// FScadRig.h - Loads a rigid-body rig (bones + parts + clips) from a .scad
// file following the ScadRig conventions (see docs/ScadRig-Design.md):
//   * user modules named "bone_*" are bones; nesting = hierarchy; the
//     translate/rotate wrapped around a bone call is its bind-pose pivot.
//   * geometry directly inside a bone module (including non-bone helper
//     modules, which get collapsed) is rigidly bound to that bone.
//   * top-level "anim_*" variables hold animation clips as plain data lists:
//     [[boneName, "rot"|"pos"|"scale", [[t, [x,y,z]], ...]], ["loop", bool]?]
// Violations emit "SCADRIG:"-prefixed warnings and degrade gracefully.
// ============================================================================

#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "Engine/Assets/Data/RigAsset.hpp"
#include "Modules/ScadLoader/FScadTypes.h"

namespace Assets
{
    namespace scad
    {
        struct SceneEvalResult;
    }

    struct ScadRigLoadOptions
    {
        float scadToWorldScale = 1.0f;
        float smoothAngleDegrees = 35.0f;
        glm::vec4 tintPlaceholder{1.0f, 0.0f, 1.0f, 1.0f};
        std::string bonePrefix = "bone_";
        std::string animPrefix = "anim_";
    };

    class FScadRigLoader
    {
    public:
        static bool LoadRig(
            const std::string& filename,
            const ScadRigLoadOptions& options,
            FRigAsset& outAsset,
            std::string& outError,
            std::vector<std::string>* outWarnings = nullptr);

        // Builds a rig from an already-evaluated scene (unit-test entry point).
        static bool BuildRig(
            const scad::SceneEvalResult& evalResult,
            const ScadRigLoadOptions& options,
            FRigAsset& outAsset,
            std::string& outError,
            std::vector<std::string>* outWarnings = nullptr);
    };
} // namespace Assets
