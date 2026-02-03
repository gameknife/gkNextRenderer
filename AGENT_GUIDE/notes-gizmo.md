# Gizmo Integration Notes (gkNextEditor / gkNextRenderer)

This note captures practical lessons from adding ImGuizmo-based transforms and selection edge highlighting.

## Key Takeaways
- Prefer a shared gizmo controller for Editor/Renderer so input gating and transform updates stay consistent.
- Use ImGuizmo with engine UBO matrices; for Vulkan + ImGui, flip projection Y (`projection[1][1] *= -1`) to avoid inverted gizmo.
- `ImGuizmo::SetDrawlist(ImGui::GetForegroundDrawList())` avoids creating the `Debug##Default` window.
- Input isolation: when gizmo is over/using, skip camera controls (mouse/key/scroll) to prevent viewport drift.
- Update CPU BVH after gizmo edits; a lightweight option is to call `UpdateBVH` when the drag ends (transition from using->not using).
- For tool UI, anchor the toolbar inside the active viewport and disable docking to prevent overlap.

## Useful Integration Points
- Selection: set `Scene::SetSelectedId()` on hit and toggle `ShowEdge` for visual feedback.
- Gizmo draw call should run during UI render with the correct viewport rect (swapchain output for renderer; central dock viewport for editor).

## Gotchas
- `ImGuizmo::BeginFrame()` is fine, but without an explicit draw list it can spawn an empty ImGui window.
- Avoid updating BVH every frame while dragging unless necessary; it is expensive.
