#include "CharacterWorkbench.hpp"

#include "Modules/ScadLoader/FScadShared.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>

#include <fmt/format.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <nlohmann/json.hpp>

namespace ScadLibrary
{
    namespace
    {
        constexpr const char* kOverrideBegin = "// SCADLIBRARY_RIG_EDITOR_BEGIN";
        constexpr const char* kOverrideEnd = "// SCADLIBRARY_RIG_EDITOR_END";
        constexpr double kRadToDeg = 57.295779513082320876;

        std::string ReadTextFile(const std::filesystem::path& path)
        {
            std::ifstream in(path, std::ios::binary);
            if (!in)
            {
                return {};
            }
            std::ostringstream stream;
            stream << in.rdbuf();
            return stream.str();
        }

        bool WriteTextFileSafely(const std::filesystem::path& path, const std::string& source, std::string& outError)
        {
            const std::filesystem::path tempPath = path.string() + ".scadlibrary.tmp";
            {
                std::ofstream out(tempPath, std::ios::binary | std::ios::trunc);
                if (!out)
                {
                    outError = fmt::format("无法写入临时文件 {}", tempPath.string());
                    return false;
                }
                out << source;
                if (!out.good())
                {
                    outError = fmt::format("写入临时文件失败 {}", tempPath.string());
                    return false;
                }
            }

            std::error_code ec;
            std::filesystem::copy_file(tempPath, path, std::filesystem::copy_options::overwrite_existing, ec);
            if (ec)
            {
                outError = fmt::format("替换文件失败 {}: {}", path.string(), ec.message());
                std::error_code cleanupError;
                std::filesystem::remove(tempPath, cleanupError);
                return false;
            }
            std::error_code cleanupError;
            std::filesystem::remove(tempPath, cleanupError);
            return true;
        }

        nlohmann::json Vec3ToJson(const glm::vec3& value) { return nlohmann::json::array({value.x, value.y, value.z}); }

        glm::vec3 Vec3FromJson(const nlohmann::json& value, const glm::vec3& fallback)
        {
            if (!value.is_array() || value.size() != 3)
            {
                return fallback;
            }
            return glm::vec3(value[0].get<float>(), value[1].get<float>(), value[2].get<float>());
        }
    } // namespace

    const char* FCharacterWorkbench::ChannelName(EEditableRigChannel type)
    {
        switch (type)
        {
        case EEditableRigChannel::Position:
            return "pos";
        case EEditableRigChannel::Rotation:
            return "rot";
        case EEditableRigChannel::Scale:
            return "scale";
        }
        return "rot";
    }

    glm::vec3 FCharacterWorkbench::ScadPositionToEngine(const glm::vec3& value)
    {
        return Assets::Scad::ScadToWorldPos(glm::dvec3(value), 1.0);
    }

    glm::quat FCharacterWorkbench::ScadRotationToEngine(const glm::vec3& degrees)
    {
        glm::vec3 translation(0.0f);
        glm::quat rotation(1.0f, 0.0f, 0.0f, 0.0f);
        glm::vec3 scale(1.0f);
        Assets::Scad::ScadLocalToEngineTRS(Assets::Scad::ScadRotateXYZ(glm::dvec3(degrees)), 1.0, translation, rotation,
                                           scale);
        return rotation;
    }

    glm::vec3 FCharacterWorkbench::ScadScaleToEngine(const glm::vec3& value)
    {
        return glm::vec3(value.x, value.z, value.y);
    }

    glm::vec3 FCharacterWorkbench::EnginePositionToScad(const glm::vec3& value)
    {
        return glm::vec3(value.x, -value.z, value.y);
    }

    glm::vec3 FCharacterWorkbench::EngineScaleToScad(const glm::vec3& value)
    {
        return glm::vec3(value.x, value.z, value.y);
    }

    glm::vec3 FCharacterWorkbench::EngineRotationToScad(const glm::quat& value)
    {
        const glm::dmat4 basis = Assets::Scad::ScadToWorldBasis(1.0);
        const glm::dmat4 engineRotation(glm::mat4_cast(glm::normalize(value)));
        const glm::dmat4 scadRotation = glm::inverse(basis) * engineRotation * basis;

        // Inverse of OpenSCAD's Rz * Ry * Rx convention.
        const double y = std::asin(std::clamp(-scadRotation[0][2], -1.0, 1.0));
        const double cy = std::cos(y);
        double x = 0.0;
        double z = 0.0;
        if (std::abs(cy) > 1e-7)
        {
            x = std::atan2(scadRotation[1][2], scadRotation[2][2]);
            z = std::atan2(scadRotation[0][1], scadRotation[0][0]);
        }
        else
        {
            x = std::atan2(-scadRotation[2][1], scadRotation[1][1]);
        }
        return glm::vec3(static_cast<float>(x * kRadToDeg), static_cast<float>(y * kRadToDeg),
                         static_cast<float>(z * kRadToDeg));
    }

    void FCharacterWorkbench::RecomputeDuration(FEditableRigClip& clip)
    {
        clip.duration = 0.0f;
        for (const FEditableRigChannel& channel : clip.channels)
        {
            for (const FEditableRigKey& key : channel.keys)
            {
                clip.duration = std::max(clip.duration, key.time);
            }
        }
    }

    void FCharacterWorkbench::CommitRigEdit()
    {
        for (FEditableRigClip& clip : clips_)
        {
            for (FEditableRigChannel& channel : clip.channels)
            {
                std::sort(channel.keys.begin(), channel.keys.end(),
                          [](const FEditableRigKey& lhs, const FEditableRigKey& rhs) { return lhs.time < rhs.time; });
                float previousTime = -0.0001f;
                for (FEditableRigKey& key : channel.keys)
                {
                    key.time = std::max(key.time, previousTime + 0.0001f);
                    previousTime = key.time;
                }
            }
            RecomputeDuration(clip);
        }
        rigDirty_ = true;
    }

    bool FCharacterWorkbench::CaptureRig(const std::string& sourcePath, const Assets::FRigAsset& asset,
                                         std::string& outError)
    {
        if (asset.bones.empty())
        {
            outError = "角色没有骨架";
            return false;
        }

        sourcePath_ = std::filesystem::absolute(sourcePath).string();
        equipmentPath_ = std::filesystem::path(sourcePath_).replace_extension(".equipment.json").string();
        boneNames_.clear();
        boneNames_.reserve(asset.bones.size());
        for (const Assets::FRigBone& bone : asset.bones)
        {
            boneNames_.push_back(bone.name);
        }
        clips_.clear();
        clips_.reserve(asset.clips.size());
        for (const Assets::FRigClip& sourceClip : asset.clips)
        {
            FEditableRigClip clip;
            clip.name = sourceClip.name;
            clip.loop = sourceClip.loop;
            for (const Assets::FRigChannel& sourceChannel : sourceClip.channels)
            {
                const auto append = [&](EEditableRigChannel type, const auto& keys, auto convert)
                {
                    if (keys.empty())
                    {
                        return;
                    }
                    FEditableRigChannel channel;
                    channel.bone = sourceChannel.bone;
                    channel.type = type;
                    channel.keys.reserve(keys.size());
                    for (const auto& key : keys)
                    {
                        channel.keys.push_back({key.Time, convert(key.Value)});
                    }
                    clip.channels.push_back(std::move(channel));
                };
                append(EEditableRigChannel::Position, sourceChannel.position.Keys, EnginePositionToScad);
                append(EEditableRigChannel::Rotation, sourceChannel.rotation.Keys, EngineRotationToScad);
                append(EEditableRigChannel::Scale, sourceChannel.scale.Keys, EngineScaleToScad);
            }
            RecomputeDuration(clip);
            clips_.push_back(std::move(clip));
        }
        rigDirty_ = false;
        outError.clear();
        return true;
    }

    bool FCharacterWorkbench::ApplyToAsset(Assets::FRigAsset& asset, std::string& outError) const
    {
        if (asset.clips.size() != clips_.size())
        {
            outError = "动作数量已变化，请从磁盘重新载入角色";
            return false;
        }

        for (size_t clipIndex = 0; clipIndex < clips_.size(); ++clipIndex)
        {
            const FEditableRigClip& sourceClip = clips_[clipIndex];
            Assets::FRigClip& clip = asset.clips[clipIndex];
            if (clip.name != sourceClip.name)
            {
                outError = "动作顺序已变化，请从磁盘重新载入角色";
                return false;
            }
            clip.loop = sourceClip.loop;
            clip.duration = 0.0f;
            clip.channels.clear();

            for (const FEditableRigChannel& sourceChannel : sourceClip.channels)
            {
                auto found =
                    std::find_if(clip.channels.begin(), clip.channels.end(), [&](const Assets::FRigChannel& channel)
                                 { return channel.bone == sourceChannel.bone; });
                if (found == clip.channels.end())
                {
                    Assets::FRigChannel channel;
                    channel.bone = sourceChannel.bone;
                    clip.channels.push_back(std::move(channel));
                    found = std::prev(clip.channels.end());
                }

                std::vector<FEditableRigKey> sorted = sourceChannel.keys;
                std::sort(sorted.begin(), sorted.end(),
                          [](const FEditableRigKey& lhs, const FEditableRigKey& rhs) { return lhs.time < rhs.time; });
                float previousTime = -0.0001f;
                for (FEditableRigKey& key : sorted)
                {
                    key.time = std::max(key.time, previousTime + 0.0001f);
                    previousTime = key.time;
                    clip.duration = std::max(clip.duration, key.time);
                    if (sourceChannel.type == EEditableRigChannel::Position)
                    {
                        found->position.Keys.push_back({key.time, ScadPositionToEngine(key.value)});
                    }
                    else if (sourceChannel.type == EEditableRigChannel::Rotation)
                    {
                        found->rotation.Keys.push_back({key.time, ScadRotationToEngine(key.value)});
                    }
                    else
                    {
                        found->scale.Keys.push_back({key.time, ScadScaleToEngine(key.value)});
                    }
                }
            }
        }
        outError.clear();
        return true;
    }

    std::string FCharacterWorkbench::BuildAnimationOverride() const
    {
        std::string source;
        source += "\n";
        source += kOverrideBegin;
        source += "\n// 此区域由 ScadLibrary 角色工作室维护；手写内容请放在标记区之外。\n";
        for (const FEditableRigClip& clip : clips_)
        {
            source += fmt::format("\nanim_{} = [\n", clip.name);
            source += fmt::format("    [\"loop\", {}]", clip.loop ? "true" : "false");
            for (const FEditableRigChannel& channel : clip.channels)
            {
                source += fmt::format(",\n    [\"{}\", \"{}\", [",
                                      channel.bone >= 0 && channel.bone < static_cast<int32_t>(boneNames_.size())
                                          ? boneNames_[channel.bone]
                                          : "bone_invalid",
                                      ChannelName(channel.type));
                for (size_t keyIndex = 0; keyIndex < channel.keys.size(); ++keyIndex)
                {
                    const FEditableRigKey& key = channel.keys[keyIndex];
                    source += fmt::format("{}[{:.6g}, [{:.6g}, {:.6g}, {:.6g}]]", keyIndex == 0 ? "" : ", ", key.time,
                                          key.value.x, key.value.y, key.value.z);
                }
                source += "]]";
            }
            source += "\n];\n";
        }
        source += "\n";
        source += kOverrideEnd;
        source += "\n";
        return source;
    }

    bool FCharacterWorkbench::SaveRig(std::string& outError)
    {
        if (sourcePath_.empty())
        {
            outError = "没有打开角色文件";
            return false;
        }
        std::string source = ReadTextFile(sourcePath_);
        if (source.empty())
        {
            outError = fmt::format("无法读取 {}", sourcePath_);
            return false;
        }

        const size_t begin = source.find(kOverrideBegin);
        if (begin != std::string::npos)
        {
            const size_t endMarker = source.find(kOverrideEnd, begin);
            if (endMarker == std::string::npos)
            {
                outError = "SCADLibrary 动作覆盖区缺少结束标记，未保存";
                return false;
            }
            size_t end = endMarker + std::char_traits<char>::length(kOverrideEnd);
            if (end < source.size() && source[end] == '\r')
                ++end;
            if (end < source.size() && source[end] == '\n')
                ++end;
            source.erase(begin, end - begin);
        }

        const std::string overrideSource = BuildAnimationOverride();

        while (!source.empty() && (source.back() == '\n' || source.back() == '\r'))
        {
            source.pop_back();
        }
        source += "\n";
        source += overrideSource;
        if (!WriteTextFileSafely(sourcePath_, source, outError))
        {
            return false;
        }
        rigDirty_ = false;
        return true;
    }

    void FCharacterWorkbench::SetDefaultEquipment(const std::string& kitPath)
    {
        equipment_ = {
            {"helmet",
             "军帽",
             "bone_head",
             kitPath,
             "cw_item_helmet",
             "seed = 0",
             true,
             {0.0f, 0.0f, 0.27f},
             {0.0f, 0.0f, 0.0f},
             {1.0f, 1.0f, 1.0f}},
            {"backpack",
             "背包",
             "bone_torso",
             kitPath,
             "cw_item_backpack",
             "seed = 0",
             true,
             {0.0f, 0.18f, 0.30f},
             {0.0f, 0.0f, 0.0f},
             {0.75f, 0.75f, 0.75f}},
            {"primary",
             "主武器",
             "bone_weapon_socket",
             kitPath,
             "cw_wpn_ak",
             "seed = 0",
             true,
             {0.0f, 0.0f, 0.0f},
             {0.0f, 0.0f, -90.0f},
             {1.0f, 1.0f, 1.0f}},
        };
        equipmentDirty_ = false;
    }

    bool FCharacterWorkbench::LoadEquipment(const std::string& equipmentPath, std::string& outError)
    {
        equipmentPath_ = equipmentPath;
        std::ifstream in(equipmentPath_);
        if (!in)
        {
            outError = fmt::format("未找到 {}", equipmentPath_);
            return false;
        }
        try
        {
            const nlohmann::json root = nlohmann::json::parse(in);
            equipment_.clear();
            for (const nlohmann::json& item : root.value("attachments", nlohmann::json::array()))
            {
                FEquipmentAttachment attachment;
                attachment.id = item.value("id", "equipment");
                attachment.label = item.value("label", attachment.id);
                attachment.bone = item.value("bone", "bone_torso");
                std::filesystem::path kitPath = item.value("kit", "");
                if (!kitPath.empty() && !kitPath.is_absolute())
                {
                    std::error_code ec;
                    kitPath = std::filesystem::weakly_canonical(
                        std::filesystem::path(equipmentPath_).parent_path() / kitPath, ec);
                }
                attachment.kitPath = kitPath.string();
                attachment.moduleName = item.value("module", "");
                attachment.arguments = item.value("args", "");
                attachment.enabled = item.value("enabled", true);
                attachment.translation =
                    Vec3FromJson(item.value("translation", nlohmann::json::array()), glm::vec3(0.0f));
                attachment.rotationDegrees =
                    Vec3FromJson(item.value("rotation", nlohmann::json::array()), glm::vec3(0.0f));
                attachment.scale = Vec3FromJson(item.value("scale", nlohmann::json::array()), glm::vec3(1.0f));
                equipment_.push_back(std::move(attachment));
            }
        }
        catch (const std::exception& e)
        {
            outError = fmt::format("装备配置解析失败: {}", e.what());
            return false;
        }
        equipmentDirty_ = false;
        outError.clear();
        return true;
    }

    bool FCharacterWorkbench::SaveEquipment(std::string& outError)
    {
        if (equipmentPath_.empty())
        {
            outError = "没有装备配置路径";
            return false;
        }
        nlohmann::json attachments = nlohmann::json::array();
        for (const FEquipmentAttachment& attachment : equipment_)
        {
            std::error_code ec;
            std::filesystem::path kitPath =
                std::filesystem::relative(attachment.kitPath, std::filesystem::path(equipmentPath_).parent_path(), ec);
            if (ec)
            {
                kitPath = attachment.kitPath;
            }
            std::string portableKitPath = kitPath.generic_string();
            attachments.push_back({
                {"id", attachment.id},
                {"label", attachment.label},
                {"bone", attachment.bone},
                {"kit", portableKitPath},
                {"module", attachment.moduleName},
                {"args", attachment.arguments},
                {"enabled", attachment.enabled},
                {"translation", Vec3ToJson(attachment.translation)},
                {"rotation", Vec3ToJson(attachment.rotationDegrees)},
                {"scale", Vec3ToJson(attachment.scale)},
            });
        }
        const nlohmann::json root = {
            {"version", 1},
            {"rig", std::filesystem::path(sourcePath_).filename().string()},
            {"attachments", std::move(attachments)},
        };
        if (!WriteTextFileSafely(equipmentPath_, root.dump(2) + "\n", outError))
        {
            return false;
        }
        equipmentDirty_ = false;
        return true;
    }
} // namespace ScadLibrary
