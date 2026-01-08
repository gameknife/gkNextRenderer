# Implementation Plan: NVIDIA Streamline Integration

## Phase 1: Foundation & Detection [checkpoint: 3dbefd2]
- [x] Task: Configure CMake to link Streamline SDK and handle `WITH_STREAMLINE` macro
- [x] Task: Implement Streamline basic initialization and shutdown lifecycle in `Renderer`
- [x] Task: Implement GPU capability detection and DLSS feature support queries
- [x] Task: Conductor - User Manual Verification 'Foundation & Detection' (Protocol in workflow.md)

## Phase 2: Buffer Preparation & DLSS SR [checkpoint: d97c9ae]
- [x] Task: Implement/Verify Motion Vector generation and Jittered Projection matrix
- [x] Task: Implement DLSS SR resource mapping (Color, Depth, MV, Exposure)
- [x] Task: Integrate DLSS SR evaluation into the main render loop
- [x] Task: Conductor - User Manual Verification 'Buffer Preparation & DLSS SR' (Protocol in workflow.md)

## Phase 3: DLSS Ray Reconstruction (RR) [checkpoint: d68b71a]
- [x] Task: Implement RR-specific resource mapping (Hit Distance, Albedo, etc.)
- [x] Task: Integrate DLSS RR evaluation and implement logic to bypass standard denoisers when RR is active
- [x] Task: Conductor - User Manual Verification 'DLSS Ray Reconstruction (RR)' (Protocol in workflow.md)

## Phase 4: UI & Configuration [checkpoint: 822207a]
- [x] Task: Add DLSS settings panel to the ImGui editor (Mode, RR Toggle, Sharpness)
- [x] Task: Implement CLI argument parsing for initial DLSS state
- [x] Task: Conductor - User Manual Verification 'UI & Configuration' (Protocol in workflow.md)

## Phase 5: Refinement & Validation [checkpoint: cb2bba1]
- [x] Task: Address visual artifacts (ghosting, moiré) through buffer fine-tuning
- [x] Task: Final performance benchmarking and stability testing on supported hardware
- [x] Task: Conductor - User Manual Verification 'Refinement & Validation' (Protocol in workflow.md)
