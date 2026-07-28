#pragma once

#include "Engine/Assets/Data/RigAsset.hpp"

#include <glm/glm.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace ScadLibrary
{
    enum class EEditableRigChannel
    {
        Position,
        Rotation,
        Scale,
    };

    struct FEditableRigKey
    {
        float time = 0.0f;
        // SCAD authoring space: metres for position, XYZ degrees for rotation,
        // unitless XYZ for scale.
        glm::vec3 value{0.0f};
    };

    struct FEditableRigChannel
    {
        int32_t bone = -1;
        EEditableRigChannel type = EEditableRigChannel::Rotation;
        std::vector<FEditableRigKey> keys;
    };

    struct FEditableRigClip
    {
        std::string name;
        bool loop = true;
        float duration = 0.0f;
        std::vector<FEditableRigChannel> channels;
    };

    // A wardrobe-ready attachment record. The source is any catalogued SCAD
    // module; the transform is authored in SCAD local space and applied under
    // the selected rig bone.
    struct FEquipmentAttachment
    {
        std::string id = "equipment";
        std::string label = "装备";
        std::string bone = "bone_torso";
        std::string kitPath;
        std::string moduleName;
        std::string arguments;
        bool enabled = true;
        glm::vec3 translation{0.0f};
        glm::vec3 rotationDegrees{0.0f};
        glm::vec3 scale{1.0f};
    };

    class FCharacterWorkbench
    {
    public:
        bool CaptureRig(const std::string& sourcePath, const Assets::FRigAsset& asset, std::string& outError);
        bool ApplyToAsset(Assets::FRigAsset& asset, std::string& outError) const;
        bool SaveRig(std::string& outError);
        int FindClipIndex(std::string_view name) const;
        bool CreateClip(std::string name, bool loop, int& outIndex, std::string& outError);
        bool ReplaceClip(std::string_view name, FEditableRigClip clip, std::string& outError);
        bool RemoveClip(std::string_view name, std::string& outError);

        bool LoadEquipment(const std::string& equipmentPath, std::string& outError);
        bool SaveEquipment(std::string& outError);
        void SetDefaultEquipment(const std::string& kitPath);

        const std::string& SourcePath() const { return sourcePath_; }
        const std::string& EquipmentPath() const { return equipmentPath_; }
        std::vector<FEditableRigClip>& Clips() { return clips_; }
        const std::vector<FEditableRigClip>& Clips() const { return clips_; }
        std::vector<FEquipmentAttachment>& Equipment() { return equipment_; }
        const std::vector<FEquipmentAttachment>& Equipment() const { return equipment_; }

        void MarkRigDirty() { rigDirty_ = true; }
        void MarkEquipmentDirty() { equipmentDirty_ = true; }
        void CommitRigEdit();
        bool RigDirty() const { return rigDirty_; }
        bool EquipmentDirty() const { return equipmentDirty_; }

        static const char* ChannelName(EEditableRigChannel type);
        static glm::vec3 ScadPositionToEngine(const glm::vec3& value);
        static glm::quat ScadRotationToEngine(const glm::vec3& degrees);
        static glm::vec3 ScadScaleToEngine(const glm::vec3& value);
        static glm::vec3 EnginePositionToScad(const glm::vec3& value);
        static glm::vec3 EngineRotationToScad(const glm::quat& value);
        static glm::vec3 EngineScaleToScad(const glm::vec3& value);

    private:
        static void RecomputeDuration(FEditableRigClip& clip);

        std::string BuildAnimationOverride() const;

        std::string sourcePath_;
        std::string equipmentPath_;
        std::vector<std::string> boneNames_;
        std::vector<FEditableRigClip> clips_;
        std::vector<FEquipmentAttachment> equipment_;
        bool rigDirty_ = false;
        bool equipmentDirty_ = false;
    };
} // namespace ScadLibrary
