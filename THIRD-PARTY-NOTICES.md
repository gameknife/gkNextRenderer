# Third-Party Notices

gkNextRenderer is distributed under the MIT License (see `LICENSE`). It bundles or links
the third-party components listed below, each of which remains under its own license.
Full license texts ship with the upstream sources; where a component's license file is
distributed inside the release package it is noted explicitly.

Last reviewed: 2026-08-08.

---

## 1. Fonts distributed in `assets/fonts/`

| File | Component | License |
|---|---|---|
| `Roboto-Regular.ttf`, `Roboto-BoldCondensed.ttf` | Roboto (Google Fonts) | Apache License 2.0 |
| `Cousine-Regular.ttf` | Cousine (Steve Matteson / Google Fonts) | Apache License 2.0 |
| `DroidSansFallback.ttf` | Droid Sans Fallback (Google / Android Open Source Project) | Apache License 2.0 |
| `fa-solid-900.ttf`, `fa-regular-400.ttf`, `fa-brands-400.ttf` | Font Awesome Free 6 | Fonts: SIL OFL 1.1 · Icons: CC BY 4.0 · Code: MIT |

Font Awesome brand icons are trademarks of their respective owners; see
<https://fontawesome.com/license/free>.

## 2. NVIDIA components distributed in `bin/`

| Component | Files | License |
|---|---|---|
| NVIDIA DLSS (Super Resolution / Ray Reconstruction / Frame Generation) | `nvngx_dlss.dll`, `nvngx_dlssd.dll`, `nvngx_dlssg.dll` | NVIDIA RTX SDK license — see `bin/nvngx_dlss.license.txt` |
| NVIDIA Streamline | `sl.*.dll` | MIT (Streamline SDK) |
| NVIDIA Image Scaling (NIS) | `sl.nis.dll` | See `bin/nis.license.txt` |
| NVIDIA Reflex / Low Latency | `sl.reflex.dll`, `NvLowLatencyVk.dll` | See `bin/reflex.license.txt` |
| NVIDIA DeepDVC | `nvngx_deepdvc.dll`, `sl.deepdvc.dll` | NVIDIA RTX SDK license |

The `.license.txt` files listed above are shipped inside `bin/` and must remain in the
package.

## 3. AMD components distributed in `bin/`

| Component | Files | License |
|---|---|---|
| AMD FidelityFX SDK (FSR) | `amd_fidelityfx_vk.dll` | MIT |

## 4. Qualcomm components distributed in `assets/shaders/`

| Component | Files | License |
|---|---|---|
| Snapdragon Game Super Resolution 2 (Slang port) | `Process.SGSR2Upscale.comp.slang`, `Process.SGSR2Convert.comp.slang` | BSD-3-Clause — © 2024 Qualcomm Innovation Center, Inc. |

## 5. Khronos / Vulkan runtime

| Component | Files | License |
|---|---|---|
| Vulkan Loader | `vulkan-1.dll` | Apache License 2.0 |
| Vulkan Headers, Vulkan Memory Allocator | linked at build time | Apache License 2.0 / MIT |

## 6. Vendored sources under `src/ThirdParty/`

| Component | License |
|---|---|
| Dear ImGui (`imgui-custom`) | MIT |
| ImGuizmo | MIT |
| imgui_markdown | zlib |
| ImAnim | MIT |
| Font Awesome icon headers (`fontawesome`) | MIT (headers) / OFL 1.1 (fonts) |
| QuickJS-ng (`quickjs-ng`) | MIT |
| ozz-animation (`ozz`) | MIT |
| miniaudio | Public domain (Unlicense) or MIT-0, at your option |
| mikktspace | zlib |
| tinybvh | MIT |
| lzav | MIT |
| RenderDoc API header (`renderdoc`) | MIT |
| FFmpeg (`ffmpeg`, optional, not shipped in the release package) | LGPL v2.1+ / GPL depending on build |

FFmpeg binaries are **not** included in the release package; video recording is disabled
when they are absent.

## 7. Dependencies resolved through vcpkg

Resolved from `vcpkg.json` at build time and statically linked unless noted.

| Component | License |
|---|---|
| Catch2 | BSL-1.0 |
| cpp-base64 | zlib |
| cpp-httplib | MIT |
| cpptrace | MIT |
| curl | curl (MIT-like) |
| cxxopts | MIT |
| dbus (Linux) | AFL-2.1 / GPL-2.0-or-later (dual) |
| draco | Apache-2.0 |
| earcut.hpp | ISC |
| EnTT | MIT |
| fmt | MIT |
| FreeType | FTL (BSD-style) or GPL-2.0 |
| GLM | MIT (Happy Bunny variant) |
| Dear ImGui (vcpkg port) | MIT |
| Jolt Physics | MIT |
| KTX-Software | Apache-2.0 |
| libdatachannel | MPL-2.0 |
| libwebp | BSD-3-Clause |
| Manifold | Apache-2.0 |
| meshoptimizer | MIT |
| nlohmann/json | MIT |
| RmlUi | MIT |
| SDL3 | zlib |
| spdlog | MIT |
| stb | MIT or Public Domain |
| tinygltf | MIT |
| Vulkan Memory Allocator | MIT |
| xxHash | BSD-2-Clause |
| libavif + aom (optional `avif` feature) | BSD-2-Clause / BSD-2-Clause-Patent |

## 8. Tools bundled for development (not shipped in the release package)

| Component | License |
|---|---|
| Slang shader compiler | Apache-2.0 with LLVM exception |
| TypeScript compiler (`tools/tsc`) | Apache-2.0 |
| CMake, Ninja, vcpkg (fetched on demand) | BSD-3-Clause / Apache-2.0 / MIT |
| llama.cpp and Gemma model weights (`gnb llm`, opt-in download) | MIT / Gemma Terms of Use |

---

If you believe a component is missing or misattributed, please open an issue at
<https://github.com/gameknife/gkNextRenderer/issues>.
