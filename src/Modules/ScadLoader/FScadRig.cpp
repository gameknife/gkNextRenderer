#include "Modules/ScadLoader/FScadRig.h"

#include "Engine/Assets/Loaders/FProcModel.h"
#include "Modules/ScadLoader/FScadEvaluator.h"
#include "Modules/ScadLoader/FScadShared.h"

#include <algorithm>
#include <cstdint>
#include <map>
#include <unordered_set>

#include <fmt/format.h>
#include <spdlog/spdlog.h>

namespace Assets
{
    namespace
    {
        constexpr size_t kMaxSectionsPerPart = 16;

        uint32_t RigQuantizeColor(const glm::vec4& c)
        {
            auto q = [](float v) -> uint32_t
            {
                const float clamped = std::min(1.0f, std::max(0.0f, v));
                return static_cast<uint32_t>(clamped * 255.0f + 0.5f);
            };
            return (q(c.r) << 24) | (q(c.g) << 16) | (q(c.b) << 8) | q(c.a);
        }

        struct RigBucket
        {
            glm::vec4 color{0.78f, 0.78f, 0.78f, 1.0f};
            std::vector<glm::dvec3> tris; // SCAD space, baked into bone-pivot-local frame
        };

        class RigBuilder
        {
        public:
            RigBuilder(const ScadRigLoadOptions& options, FRigAsset& asset, std::vector<std::string>* warnings)
                : options_(options), asset_(asset), warnings_(warnings)
            {
            }

            bool Build(const scad::SceneEvalResult& evalResult, std::string& outError)
            {
                const scad::SceneNode* rootBone = nullptr;
                for (const scad::SceneNode& root : evalResult.roots)
                {
                    if (!IsBone(root.name))
                    {
                        Warn(fmt::format("top-level non-bone content '{}' is ignored (only the root bone call may "
                                         "appear at top level)", root.name));
                        continue;
                    }
                    if (rootBone)
                    {
                        Warn(fmt::format("multiple top-level bone calls; keeping '{}', ignoring '{}'",
                                         rootBone->name, root.name));
                        continue;
                    }
                    rootBone = &root;
                }

                if (!rootBone)
                {
                    outError = "SCADRIG: no top-level bone_* call found";
                    return false;
                }

                BuildBone(*rootBone, rootBone->localTransform, -1);
                ParseClips(evalResult);
                return true;
            }

        private:
            const ScadRigLoadOptions& options_;
            FRigAsset& asset_;
            std::vector<std::string>* warnings_ = nullptr;
            std::unordered_set<std::string> seenBones_;

            double Scale() const
            {
                return static_cast<double>(options_.scadToWorldScale > 0.0f ? options_.scadToWorldScale : 1.0f);
            }

            void Warn(const std::string& msg)
            {
                SPDLOG_WARN("SCADRIG: {}", msg);
                if (warnings_)
                {
                    warnings_->push_back(msg);
                }
            }

            bool IsBone(const std::string& name) const
            {
                return name.rfind(options_.bonePrefix, 0) == 0;
            }

            // Merges this node's meshes (transformed by accum into the owning
            // bone's pivot-local frame) and recurses into non-bone children;
            // bone children are collected for recursion with their accumulated
            // local transform.
            void CollapseInto(
                const scad::SceneNode& node,
                const glm::dmat4& accum,
                std::map<uint32_t, RigBucket>& buckets,
                std::vector<std::pair<const scad::SceneNode*, glm::dmat4>>& childBones)
            {
                for (const scad::SceneMeshBucket& mesh : node.meshes)
                {
                    RigBucket& bucket = buckets[RigQuantizeColor(mesh.color)];
                    bucket.color = mesh.color;
                    bucket.tris.reserve(bucket.tris.size() + mesh.tris.size());
                    for (const glm::dvec3& p : mesh.tris)
                    {
                        bucket.tris.push_back(glm::dvec3(accum * glm::dvec4(p, 1.0)));
                    }
                }

                for (const scad::SceneNode& child : node.children)
                {
                    if (IsBone(child.name))
                    {
                        childBones.emplace_back(&child, accum * child.localTransform);
                    }
                    else
                    {
                        CollapseInto(child, accum * child.localTransform, buckets, childBones);
                    }
                }
            }

            void BuildBone(const scad::SceneNode& boneNode, const glm::dmat4& pivotScad, int32_t parentIndex)
            {
                if (!seenBones_.insert(boneNode.name).second)
                {
                    Warn(fmt::format("bone module '{}' is called more than once; keeping the first call",
                                     boneNode.name));
                    return;
                }

                const int32_t boneIndex = static_cast<int32_t>(asset_.bones.size());
                FRigBone bone;
                bone.name = boneNode.name;
                bone.parent = parentIndex;
                scad::ScadLocalToEngineTRS(pivotScad, Scale(), bone.bindT, bone.bindR, bone.bindS);

                const glm::dmat3 linear(pivotScad);
                if (glm::determinant(linear) < 0.0)
                {
                    Warn(fmt::format("bone '{}' pivot contains a mirror; decomposition is unreliable "
                                     "(move mirror() inside the bone body)", boneNode.name));
                }
                else if (glm::any(glm::greaterThan(glm::abs(bone.bindS - glm::vec3(1.0f)), glm::vec3(1e-3f))))
                {
                    Warn(fmt::format("bone '{}' pivot contains scale; only translate/rotate are allowed "
                                     "around a bone call", boneNode.name));
                }

                asset_.bones.push_back(std::move(bone));
                if (parentIndex >= 0)
                {
                    asset_.bones[parentIndex].children.push_back(boneIndex);
                }

                std::map<uint32_t, RigBucket> buckets;
                std::vector<std::pair<const scad::SceneNode*, glm::dmat4>> childBones;
                CollapseInto(boneNode, glm::dmat4(1.0), buckets, childBones);
                BuildParts(boneIndex, buckets);

                for (const auto& [childNode, childPivot] : childBones)
                {
                    BuildBone(*childNode, childPivot, boneIndex);
                }
            }

            void BuildParts(int32_t boneIndex, const std::map<uint32_t, RigBucket>& buckets)
            {
                std::vector<const RigBucket*> nonEmpty;
                for (const auto& entry : buckets)
                {
                    if (!entry.second.tris.empty())
                    {
                        nonEmpty.push_back(&entry.second);
                    }
                }

                const uint32_t tintKey = RigQuantizeColor(options_.tintPlaceholder);
                for (size_t start = 0; start < nonEmpty.size(); start += kMaxSectionsPerPart)
                {
                    const size_t end = std::min(nonEmpty.size(), start + kMaxSectionsPerPart);

                    FRigPart part;
                    part.bone = boneIndex;

                    std::vector<Vertex> vertices;
                    std::vector<uint32_t> indices;
                    uint32_t sectionIndex = 0;
                    for (size_t b = start; b < end; ++b)
                    {
                        const RigBucket& bucket = *nonEmpty[b];
                        std::vector<glm::vec3> localPos(bucket.tris.size());
                        for (size_t i = 0; i < localPos.size(); ++i)
                        {
                            localPos[i] = scad::ScadToWorldPos(bucket.tris[i], Scale());
                        }
                        const std::vector<glm::vec3> normals =
                            scad::ScadComputeSmoothNormals(localPos, options_.smoothAngleDegrees);

                        const uint32_t vertexOffset = static_cast<uint32_t>(vertices.size());
                        vertices.reserve(vertices.size() + localPos.size());
                        indices.reserve(indices.size() + localPos.size());
                        for (uint32_t i = 0; i < static_cast<uint32_t>(localPos.size()); ++i)
                        {
                            vertices.push_back(Vertex{localPos[i], normals[i], glm::vec4(1, 0, 0, 1),
                                                      glm::vec2(0, 0), sectionIndex});
                            indices.push_back(vertexOffset + i);
                        }

                        part.sectionColors.push_back(bucket.color);
                        part.sectionTintable.push_back(RigQuantizeColor(bucket.color) == tintKey);
                        ++sectionIndex;
                    }

                    if (vertices.empty())
                    {
                        continue;
                    }

                    Model model = FProcModel::CreateFromBuffers(
                        fmt::format("{}__part_{}", asset_.bones[boneIndex].name, start / kMaxSectionsPerPart),
                        std::move(vertices),
                        std::move(indices),
                        false);
                    model.SetSectionCount(sectionIndex);

                    part.modelIndex = static_cast<int32_t>(asset_.partModels.size());
                    asset_.partModels.push_back(std::move(model));
                    asset_.parts.push_back(std::move(part));
                }
            }

            // ---------------- Animation clips ----------------

            void ParseClips(const scad::SceneEvalResult& evalResult)
            {
                for (const auto& [varName, value] : evalResult.topLevelVariables)
                {
                    if (varName.rfind(options_.animPrefix, 0) != 0)
                    {
                        continue;
                    }
                    const std::string clipName = varName.substr(options_.animPrefix.size());
                    if (clipName.empty())
                    {
                        continue;
                    }
                    if (!value.IsVec())
                    {
                        Warn(fmt::format("clip '{}' is not a list; ignored", clipName));
                        continue;
                    }

                    FRigClip clip;
                    clip.name = clipName;
                    std::map<int32_t, size_t> channelByBone;

                    for (const scad::Value& row : value.vec)
                    {
                        ParseClipRow(clip, channelByBone, row);
                    }

                    asset_.clips.push_back(std::move(clip));
                }
            }

            void ParseClipRow(FRigClip& clip, std::map<int32_t, size_t>& channelByBone, const scad::Value& row)
            {
                if (!row.IsVec() || row.vec.empty())
                {
                    Warn(fmt::format("clip '{}': malformed row; ignored", clip.name));
                    return;
                }

                // Metadata row: ["loop", bool]
                if (row.vec.size() == 2 && row.vec[0].type == scad::Value::Type::Str && row.vec[0].str == "loop")
                {
                    clip.loop = row.vec[1].IsTruthy();
                    return;
                }

                if (row.vec.size() != 3 ||
                    row.vec[0].type != scad::Value::Type::Str ||
                    row.vec[1].type != scad::Value::Type::Str ||
                    !row.vec[2].IsVec())
                {
                    Warn(fmt::format("clip '{}': channel row must be [bone, channel, keys]; ignored", clip.name));
                    return;
                }

                const std::string& boneName = row.vec[0].str;
                const std::string& channelName = row.vec[1].str;
                const int32_t boneIndex = asset_.FindBone(boneName);
                if (boneIndex < 0)
                {
                    Warn(fmt::format("clip '{}': unknown bone '{}'; channel dropped", clip.name, boneName));
                    return;
                }
                if (channelName != "rot" && channelName != "pos" && channelName != "scale")
                {
                    Warn(fmt::format("clip '{}': unknown channel '{}' on bone '{}'; ignored",
                                     clip.name, channelName, boneName));
                    return;
                }

                auto found = channelByBone.find(boneIndex);
                if (found == channelByBone.end())
                {
                    FRigChannel channel;
                    channel.bone = boneIndex;
                    found = channelByBone.emplace(boneIndex, clip.channels.size()).first;
                    clip.channels.push_back(std::move(channel));
                }
                FRigChannel& channel = clip.channels[found->second];

                float lastTime = -1.0f;
                for (const scad::Value& key : row.vec[2].vec)
                {
                    if (!key.IsVec() || key.vec.size() < 2 || !key.vec[0].IsNumber())
                    {
                        Warn(fmt::format("clip '{}': malformed key on bone '{}'; key skipped",
                                         clip.name, boneName));
                        continue;
                    }
                    const float time = static_cast<float>(key.vec[0].num);
                    glm::dvec3 v(0.0);
                    if (!key.vec[1].AsVec3(v))
                    {
                        Warn(fmt::format("clip '{}': key value on bone '{}' is not a vec3; key skipped",
                                         clip.name, boneName));
                        continue;
                    }
                    if (time <= lastTime)
                    {
                        Warn(fmt::format("clip '{}': non-monotonic key time {} on bone '{}'; key skipped",
                                         clip.name, time, boneName));
                        continue;
                    }
                    lastTime = time;
                    clip.duration = std::max(clip.duration, time);

                    if (channelName == "pos")
                    {
                        channel.position.Keys.push_back({time, scad::ScadToWorldPos(v, Scale())});
                    }
                    else if (channelName == "rot")
                    {
                        // Convert euler degrees (OpenSCAD rotate semantics, Z-up)
                        // to an engine-space quaternion at load time; runtime
                        // interpolation is then a plain slerp.
                        glm::vec3 t(0.0f);
                        glm::quat r(1.0f, 0.0f, 0.0f, 0.0f);
                        glm::vec3 s(1.0f);
                        scad::ScadLocalToEngineTRS(scad::ScadRotateXYZ(v), Scale(), t, r, s);
                        channel.rotation.Keys.push_back({time, r});
                    }
                    else // scale
                    {
                        channel.scale.Keys.push_back({time, glm::vec3(static_cast<float>(v.x),
                                                                      static_cast<float>(v.z),
                                                                      static_cast<float>(v.y))});
                    }
                }
            }
        };
    } // namespace

    bool FScadRigLoader::LoadRig(
        const std::string& filename,
        const ScadRigLoadOptions& options,
        FRigAsset& outAsset,
        std::string& outError,
        std::vector<std::string>* outWarnings)
    {
        scad::ScadProgram program;
        if (!scad::LoadScadProgram(filename, program, outError))
        {
            return false;
        }

        ScadLoadOptions evalOptions;
        evalOptions.scadToWorldScale = options.scadToWorldScale;
        evalOptions.smoothAngleDegrees = options.smoothAngleDegrees;

        scad::SceneEvalResult result;
        if (!scad::ScadEvaluator::EvaluateScene(
                program.mainTopLevel, program.modules, program.functions, evalOptions, result, outError))
        {
            return false;
        }

        if (!BuildRig(result, options, outAsset, outError, outWarnings))
        {
            return false;
        }

        SPDLOG_INFO("SCADRIG: loaded {} -> {} bones, {} parts, {} clips",
                    filename, outAsset.bones.size(), outAsset.parts.size(), outAsset.clips.size());
        return true;
    }

    bool FScadRigLoader::BuildRig(
        const scad::SceneEvalResult& evalResult,
        const ScadRigLoadOptions& options,
        FRigAsset& outAsset,
        std::string& outError,
        std::vector<std::string>* outWarnings)
    {
        outAsset = FRigAsset{};
        RigBuilder builder(options, outAsset, outWarnings);
        return builder.Build(evalResult, outError);
    }
} // namespace Assets
