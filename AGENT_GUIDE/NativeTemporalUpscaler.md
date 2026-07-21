# NativeTemporal Upscaler

`NextTemporalUpscaler` is the engine-owned, compute-only temporal upscaler. It has no vendor
SDK dependency and is registered on every Vulkan target, including Windows, Linux, macOS/iOS
through MoltenVK, and Android. At runtime it is exposed as `Native TAAU` / `r.taau`.

## Pipeline

The provider consumes the existing `IUpscaler` color, render-pixel motion-vector, and depth
contract and records two display-resolution compute passes, followed by the engine's shared
temporal-upscaler post-filter when `r.fsr.postFilter` is enabled:

1. `Process.NativeTemporalReproject`: Catmull-Rom reconstruction of the jittered render image,
   motion-vector reprojection, depth disocclusion rejection, YCoCg variance/neighborhood
   clipping, luminance reactivity, and confidence-based temporal locking.
2. `Process.NativeTemporalSharpen`: a conservative, edge-adaptive five-tap sharpen over the
   completed temporal image.
3. `Process.NativeTemporalAtrous`: an explicit-descriptor variant of the same
   normal/albedo-guided B3-spline a-trous algorithm used by `Process.FsrPostFilter`. Its first
   pass rejects isolated fireflies from neighbor-only luminance statistics, and later passes
   expand the filter support without downsampling. Keeping the Native variant inside the
   provider avoids depending on the engine's global bindless post-filter set.

Color history uses ping-pong `R16G16B16A16_SFLOAT` images, depth history uses ping-pong
`R32_SFLOAT` images, luminance moments use ping-pong `R16G16_SFLOAT` images, and the final
a-trous stage uses two `R16G16B16A16_SFLOAT` scratch images. All provider resources remain in `GENERAL` layout and are destroyed on
swapchain teardown. The provider restores the engine depth attachment layout after dispatch.

## Controls

- `r.taau`: enable Native TAAU.
- `r.superResolution`: shared Quality/Balanced/Performance/Ultra Performance/Native/Auto mode.
- `r.taau.historyWeight` (default `0.97`, range `0.5..0.98`): stable-history contribution.
- `r.taau.sharpness` (default `0.25`, range `0..1`): display-resolution adaptive sharpening.
- `r.fsr.postFilter` and its `postFilterPasses`, `postFilterStrength`, `postFilterLumaSigma`,
  and `fireflySigma` controls are shared by temporal FSR and Native TAAU for compatibility.

The main renderer UI keeps DLSS, FSR, and Native TAAU mutually exclusive. If several cvars are
set externally, selection priority is DLSS, requested FSR (FidelityFX when available, otherwise
the spatial FSR1 fallback), then Native TAAU. Native TAAU requires depth and motion outputs, swapchain storage-image usage, and
formatless storage-image reads/writes; unsupported devices disable it without failing startup.

## Validation

Use the deterministic smoke script for both visual and synchronization validation:

```bash
gnb validate --script assets/agentscripts/native-taau-smoke.agentscript.json
gnb validate --script assets/agentscripts/native-taau-smoke.agentscript.json --sync-validation
gnb validate --script assets/agentscripts/native-taau-motion.agentscript.json
```

The first implementation derives reactivity from color change because the renderer contract has
no explicit reactive/transparency mask. Thin alpha-tested or highly emissive animated content is
therefore the main area for future quality work; adding optional reactive and composition masks
to `FFrameInputs` is the intended extension point.
