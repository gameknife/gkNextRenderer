# Specification: glTF Skinned Animation Support

## Overview
This track implements glTF 2.0 skinned animation support in the `gkNextRenderer`. It enables the loading and rendering of animated skeletal meshes, utilizing GPU compute shaders for efficient skinning and ensuring compatibility with the ray tracing pipeline.

## Functional Requirements
- **Skeletal Data Loading:** Import joint hierarchies, bind poses, and inverse bind matrices from glTF assets using `tinygltf`.
- **Animation Loading:** Import animation channels (Translation, Rotation, Scale) and samplers from glTF.
- **Compute Shader Skinning:** 
    - Implement a compute pass to calculate vertex deformations based on bone weights and matrices.
    - Support standard glTF skinning (max 4 weights per vertex initially).
- **Ray Tracing Integration:** 
    - **BLAS Update:** Ensure the Bottom-Level Acceleration Structure (BLAS) is updated or rebuilt after the compute skinning pass to reflect the deformed mesh geometry in ray tracing.
- **Unified Component:** Implement `SkinnedMeshComponent` to manage mesh data, skeletal hierarchy, and animation playback state.
- **Playback Control:**
    - Support basic playback: Play, Stop, Loop, and One-shot.
    - Select animations via their string names defined in the glTF file.
- **Debug Visualization:** Provide a toggleable debug view to render the skeleton hierarchy (joints and bones) as lines or points.

## Non-Functional Requirements
- **Performance:** Skinning must be handled on the GPU to minimize CPU overhead.
- **Engine Integration:** Follow existing patterns for Bindless rendering and GPU-driven data management.
- **Stability:** Ensure robust handling of glTF files with varying bone counts or missing animation data.

## Acceptance Criteria
- [ ] Successfully load and display a glTF model with a skeleton.
- [ ] Play a specific animation clip by name and observe correct vertex deformation.
- [ ] **Ray Tracing Verification:** Ensure reflections/shadows update correctly as the mesh animates (verifying BLAS update).
- [ ] Toggle debug view to see the bone hierarchy correctly overlaid on the model.
- [ ] No significant performance regression in scenes without skinned meshes.

## Out of Scope
- Animation blending/cross-fading.
- Inverse Kinematics (IK).
- Complex animation state machines or layering.
- CPU-based skinning fallback.
