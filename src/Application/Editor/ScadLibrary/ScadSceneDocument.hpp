#pragma once

// ============================================================================
// ScadSceneDocument.hpp - One editable model for a .scad scene root.
//
// A scene file is NOT one of three mutually exclusive kinds any more. It is an
// ordered list of top-level statements, each of which the editor classifies on
// its own:
//
//   Instance     a `[color] translate rotate scale kit_module(...)` chain that
//                the object list and the viewport gizmo can edit directly
//   Terrain      the TERR spec assignment and its gk_terrain() call
//   TerrainRule  a top-level ter_* placement rule
//   Source       everything else - loops, conditionals, module/function
//                definitions, free geometry - kept byte-for-byte
//
// All four coexist in one file: a program written by hand can gain hand-placed
// instances, a generated instance list can gain a terrain, and either can gain
// procedural scatter rules. Writing back only splices the statements the user
// actually edited, so comments, formatting and unsupported syntax survive.
//
// A single structure can also be switched off in place (OpenSCAD's `*`
// modifier, which the evaluator already honours) and replaced by the concrete
// instances its evaluation produced - the local equivalent of what used to be
// a whole-file "convert to evaluated" conversion.
// ============================================================================

#include "TerrainProcessDocument.hpp"

#include "Modules/ScadLoader/FScadEvaluator.h"
#include "Modules/ScadLoader/FScadSourceIndex.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <limits>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace ScadLibrary
{
    enum class EScadSegmentKind
    {
        Instance,
        Terrain,
        TerrainRule,
        Source,
    };

    const char* ScadSegmentKindLabel(EScadSegmentKind kind);

    // One placed instance in a scene. `sourceBegin`/`sourceEnd` address the
    // statement it was parsed from; npos means the editor created it and it
    // still has to be written into the file at `insertAt`.
    struct FBenchItem
    {
        int kitIndex = -1;
        std::string moduleName;
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float rotX = 0.0f;
        float rotY = 0.0f;
        float rotZ = 0.0f;
        float scale = 1.0f;
        float scaleY = 1.0f;
        float scaleZ = 1.0f;
        float color[4] = {0.78f, 0.78f, 0.78f, 1.0f};
        bool hasColor = false;
        char args[512] = {}; // extra call arguments, e.g. "seed = 3"
        bool evaluated = false;
        int sourceLine = 0;
        uint32_t runtimeNodeId = std::numeric_limits<uint32_t>::max();

        // Document bookkeeping.
        size_t sourceBegin = std::string::npos;
        size_t sourceEnd = std::string::npos;
        size_t insertAt = std::string::npos;
        int segmentIndex = -1;
        int originSegment = -1; // Source segment this was exploded from
        bool disabled = false;
        bool removed = false;

        bool SamePlacement(const FBenchItem& other) const;
    };

    struct FScadSceneSegment
    {
        EScadSegmentKind kind = EScadSegmentKind::Source;
        // Only StmtKind::Instance statements can carry OpenSCAD's `*` modifier,
        // so only those can be switched off or exploded in place.
        Assets::Scad::StmtKind statementKind = Assets::Scad::StmtKind::Instance;
        size_t begin = 0;
        size_t end = 0;
        int line = 1;
        int endLine = 1;
        std::string name;  // module / variable name of the statement
        std::string label; // single-line summary for the outliner
        bool disabled = false;
        bool disabledByEditor = false; // switched off through Explode/SetDisabled
        int instanceIndex = -1;        // Instance segments
        int ruleIndex = -1;            // TerrainRule segments
        int explodedInstances = 0;     // instances this Source segment produced
    };

    // Extra inputs BuildSource needs that the document does not own.
    struct FScadSceneWriteOptions
    {
        // `use <...>` paths (already relative to the output file) that the
        // written source must declare. Missing ones are added to the directive
        // block; existing ones are left alone.
        std::vector<std::string> requiredUsePaths;
    };

    class FScadSceneDocument
    {
    public:
        // `index` must be the statement index of `source` itself, and
        // `topLevelVariables` the evaluated top-level scope of the same file.
        // `isKitModule` decides which module calls are editable instances;
        // unresolved calls stay Source, so builtins and marker modules are
        // never mistaken for placed objects.
        bool Parse(std::string source, const Assets::Scad::FScadSourceIndex& index,
                   const std::map<std::string, Assets::Scad::Value>& topLevelVariables,
                   const std::function<bool(const std::string&)>& isKitModule,
                   std::vector<std::string>& outWarnings);

        void Clear();

        const std::string& Source() const { return source_; }
        const std::vector<FScadSceneSegment>& Segments() const { return segments_; }
        std::vector<FBenchItem>& Instances() { return instances_; }
        const std::vector<FBenchItem>& Instances() const { return instances_; }
        FTerrainProcessDocument& Terrain() { return terrain_; }
        const FTerrainProcessDocument& Terrain() const { return terrain_; }

        bool HasInstances() const { return !instances_.empty(); }
        bool HasTerrain() const { return hasTerrain_; }
        size_t SourceSegmentCount() const;
        size_t InstanceSegmentCount() const;
        const std::vector<std::string>& TerrainWarnings() const { return terrainWarnings_; }

        // Switches a statement off with OpenSCAD's `*` modifier, or back on.
        // Only instantiation statements accept the modifier; assignments and
        // module/function definitions are refused.
        bool SetSegmentDisabled(size_t segmentIndex, bool disabled);

        // True when the segment is an instantiation statement the editor can
        // switch off or explode.
        bool IsSwitchable(size_t segmentIndex) const;

        // Switches `segmentIndex` off and writes `produced` into the file right
        // after it. This is the per-structure replacement for whole-file
        // source -> evaluated conversion.
        bool ExplodeSegment(size_t segmentIndex, std::vector<FBenchItem> produced, std::string& outError);

        // Undo of ExplodeSegment while the link is still live (before a save
        // and reparse): drops the instances it produced and switches the
        // original statement back on.
        bool CollapseSegment(size_t segmentIndex);

        // Structured editors for source constructs (for example lay_scatter)
        // replace only their owning top-level statement. Untouched source
        // segments remain byte-for-byte intact.
        std::string GetSegmentSource(size_t segmentIndex) const;
        bool ReplaceSegmentSource(size_t segmentIndex, std::string replacement);

        int AddInstance(FBenchItem item);
        void RemoveInstance(int instanceIndex);

        // Existing statements keep their bytes unless their instance actually
        // moved, so opening and saving an untouched file is a no-op.
        std::string BuildSource(const FScadSceneWriteOptions& options) const;

        // Serializes one instance exactly the way BuildSource writes it.
        static std::string SerializeInstance(const FBenchItem& item);

    private:
        // Recognizes `[color] translate rotate scale kit_module(args);`. The
        // transforms must be literal and in canonical order, so writing the
        // statement back reproduces it exactly; anything else stays Source.
        bool ClassifyInstance(const Assets::Scad::Stmt& statement, const Assets::Scad::FScadStatementSpan& span,
                              const std::function<bool(const std::string&)>& isKitModule, FBenchItem& outItem) const;
        void RebuildSegmentLabels();
        void ReindexInstances();

        std::string source_;
        std::vector<FScadSceneSegment> segments_;
        std::vector<FBenchItem> instances_;
        std::vector<FBenchItem> parsedInstances_; // pristine copies for dirty checks
        std::map<size_t, std::string> sourceSegmentReplacements_;
        // Statements whose instance the object list deleted; their bytes are
        // still in `source_` and have to be spliced out on write.
        std::vector<std::pair<size_t, size_t>> deletedSpans_;
        FTerrainProcessDocument terrain_;
        std::vector<std::string> terrainWarnings_;
        bool hasTerrain_ = false;
        size_t directiveInsertPoint_ = 0;
    };
} // namespace ScadLibrary
