# Plan: Node Component Refactor

## Phase 1: Foundation & Component System [checkpoint: f65b3e4]
- [x] Task: Define `Component` base class and generic management in `Node` f65b3e4
    - [x] Create `src/Assets/Component.h` with a base `Component` class.
    - [x] Add `std::vector<std::shared_ptr<Component>> components_` to `Node`.
    - [x] Implement `Node::AddComponent<T>()` with "one per type" enforcement.
    - [x] Implement `Node::GetComponent<T>()` using `dynamic_pointer_cast`.
- [x] Task: Conductor - User Manual Verification 'Phase 1: Foundation & Component System' (Protocol in workflow.md) f65b3e4

## Phase 2: RenderComponent Refactor [checkpoint: ea08589]
- [x] Task: Create `RenderComponent` and migrate data ea08589
    - [x] Implement `RenderComponent` inheriting from `Component`.
    - [x] Move `modelId`, `materialIdx`, `visible`, and `rayCastVisible` to `RenderComponent`.
    - [x] Update `Node` to remove these fields and their direct getters/setters.
- [x] Task: Update Renderer and Scene loading for `RenderComponent` ea08589
    - [x] Update `GltfLoader` or equivalent to attach `RenderComponent` during node creation.
    - [x] Update rendering loops to fetch data via `node->GetComponent<RenderComponent>()`.
- [x] Task: Conductor - User Manual Verification 'Phase 2: RenderComponent Refactor' (Protocol in workflow.md) ea08589

## Phase 3: PhysicsComponent Refactor
- [ ] Task: Create `PhysicsComponent` and migrate data
    - [ ] Implement `PhysicsComponent` inheriting from `Component`.
    - [ ] Move `physicsBodyTemp`, `mobility`, and `physicsOffset` to `PhysicsComponent`.
    - [ ] Update `Node` to remove these fields and their direct getters/setters.
- [ ] Task: Update Physics System for `PhysicsComponent`
    - [ ] Update physics initialization to use `node->GetComponent<PhysicsComponent>()`.
    - [ ] Update physics-to-transform sync logic.
- [ ] Task: Conductor - User Manual Verification 'Phase 3: PhysicsComponent Refactor' (Protocol in workflow.md)

## Phase 4: SkinnedMeshComponent Integration
- [ ] Task: Adapt `SkinnedMeshComponent` to the new system
    - [ ] Update `SkinnedMeshComponent` to inherit from `Component`.
    - [ ] Move `skinIndex` (if applicable) to `RenderComponent` or manage via `SkinnedMeshComponent`.
    - [ ] Remove `skinnedMesh_` pointer from `Node`.
- [ ] Task: Update Skinned Mesh call sites
    - [ ] Update animation and skinning logic to retrieve the component via `GetComponent<SkinnedMeshComponent>()`.
- [ ] Task: Conductor - User Manual Verification 'Phase 4: SkinnedMeshComponent Integration' (Protocol in workflow.md)

## Phase 5: Cleanup & Verification
- [ ] Task: Final `Node` interface cleanup
    - [ ] Remove all redundant helper methods in `Node`.
    - [ ] Ensure `Node` header is minimal (Hierarchy + Transform).
- [ ] Task: Run full test suite and verify renderer/physics
    - [ ] Execute `gkNextUnitTests.exe`.
    - [ ] Manually verify complex scenes (with skinning and physics) in the renderer.
- [ ] Task: Conductor - User Manual Verification 'Phase 5: Cleanup & Verification' (Protocol in workflow.md)
