#pragma once
// FScadEvaluator.Detail.h — private implementation header shared by the
// FScadEvaluator*.cpp translation units (statement evaluation stays inline
// here; geometry/argument methods live in FScadEvaluator.Geometry.cpp and
// expression/builtin methods in FScadEvaluator.Expr.cpp).
// Not part of the ScadLoader module interface.

#include "Modules/ScadLoader/FScadEvaluator.h"

#include "Modules/ScadLoader/FScadCsg.h"
#include "Modules/ScadLoader/FScadGeometry.h"
#include "Modules/ScadLoader/FScadTerrain.h"
#include "Modules/ScadLoader/FScadTess.h"
#include "Modules/ScadLoader/FScadText.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <functional>
#include <limits>
#include <memory>
#include <thread>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/matrix_transform.hpp>

#include <spdlog/spdlog.h>

namespace Assets::Scad
{
    namespace EvalDetail
    {
        // A piece of colored geometry, accumulated as a triangle soup in SCAD
        // (Z-up) world space. The loader converts these to engine Y-up vertices.
        struct ColoredSoup
        {
            glm::dvec4 color = glm::dvec4(0.78, 0.78, 0.78, 1.0);
            ScadMaterialProperties material;
            bool hasColor = false;
            bool faceted = false; // terrain: keep flat shading in the loader
            bool terrainWater = false; // terrain water surface (dedicated node)
            std::string materialName;
            std::string groupName;
            uint64_t groupInstanceId = 0;
            TriSoup soup;
        };
        using GeomList = std::vector<ColoredSoup>;

        inline uint32_t QuantizeColor(const glm::vec4& c)
        {
            auto q = [](float v) -> uint32_t
            {
                const float clamped = std::min(1.0f, std::max(0.0f, v));
                return static_cast<uint32_t>(clamped * 255.0f + 0.5f);
            };
            return (q(c.r) << 24) | (q(c.g) << 16) | (q(c.b) << 8) | q(c.a);
        }

        inline uint64_t QuantizeMaterial(const glm::vec4& c, const ScadMaterialProperties& material)
        {
            auto q = [](float v) -> uint64_t
            {
                const float clamped = std::min(1.0f, std::max(0.0f, v));
                return static_cast<uint64_t>(clamped * 65535.0f + 0.5f);
            };
            const float effectiveRoughness =
                c.a < 0.99f && !material.explicitParameters ? 0.0f : material.roughness;
            return (static_cast<uint64_t>(QuantizeColor(c)) << 32u) |
                   (q(effectiveRoughness) << 16u) |
                   q(material.metalness);
        }

        inline void AppendMove(GeomList& dst, GeomList&& src)
        {
            dst.insert(dst.end(), std::make_move_iterator(src.begin()), std::make_move_iterator(src.end()));
        }

        // Variable scope chain (dynamic scoping; pragmatic for the MVP).
        class Context
        {
        public:
            void Push() { frames_.emplace_back(); }
            void Pop() { frames_.pop_back(); }
            void Set(const std::string& name, const Value& v)
            {
                if (frames_.empty())
                    frames_.emplace_back();
                frames_.back()[name] = v;
            }
            const std::unordered_map<std::string, Value>& TopFrame() const
            {
                static const std::unordered_map<std::string, Value> kEmpty;
                return frames_.empty() ? kEmpty : frames_.back();
            }
            const Value* Get(const std::string& name) const
            {
                for (auto it = frames_.rbegin(); it != frames_.rend(); ++it)
                {
                    auto found = it->find(name);
                    if (found != it->end())
                        return &found->second;
                }
                return nullptr;
            }
            double GetNumber(const std::string& name, double fallback) const
            {
                const Value* v = Get(name);
                return v ? v->AsNumber(fallback) : fallback;
            }

        private:
            std::vector<std::unordered_map<std::string, Value>> frames_;
        };

        class Evaluator
        {
        public:
            Evaluator(const std::unordered_map<std::string, StmtPtr>& modules,
                      const std::unordered_map<std::string, StmtPtr>& functions, const ScadLoadOptions& options,
                      EvalResult& result) :
                modules_(modules), functions_(functions), options_(options), flatResult_(&result)
            {
            }

            Evaluator(const std::unordered_map<std::string, StmtPtr>& modules,
                      const std::unordered_map<std::string, StmtPtr>& functions, const ScadLoadOptions& options,
                      SceneEvalResult& result) :
                modules_(modules), functions_(functions), options_(options), sceneResult_(&result)
            {
            }

            void RunFlat(const Scope& topLevel)
            {
                InitGlobals();

                DefinitionFrameGuard definitionFrame(*this);
                RegisterLocalDefinitions(topLevel);

                GeomList geom;
                for (const StmtPtr& s : topLevel)
                {
                    if (!s)
                        continue;
                    if (s->kind == StmtKind::Assign)
                    {
                        ctx_.Set(s->name, EvalExpr(s->value));
                    }
                    else if (s->kind == StmtKind::Instance)
                    {
                        topLevelFallbackLabel_ = s->name;
                        topLevelFallbackInstanceId_ = nextGroupInstanceId_++;
                        AppendMove(geom, EvalInstance(*s, glm::dmat4(1.0), glm::dvec4(0.78, 0.78, 0.78, 1.0), false));
                        topLevelFallbackLabel_.clear();
                        topLevelFallbackInstanceId_ = 0;
                    }
                }
                EmitFlat(geom);
                ctx_.Pop();
            }

            void RunScene(const Scope& topLevel)
            {
                InitGlobals();

                DefinitionFrameGuard definitionFrame(*this);
                RegisterLocalDefinitions(topLevel);

                // A generated area is a run of independent top-level calls —
                // one per 1 km part — and evaluating a dense part costs
                // seconds, so a 5x5 is minutes of one core. When the tail of
                // the file is nothing but instances they can be evaluated
                // concurrently; see ParallelTopLevelStart for what "can" means.
                const size_t parallelFrom = ParallelTopLevelStart(topLevel);

                for (size_t i = 0; i < topLevel.size() && i < parallelFrom; ++i)
                {
                    const StmtPtr& s = topLevel[i];
                    if (!s)
                        continue;
                    if (s->kind == StmtKind::Assign)
                    {
                        ctx_.Set(s->name, EvalExpr(s->value));
                    }
                    else if (s->kind == StmtKind::Instance)
                    {
                        RunTopLevelInstance(*s);
                    }
                }
                if (parallelFrom < topLevel.size())
                {
                    RunTopLevelInstancesParallel(topLevel, parallelFrom);
                }

                // Snapshot the final top-level bindings (the global frame) so
                // data-only variables (anim_*, palettes, ...) survive evaluation.
                if (sceneResult_)
                {
                    for (const auto& entry : ctx_.TopFrame())
                    {
                        sceneResult_->topLevelVariables[entry.first] = entry.second;
                    }
                }

                FinalizeScene();
                ctx_.Pop();
            }

            // Evaluates one top-level instance into its own result, with a
            // copy of the caller's globals. Used only by the parallel path;
            // everything it touches is either its own state or immutable.
            void RunIsolatedTopLevel(const Stmt& instance,
                                     const std::unordered_map<std::string, Value>& globals,
                                     const Scope& topLevel,
                                     uint64_t idBase)
            {
                InitGlobals();
                for (const auto& entry : globals)
                {
                    ctx_.Set(entry.first, entry.second);
                }
                // Disjoint id ranges. Node ids have to stay unique across the
                // whole scene (the loader keys terrain payloads on them), and
                // value identities key a per-evaluator memo whose entries would
                // otherwise be shadowed by an identity minted in the parent.
                nextGroupInstanceId_ = idBase;
                nextValueIdentity_ = idBase;

                DefinitionFrameGuard definitionFrame(*this);
                RegisterLocalDefinitions(topLevel);
                RunTopLevelInstance(instance);
                FinalizeScene();
                ctx_.Pop();
            }

        private:
            void RunTopLevelInstance(const Stmt& instance)
            {
                topLevelFallbackLabel_ = instance.name;
                topLevelFallbackInstanceId_ = nextGroupInstanceId_++;
                currentTopLevelFallbackRoot_ = nullptr;
                EmitSceneGeometry(EvalInstance(instance, glm::dmat4(1.0), glm::dvec4(0.78, 0.78, 0.78, 1.0), false));
                currentTopLevelFallbackRoot_ = nullptr;
                topLevelFallbackLabel_.clear();
                topLevelFallbackInstanceId_ = 0;
            }

            // Index of the first top-level instance of a tail made up entirely
            // of instances, or topLevel.size() for "do not parallelise".
            //
            // The whole tail, not any run of instances: an instance evaluated
            // out of band produces its scene roots in its own result, and
            // interleaving those with roots the sequential path appended would
            // mean tracking an ordering key through both. A generated scene
            // puts every assignment first and every call last, which is the
            // case worth having.
            size_t ParallelTopLevelStart(const Scope& topLevel) const
            {
                const size_t none = topLevel.size();
                if (!options_.parallelTopLevel || !IsSceneMode())
                {
                    return none;
                }
                if (std::thread::hardware_concurrency() < 2)
                {
                    return none;
                }
                size_t first = none;
                size_t count = 0;
                for (size_t i = 0; i < topLevel.size(); ++i)
                {
                    const StmtPtr& s = topLevel[i];
                    if (!s)
                    {
                        continue;
                    }
                    if (s->kind != StmtKind::Instance)
                    {
                        // A statement of any other kind after an instance ends
                        // the tail, and with it the opportunity.
                        if (first != none)
                        {
                            return none;
                        }
                        continue;
                    }
                    if (first == none)
                    {
                        first = i;
                    }
                    ++count;
                }
                // Below this the thread handshake costs more than it saves, and
                // every ordinary scene stays on the path it has always taken.
                return count >= kMinParallelTopLevel ? first : none;
            }

            void RunTopLevelInstancesParallel(const Scope& topLevel, size_t from)
            {
                const size_t count = topLevel.size() - from;
                std::vector<SceneEvalResult> partials(count);
                // The globals frame is the shared input: the TERR literals, the
                // palettes, $fn. Copied per worker rather than shared, because a
                // worker's context is free to shadow them.
                const std::unordered_map<std::string, Value> globals = ctx_.TopFrame();

                std::atomic<size_t> cursor{0};
                const unsigned hardware = std::max(1u, std::thread::hardware_concurrency());
                const unsigned workers =
                    static_cast<unsigned>(std::min<size_t>(hardware, count));
                const auto run = [&]()
                {
                    for (;;)
                    {
                        const size_t k = cursor.fetch_add(1);
                        if (k >= count)
                        {
                            break;
                        }
                        const StmtPtr& s = topLevel[from + k];
                        if (!s)
                        {
                            continue;
                        }
                        Evaluator sub(modules_, functions_, options_, partials[k]);
                        sub.RunIsolatedTopLevel(*s, globals, topLevel, kParallelIdStride * (k + 1));
                    }
                };
                std::vector<std::thread> pool;
                pool.reserve(workers > 0 ? workers - 1 : 0);
                for (unsigned t = 1; t < workers; ++t)
                {
                    pool.emplace_back(run);
                }
                run();
                for (std::thread& worker : pool)
                {
                    worker.join();
                }

                // Merged in statement order, so the scene is identical whatever
                // order the workers finished in.
                for (SceneEvalResult& partial : partials)
                {
                    for (SceneNode& root : partial.roots)
                    {
                        parallelRoots_.push_back(std::move(root));
                    }
                    for (SceneTerrain& terrain : partial.terrains)
                    {
                        sceneResult_->terrains.push_back(std::move(terrain));
                    }
                    sceneResult_->warningCount += partial.warningCount;
                    sceneResult_->triangleCount += partial.triangleCount;
                }
            }

            // One worker's id range. Wide enough that no single top-level call
            // can run into the next worker's numbers.
            static constexpr uint64_t kParallelIdStride = 1ull << 40;
            static constexpr size_t kMinParallelTopLevel = 4;

            const std::unordered_map<std::string, StmtPtr>& modules_;
            const std::unordered_map<std::string, StmtPtr>& functions_;
            const ScadLoadOptions& options_;
            EvalResult* flatResult_ = nullptr;
            SceneEvalResult* sceneResult_ = nullptr;
            Context ctx_;
            int depth_ = 0;
            std::unordered_map<std::string, int> warnOnce_;
            std::vector<const Scope*> childrenStack_; // caller children for children()
            std::vector<std::unordered_map<std::string, StmtPtr>> localModules_;
            std::vector<std::unordered_map<std::string, StmtPtr>> localFunctions_;
            struct GroupFrame
            {
                std::string name;
                uint64_t instanceId = 0;
            };
            std::vector<GroupFrame> moduleCallStack_;
            std::vector<std::string> materialNameStack_;
            std::vector<ScadMaterialProperties> materialPropertiesStack_;
            std::string topLevelFallbackLabel_;
            uint64_t topLevelFallbackInstanceId_ = 0;
            uint64_t nextGroupInstanceId_ = 1;
            // Finished subtrees handed back by the parallel top-level workers.
            std::vector<SceneNode> parallelRoots_;
            uint64_t nextValueIdentity_ = 1;
            int suppressSceneNodes_ = 0;

            struct SceneNodeBuild
            {
                std::string name;
                uint64_t instanceId = 0;
                glm::dmat4 localTransform = glm::dmat4(1.0);
                int sourceLine = 0;
                std::vector<std::pair<std::string, Value>> parameters;
                glm::dvec4 callColor = glm::dvec4(0.78, 0.78, 0.78, 1.0);
                bool hasCallColor = false;
                std::map<uint64_t, SceneMeshBucket> meshes;
                std::vector<std::unique_ptr<SceneNodeBuild>> children;
            };
            std::vector<std::unique_ptr<SceneNodeBuild>> sceneRoots_;
            std::vector<SceneNodeBuild*> sceneOwnerStack_;
            SceneNodeBuild* currentTopLevelFallbackRoot_ = nullptr;

            struct DefinitionFrameGuard
            {
                explicit DefinitionFrameGuard(Evaluator& owner) : owner_(owner)
                {
                    owner_.localModules_.emplace_back();
                    owner_.localFunctions_.emplace_back();
                }
                DefinitionFrameGuard(const DefinitionFrameGuard&) = delete;
                DefinitionFrameGuard& operator=(const DefinitionFrameGuard&) = delete;

                ~DefinitionFrameGuard()
                {
                    owner_.localFunctions_.pop_back();
                    owner_.localModules_.pop_back();
                }

                Evaluator& owner_;
            };

            struct SuppressSceneNodeGuard
            {
                explicit SuppressSceneNodeGuard(Evaluator& owner) : owner_(owner) { ++owner_.suppressSceneNodes_; }

                ~SuppressSceneNodeGuard() { --owner_.suppressSceneNodes_; }

                Evaluator& owner_;
            };

            void Warn(const std::string& key, const std::string& msg)
            {
                if (flatResult_)
                    ++flatResult_->warningCount;
                if (sceneResult_)
                    ++sceneResult_->warningCount;
                int& count = warnOnce_[key];
                if (count < 3)
                {
                    SPDLOG_WARN("SCAD: {}", msg);
                    ++count;
                }
            }

            void InitGlobals()
            {
                ctx_.Push(); // global frame
                ctx_.Set("$fn", Value::MakeNumber(0.0));
                ctx_.Set("$fa", Value::MakeNumber(12.0));
                ctx_.Set("$fs", Value::MakeNumber(2.0));
                ctx_.Set("$t", Value::MakeNumber(0.0));
                ctx_.Set("PI", Value::MakeNumber(kPi));
            }

            void AddTriangleCount(size_t triangleCount)
            {
                if (flatResult_)
                    flatResult_->triangleCount += triangleCount;
                if (sceneResult_)
                    sceneResult_->triangleCount += triangleCount;
            }

            void EmitFlat(const GeomList& geom)
            {
                if (!flatResult_)
                    return;
                for (const ColoredSoup& cs : geom)
                {
                    const glm::vec4 c = cs.hasColor ? glm::vec4(cs.color) : glm::vec4(0.78f, 0.78f, 0.78f, 1.0f);
                    const std::string groupName = cs.groupName.empty() ? std::string("part") : cs.groupName;
                    const uint64_t groupInstanceId =
                        cs.groupInstanceId == 0 ? nextGroupInstanceId_++ : cs.groupInstanceId;
                    const BucketKey key{groupName, groupInstanceId, QuantizeMaterial(c, cs.material)};
                    ColorBucket& bucket = flatResult_->buckets[key];
                    bucket.color = c;
                    bucket.material = cs.material;
                    bucket.groupName = groupName;
                    bucket.faceted = bucket.faceted || cs.faceted;
                    bucket.tris.insert(bucket.tris.end(), cs.soup.begin(), cs.soup.end());
                    AddTriangleCount(cs.soup.size() / 3);
                }
            }

            bool IsSceneMode() const { return sceneResult_ != nullptr; }

            bool ShouldCreateSceneNodeForModule() const { return IsSceneMode() && suppressSceneNodes_ == 0; }

            SceneNodeBuild* CurrentSceneOwner() const
            {
                return sceneOwnerStack_.empty() ? nullptr : sceneOwnerStack_.back();
            }

            SceneNodeBuild* CreateSceneNode(const Stmt& definition, const Stmt& call, const glm::dmat4& localTransform,
                                            const glm::dvec4& color, bool hasColor)
            {
                auto node = std::make_unique<SceneNodeBuild>();
                node->name = definition.name.empty() ? "part" : definition.name;
                node->instanceId = nextGroupInstanceId_++;
                node->localTransform = localTransform;
                node->sourceLine = call.line;
                node->callColor = color;
                node->hasCallColor = hasColor;
                node->parameters.reserve(definition.params.size());
                for (const Param& parameter : definition.params)
                {
                    if (const Value* value = ctx_.Get(parameter.name))
                    {
                        node->parameters.emplace_back(parameter.name, *value);
                    }
                }

                SceneNodeBuild* raw = node.get();
                SceneNodeBuild* parent = CurrentSceneOwner();
                if (parent)
                {
                    parent->children.push_back(std::move(node));
                }
                else
                {
                    sceneRoots_.push_back(std::move(node));
                }
                return raw;
            }

            SceneNodeBuild* EnsureSceneFallbackRoot()
            {
                if (currentTopLevelFallbackRoot_)
                {
                    return currentTopLevelFallbackRoot_;
                }

                auto node = std::make_unique<SceneNodeBuild>();
                node->name = topLevelFallbackLabel_.empty() ? "part" : topLevelFallbackLabel_;
                node->instanceId =
                    topLevelFallbackInstanceId_ != 0 ? topLevelFallbackInstanceId_ : nextGroupInstanceId_++;
                node->localTransform = glm::dmat4(1.0);
                currentTopLevelFallbackRoot_ = node.get();
                sceneRoots_.push_back(std::move(node));
                return currentTopLevelFallbackRoot_;
            }

            void EmitSceneGeometry(const GeomList& geom)
            {
                if (!IsSceneMode() || geom.empty())
                {
                    return;
                }

                SceneNodeBuild* owner = CurrentSceneOwner();
                if (!owner)
                {
                    owner = EnsureSceneFallbackRoot();
                }

                for (const ColoredSoup& cs : geom)
                {
                    const glm::vec4 c = cs.hasColor ? glm::vec4(cs.color) : glm::vec4(0.78f, 0.78f, 0.78f, 1.0f);
                    SceneMeshBucket& bucket = owner->meshes[QuantizeMaterial(c, cs.material)];
                    bucket.color = c;
                    bucket.material = cs.material;
                    if (bucket.materialName.empty() && !cs.materialName.empty())
                    {
                        bucket.materialName = cs.materialName;
                    }
                    bucket.faceted = bucket.faceted || cs.faceted;
                    bucket.terrainWater = bucket.terrainWater || cs.terrainWater;
                    bucket.tris.insert(bucket.tris.end(), cs.soup.begin(), cs.soup.end());
                    AddTriangleCount(cs.soup.size() / 3);
                }
            }

            SceneNode FinalizeSceneNode(const SceneNodeBuild& build) const
            {
                SceneNode node;
                node.name = build.name;
                node.instanceId = build.instanceId;
                node.localTransform = build.localTransform;
                node.sourceLine = build.sourceLine;
                node.parameters = build.parameters;
                node.callColor = build.callColor;
                node.hasCallColor = build.hasCallColor;
                node.meshes.reserve(build.meshes.size());
                for (const auto& entry : build.meshes)
                {
                    node.meshes.push_back(entry.second);
                }
                node.children.reserve(build.children.size());
                for (const auto& child : build.children)
                {
                    node.children.push_back(FinalizeSceneNode(*child));
                }
                return node;
            }

            void FinalizeScene()
            {
                if (!sceneResult_)
                {
                    return;
                }

                sceneResult_->roots.clear();
                sceneResult_->roots.reserve(sceneRoots_.size() + parallelRoots_.size());
                for (const auto& root : sceneRoots_)
                {
                    sceneResult_->roots.push_back(FinalizeSceneNode(*root));
                }
                // Workers finalise their own subtrees; they are appended in
                // statement order (see RunTopLevelInstancesParallel).
                for (SceneNode& root : parallelRoots_)
                {
                    sceneResult_->roots.push_back(std::move(root));
                }
                parallelRoots_.clear();
            }

            std::string CurrentGroupLabel() const
            {
                if (!moduleCallStack_.empty())
                {
                    return moduleCallStack_.back().name;
                }
                if (!topLevelFallbackLabel_.empty())
                {
                    return topLevelFallbackLabel_;
                }
                return "part";
            }

            uint64_t CurrentGroupInstanceId() const
            {
                if (!moduleCallStack_.empty())
                {
                    return moduleCallStack_.back().instanceId;
                }
                if (topLevelFallbackInstanceId_ != 0)
                {
                    return topLevelFallbackInstanceId_;
                }
                return 0;
            }

            // --------------------------------------------------------------
            // Tree walk -> colored geometry
            // --------------------------------------------------------------
            GeomList EvalScope(const Scope& scope, const glm::dmat4& xform, const glm::dvec4& color, bool hasColor)
            {
                DefinitionFrameGuard definitionFrame(*this);
                RegisterLocalDefinitions(scope);

                GeomList out;
                for (const StmtPtr& s : scope)
                {
                    if (!s)
                        continue;
                    if (s->kind == StmtKind::Assign)
                    {
                        ctx_.Set(s->name, EvalExpr(s->value));
                    }
                    else if (s->kind == StmtKind::Instance)
                    {
                        AppendMove(out, EvalInstance(*s, xform, color, hasColor));
                    }
                }
                return out;
            }

            void RegisterLocalDefinitions(const Scope& scope)
            {
                for (const StmtPtr& s : scope)
                {
                    if (!s)
                        continue;
                    if (s->kind == StmtKind::ModuleDef)
                    {
                        localModules_.back()[s->name] = s;
                    }
                    else if (s->kind == StmtKind::FunctionDef)
                    {
                        localFunctions_.back()[s->name] = s;
                    }
                }
            }

            StmtPtr FindModule(const std::string& name) const
            {
                for (auto it = localModules_.rbegin(); it != localModules_.rend(); ++it)
                {
                    auto found = it->find(name);
                    if (found != it->end())
                        return found->second;
                }

                auto found = modules_.find(name);
                return found != modules_.end() ? found->second : nullptr;
            }

            StmtPtr FindFunction(const std::string& name) const
            {
                for (auto it = localFunctions_.rbegin(); it != localFunctions_.rend(); ++it)
                {
                    auto found = it->find(name);
                    if (found != it->end())
                        return found->second;
                }

                auto found = functions_.find(name);
                return found != functions_.end() ? found->second : nullptr;
            }

            GeomList EvalInstance(const Stmt& inst, const glm::dmat4& xform, glm::dvec4 color, bool hasColor)
            {
                if (inst.modifiers.find('*') != std::string::npos)
                {
                    return {}; // disabled subtree
                }

                const std::string& name = inst.name;

                if (name == "translate")
                {
                    glm::dvec3 t(0.0);
                    Arg(inst, "v", 0).AsVec3(t);
                    return EvalScope(inst.children, xform * glm::translate(glm::dmat4(1.0), t), color, hasColor);
                }
                if (name == "scale")
                {
                    glm::dvec3 s(1.0);
                    const Value v = Arg(inst, "v", 0);
                    if (v.IsNumber())
                        s = glm::dvec3(v.num);
                    else
                        v.AsVec3(s);
                    return EvalScope(inst.children, xform * glm::scale(glm::dmat4(1.0), s), color, hasColor);
                }
                if (name == "rotate")
                {
                    return EvalScope(inst.children, xform * BuildRotate(inst), color, hasColor);
                }
                if (name == "mirror")
                {
                    glm::dvec3 nrm(1.0, 0.0, 0.0);
                    Arg(inst, "v", 0).AsVec3(nrm);
                    return EvalScope(inst.children, xform * BuildMirror(nrm), color, hasColor);
                }
                if (name == "multmatrix")
                {
                    return EvalScope(inst.children, xform * BuildMultmatrix(inst), color, hasColor);
                }
                if (name == "color")
                {
                    glm::dvec4 c = color;
                    if (ResolveColor(inst, c))
                    {
                        materialNameStack_.push_back(ResolveColorName(inst));
                        GeomList result = EvalScope(inst.children, xform, c, true);
                        materialNameStack_.pop_back();
                        return result;
                    }
                    return EvalScope(inst.children, xform, color, hasColor);
                }
                if (name == "gk_material")
                {
                    glm::dvec4 c = color;
                    if (ResolveMaterialColor(inst, c))
                    {
                        ScadMaterialProperties material;
                        material.explicitParameters = true;
                        material.roughness = static_cast<float>(std::clamp(
                            Arg(inst, "roughness", 1).AsNumber(1.0), 0.0, 1.0));
                        material.metalness = static_cast<float>(std::clamp(
                            Arg(inst, "metalness", 2).AsNumber(0.0), 0.0, 1.0));
                        materialNameStack_.push_back(ResolveColorName(inst));
                        materialPropertiesStack_.push_back(material);
                        GeomList result = EvalScope(inst.children, xform, c, true);
                        materialPropertiesStack_.pop_back();
                        materialNameStack_.pop_back();
                        return result;
                    }
                    return EvalScope(inst.children, xform, color, hasColor);
                }
                if (name == "union" || name == "group" || name == "render")
                {
                    return EvalScope(inst.children, xform, color, hasColor);
                }
                if (name == "gk_flatten")
                {
                    // Declares "everything below me is one piece of geometry, not
                    // scene structure". A rule library that expands a data table
                    // into thousands of small module calls (kit_road turning a
                    // street network into per-segment slabs) would otherwise emit
                    // one scene Node per call: measured at 7683 nodes for a 1km
                    // city tile, which cost 1.2 s of physics shape cooking alone
                    // because every node becomes its own Model and collider.
                    SuppressSceneNodeGuard suppressGuard(*this);
                    return EvalScope(inst.children, xform, color, hasColor);
                }
                if (name == "minkowski")
                {
                    Warn("minkowski", "minkowski() approximated as union (no backend)");
                    return EvalScope(inst.children, xform, color, hasColor);
                }
                if (name == "difference")
                {
                    return EvalDifference(inst, xform, color, hasColor);
                }
                if (name == "intersection")
                {
                    return EvalIntersection(inst, xform, color, hasColor);
                }
                if (name == "hull")
                {
                    return EvalHull(inst, xform, color, hasColor);
                }
                if (name == "for" || name == "intersection_for")
                {
                    return EvalFor(inst, xform, color, hasColor);
                }
                if (name == "if")
                {
                    const Value cond = inst.args.empty() ? Value() : EvalExpr(inst.args[0].value);
                    return cond.IsTruthy() ? EvalScope(inst.children, xform, color, hasColor)
                                           : EvalScope(inst.elseChildren, xform, color, hasColor);
                }
                if (name == "let")
                {
                    ctx_.Push();
                    for (const CallArg& a : inst.args)
                    {
                        if (!a.name.empty())
                            ctx_.Set(a.name, EvalExpr(a.value));
                    }
                    GeomList g = EvalScope(inst.children, xform, color, hasColor);
                    ctx_.Pop();
                    return g;
                }
                if (name == "echo")
                {
                    std::string msg;
                    for (size_t i = 0; i < inst.args.size(); ++i)
                    {
                        if (i)
                            msg += ", ";
                        if (!inst.args[i].name.empty())
                            msg += inst.args[i].name + " = ";
                        msg += ValueToString(EvalExpr(inst.args[i].value));
                    }
                    SPDLOG_INFO("SCAD: ECHO: {}", msg);
                    return EvalScope(inst.children, xform, color, hasColor);
                }
                if (name == "assert")
                {
                    if (!inst.args.empty() && !EvalExpr(inst.args[0].value).IsTruthy())
                    {
                        Warn("assert", "assert() failed");
                    }
                    return EvalScope(inst.children, xform, color, hasColor);
                }
                if (name == "resize")
                {
                    Warn("resize", "resize() is treated as a no-op (children rendered unscaled)");
                    return EvalScope(inst.children, xform, color, hasColor);
                }
                if (name == "children")
                {
                    return EvalChildren(inst, xform, color, hasColor);
                }

                if (name == "cube" || name == "sphere" || name == "cylinder" || name == "polyhedron")
                {
                    return EvalPrimitive(inst, xform, color, hasColor);
                }
                if (name == "gk_terrain")
                {
                    return EvalTerrain(inst, xform, color, hasColor);
                }
                if (name == "linear_extrude")
                {
                    return EvalLinearExtrude(inst, xform, color, hasColor);
                }
                if (name == "rotate_extrude")
                {
                    return EvalRotateExtrude(inst, xform, color, hasColor);
                }
                if (name == "polygon" || name == "circle" || name == "square" || name == "text")
                {
                    Warn("2d", name + "() used outside linear_extrude is ignored");
                    return {};
                }

                StmtPtr found = FindModule(name);
                if (found)
                {
                    return CallUserModule(*found, inst, xform, color, hasColor);
                }

                Warn("unknown", "unknown module '" + name + "' (rendering its children verbatim)");
                return EvalScope(inst.children, xform, color, hasColor);
            }

            GeomList EvalDifference(const Stmt& inst, const glm::dmat4& xform, const glm::dvec4& color, bool hasColor)
            {
                SuppressSceneNodeGuard suppressGuard(*this);
                DefinitionFrameGuard definitionFrame(*this);
                RegisterLocalDefinitions(inst.children);

                std::vector<TriSoup> positives;
                std::vector<TriSoup> negatives;
                glm::dvec4 posColor = color;
                bool posHas = hasColor;
                ScadMaterialProperties posMaterial;
                std::string posMaterialName;
                std::string posGroupName = CurrentGroupLabel();
                uint64_t posGroupInstanceId = CurrentGroupInstanceId();
                bool gotColor = false;
                int instIdx = 0;
                for (const StmtPtr& s : inst.children)
                {
                    if (!s)
                        continue;
                    if (s->kind == StmtKind::Assign)
                    {
                        ctx_.Set(s->name, EvalExpr(s->value));
                        continue;
                    }
                    if (s->kind != StmtKind::Instance)
                        continue;
                    GeomList g = EvalInstance(*s, xform, color, hasColor);
                    if (instIdx == 0)
                    {
                        for (ColoredSoup& cs : g)
                        {
                            if (!gotColor)
                            {
                                posColor = cs.color;
                                posHas = cs.hasColor;
                                posMaterial = cs.material;
                                posMaterialName = cs.materialName;
                                posGroupName = cs.groupName.empty() ? posGroupName : cs.groupName;
                                posGroupInstanceId = cs.groupInstanceId == 0 ? posGroupInstanceId : cs.groupInstanceId;
                                gotColor = true;
                            }
                            positives.push_back(std::move(cs.soup));
                        }
                    }
                    else
                    {
                        for (ColoredSoup& cs : g)
                            negatives.push_back(std::move(cs.soup));
                    }
                    ++instIdx;
                }
                if (positives.empty())
                    return {};

                // Union the positive parts into one solid, then subtract.
                bool unionOk = false;
                TriSoup positiveSolid =
                    (positives.size() == 1) ? std::move(positives[0]) : ScadCsg::Union(positives, unionOk);

                bool ok = false;
                TriSoup r = ScadCsg::Difference(positiveSolid, negatives, ok);
                if (!ok && !ScadCsg::BackendAvailable())
                {
                    Warn("difference", "difference() approximated: only the first child is kept (no boolean backend)");
                }
                GeomList out;
                ColoredSoup result;
                result.color = posColor;
                result.hasColor = posHas;
                result.material = posMaterial;
                result.materialName = posMaterialName;
                result.groupName = posGroupName;
                result.groupInstanceId = posGroupInstanceId;
                result.soup = std::move(r);
                out.push_back(std::move(result));
                return out;
            }

            GeomList EvalIntersection(const Stmt& inst, const glm::dmat4& xform, const glm::dvec4& color, bool hasColor)
            {
                SuppressSceneNodeGuard suppressGuard(*this);
                DefinitionFrameGuard definitionFrame(*this);
                RegisterLocalDefinitions(inst.children);

                std::vector<TriSoup> operands;
                glm::dvec4 firstColor = color;
                bool firstHas = hasColor;
                ScadMaterialProperties firstMaterial;
                std::string firstMaterialName;
                std::string firstGroupName = CurrentGroupLabel();
                uint64_t firstGroupInstanceId = CurrentGroupInstanceId();
                bool gotColor = false;
                for (const StmtPtr& s : inst.children)
                {
                    if (!s)
                        continue;
                    if (s->kind == StmtKind::Assign)
                    {
                        ctx_.Set(s->name, EvalExpr(s->value));
                        continue;
                    }
                    if (s->kind != StmtKind::Instance)
                        continue;
                    GeomList g = EvalInstance(*s, xform, color, hasColor);
                    std::vector<TriSoup> parts;
                    for (ColoredSoup& cs : g)
                    {
                        if (!gotColor)
                        {
                            firstColor = cs.color;
                            firstHas = cs.hasColor;
                            firstMaterial = cs.material;
                            firstMaterialName = cs.materialName;
                            firstGroupName = cs.groupName.empty() ? firstGroupName : cs.groupName;
                            firstGroupInstanceId = cs.groupInstanceId == 0 ? firstGroupInstanceId : cs.groupInstanceId;
                            gotColor = true;
                        }
                        parts.push_back(std::move(cs.soup));
                    }
                    if (parts.empty())
                        continue;
                    bool uok = false;
                    operands.push_back(parts.size() == 1 ? std::move(parts[0]) : ScadCsg::Union(parts, uok));
                }

                bool ok = false;
                TriSoup r = ScadCsg::Intersection(operands, ok);
                if (!ok && !ScadCsg::BackendAvailable())
                {
                    Warn("intersection", "intersection() approximated: only the first child is kept");
                }
                GeomList out;
                ColoredSoup result;
                result.color = firstColor;
                result.hasColor = firstHas;
                result.material = firstMaterial;
                result.materialName = firstMaterialName;
                result.groupName = firstGroupName;
                result.groupInstanceId = firstGroupInstanceId;
                result.soup = std::move(r);
                out.push_back(std::move(result));
                return out;
            }

            GeomList EvalHull(const Stmt& inst, const glm::dmat4& xform, const glm::dvec4& color, bool hasColor)
            {
                SuppressSceneNodeGuard suppressGuard(*this);
                GeomList children = EvalScope(inst.children, xform, color, hasColor);
                std::vector<TriSoup> parts;
                glm::dvec4 firstColor = color;
                bool firstHas = hasColor;
                ScadMaterialProperties firstMaterial;
                std::string firstMaterialName;
                std::string firstGroupName = CurrentGroupLabel();
                uint64_t firstGroupInstanceId = CurrentGroupInstanceId();
                bool gotColor = false;
                for (ColoredSoup& cs : children)
                {
                    if (!gotColor)
                    {
                        firstColor = cs.color;
                        firstHas = cs.hasColor;
                        firstMaterial = cs.material;
                        firstMaterialName = cs.materialName;
                        firstGroupName = cs.groupName.empty() ? firstGroupName : cs.groupName;
                        firstGroupInstanceId = cs.groupInstanceId == 0 ? firstGroupInstanceId : cs.groupInstanceId;
                        gotColor = true;
                    }
                    parts.push_back(std::move(cs.soup));
                }

                bool ok = false;
                TriSoup r = ScadCsg::Hull(parts, ok);
                if (!ok && !ScadCsg::BackendAvailable())
                {
                    Warn("hull", "hull() approximated as union (no backend)");
                }
                GeomList out;
                ColoredSoup result;
                result.color = firstColor;
                result.hasColor = firstHas;
                result.material = firstMaterial;
                result.materialName = firstMaterialName;
                result.groupName = firstGroupName;
                result.groupInstanceId = firstGroupInstanceId;
                result.soup = std::move(r);
                out.push_back(std::move(result));
                return out;
            }

            GeomList EvalFor(const Stmt& inst, const glm::dmat4& xform, const glm::dvec4& color, bool hasColor)
            {
                GeomList out;

                std::function<void(size_t)> evalBinding = [&](size_t argIndex)
                {
                    while (argIndex < inst.args.size() && inst.args[argIndex].name.empty())
                    {
                        ++argIndex;
                    }
                    if (argIndex >= inst.args.size())
                    {
                        AppendMove(out, EvalScope(inst.children, xform, color, hasColor));
                        return;
                    }

                    const CallArg& binding = inst.args[argIndex];
                    std::vector<Value> values;
                    BindingValues(EvalExpr(binding.value), values);
                    if (values.empty())
                        return;

                    for (const Value& value : values)
                    {
                        ctx_.Push();
                        ctx_.Set(binding.name, value);
                        evalBinding(argIndex + 1);
                        ctx_.Pop();
                    }
                };

                evalBinding(0);
                return out;
            }

            static void EnumerateRange(const Value& r, std::vector<Value>& out)
            {
                double step = r.rangeStep;
                if (step == 0.0)
                    step = 1.0;
                const double begin = r.rangeBegin;
                const double end = r.rangeEnd;
                const double eps = 1e-9;
                size_t guard = 0;
                if (step > 0.0)
                {
                    for (double v = begin; v <= end + eps && guard < 1000000; v += step, ++guard)
                    {
                        out.push_back(Value::MakeNumber(v));
                    }
                }
                else
                {
                    for (double v = begin; v >= end - eps && guard < 1000000; v += step, ++guard)
                    {
                        out.push_back(Value::MakeNumber(v));
                    }
                }
            }

            GeomList CallUserModule(const Stmt& def, const Stmt& call, const glm::dmat4& xform, const glm::dvec4& color,
                                    bool hasColor)
            {
                if (depth_ >= options_.maxRecursionDepth)
                {
                    Warn("depth", "max recursion depth reached at module '" + def.name + "'");
                    return {};
                }
                ++depth_;
                ctx_.Push();
                const bool createSceneNode = ShouldCreateSceneNodeForModule();
                const uint64_t instanceId = nextGroupInstanceId_++;
                moduleCallStack_.push_back(GroupFrame{def.name, instanceId});
                BindParams(def.params, call.args);
                ctx_.Set("$children", Value::MakeNumber(static_cast<double>(CountInstanceChildren(call.children))));
                childrenStack_.push_back(&call.children);

                GeomList g;
                if (createSceneNode)
                {
                    SceneNodeBuild* node = CreateSceneNode(def, call, xform, color, hasColor);
                    sceneOwnerStack_.push_back(node);
                    g = EvalScope(def.body, glm::dmat4(1.0), color, hasColor);
                    EmitSceneGeometry(g);
                    sceneOwnerStack_.pop_back();
                    g.clear();
                }
                else
                {
                    g = EvalScope(def.body, xform, color, hasColor);
                }

                childrenStack_.pop_back();
                moduleCallStack_.pop_back();
                ctx_.Pop();
                --depth_;
                return g;
            }

            static int CountInstanceChildren(const Scope& scope)
            {
                int count = 0;
                for (const StmtPtr& s : scope)
                {
                    if (s && s->kind == StmtKind::Instance)
                        ++count;
                }
                return count;
            }

            // Evaluates the caller children for children() / children(i), temporarily
            // popping the current level so nested children() resolves to the outer scope.
            GeomList EvalChildren(const Stmt& inst, const glm::dmat4& xform, const glm::dvec4& color, bool hasColor)
            {
                if (childrenStack_.empty())
                    return {};
                const Scope* level = childrenStack_.back();
                childrenStack_.pop_back();

                GeomList result;
                if (inst.args.empty())
                {
                    result = EvalScope(*level, xform, color, hasColor);
                }
                else
                {
                    const int want = static_cast<int>(EvalExpr(inst.args[0].value).AsNumber(0.0));
                    int idx = 0;
                    for (const StmtPtr& s : *level)
                    {
                        if (!s || s->kind != StmtKind::Instance)
                            continue;
                        if (idx == want)
                        {
                            AppendMove(result, EvalInstance(*s, xform, color, hasColor));
                            break;
                        }
                        ++idx;
                    }
                }

                childrenStack_.push_back(level);
                return result;
            }

            void BindParams(const std::vector<Param>& params, const std::vector<CallArg>& args)
            {
                std::vector<Value> positional;
                std::unordered_map<std::string, Value> named;
                for (const CallArg& a : args)
                {
                    if (a.name.empty())
                        positional.push_back(EvalExpr(a.value));
                    else
                        named[a.name] = EvalExpr(a.value);
                }

                size_t posIdx = 0;
                for (const Param& p : params)
                {
                    auto n = named.find(p.name);
                    if (n != named.end())
                    {
                        ctx_.Set(p.name, n->second);
                    }
                    else if (posIdx < positional.size())
                    {
                        ctx_.Set(p.name, positional[posIdx++]);
                    }
                    else if (p.defaultValue)
                    {
                        ctx_.Set(p.name, EvalExpr(p.defaultValue));
                    }
                    else
                    {
                        ctx_.Set(p.name, Value());
                    }
                }
            }

            // --------------------------------------------------------------
            // Primitive geometry
            // --------------------------------------------------------------
            GeomList MakeGeom(TriSoup&& objSpace, const glm::dmat4& xform, const glm::dvec4& color, bool hasColor);

            GeomList EvalPrimitive(const Stmt& inst, const glm::dmat4& xform, const glm::dvec4& color, bool hasColor);

            // --------------------------------------------------------------
            // gk_terrain (native low-poly heightfield; FScadTerrain backend)
            // --------------------------------------------------------------
            GeomList EvalTerrain(const Stmt& inst, const glm::dmat4& xform, const glm::dvec4& color, bool hasColor);

            // Decodes + builds (or returns the cached) terrain for a TERR value.
            // Returns nullptr after warning on malformed specs.
            std::shared_ptr<const FTerrainData> TerrainFromValue(const Value& value, const char* where);

            std::unordered_map<std::string, std::shared_ptr<const FTerrainData>> terrainCache_;
            std::unordered_map<uint64_t, std::shared_ptr<const FTerrainData>> terrainIdentityCache_;

            GeomList EvalLinearExtrude(const Stmt& inst, const glm::dmat4& xform, const glm::dvec4& color,
                                       bool hasColor);

            // --------------------------------------------------------------
            // 2D sub-evaluation (for linear_extrude / rotate_extrude children)
            // --------------------------------------------------------------
            static glm::dmat3 Translate2D(double tx, double ty);
            static glm::dmat3 Scale2D(double sx, double sy);
            static glm::dmat3 Rot2D(double rad);
            static glm::dvec2 Apply2D(const glm::dmat3& m, const glm::dvec2& p);

            // Collects closed 2D outlines (after nested 2D transforms) from a scope.
            void Collect2D(const Scope& children, const glm::dmat3& m, std::vector<std::vector<glm::dvec2>>& out);

            GeomList EvalRotateExtrude(const Stmt& inst, const glm::dmat4& xform, const glm::dvec4& color,
                                       bool hasColor);

            // --------------------------------------------------------------
            // Argument / transform helpers
            // --------------------------------------------------------------
            Value Arg(const Stmt& inst, const char* name, int posIndex);

            double ResolveRadius(const Stmt& inst, const char* rName, const char* dName, double fallback);

            glm::dmat4 BuildRotate(const Stmt& inst);

            glm::dmat4 BuildMirror(const glm::dvec3& nrm);

            glm::dmat4 BuildMultmatrix(const Stmt& inst);

            bool ResolveColor(const Stmt& inst, glm::dvec4& out);

            // gk_material() keeps roughness/metalness positional arguments
            // separate from color's legacy positional alpha argument.
            bool ResolveMaterialColor(const Stmt& inst, glm::dvec4& out);

            const ExprPtr& ColorArgExpr(const Stmt& inst) const;

            std::string MaterialNameFromExpr(const ExprPtr& expr) const;

            std::string ResolveColorName(const Stmt& inst) const;

            static glm::dvec4 NamedColor(const std::string& nameIn);

            // --------------------------------------------------------------
            // Expression evaluation
            // --------------------------------------------------------------
            Value EvalExpr(const ExprPtr& e);

            Value MakeVector(std::vector<Value>&& values);

            static void BindingValues(const Value& v, std::vector<Value>& out);

            // Appends one vector-literal element (plain expr or comprehension generator).
            void AppendElement(const ExprPtr& e, std::vector<Value>& out);

            Value EvalUnary(const ExprPtr& e);

            Value EvalBinary(const ExprPtr& e);

            Value ScaleVec(const Value& v, double s);

            static std::string NumToStr(double v);

            static std::string ValueToString(const Value& v);

            static bool ValuesEqual(const Value& a, const Value& b);

            Value EvalCall(const ExprPtr& e);

            Value EvalBuiltinFunction(const std::string& name, const std::vector<Value>& a);
        };
    } // namespace EvalDetail
} // namespace Assets::Scad
