#include "Modules/ScadLoader/FScadEvaluator.h"

#include "Modules/ScadLoader/FScadCsg.h"
#include "Modules/ScadLoader/FScadGeometry.h"
#include "Modules/ScadLoader/FScadTess.h"
#include "Modules/ScadLoader/FScadText.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <memory>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/matrix_transform.hpp>

#include <spdlog/spdlog.h>

namespace Assets::scad
{
    // ----------------------------------------------------------------------
    // Value helpers (declared in FScadTypes.h)
    // ----------------------------------------------------------------------
    bool Value::IsTruthy() const
    {
        switch (type)
        {
        case Type::Bool: return boolean;
        case Type::Number: return num != 0.0;
        case Type::Str: return !str.empty();
        case Type::Vec: return !vec.empty();
        case Type::Range: return true;
        default: return false;
        }
    }

    double Value::AsNumber(double fallback) const
    {
        if (type == Type::Number) return num;
        if (type == Type::Bool) return boolean ? 1.0 : 0.0;
        return fallback;
    }

    bool Value::AsVec3(glm::dvec3& out) const
    {
        if (type != Type::Vec) return false;
        for (size_t i = 0; i < vec.size() && i < 3; ++i)
        {
            out[static_cast<int>(i)] = vec[i].AsNumber(out[static_cast<int>(i)]);
        }
        return true;
    }

    bool Value::AsVec4(glm::dvec4& out) const
    {
        if (type != Type::Vec) return false;
        for (size_t i = 0; i < vec.size() && i < 4; ++i)
        {
            out[static_cast<int>(i)] = vec[i].AsNumber(out[static_cast<int>(i)]);
        }
        return true;
    }

    namespace
    {
        // A piece of colored geometry, accumulated as a triangle soup in SCAD
        // (Z-up) world space. The loader converts these to engine Y-up vertices.
        struct ColoredSoup
        {
            glm::dvec4 color = glm::dvec4(0.78, 0.78, 0.78, 1.0);
            bool hasColor = false;
            std::string groupName;
            uint64_t groupInstanceId = 0;
            TriSoup soup;
        };
        using GeomList = std::vector<ColoredSoup>;

        uint32_t QuantizeColor(const glm::vec4& c)
        {
            auto q = [](float v) -> uint32_t
            {
                const float clamped = std::min(1.0f, std::max(0.0f, v));
                return static_cast<uint32_t>(clamped * 255.0f + 0.5f);
            };
            return (q(c.r) << 24) | (q(c.g) << 16) | (q(c.b) << 8) | q(c.a);
        }

        void AppendMove(GeomList& dst, GeomList&& src)
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
                if (frames_.empty()) frames_.emplace_back();
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
                    if (found != it->end()) return &found->second;
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
                      const std::unordered_map<std::string, StmtPtr>& functions,
                      const ScadLoadOptions& options,
                      EvalResult& result)
                : modules_(modules), functions_(functions), options_(options), flatResult_(&result)
            {
            }

            Evaluator(const std::unordered_map<std::string, StmtPtr>& modules,
                      const std::unordered_map<std::string, StmtPtr>& functions,
                      const ScadLoadOptions& options,
                      SceneEvalResult& result)
                : modules_(modules), functions_(functions), options_(options), sceneResult_(&result)
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
                    if (!s) continue;
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

                for (const StmtPtr& s : topLevel)
                {
                    if (!s) continue;
                    if (s->kind == StmtKind::Assign)
                    {
                        ctx_.Set(s->name, EvalExpr(s->value));
                    }
                    else if (s->kind == StmtKind::Instance)
                    {
                        topLevelFallbackLabel_ = s->name;
                        topLevelFallbackInstanceId_ = nextGroupInstanceId_++;
                        currentTopLevelFallbackRoot_ = nullptr;
                        EmitSceneGeometry(EvalInstance(*s, glm::dmat4(1.0), glm::dvec4(0.78, 0.78, 0.78, 1.0), false));
                        currentTopLevelFallbackRoot_ = nullptr;
                        topLevelFallbackLabel_.clear();
                        topLevelFallbackInstanceId_ = 0;
                    }
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

        private:
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
            std::string topLevelFallbackLabel_;
            uint64_t topLevelFallbackInstanceId_ = 0;
            uint64_t nextGroupInstanceId_ = 1;
            int suppressSceneNodes_ = 0;

            struct SceneNodeBuild
            {
                std::string name;
                uint64_t instanceId = 0;
                glm::dmat4 localTransform = glm::dmat4(1.0);
                std::map<uint32_t, SceneMeshBucket> meshes;
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
                explicit SuppressSceneNodeGuard(Evaluator& owner) : owner_(owner)
                {
                    ++owner_.suppressSceneNodes_;
                }

                ~SuppressSceneNodeGuard()
                {
                    --owner_.suppressSceneNodes_;
                }

                Evaluator& owner_;
            };

            void Warn(const std::string& key, const std::string& msg)
            {
                if (flatResult_) ++flatResult_->warningCount;
                if (sceneResult_) ++sceneResult_->warningCount;
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
                if (flatResult_) flatResult_->triangleCount += triangleCount;
                if (sceneResult_) sceneResult_->triangleCount += triangleCount;
            }

            void EmitFlat(const GeomList& geom)
            {
                if (!flatResult_) return;
                for (const ColoredSoup& cs : geom)
                {
                    const glm::vec4 c = cs.hasColor ? glm::vec4(cs.color) : glm::vec4(0.78f, 0.78f, 0.78f, 1.0f);
                    const std::string groupName = cs.groupName.empty() ? std::string("part") : cs.groupName;
                    const uint64_t groupInstanceId = cs.groupInstanceId == 0 ? nextGroupInstanceId_++ : cs.groupInstanceId;
                    const BucketKey key{groupName, groupInstanceId, QuantizeColor(c)};
                    ColorBucket& bucket = flatResult_->buckets[key];
                    bucket.color = c;
                    bucket.groupName = groupName;
                    bucket.tris.insert(bucket.tris.end(), cs.soup.begin(), cs.soup.end());
                    AddTriangleCount(cs.soup.size() / 3);
                }
            }

            bool IsSceneMode() const
            {
                return sceneResult_ != nullptr;
            }

            bool ShouldCreateSceneNodeForModule() const
            {
                return IsSceneMode() && suppressSceneNodes_ == 0;
            }

            SceneNodeBuild* CurrentSceneOwner() const
            {
                return sceneOwnerStack_.empty() ? nullptr : sceneOwnerStack_.back();
            }

            SceneNodeBuild* CreateSceneNode(const std::string& name, const glm::dmat4& localTransform)
            {
                auto node = std::make_unique<SceneNodeBuild>();
                node->name = name.empty() ? "part" : name;
                node->instanceId = nextGroupInstanceId_++;
                node->localTransform = localTransform;

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
                node->instanceId = topLevelFallbackInstanceId_ != 0 ? topLevelFallbackInstanceId_ : nextGroupInstanceId_++;
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
                    SceneMeshBucket& bucket = owner->meshes[QuantizeColor(c)];
                    bucket.color = c;
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
                sceneResult_->roots.reserve(sceneRoots_.size());
                for (const auto& root : sceneRoots_)
                {
                    sceneResult_->roots.push_back(FinalizeSceneNode(*root));
                }
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
                    if (!s) continue;
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
                    if (!s) continue;
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
                    if (found != it->end()) return found->second;
                }

                auto found = modules_.find(name);
                return found != modules_.end() ? found->second : nullptr;
            }

            StmtPtr FindFunction(const std::string& name) const
            {
                for (auto it = localFunctions_.rbegin(); it != localFunctions_.rend(); ++it)
                {
                    auto found = it->find(name);
                    if (found != it->end()) return found->second;
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
                    if (v.IsNumber()) s = glm::dvec3(v.num);
                    else v.AsVec3(s);
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
                        return EvalScope(inst.children, xform, c, true);
                    }
                    return EvalScope(inst.children, xform, color, hasColor);
                }
                if (name == "union" || name == "group" || name == "render")
                {
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
                        if (!a.name.empty()) ctx_.Set(a.name, EvalExpr(a.value));
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
                        if (i) msg += ", ";
                        if (!inst.args[i].name.empty()) msg += inst.args[i].name + " = ";
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
                std::string posGroupName = CurrentGroupLabel();
                uint64_t posGroupInstanceId = CurrentGroupInstanceId();
                bool gotColor = false;
                int instIdx = 0;
                for (const StmtPtr& s : inst.children)
                {
                    if (!s) continue;
                    if (s->kind == StmtKind::Assign)
                    {
                        ctx_.Set(s->name, EvalExpr(s->value));
                        continue;
                    }
                    if (s->kind != StmtKind::Instance) continue;
                    GeomList g = EvalInstance(*s, xform, color, hasColor);
                    if (instIdx == 0)
                    {
                        for (ColoredSoup& cs : g)
                        {
                            if (!gotColor)
                            {
                                posColor = cs.color;
                                posHas = cs.hasColor;
                                posGroupName = cs.groupName.empty() ? posGroupName : cs.groupName;
                                posGroupInstanceId = cs.groupInstanceId == 0 ? posGroupInstanceId : cs.groupInstanceId;
                                gotColor = true;
                            }
                            positives.push_back(std::move(cs.soup));
                        }
                    }
                    else
                    {
                        for (ColoredSoup& cs : g) negatives.push_back(std::move(cs.soup));
                    }
                    ++instIdx;
                }
                if (positives.empty()) return {};

                // Union the positive parts into one solid, then subtract.
                bool unionOk = false;
                TriSoup positiveSolid = (positives.size() == 1) ? std::move(positives[0]) : ScadCsg::Union(positives, unionOk);

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
                std::string firstGroupName = CurrentGroupLabel();
                uint64_t firstGroupInstanceId = CurrentGroupInstanceId();
                bool gotColor = false;
                for (const StmtPtr& s : inst.children)
                {
                    if (!s) continue;
                    if (s->kind == StmtKind::Assign)
                    {
                        ctx_.Set(s->name, EvalExpr(s->value));
                        continue;
                    }
                    if (s->kind != StmtKind::Instance) continue;
                    GeomList g = EvalInstance(*s, xform, color, hasColor);
                    std::vector<TriSoup> parts;
                    for (ColoredSoup& cs : g)
                    {
                        if (!gotColor)
                        {
                            firstColor = cs.color;
                            firstHas = cs.hasColor;
                            firstGroupName = cs.groupName.empty() ? firstGroupName : cs.groupName;
                            firstGroupInstanceId = cs.groupInstanceId == 0 ? firstGroupInstanceId : cs.groupInstanceId;
                            gotColor = true;
                        }
                        parts.push_back(std::move(cs.soup));
                    }
                    if (parts.empty()) continue;
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
                std::string firstGroupName = CurrentGroupLabel();
                uint64_t firstGroupInstanceId = CurrentGroupInstanceId();
                bool gotColor = false;
                for (ColoredSoup& cs : children)
                {
                    if (!gotColor)
                    {
                        firstColor = cs.color;
                        firstHas = cs.hasColor;
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
                result.groupName = firstGroupName;
                result.groupInstanceId = firstGroupInstanceId;
                result.soup = std::move(r);
                out.push_back(std::move(result));
                return out;
            }

            GeomList EvalFor(const Stmt& inst, const glm::dmat4& xform, const glm::dvec4& color, bool hasColor)
            {
                struct Binding
                {
                    std::string name;
                    std::vector<Value> values;
                };
                std::vector<Binding> bindings;
                for (const CallArg& a : inst.args)
                {
                    if (a.name.empty()) continue;
                    Binding b;
                    b.name = a.name;
                    const Value v = EvalExpr(a.value);
                    if (v.type == Value::Type::Range)
                    {
                        EnumerateRange(v, b.values);
                    }
                    else if (v.type == Value::Type::Vec)
                    {
                        b.values = v.vec;
                    }
                    else
                    {
                        b.values.push_back(v);
                    }
                    bindings.push_back(std::move(b));
                }
                GeomList out;
                if (bindings.empty()) return out;

                std::vector<size_t> idx(bindings.size(), 0);
                while (true)
                {
                    ctx_.Push();
                    for (size_t i = 0; i < bindings.size(); ++i)
                    {
                        ctx_.Set(bindings[i].name, bindings[i].values.empty() ? Value() : bindings[i].values[idx[i]]);
                    }
                    AppendMove(out, EvalScope(inst.children, xform, color, hasColor));
                    ctx_.Pop();

                    int dim = static_cast<int>(bindings.size()) - 1;
                    while (dim >= 0)
                    {
                        if (bindings[dim].values.empty()) { --dim; continue; }
                        if (++idx[dim] < bindings[dim].values.size()) break;
                        idx[dim] = 0;
                        --dim;
                    }
                    if (dim < 0) break;
                }
                return out;
            }

            static void EnumerateRange(const Value& r, std::vector<Value>& out)
            {
                double step = r.rangeStep;
                if (step == 0.0) step = 1.0;
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

            GeomList CallUserModule(const Stmt& def, const Stmt& call, const glm::dmat4& xform, const glm::dvec4& color, bool hasColor)
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
                    SceneNodeBuild* node = CreateSceneNode(def.name, xform);
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
                    if (s && s->kind == StmtKind::Instance) ++count;
                }
                return count;
            }

            // Evaluates the caller children for children() / children(i), temporarily
            // popping the current level so nested children() resolves to the outer scope.
            GeomList EvalChildren(const Stmt& inst, const glm::dmat4& xform, const glm::dvec4& color, bool hasColor)
            {
                if (childrenStack_.empty()) return {};
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
                        if (!s || s->kind != StmtKind::Instance) continue;
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
                    if (a.name.empty()) positional.push_back(EvalExpr(a.value));
                    else named[a.name] = EvalExpr(a.value);
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
            GeomList MakeGeom(TriSoup&& objSpace, const glm::dmat4& xform, const glm::dvec4& color, bool hasColor)
            {
                if (objSpace.empty()) return {};
                ColoredSoup cs;
                cs.color = color;
                cs.hasColor = hasColor;
                cs.groupName = CurrentGroupLabel();
                cs.groupInstanceId = CurrentGroupInstanceId();
                cs.soup.reserve(objSpace.size());
                const bool flipWinding = glm::determinant(glm::dmat3(xform)) < 0.0;
                for (size_t i = 0; i + 2 < objSpace.size(); i += 3)
                {
                    const glm::dvec3 a = glm::dvec3(xform * glm::dvec4(objSpace[i + 0], 1.0));
                    const glm::dvec3 b = glm::dvec3(xform * glm::dvec4(objSpace[i + 1], 1.0));
                    const glm::dvec3 c = glm::dvec3(xform * glm::dvec4(objSpace[i + 2], 1.0));
                    cs.soup.push_back(a);
                    if (flipWinding)
                    {
                        cs.soup.push_back(c);
                        cs.soup.push_back(b);
                    }
                    else
                    {
                        cs.soup.push_back(b);
                        cs.soup.push_back(c);
                    }
                }
                GeomList out;
                out.push_back(std::move(cs));
                return out;
            }

            GeomList EvalPrimitive(const Stmt& inst, const glm::dmat4& xform, const glm::dvec4& color, bool hasColor)
            {
                TriSoup soup;
                const std::string& name = inst.name;
                const double fn = ctx_.GetNumber("$fn", 0.0);
                const double fa = ctx_.GetNumber("$fa", 12.0);
                const double fs = ctx_.GetNumber("$fs", 2.0);

                if (name == "cube")
                {
                    const Value sizeVal = Arg(inst, "size", 0);
                    glm::dvec3 size(1.0);
                    if (sizeVal.IsNumber()) size = glm::dvec3(sizeVal.num);
                    else sizeVal.AsVec3(size);
                    const bool center = Arg(inst, "center", 1).IsTruthy();
                    ScadGeometry::BuildCube(size, center, soup);
                }
                else if (name == "sphere")
                {
                    const double r = ResolveRadius(inst, "r", "d", 1.0);
                    ScadGeometry::BuildSphere(r, ScadGeometry::Fragments(r, fn, fa, fs), soup);
                }
                else if (name == "cylinder")
                {
                    const double h = Arg(inst, "h", 0).AsNumber(1.0);
                    double r1 = ResolveRadius(inst, "r1", "d1", -1.0);
                    double r2 = ResolveRadius(inst, "r2", "d2", -1.0);
                    const double r = ResolveRadius(inst, "r", "d", -1.0);
                    if (r1 < 0.0) r1 = (r >= 0.0) ? r : 1.0;
                    if (r2 < 0.0) r2 = (r >= 0.0) ? r : r1;
                    const bool center = Arg(inst, "center", -1).IsTruthy();
                    const double maxR = std::max(r1, r2);
                    ScadGeometry::BuildCylinder(h, r1, r2, center, ScadGeometry::Fragments(maxR, fn, fa, fs), soup);
                }
                else if (name == "polyhedron")
                {
                    std::vector<glm::dvec3> points;
                    std::vector<std::vector<int>> faces;
                    const Value ptsVal = Arg(inst, "points", 0);
                    Value facesVal = Arg(inst, "faces", 1);
                    if (facesVal.type == Value::Type::Undef) facesVal = Arg(inst, "triangles", -1);
                    if (ptsVal.type == Value::Type::Vec)
                    {
                        for (const Value& p : ptsVal.vec)
                        {
                            glm::dvec3 v(0.0);
                            p.AsVec3(v);
                            points.push_back(v);
                        }
                    }
                    if (facesVal.type == Value::Type::Vec)
                    {
                        for (const Value& f : facesVal.vec)
                        {
                            std::vector<int> face;
                            if (f.type == Value::Type::Vec)
                            {
                                for (const Value& iv : f.vec)
                                {
                                    face.push_back(static_cast<int>(iv.AsNumber(0.0)));
                                }
                            }
                            faces.push_back(std::move(face));
                        }
                    }
                    ScadGeometry::BuildPolyhedron(points, faces, soup);
                }

                return MakeGeom(std::move(soup), xform, color, hasColor);
            }

            GeomList EvalLinearExtrude(const Stmt& inst, const glm::dmat4& xform, const glm::dvec4& color, bool hasColor)
            {
                const double height = Arg(inst, "height", 0).AsNumber(1.0);
                const bool center = Arg(inst, "center", -1).IsTruthy();
                const double fn = ctx_.GetNumber("$fn", 0.0);

                GeomList out;

                // text() children: shaped via FreeType into their own geometry.
                for (const StmtPtr& cptr : inst.children)
                {
                    if (!cptr || cptr->kind != StmtKind::Instance || cptr->name != "text") continue;
                    const Stmt& child = *cptr;
                    if (!ScadText::Available())
                    {
                        Warn("text", "text() unavailable (no FreeType backend); skipped");
                        continue;
                    }
                    const Value tv = Arg(child, "text", 0);
                    const std::string str = (tv.type == Value::Type::Str) ? tv.str : std::string();
                    const double tsize = Arg(child, "size", 1).AsNumber(10.0);
                    const Value ha = Arg(child, "halign", -1);
                    const Value va = Arg(child, "valign", -1);
                    const std::string halign = (ha.type == Value::Type::Str) ? ha.str : "left";
                    const std::string valign = (va.type == Value::Type::Str) ? va.str : "baseline";
                    TriSoup soup;
                    if (!str.empty() && ScadText::BuildText(str, tsize, halign, valign, height, center, fn, soup))
                    {
                        AppendMove(out, MakeGeom(std::move(soup), xform, color, hasColor));
                    }
                }

                // 2D shape children (concave + holes + nested transforms) via earcut.
                std::vector<std::vector<glm::dvec2>> contours;
                Collect2D(inst.children, glm::dmat3(1.0), contours);
                if (!contours.empty())
                {
                    TriSoup soup;
                    if (ScadTess::Available())
                    {
                        ScadTess::ExtrudeEvenOdd(contours, height, center, soup);
                    }
                    else
                    {
                        for (const std::vector<glm::dvec2>& c : contours)
                        {
                            ScadGeometry::ExtrudePolygon(c, height, center, soup);
                        }
                    }
                    AppendMove(out, MakeGeom(std::move(soup), xform, color, hasColor));
                }
                return out;
            }

            // --------------------------------------------------------------
            // 2D sub-evaluation (for linear_extrude / rotate_extrude children)
            // --------------------------------------------------------------
            static glm::dmat3 Translate2D(double tx, double ty)
            {
                glm::dmat3 m(1.0);
                m[2] = glm::dvec3(tx, ty, 1.0);
                return m;
            }
            static glm::dmat3 Scale2D(double sx, double sy)
            {
                glm::dmat3 m(1.0);
                m[0][0] = sx;
                m[1][1] = sy;
                return m;
            }
            static glm::dmat3 Rot2D(double rad)
            {
                const double c = std::cos(rad);
                const double s = std::sin(rad);
                glm::dmat3 m(1.0);
                m[0] = glm::dvec3(c, s, 0.0);
                m[1] = glm::dvec3(-s, c, 0.0);
                return m;
            }
            static glm::dvec2 Apply2D(const glm::dmat3& m, const glm::dvec2& p)
            {
                const glm::dvec3 r = m * glm::dvec3(p.x, p.y, 1.0);
                return glm::dvec2(r.x, r.y);
            }

            // Collects closed 2D outlines (after nested 2D transforms) from a scope.
            void Collect2D(const Scope& children, const glm::dmat3& m, std::vector<std::vector<glm::dvec2>>& out)
            {
                DefinitionFrameGuard definitionFrame(*this);
                RegisterLocalDefinitions(children);

                const double fn = ctx_.GetNumber("$fn", 0.0);
                const double fa = ctx_.GetNumber("$fa", 12.0);
                const double fs = ctx_.GetNumber("$fs", 2.0);

                for (const StmtPtr& sp : children)
                {
                    if (!sp) continue;
                    if (sp->kind == StmtKind::Assign)
                    {
                        ctx_.Set(sp->name, EvalExpr(sp->value));
                        continue;
                    }
                    if (sp->kind != StmtKind::Instance) continue;
                    const Stmt& c = *sp;
                    if (c.modifiers.find('*') != std::string::npos) continue;

                    if (c.name == "translate")
                    {
                        glm::dvec3 t(0.0);
                        Arg(c, "v", 0).AsVec3(t);
                        Collect2D(c.children, m * Translate2D(t.x, t.y), out);
                    }
                    else if (c.name == "scale")
                    {
                        glm::dvec3 s(1.0);
                        const Value v = Arg(c, "v", 0);
                        if (v.IsNumber()) s = glm::dvec3(v.num);
                        else v.AsVec3(s);
                        Collect2D(c.children, m * Scale2D(s.x, s.y), out);
                    }
                    else if (c.name == "rotate")
                    {
                        const Value a0 = Arg(c, "a", 0);
                        double deg = 0.0;
                        if (a0.type == Value::Type::Vec) { glm::dvec3 e(0.0); a0.AsVec3(e); deg = e.z; }
                        else deg = a0.AsNumber(0.0);
                        Collect2D(c.children, m * Rot2D(deg * kDeg2Rad), out);
                    }
                    else if (c.name == "union" || c.name == "group")
                    {
                        Collect2D(c.children, m, out);
                    }
                    else if (c.name == "circle")
                    {
                        const double r = ResolveRadius(c, "r", "d", 1.0);
                        const int frag = ScadGeometry::Fragments(r, fn, fa, fs);
                        std::vector<glm::dvec2> poly;
                        poly.reserve(frag);
                        for (int i = 0; i < frag; ++i)
                        {
                            const double a = 2.0 * kPi * static_cast<double>(i) / static_cast<double>(frag);
                            poly.push_back(Apply2D(m, glm::dvec2(r * std::cos(a), r * std::sin(a))));
                        }
                        out.push_back(std::move(poly));
                    }
                    else if (c.name == "square")
                    {
                        const Value sz = Arg(c, "size", 0);
                        glm::dvec2 s(1.0, 1.0);
                        if (sz.IsNumber()) s = glm::dvec2(sz.num);
                        else { glm::dvec3 v(1.0); sz.AsVec3(v); s = glm::dvec2(v.x, v.y); }
                        const bool sc = Arg(c, "center", 1).IsTruthy();
                        const glm::dvec2 o = sc ? -s * 0.5 : glm::dvec2(0.0);
                        std::vector<glm::dvec2> poly = {
                            Apply2D(m, o),
                            Apply2D(m, glm::dvec2(o.x + s.x, o.y)),
                            Apply2D(m, glm::dvec2(o.x + s.x, o.y + s.y)),
                            Apply2D(m, glm::dvec2(o.x, o.y + s.y))};
                        out.push_back(std::move(poly));
                    }
                    else if (c.name == "polygon")
                    {
                        const Value pts = Arg(c, "points", 0);
                        if (pts.type == Value::Type::Vec)
                        {
                            std::vector<glm::dvec2> all;
                            all.reserve(pts.vec.size());
                            for (const Value& p : pts.vec)
                            {
                                glm::dvec3 v(0.0);
                                p.AsVec3(v);
                                all.push_back(Apply2D(m, glm::dvec2(v.x, v.y)));
                            }
                            const Value paths = Arg(c, "paths", 1);
                            if (paths.type == Value::Type::Vec && !paths.vec.empty())
                            {
                                // Each path is an index loop into points (outer + holes).
                                for (const Value& path : paths.vec)
                                {
                                    if (path.type != Value::Type::Vec) continue;
                                    std::vector<glm::dvec2> ring;
                                    for (const Value& iv : path.vec)
                                    {
                                        const int idx = static_cast<int>(iv.AsNumber(0.0));
                                        if (idx >= 0 && idx < static_cast<int>(all.size())) ring.push_back(all[idx]);
                                    }
                                    if (ring.size() >= 3) out.push_back(std::move(ring));
                                }
                            }
                            else if (all.size() >= 3)
                            {
                                out.push_back(std::move(all));
                            }
                        }
                    }
                    else
                    {
                        StmtPtr found = FindModule(c.name);
                        if (found && depth_ < options_.maxRecursionDepth)
                        {
                            ++depth_;
                            ctx_.Push();
                            BindParams(found->params, c.args);
                            Collect2D(found->body, m, out);
                            ctx_.Pop();
                            --depth_;
                        }
                    }
                }
            }

            GeomList EvalRotateExtrude(const Stmt& inst, const glm::dmat4& xform, const glm::dvec4& color, bool hasColor)
            {
                const double angle = Arg(inst, "angle", 0).AsNumber(360.0);
                const double fn = ctx_.GetNumber("$fn", 0.0);
                const double fa = ctx_.GetNumber("$fa", 12.0);
                const double fs = ctx_.GetNumber("$fs", 2.0);

                std::vector<std::vector<glm::dvec2>> profiles;
                Collect2D(inst.children, glm::dmat3(1.0), profiles);
                if (profiles.empty()) return {};

                double maxR = 0.0;
                for (const auto& prof : profiles)
                {
                    for (const glm::dvec2& p : prof) maxR = std::max(maxR, p.x);
                }
                const int base = ScadGeometry::Fragments(maxR, fn, fa, fs);
                const int segs = (angle >= 359.999)
                                     ? base
                                     : std::max(2, static_cast<int>(std::ceil(static_cast<double>(base) * angle / 360.0)));

                TriSoup soup;
                ScadGeometry::BuildRotateExtrude(profiles, angle, segs, soup);
                return MakeGeom(std::move(soup), xform, color, hasColor);
            }

            // --------------------------------------------------------------
            // Argument / transform helpers
            // --------------------------------------------------------------
            Value Arg(const Stmt& inst, const char* name, int posIndex)
            {
                for (const CallArg& a : inst.args)
                {
                    if (a.name == name) return EvalExpr(a.value);
                }
                if (posIndex >= 0)
                {
                    int p = 0;
                    for (const CallArg& a : inst.args)
                    {
                        if (!a.name.empty()) continue;
                        if (p == posIndex) return EvalExpr(a.value);
                        ++p;
                    }
                }
                return Value();
            }

            double ResolveRadius(const Stmt& inst, const char* rName, const char* dName, double fallback)
            {
                for (const CallArg& a : inst.args)
                {
                    if (a.name == rName) return EvalExpr(a.value).AsNumber(fallback);
                }
                for (const CallArg& a : inst.args)
                {
                    if (a.name == dName) return EvalExpr(a.value).AsNumber(fallback * 2.0) * 0.5;
                }
                return fallback;
            }

            glm::dmat4 BuildRotate(const Stmt& inst)
            {
                const Value a0 = Arg(inst, "a", 0);
                if (a0.type == Value::Type::Vec)
                {
                    glm::dvec3 deg(0.0);
                    a0.AsVec3(deg);
                    glm::dmat4 r(1.0);
                    r = glm::rotate(r, deg.z * kDeg2Rad, glm::dvec3(0, 0, 1));
                    r = glm::rotate(r, deg.y * kDeg2Rad, glm::dvec3(0, 1, 0));
                    r = glm::rotate(r, deg.x * kDeg2Rad, glm::dvec3(1, 0, 0));
                    return r;
                }
                if (a0.IsNumber())
                {
                    glm::dvec3 axis(0, 0, 1);
                    const Value v = Arg(inst, "v", 1);
                    if (v.type == Value::Type::Vec) v.AsVec3(axis);
                    if (glm::dot(axis, axis) < 1e-12) axis = glm::dvec3(0, 0, 1);
                    return glm::rotate(glm::dmat4(1.0), a0.num * kDeg2Rad, glm::normalize(axis));
                }
                return glm::dmat4(1.0);
            }

            glm::dmat4 BuildMirror(const glm::dvec3& nrm)
            {
                const double len2 = glm::dot(nrm, nrm);
                if (len2 < 1e-12) return glm::dmat4(1.0);
                const glm::dvec3 n = nrm / std::sqrt(len2);
                glm::dmat4 m(1.0);
                for (int i = 0; i < 3; ++i)
                {
                    for (int j = 0; j < 3; ++j)
                    {
                        m[j][i] = ((i == j) ? 1.0 : 0.0) - 2.0 * n[i] * n[j];
                    }
                }
                return m;
            }

            glm::dmat4 BuildMultmatrix(const Stmt& inst)
            {
                const Value m = Arg(inst, "m", 0);
                if (m.type != Value::Type::Vec) return glm::dmat4(1.0);
                glm::dmat4 out(1.0);
                for (size_t row = 0; row < m.vec.size() && row < 4; ++row)
                {
                    const Value& r = m.vec[row];
                    if (r.type != Value::Type::Vec) continue;
                    for (size_t col = 0; col < r.vec.size() && col < 4; ++col)
                    {
                        out[static_cast<int>(col)][static_cast<int>(row)] = r.vec[col].AsNumber(0.0);
                    }
                }
                return out;
            }

            bool ResolveColor(const Stmt& inst, glm::dvec4& out)
            {
                const Value c = Arg(inst, "c", 0);
                if (c.type == Value::Type::Vec)
                {
                    out = glm::dvec4(0.0, 0.0, 0.0, 1.0);
                    c.AsVec4(out);
                }
                else if (c.type == Value::Type::Str)
                {
                    out = NamedColor(c.str);
                }
                else
                {
                    return false;
                }
                const Value alpha = Arg(inst, "alpha", 1);
                if (alpha.IsNumber()) out.a = alpha.num;
                return true;
            }

            static glm::dvec4 NamedColor(const std::string& nameIn)
            {
                std::string n;
                n.reserve(nameIn.size());
                for (char ch : nameIn) n.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
                if (n == "red") return {1, 0, 0, 1};
                if (n == "green") return {0, 0.5, 0, 1};
                if (n == "blue") return {0, 0, 1, 1};
                if (n == "white") return {1, 1, 1, 1};
                if (n == "black") return {0, 0, 0, 1};
                if (n == "gray" || n == "grey") return {0.5, 0.5, 0.5, 1};
                if (n == "yellow") return {1, 1, 0, 1};
                if (n == "orange") return {1, 0.65, 0, 1};
                if (n == "brown") return {0.65, 0.16, 0.16, 1};
                return {0.78, 0.78, 0.78, 1};
            }

            // --------------------------------------------------------------
            // Expression evaluation
            // --------------------------------------------------------------
            Value EvalExpr(const ExprPtr& e)
            {
                if (!e) return Value();
                switch (e->kind)
                {
                case ExprKind::Number: return Value::MakeNumber(e->num);
                case ExprKind::Bool: return Value::MakeBool(e->boolean);
                case ExprKind::Str: return Value::MakeStr(e->str);
                case ExprKind::Undef: return Value();
                case ExprKind::VectorLit:
                {
                    std::vector<Value> items;
                    for (const ExprPtr& it : e->list) AppendElement(it, items);
                    return Value::MakeVec(std::move(items));
                }
                case ExprKind::RangeLit:
                {
                    if (e->list.size() == 2)
                    {
                        return Value::MakeRange(EvalExpr(e->list[0]).AsNumber(0.0), 1.0, EvalExpr(e->list[1]).AsNumber(0.0));
                    }
                    if (e->list.size() == 3)
                    {
                        return Value::MakeRange(EvalExpr(e->list[0]).AsNumber(0.0),
                                                EvalExpr(e->list[1]).AsNumber(1.0),
                                                EvalExpr(e->list[2]).AsNumber(0.0));
                    }
                    return Value();
                }
                case ExprKind::Ident:
                {
                    const Value* v = ctx_.Get(e->str);
                    if (v) return *v;
                    Warn("undefvar", "undefined variable '" + e->str + "'");
                    return Value();
                }
                case ExprKind::Index:
                {
                    const Value base = EvalExpr(e->list[0]);
                    const int idx = static_cast<int>(EvalExpr(e->list[1]).AsNumber(0.0));
                    if (base.type == Value::Type::Vec && idx >= 0 && idx < static_cast<int>(base.vec.size()))
                    {
                        return base.vec[idx];
                    }
                    return Value();
                }
                case ExprKind::Member:
                {
                    const Value base = EvalExpr(e->list[0]);
                    int idx = -1;
                    if (e->str == "x") idx = 0;
                    else if (e->str == "y") idx = 1;
                    else if (e->str == "z") idx = 2;
                    if (base.type == Value::Type::Vec && idx >= 0 && idx < static_cast<int>(base.vec.size()))
                    {
                        return base.vec[idx];
                    }
                    return Value();
                }
                case ExprKind::Unary: return EvalUnary(e);
                case ExprKind::Binary: return EvalBinary(e);
                case ExprKind::Cond:
                    return EvalExpr(e->list[0]).IsTruthy() ? EvalExpr(e->list[1]) : EvalExpr(e->list[2]);
                case ExprKind::Call: return EvalCall(e);
                case ExprKind::CompFor:
                case ExprKind::CompLet:
                case ExprKind::CompIf:
                case ExprKind::CompEach:
                    return Value(); // only meaningful inside a VectorLit (see AppendElement)
                }
                return Value();
            }

            static void BindingValues(const Value& v, std::vector<Value>& out)
            {
                if (v.type == Value::Type::Range) EnumerateRange(v, out);
                else if (v.type == Value::Type::Vec) out = v.vec;
                else out.push_back(v);
            }

            // Appends one vector-literal element (plain expr or comprehension generator).
            void AppendElement(const ExprPtr& e, std::vector<Value>& out)
            {
                if (!e) return;
                switch (e->kind)
                {
                case ExprKind::CompFor:
                {
                    struct B { std::string name; std::vector<Value> vals; };
                    std::vector<B> bs;
                    for (const CallArg& a : e->args)
                    {
                        if (a.name.empty()) continue;
                        B b;
                        b.name = a.name;
                        BindingValues(EvalExpr(a.value), b.vals);
                        if (b.vals.empty()) return; // empty range -> no iterations
                        bs.push_back(std::move(b));
                    }
                    if (bs.empty() || e->list.empty()) return;
                    std::vector<size_t> idx(bs.size(), 0);
                    while (true)
                    {
                        ctx_.Push();
                        for (size_t i = 0; i < bs.size(); ++i) ctx_.Set(bs[i].name, bs[i].vals[idx[i]]);
                        AppendElement(e->list[0], out);
                        ctx_.Pop();
                        int dim = static_cast<int>(bs.size()) - 1;
                        while (dim >= 0)
                        {
                            if (++idx[dim] < bs[dim].vals.size()) break;
                            idx[dim] = 0;
                            --dim;
                        }
                        if (dim < 0) break;
                    }
                    return;
                }
                case ExprKind::CompLet:
                {
                    ctx_.Push();
                    for (const CallArg& a : e->args)
                    {
                        if (!a.name.empty()) ctx_.Set(a.name, EvalExpr(a.value));
                    }
                    if (!e->list.empty()) AppendElement(e->list[0], out);
                    ctx_.Pop();
                    return;
                }
                case ExprKind::CompIf:
                {
                    if (e->list.size() >= 2 && EvalExpr(e->list[0]).IsTruthy()) AppendElement(e->list[1], out);
                    else if (e->list.size() >= 3) AppendElement(e->list[2], out);
                    return;
                }
                case ExprKind::CompEach:
                {
                    if (e->list.empty()) return;
                    const Value v = EvalExpr(e->list[0]);
                    if (v.type == Value::Type::Vec) out.insert(out.end(), v.vec.begin(), v.vec.end());
                    else if (v.type == Value::Type::Range) { std::vector<Value> vv; EnumerateRange(v, vv); out.insert(out.end(), vv.begin(), vv.end()); }
                    else out.push_back(v);
                    return;
                }
                default:
                    out.push_back(EvalExpr(e));
                    return;
                }
            }

            Value EvalUnary(const ExprPtr& e)
            {
                const Value v = EvalExpr(e->list[0]);
                if (e->str == "-")
                {
                    if (v.type == Value::Type::Vec)
                    {
                        std::vector<Value> out;
                        out.reserve(v.vec.size());
                        for (const Value& c : v.vec) out.push_back(Value::MakeNumber(-c.AsNumber(0.0)));
                        return Value::MakeVec(std::move(out));
                    }
                    return Value::MakeNumber(-v.AsNumber(0.0));
                }
                if (e->str == "!") return Value::MakeBool(!v.IsTruthy());
                return v; // unary '+'
            }

            Value EvalBinary(const ExprPtr& e)
            {
                const std::string& op = e->str;
                const Value a = EvalExpr(e->list[0]);
                if (op == "&&") return Value::MakeBool(a.IsTruthy() && EvalExpr(e->list[1]).IsTruthy());
                if (op == "||") return Value::MakeBool(a.IsTruthy() || EvalExpr(e->list[1]).IsTruthy());

                const Value b = EvalExpr(e->list[1]);

                if (op == "==") return Value::MakeBool(ValuesEqual(a, b));
                if (op == "!=") return Value::MakeBool(!ValuesEqual(a, b));
                if (op == "<") return Value::MakeBool(a.AsNumber(0.0) < b.AsNumber(0.0));
                if (op == ">") return Value::MakeBool(a.AsNumber(0.0) > b.AsNumber(0.0));
                if (op == "<=") return Value::MakeBool(a.AsNumber(0.0) <= b.AsNumber(0.0));
                if (op == ">=") return Value::MakeBool(a.AsNumber(0.0) >= b.AsNumber(0.0));

                const bool aVec = a.type == Value::Type::Vec;
                const bool bVec = b.type == Value::Type::Vec;

                if (op == "+" || op == "-")
                {
                    if (aVec && bVec)
                    {
                        std::vector<Value> out;
                        const size_t n = std::min(a.vec.size(), b.vec.size());
                        for (size_t i = 0; i < n; ++i)
                        {
                            const double x = a.vec[i].AsNumber(0.0);
                            const double y = b.vec[i].AsNumber(0.0);
                            out.push_back(Value::MakeNumber(op == "+" ? x + y : x - y));
                        }
                        return Value::MakeVec(std::move(out));
                    }
                    const double x = a.AsNumber(0.0);
                    const double y = b.AsNumber(0.0);
                    return Value::MakeNumber(op == "+" ? x + y : x - y);
                }
                if (op == "*")
                {
                    if (aVec && !bVec) return ScaleVec(a, b.AsNumber(0.0));
                    if (!aVec && bVec) return ScaleVec(b, a.AsNumber(0.0));
                    return Value::MakeNumber(a.AsNumber(0.0) * b.AsNumber(0.0));
                }
                if (op == "/")
                {
                    if (aVec && !bVec) return ScaleVec(a, 1.0 / b.AsNumber(1.0));
                    const double denom = b.AsNumber(1.0);
                    return Value::MakeNumber(denom != 0.0 ? a.AsNumber(0.0) / denom : 0.0);
                }
                if (op == "%")
                {
                    return Value::MakeNumber(std::fmod(a.AsNumber(0.0), b.AsNumber(1.0)));
                }
                return Value();
            }

            static Value ScaleVec(const Value& v, double s)
            {
                std::vector<Value> out;
                out.reserve(v.vec.size());
                for (const Value& c : v.vec) out.push_back(Value::MakeNumber(c.AsNumber(0.0) * s));
                return Value::MakeVec(std::move(out));
            }

            static std::string NumToStr(double v)
            {
                if (v == static_cast<double>(static_cast<long long>(v)))
                {
                    return std::to_string(static_cast<long long>(v));
                }
                std::string s = std::to_string(v);
                // Trim trailing zeros for a tidier echo output.
                while (!s.empty() && s.back() == '0') s.pop_back();
                if (!s.empty() && s.back() == '.') s.pop_back();
                return s;
            }

            static std::string ValueToString(const Value& v)
            {
                switch (v.type)
                {
                case Value::Type::Number: return NumToStr(v.num);
                case Value::Type::Bool: return v.boolean ? "true" : "false";
                case Value::Type::Str: return v.str;
                case Value::Type::Vec:
                {
                    std::string s = "[";
                    for (size_t i = 0; i < v.vec.size(); ++i)
                    {
                        if (i) s += ", ";
                        s += ValueToString(v.vec[i]);
                    }
                    return s + "]";
                }
                case Value::Type::Range:
                    return "[" + NumToStr(v.rangeBegin) + " : " + NumToStr(v.rangeStep) + " : " + NumToStr(v.rangeEnd) + "]";
                default: return "undef";
                }
            }

            static bool ValuesEqual(const Value& a, const Value& b)
            {
                if (a.type != b.type)
                {
                    if ((a.type == Value::Type::Number || a.type == Value::Type::Bool) &&
                        (b.type == Value::Type::Number || b.type == Value::Type::Bool))
                    {
                        return a.AsNumber(0.0) == b.AsNumber(1.0);
                    }
                    return false;
                }
                switch (a.type)
                {
                case Value::Type::Number: return a.num == b.num;
                case Value::Type::Bool: return a.boolean == b.boolean;
                case Value::Type::Str: return a.str == b.str;
                default: return false;
                }
            }

            Value EvalCall(const ExprPtr& e)
            {
                const std::string& name = e->str;

                StmtPtr fn = FindFunction(name);
                if (fn)
                {
                    if (depth_ >= options_.maxRecursionDepth)
                    {
                        Warn("depth", "max recursion depth reached in function '" + name + "'");
                        return Value();
                    }
                    ++depth_;
                    ctx_.Push();
                    BindParams(fn->params, e->args);
                    const Value r = EvalExpr(fn->value);
                    ctx_.Pop();
                    --depth_;
                    return r;
                }

                std::vector<Value> a;
                a.reserve(e->args.size());
                for (const CallArg& arg : e->args) a.push_back(EvalExpr(arg.value));
                return EvalBuiltinFunction(name, a);
            }

            Value EvalBuiltinFunction(const std::string& name, const std::vector<Value>& a)
            {
                auto num = [&](size_t i, double d = 0.0) { return i < a.size() ? a[i].AsNumber(d) : d; };

                if (name == "max")
                {
                    double m = -std::numeric_limits<double>::infinity();
                    for (const Value& v : a) m = std::max(m, v.AsNumber(m));
                    return Value::MakeNumber(m);
                }
                if (name == "min")
                {
                    double m = std::numeric_limits<double>::infinity();
                    for (const Value& v : a) m = std::min(m, v.AsNumber(m));
                    return Value::MakeNumber(m);
                }
                if (name == "abs") return Value::MakeNumber(std::abs(num(0)));
                if (name == "floor") return Value::MakeNumber(std::floor(num(0)));
                if (name == "ceil") return Value::MakeNumber(std::ceil(num(0)));
                if (name == "round") return Value::MakeNumber(std::round(num(0)));
                if (name == "sqrt") return Value::MakeNumber(std::sqrt(std::max(0.0, num(0))));
                if (name == "pow") return Value::MakeNumber(std::pow(num(0), num(1)));
                if (name == "exp") return Value::MakeNumber(std::exp(num(0)));
                if (name == "ln") return Value::MakeNumber(std::log(num(0)));
                if (name == "log") return Value::MakeNumber(std::log10(num(0)));
                if (name == "sign") { const double v = num(0); return Value::MakeNumber((v > 0) - (v < 0)); }
                if (name == "sin") return Value::MakeNumber(std::sin(num(0) * kDeg2Rad));
                if (name == "cos") return Value::MakeNumber(std::cos(num(0) * kDeg2Rad));
                if (name == "tan") return Value::MakeNumber(std::tan(num(0) * kDeg2Rad));
                if (name == "asin") return Value::MakeNumber(std::asin(num(0)) / kDeg2Rad);
                if (name == "acos") return Value::MakeNumber(std::acos(num(0)) / kDeg2Rad);
                if (name == "atan") return Value::MakeNumber(std::atan(num(0)) / kDeg2Rad);
                if (name == "atan2") return Value::MakeNumber(std::atan2(num(0), num(1)) / kDeg2Rad);
                if (name == "len")
                {
                    if (!a.empty() && a[0].type == Value::Type::Vec) return Value::MakeNumber(static_cast<double>(a[0].vec.size()));
                    if (!a.empty() && a[0].type == Value::Type::Str) return Value::MakeNumber(static_cast<double>(a[0].str.size()));
                    return Value::MakeNumber(0.0);
                }
                if (name == "norm")
                {
                    if (!a.empty() && a[0].type == Value::Type::Vec)
                    {
                        double s = 0.0;
                        for (const Value& c : a[0].vec) s += c.AsNumber(0.0) * c.AsNumber(0.0);
                        return Value::MakeNumber(std::sqrt(s));
                    }
                    return Value::MakeNumber(0.0);
                }
                if (name == "concat")
                {
                    std::vector<Value> out;
                    for (const Value& v : a)
                    {
                        if (v.type == Value::Type::Vec) out.insert(out.end(), v.vec.begin(), v.vec.end());
                        else out.push_back(v);
                    }
                    return Value::MakeVec(std::move(out));
                }
                if (name == "str")
                {
                    std::string s;
                    for (const Value& v : a) s += ValueToString(v);
                    return Value::MakeStr(std::move(s));
                }

                Warn("unknownfn", "unknown function '" + name + "'");
                return Value();
            }
        };
    } // namespace

    bool ScadEvaluator::Evaluate(const Scope& mainTopLevel,
                                 const std::unordered_map<std::string, StmtPtr>& modules,
                                 const std::unordered_map<std::string, StmtPtr>& functions,
                                 const ScadLoadOptions& options,
                                 EvalResult& outResult,
                                 std::string& outError)
    {
        outError.clear();
        Evaluator evaluator(modules, functions, options, outResult);
        evaluator.RunFlat(mainTopLevel);
        return true;
    }

    bool ScadEvaluator::EvaluateScene(const Scope& mainTopLevel,
                                      const std::unordered_map<std::string, StmtPtr>& modules,
                                      const std::unordered_map<std::string, StmtPtr>& functions,
                                      const ScadLoadOptions& options,
                                      SceneEvalResult& outResult,
                                      std::string& outError)
    {
        outError.clear();
        Evaluator evaluator(modules, functions, options, outResult);
        evaluator.RunScene(mainTopLevel);
        return true;
    }
} // namespace Assets::scad
