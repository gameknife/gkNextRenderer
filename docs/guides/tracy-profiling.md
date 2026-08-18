# Tracy Profiling

gkNextEngine keeps the ImGui profiler and Superluminal integration. Tracy is an additional, on-demand sink: the existing `SCOPED_CPU_TIMER` and `SCOPED_GPU_TIMER` call sites feed the overlay and Tracy without source changes. CPU zones are available on the worker threads; GPU zones use Tracy's own Vulkan query context while the overlay keeps using `GpuQueryTimer`.

## Build and launch on desktop

The development default is `GK_ENABLE_TRACY=ON`; iOS is always off. Release jobs explicitly use `--tracy=off`.

```bash
gnb build --reconfigure                 # development client enabled
gnb tracy fetch                          # download the official matching GUI
gnb tracy                                # launch the GUI
gnb run gkNextRenderer                   # start a profiled target
```

The client and GUI must use the same version. The version is configured in `gnb.toml` under `[external.tracy]` and checked against `.vcpkg/ports/tracy/vcpkg.json`. Tracy is on-demand, so disconnecting the GUI does not stop the application and reconnecting later is supported.

## Android

Build the symbol-preserving variant first:

```bash
gnb android build relwithdebinfo
gnb tracy --android --serial <device-serial>
```

`gnb tracy --android` installs and launches the `relwithdebinfo` APK, forwards the Android `tcp:8086` endpoint with adb, and launches the matching desktop GUI. UDP discovery does not cross adb; enter `127.0.0.1:8086` in the Tracy connection dialog. To profile multiple devices, use `--port <host-port>`; it maps that local port to the APK's fixed 8086 endpoint. The APK has the `INTERNET` permission required by Tracy. `release` is not suitable for symbol-rich call stacks.

## Troubleshooting

- GUI says the process is missing: confirm `GK_ENABLE_TRACY=ON` in the configure output and rebuild; release builds intentionally omit the client.
- Desktop GUI refuses the connection: run `gnb tracy fetch` again and check that the GUI version matches the vcpkg port version.
- Android GUI cannot connect: check `adb devices`, use the correct `--serial`, and verify the `adb forward tcp:8086 tcp:8086` line. Connect to `127.0.0.1`, not the device's LAN address.
- Port 8086 is occupied: stop the conflicting forward/process before starting Tracy. The client protocol currently uses the default endpoint; changing the forward alone does not reconfigure an already-built APK.
- GPU zones are absent: this can be a device timestamp limitation. The engine logs a warning and keeps CPU profiling/overlay profiling alive; calibrated timestamps are optional and fall back to the regular Tracy Vulkan context.

The Tracy path deliberately does not enable memory hooks, lock tracing, fibers, or vcpkg `gui-tools`.
