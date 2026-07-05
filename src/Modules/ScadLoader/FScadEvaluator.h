#pragma once

// ============================================================================
// FScadEvaluator.h - Evaluates a parsed SCAD program into colored geometry.
//
// MVP semantics:
//   * union/group/hull/minkowski  -> concatenate all child geometry
//   * difference/intersection      -> keep only the first child (degraded; a
//                                     true boolean backend (Manifold) is a
//                                     later phase). Emits a one-time warning.
// Evaluate() keeps the legacy flat output grouped by leaf user-module instance
// (falling back to the root instance label when no user module is active) +
// quantized color. EvaluateScene() additionally reconstructs the user-module
// hierarchy so the loader can emit parent/child Nodes and merge direct geometry
// on each logical module node.
// Triangles are in native SCAD world space (Z-up); the loader converts to Y-up.
// ============================================================================

#include <cstdint>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

#include "Modules/ScadLoader/FScadTypes.h"

namespace Assets::scad
{
    struct ColorBucket
    {
        glm::vec4 color = glm::vec4(0.78f, 0.78f, 0.78f, 1.0f);
        std::string groupName;
        std::vector<glm::dvec3> tris; // triangle soup (3N), SCAD world space (Z-up)
    };

    struct BucketKey
    {
        std::string groupName;
        uint64_t groupInstanceId = 0;
        uint32_t colorKey = 0;

        bool operator<(const BucketKey& other) const
        {
            if (groupName != other.groupName)
            {
                return groupName < other.groupName;
            }
            if (groupInstanceId != other.groupInstanceId)
            {
                return groupInstanceId < other.groupInstanceId;
            }
            return colorKey < other.colorKey;
        }
    };

    struct EvalResult
    {
        std::map<BucketKey, ColorBucket> buckets; // ordered by group label + instance + color for determinism
        int warningCount = 0;
        size_t triangleCount = 0;
    };

    struct SceneMeshBucket
    {
        glm::vec4 color = glm::vec4(0.78f, 0.78f, 0.78f, 1.0f);
        std::vector<glm::dvec3> tris; // triangle soup (3N), local to the owning scene node
    };

    struct SceneNode
    {
        std::string name;
        uint64_t instanceId = 0;
        glm::dmat4 localTransform = glm::dmat4(1.0);
        std::vector<SceneMeshBucket> meshes;
        std::vector<SceneNode> children;
    };

    struct SceneEvalResult
    {
        std::vector<SceneNode> roots;
        // Final top-level variable bindings (snapshot of the global scope after
        // the main file's top level runs). Lets rig/animation loaders read data
        // variables (e.g. anim_*) without re-evaluating expressions.
        std::map<std::string, Value> topLevelVariables;
        int warningCount = 0;
        size_t triangleCount = 0;
    };

    class ScadEvaluator
    {
    public:
        // modules/functions are the merged definition tables from the use/include closure.
        // mainTopLevel is the executable top-level scope of the main file only.
        static bool Evaluate(const Scope& mainTopLevel,
                             const std::unordered_map<std::string, StmtPtr>& modules,
                             const std::unordered_map<std::string, StmtPtr>& functions,
                             const ScadLoadOptions& options,
                             EvalResult& outResult,
                             std::string& outError);

        static bool EvaluateScene(const Scope& mainTopLevel,
                                  const std::unordered_map<std::string, StmtPtr>& modules,
                                  const std::unordered_map<std::string, StmtPtr>& functions,
                                  const ScadLoadOptions& options,
                                  SceneEvalResult& outResult,
                                  std::string& outError);
    };
} // namespace Assets::scad
