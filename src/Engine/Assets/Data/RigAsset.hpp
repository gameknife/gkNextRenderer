#pragma once

// ============================================================================
// RigAsset.hpp - Format-agnostic rigid-body rig asset (no skinning).
//
// A rig is a bone hierarchy where each bone carries zero or more rigid parts
// (whole meshes that follow the bone node transform, Minecraft-entity style),
// plus a set of animation clips that offset bones from their bind pose:
//   L_final = L_bind * T(pos) * R(rot) * S(scale)
//
// Producers live in loader modules (e.g. ScadLoader's FScadRigLoader);
// consumers live in NextGameplay (FRigInstance / FRigAnimator). All data is
// already in engine space (Y-up) by the time it lands here.
// ============================================================================

#include <string>
#include <string_view>
#include <vector>

#include "Engine/Assets/Core/Model.hpp"
#include "Engine/Utilities/Glm.hpp"
#include <glm/gtc/quaternion.hpp>

namespace Assets
{
    struct FRigBone
    {
        std::string name;                 // e.g. "bone_torso"
        int32_t parent = -1;              // index into FRigAsset::bones, root = -1
        glm::vec3 bindT{0.0f};            // bind-pose local TRS (engine space)
        glm::quat bindR{1.0f, 0.0f, 0.0f, 0.0f};
        glm::vec3 bindS{1.0f};
        std::vector<int32_t> children;
    };

    // One renderable chunk of geometry rigidly bound to a bone.
    struct FRigPart
    {
        int32_t bone = -1;
        int32_t modelIndex = -1;                // index into FRigAsset::partModels
        std::vector<glm::vec4> sectionColors;   // baked color per mesh section
        std::vector<bool> sectionTintable;      // section hit the tint placeholder color
    };

    struct FRigChannel
    {
        int32_t bone = -1;
        AnimationChannel<glm::vec3> position;   // pivot-local offset (engine space)
        AnimationChannel<glm::quat> rotation;   // euler keys converted at load time
        AnimationChannel<glm::vec3> scale;
    };

    struct FRigClip
    {
        std::string name;
        float duration = 0.0f;
        bool loop = true;
        std::vector<FRigChannel> channels;
    };

    struct FRigAsset
    {
        std::vector<FRigBone> bones;      // bones[0] = root, parents before children
        std::vector<Model> partModels;    // staging storage until injected into a scene
        std::vector<FRigPart> parts;
        std::vector<FRigClip> clips;

        int32_t FindBone(std::string_view name) const
        {
            for (size_t i = 0; i < bones.size(); ++i)
            {
                if (bones[i].name == name)
                {
                    return static_cast<int32_t>(i);
                }
            }
            return -1;
        }

        const FRigClip* FindClip(std::string_view name) const
        {
            for (const FRigClip& clip : clips)
            {
                if (clip.name == name)
                {
                    return &clip;
                }
            }
            return nullptr;
        }
    };
} // namespace Assets
