#pragma once

// ============================================================================
// FScadShared.h - Internal helpers shared between the SCAD scene loader
// (FScadLoader) and the SCAD rig loader (FScadRigLoader): use/include closure
// resolution and SCAD (Z-up) -> engine (Y-up) conversions.
// ============================================================================

#include <cstdint>
#include <string>
#include <unordered_map>
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
} // namespace Assets::Scad
