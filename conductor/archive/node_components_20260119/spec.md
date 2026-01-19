# Specification: Node Component Refactor

## Overview
This track involves a systematic refactoring of the `Node` class to transition from a monolithic structure to a component-based architecture. Specifically, rendering and physics-related properties will be abstracted into dedicated `RenderComponent` and `PhysicsComponent` classes. Additionally, the recently introduced `SkinnedMeshComponent` will be integrated into this new management system. This will simplify the `Node` interface, improve modularity, and allow for a cleaner separation of concerns.

## Functional Requirements
1.  **Component Base System:**
    -   Implement or refine a base `Component` class if not already present.
    -   Update `Node` to support generic component management via template methods: `AddComponent<T>()` and `GetComponent<T>()`.
    -   Enforce a constraint where each `Node` can have at most one instance of a specific component type.

2.  **RenderComponent:**
    -   Move the following from `Node` to `RenderComponent`:
        -   `modelId` (uint32_t)
        -   `materialIdx` (std::array<uint32_t, 16>)
        -   `visible` (bool)
        -   `rayCastVisible` (bool)
    -   Transfer related logic (visibility toggling, etc.) to this component.

3.  **PhysicsComponent:**
    -   Move the following from `Node` to `PhysicsComponent`:
        -   `physicsBodyTemp` (NextBodyID)
        -   `mobility` (ENodeMobility)
        -   `physicsOffset` (glm::vec3)
    -   Transfer related physics interaction logic to this component.

4.  **SkinnedMeshComponent Integration:**
    -   Ensure `SkinnedMeshComponent` inherits from the base `Component` class (if it doesn't already) or is adapted to fit the new `AddComponent/GetComponent` system.
    -   Move direct `Node` references to `SkinnedMeshComponent` (like `skinnedMesh_` pointer and `skinIndex`) to be managed via the component system (likely removing `skinIndex` or moving it to `RenderComponent` if it's strictly render-data, but the component itself should be retrieved via the generic interface). *Clarification: The `skinnedMesh` pointer itself is the component instance.*

5.  **Node Interface Simplification:**
    -   Remove direct member variables and explicit getter/setter methods for the moved properties from the `Node` class.
    -   Update all call sites in the engine (Scene loading, Rendering, Physics simulation) to use the new component-based access.

## Non-Functional Requirements
-   **No Backward Compatibility:** This refactor does not need to maintain compatibility with the previous `Node` API.
-   **Performance:** The transition to components should not introduce significant performance overhead during scene traversal or updates.
-   **Clean Code:** Strictly adhere to the project's `.clang-format` and modernization standards.

## Acceptance Criteria
-   `Node` class header is significantly smaller and focused on hierarchy and transforms.
-   Render, Physics, and SkinnedMesh properties are successfully accessed and modified via the generic component system.
-   The project compiles successfully without any remaining references to the old `Node` members.
-   The renderer and physics simulation function correctly with the new structure.

## Out of Scope
-   Moving transform logic to a `TransformComponent` (at this stage, `Node` retains its transform data).
