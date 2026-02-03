# Implementation Plan: MagicaLego UI Modernization & Gamification

## Phase 1: Infrastructure & Dependencies
- [x] Task: Integrate `ImAnim` library via `cmake/SetupExternalLibs.cmake` using `FetchContent`. 3320b5f
- [x] Task: Update `CMakeLists.txt` to include `ImAnim` and link it to the `MagicaLego` target (or Engine if needed). 3320b5f
- [x] Task: Verify `ImAnim` integration by including its header in a dummy test or `MagicaLegoUserInterface.cpp`. 3320b5f
- [ ] Task: Conductor - User Manual Verification 'Phase 1' (Protocol in workflow.md)

## Phase 2: Theme & Visual Foundations
- [ ] Task: Define Lego-themed color palette and Glassmorphism style constants (e.g., `LegoYellow`, `GlassBackground`, `Rounding`).
- [ ] Task: Implement a helper function or update `UserInterface` to apply the new ImGui style (Colors, Rounding, ChildBorderSize).
- [ ] Task: Integrate modern icons (ensure FontAwesome 6 is fully utilized) and a modern sans-serif font.
- [ ] Task: Conductor - User Manual Verification 'Phase 2' (Protocol in workflow.md)

## Phase 3: HUD-style Layout Implementation
- [ ] Task: Modify `MagicaLegoUserInterface::DrawLeftBar`, `DrawRightBar`, and `DrawMainToolBar` to use floating window flags (`ImGuiWindowFlags_NoDocking`, `ImGuiWindowFlags_NoResize`, etc.).
- [ ] Task: Position these HUD panels relative to the viewport corners/edges with appropriate padding.
- [ ] Task: Update panels to use semi-transparent backgrounds and rounded corners to achieve the Glassmorphism effect.
- [ ] Task: Conductor - User Manual Verification 'Phase 3' (Protocol in workflow.md)

## Phase 4: Animation & Interactivity (ImAnim)
- [ ] Task: Implement entrance/exit animations for HUD panels using `ImAnim` (e.g., sliding from off-screen).
- [ ] Task: Add hover and click feedback animations to buttons and interactive controls.
- [ ] Task: Implement smooth transitions for HUD elements when switching editor modes (Selection/Move/Rotate).
- [ ] Task: Animate the notification system (`DrawNotify`).
- [ ] Task: Conductor - User Manual Verification 'Phase 4' (Protocol in workflow.md)

## Phase 5: Polishing & Performance
- [ ] Task: Fine-tune animation timings and transparency levels for optimal "game feel".
- [ ] Task: Verify UI performance and ensure no frame rate regression due to transparency/animations.
- [ ] Task: Final code cleanup and documentation of new UI utilities.
- [ ] Task: Conductor - User Manual Verification 'Phase 5' (Protocol in workflow.md)
