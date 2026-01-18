# Implementation Plan: glTF Skinned Animation Support

## Phase 1: Foundation and Data Structures
- [x] Task: Extend Asset Loading for Skinned Meshes
    - [x] Update `GltfLoader` to parse `skins`, `joints`, and `animations` using `tinygltf`.
    - [x] Define `Skeleton` and `AnimationClip` structures in the `Assets` namespace.
    - [x] Load and store Inverse Bind Matrices.
- [ ] Task: Implement `SkinnedMeshComponent`
    - [ ] Create the component class to store animation playback state and skeleton reference.
    - [ ] Implement playback logic (update current time, sample animation curves).
- [ ] Task: GPU Buffer Management
    - [ ] Allocate GPU buffers for bone matrices (Joint Buffers).
    - [ ] Ensure mesh data (positions, weights, joints) is correctly uploaded for skinning.
- [x] Task: **Checkpoint: Data Verification**
    - [x] **Goal:** Verify that a sample glTF file with skinning data is loaded without errors. Inspect logs or debuggers to ensure bone hierarchies and animation clips are populated correctly in memory. (Verified via debug logs during development)
- [ ] Task: Conductor - User Manual Verification 'Phase 1: Foundation and Data Structures' (Protocol in workflow.md)

## Phase 2: Compute Skinning Implementation
- [ ] Task: Develop Skinning Compute Shader
    - [ ] Write Slang/GLSL compute shader for vertex deformation.
    - [ ] Implement 4-weight linear blend skinning (LBS).
- [ ] Task: Integrate Skinning Pass into Renderer
    - [ ] Add a new compute pass to the frame graph or rendering pipeline.
    - [ ] Handle synchronization (barriers) between skinning pass and subsequent rendering/RT passes.
- [ ] Task: **Checkpoint: Visual Verification (Rasterization)**
    - [ ] **Goal:** Render the animated mesh using standard rasterization (or a simple debug shader). Verify that the mesh deforms visibly when an animation plays, even if Ray Tracing isn't updated yet.
- [ ] Task: Conductor - User Manual Verification 'Phase 2: Compute Skinning Implementation' (Protocol in workflow.md)

## Phase 3: Ray Tracing & BLAS Integration
- [ ] Task: BLAS Update Logic
    - [ ] Modify the acceleration structure builder to support dynamic mesh updates.
    - [ ] Implement `vkCmdBuildAccelerationStructuresKHR` for updating BLAS using the skinned vertex buffer.
    - [ ] Optimize for `VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR`.
- [ ] Task: **Checkpoint: Ray Tracing Verification**
    - [ ] **Goal:** Observe the animated mesh in a full Path Tracing / Ray Tracing view. Verify that reflections and shadows correctly follow the deforming mesh (confirming BLAS updates).
- [ ] Task: Conductor - User Manual Verification 'Phase 3: Ray Tracing & BLAS Integration' (Protocol in workflow.md)

## Phase 4: Debugging and Polish
- [ ] Task: Debug Visualization
    - [ ] Implement a system to draw skeletal hierarchies using the existing line rendering utility.
- [ ] Task: Playback API & Name-based Selection
    - [ ] Expose API to start/stop animations by string name.
- [ ] Task: Integration Test
    - [ ] Create a test case (or sample scene) with a rigged glTF model to verify animation and RT reflections.
- [ ] Task: **Checkpoint: Final Feature Review**
    - [ ] **Goal:** Test full end-to-end functionality: Load asset -> Select Animation by Name -> Play -> Verify Visuals + Debug Lines.
- [ ] Task: Conductor - User Manual Verification 'Phase 4: Debugging and Polish' (Protocol in workflow.md)
