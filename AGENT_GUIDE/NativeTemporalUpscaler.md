# Native temporal upscalers

`NextTemporalUpscaler` owns two compute-only temporal providers with no binary SDK dependency:
Native TAAU (`r.upscalerType 4`) and Snapdragon Game Super Resolution 2 (`r.upscalerType 5`). Both are registered
on every Vulkan target, including Windows, Linux, macOS/iOS through MoltenVK, and Android.

## Pipeline

The provider consumes the existing `IUpscaler` color, render-pixel motion-vector, and depth
contract and records two display-resolution compute passes, followed by the engine's shared
temporal-upscaler post-filter when `r.upscaler.postFilter` is enabled:

1. `Process.NativeTemporalReproject`: Catmull-Rom reconstruction of the jittered render image,
   motion-vector reprojection, depth disocclusion rejection, YCoCg variance/neighborhood
   clipping, luminance reactivity, and confidence-based temporal locking.
2. `Process.NativeTemporalSharpen`: a conservative, edge-adaptive five-tap sharpen over the
   completed temporal image.
3. The renderer's shared bindless `Process.TemporalPostFilter` pass, also used after FidelityFX FSR.
   Its first pass rejects isolated fireflies from neighbor-only luminance statistics, and later
   passes apply normal/albedo-guided B3-spline a-trous filtering with expanding support.

Color history uses ping-pong `R16G16B16A16_SFLOAT` images, depth history uses ping-pong
`R32_SFLOAT` images, and luminance moments use ping-pong `R16G16_SFLOAT` images. These provider
resources remain in `GENERAL` layout and are destroyed on swapchain teardown. The renderer owns
one bindless post-filter ping/pong pair per swapchain image and reuses it for both temporal FSR
and Native TAAU. The provider restores the engine depth attachment layout after dispatch.

## Controls

- `r.upscalerType 5`: select the BSD-3-Clause SGSR2 2-pass compute provider. It uses the official
  Convert + Upscale data flow, render-resolution `RGBA16F`/`R32UI` intermediates, and ping-pong
  display-resolution `RGBA16F` history. The engine's full-scene render-pixel motion is converted
  to SGSR2's Vulkan clip-space convention in Convert. Published SGSR2 ratios stop at 2x, so the
  shared Ultra Performance mode is clamped to the 2x Performance extent for this provider. The
  port tracks Qualcomm's [SGSR2 v2 source](https://github.com/SnapdragonGameStudios/snapdragon-gsr/tree/main/sgsr/v2)
  and retains its BSD-3-Clause notice under `assets/shaders/third_party/sgsr2/`.
- `r.upscalerType 4`: select Native TAAU.
- `r.superResolution`: shared Quality/Balanced/Performance/Ultra Performance/Native/Auto mode.
- `r.taau.historyWeight` (default `0.97`, range `0.5..0.98`): stable-history contribution.
- `r.taau.sharpness` (default `0.25`, range `0..1`): display-resolution adaptive sharpening.
- `r.upscaler.postFilter` and its `postFilterPasses`, `postFilterStrength`, `postFilterLumaSigma`,
  and `fireflySigma` controls are shared by temporal FSR, SGSR2, and Native TAAU for compatibility.
  All three paths reuse the same bindless ping/pong images and `Process.TemporalPostFilter` a-trous
  compute pipeline.

The main renderer and CVar layer expose one ordered upscaler type: None, DLSS, DLSS Ray
Reconstruction, FidelityFX FSR, Native TAAU, and SGSR2. There is no provider priority chain or
spatial FSR1 fallback. Both native providers require depth and
motion outputs, swapchain storage-image usage, and formatless storage-image access. SGSR2 also
requires sampled/storage support for `RGBA16F` and `R32UI`; unsupported devices disable the
provider without failing startup.

## Validation

Use the deterministic smoke script for both visual and synchronization validation:

```bash
gnb validate --script assets/agentscripts/native-taau-smoke.agentscript.json
gnb validate --script assets/agentscripts/native-taau-smoke.agentscript.json --sync-validation
gnb validate --script assets/agentscripts/native-taau-motion.agentscript.json
gnb validate --script assets/agentscripts/sgsr2-smoke.agentscript.json
gnb validate --script assets/agentscripts/sgsr2-motion.agentscript.json
```

The first implementation derives reactivity from color change because the renderer contract has
no explicit reactive/transparency mask. Thin alpha-tested or highly emissive animated content is
therefore the main area for future quality work; adding optional reactive and composition masks
to `FFrameInputs` is the intended extension point.
