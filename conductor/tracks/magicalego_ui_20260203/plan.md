# Implementation Plan: MagicaLego UI Modernization & Gamification

## Phase 1: Infrastructure & Dependencies
- [x] Task: Integrate `ImAnim` library via `cmake/SetupExternalLibs.cmake` using `FetchContent`. 3320b5f
- [x] Task: Update `CMakeLists.txt` to include `ImAnim` and link it to the `MagicaLego` target (or Engine if needed). 3320b5f
- [x] Task: Verify `ImAnim` integration by including its header in a dummy test or `MagicaLegoUserInterface.cpp`. 3320b5f
- [x] Task: Conductor - User Manual Verification 'Phase 1' (Protocol in workflow.md) [checkpoint: 3af554f]

## Phase 2: Theme & Visual Foundations
- [x] Task: Define Lego-themed color palette and Glassmorphism style constants (e.g., `LegoYellow`, `GlassBackground`, `Rounding`). 8a7d2db
- [x] Task: Implement a helper function or update `UserInterface` to apply the new ImGui style (Colors, Rounding, ChildBorderSize). 8a7d2db
- [x] Task: Integrate modern icons (ensure FontAwesome 6 is fully utilized) and a modern sans-serif font. 8a7d2db
- [x] Task: Conductor - User Manual Verification 'Phase 2' (Protocol in workflow.md) [checkpoint: 6f0af35]

## Phase 3: HUD-style Layout Implementation
- [x] Task: Modify `MagicaLegoUserInterface::DrawLeftBar`, `DrawRightBar`, and `DrawMainToolBar` to use floating window flags (`ImGuiWindowFlags_NoDocking`, `ImGuiWindowFlags_NoResize`, etc.). 9196634
- [x] Task: Position these HUD panels relative to the viewport corners/edges with appropriate padding. 9196634
- [x] Task: Update panels to use semi-transparent backgrounds and rounded corners to achieve the Glassmorphism effect. 9196634
- [x] Task: Conductor - User Manual Verification 'Phase 3' (Protocol in workflow.md) [checkpoint: b2ff0ef]

## Phase 4: Animation & Interactivity (ImAnim)
- [x] Task: Implement entrance/exit animations for HUD panels using `ImAnim` (e.g., sliding from off-screen). 5a9cdc8
- [x] Task: Add hover and click feedback animations to buttons and interactive controls. 5a9cdc8
- [x] Task: Implement smooth transitions for HUD elements when switching editor modes (Selection/Move/Rotate). 5a9cdc8
- [x] Task: Animate the notification system (`DrawNotify`). 5a9cdc8
- [x] Task: Conductor - User Manual Verification 'Phase 4' (Protocol in workflow.md) [checkpoint: 6abfe40]

## Phase 5: Polishing & Performance
- [x] Task: Fine-tune animation timings and transparency levels for optimal "game feel". a65ddb3
- [x] Task: Verify UI performance and ensure no frame rate regression due to transparency/animations. a65ddb3
- [x] Task: Final code cleanup and documentation of new UI utilities. a65ddb3
- [ ] Task: Conductor - User Manual Verification 'Phase 5' (Protocol in workflow.md)
