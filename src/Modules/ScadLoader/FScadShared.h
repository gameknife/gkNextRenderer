#pragma once

// ============================================================================
// FScadShared.h - Internal helpers shared between the SCAD scene loader
// (FScadLoader) and the SCAD rig loader (FScadRigLoader): use/include closure
// resolution and SCAD (Z-up) -> engine (Y-up) conversions.
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "Modules/ScadLoader/FScadTypes.h"

namespace Assets::Scad
{
    // Parsed use/include closure of a .scad program: merged definition tables
    // plus the executable top level of the main file (and its includes).
    struct ScadProgram
    {
        std::unordered_map<std::string, StmtPtr> modules;
        std::unordered_map<std::string, StmtPtr> functions;
        Scope mainTopLevel;
    };

    // Resolves filename (platform-relative or absolute), reads the file and its
    // use/include closure, and lexes/parses everything into outProgram.
    bool LoadScadProgram(const std::string& filename, ScadProgram& outProgram, std::string& outError);

    // Reads a runtime-root-relative asset as raw bytes through the package file
    // system (with a loose-file fallback). Same resolution the use/include
    // closure uses, so binary side-car assets referenced from .scad (e.g. the
    // terrain .hmap) work identically in packed and loose builds.
    bool ScadReadAsset(const std::string& path, std::vector<uint8_t>& out);

    // Converts a SCAD (Z-up) point to engine (Y-up) space with uniform scale.
    glm::vec3 ScadToWorldPos(const glm::dvec3& p, double scale);

    // Basis matrix for the Z-up -> Y-up change (pure rotation times scale).
    glm::dmat4 ScadToWorldBasis(double scale);

    // Stable per-load identity for a concrete SCAD call site. It is based on
    // the call's source byte offset, so same-named modules on one line remain
    // distinct placement targets.
    std::string ScadPlacementTag(size_t sourceOffset);

    // Conjugates a SCAD-local transform into engine space and decomposes it.
    void ScadLocalToEngineTRS(
        const glm::dmat4& scadLocal,
        double scale,
        glm::vec3& outTranslation,
        glm::quat& outRotation,
        glm::vec3& outScale);

    // OpenSCAD rotate([x,y,z]) semantics: Rz * Ry * Rx, degrees.
    glm::dmat4 ScadRotateXYZ(const glm::dvec3& degrees);

    // Per-corner smoothed normals for a triangle soup (see FScadLoader).
    std::vector<glm::vec3> ScadComputeSmoothNormals(const std::vector<glm::vec3>& pos, float angleThresholdDeg);

    // ------------------------------------------------------------------
    // Node metadata: the evaluated named parameters of a user-module call,
    // serialized as "k=v;k=v" into Assets::Node::SetMetadata by the scene
    // loader. Numbers use %g, bools "true"/"false", strings are copied
    // verbatim; vectors and ranges are skipped (gameplay reads scalars).
    // ------------------------------------------------------------------
    using FMetadata = std::vector<std::pair<std::string, std::string>>;

    FMetadata ParseScadMetadata(std::string_view metadata);
    // Reads one key out of an already parsed metadata table.
    double MetadataNumber(const FMetadata& metadata, std::string_view key, double fallback);
    bool MetadataBool(const FMetadata& metadata, std::string_view key, bool fallback);
    std::string MetadataString(const FMetadata& metadata, std::string_view key, std::string_view fallback);
} // namespace Assets::Scad
