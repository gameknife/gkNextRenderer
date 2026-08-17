package android

import (
	"fmt"
	"io"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strings"
	"time"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/config"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/console"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/fetcher"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/vcpkg"
)

const (
	defaultVariant = "release"
	packageName    = "com.gknext.renderer"
	activityName   = packageName + "/" + packageName + ".GkNextActivity"
)

// Artifact identifies the APK emitted by the Android CMake driver.
type Artifact struct {
	APKPath string
	SDKRoot string
}

// RunResult describes the device that received the APK.
type RunResult struct {
	Serial          string
	EmulatorStarted bool
	AVD             string
	CapturePath     string
}

// ListDevices writes the detailed adb device list to output. It includes
// online, offline, and unauthorized devices so connection problems are visible.
func ListDevices(repoRoot string, output io.Writer) error {
	adbPath, err := discoverADBPath(repoRoot)
	if err != nil {
		return err
	}
	console.Command(adbPath, "devices", "-l")
	cmd := exec.Command(adbPath, "devices", "-l")
	cmd.Stdout = output
	cmd.Stderr = os.Stderr
	if err := cmd.Run(); err != nil {
		return fmt.Errorf("adb devices -l: %w", err)
	}
	return nil
}

// Connect attaches adb to a remote Android device at address. The address is
// passed as a single argument to `adb connect`, so both host:port and adb's
// supported host names are accepted.
func Connect(repoRoot, address string) error {
	address = strings.TrimSpace(address)
	if address == "" {
		return fmt.Errorf("remote Android device address must not be empty (expected host:port)")
	}
	adbPath, err := discoverADBPath(repoRoot)
	if err != nil {
		return err
	}
	if err := runCommand("", nil, adbPath, "connect", address); err != nil {
		return fmt.Errorf("adb connect %q: %w", address, err)
	}
	return nil
}

// OpenRenderDoc installs the matching Android replay helper and opens the
// desktop RenderDoc UI connected to an online adb device.
func OpenRenderDoc(repoRoot, requestedSerial string) (RunResult, error) {
	sdkADBPath, err := discoverADBPath(repoRoot)
	if err != nil {
		return RunResult{}, err
	}
	sdkRoot := filepath.Dir(filepath.Dir(sdkADBPath))
	renderDocRoot, err := discoverRenderDocRoot()
	if err != nil {
		return RunResult{}, err
	}
	adbPath := sdkADBPath
	devices, err := onlineDevices(adbPath)
	if err != nil {
		return RunResult{}, err
	}
	serial, err := selectDevice(devices, requestedSerial)
	if err != nil {
		return RunResult{}, err
	}
	resetRenderDocAndroidSettings(adbPath, serial)
	if err := installRenderDocLayer(adbPath, serial, renderDocRoot); err != nil {
		return RunResult{}, err
	}
	if err := startRenderDocReplayUI(renderDocRoot, serial, sdkRoot); err != nil {
		return RunResult{}, err
	}
	return RunResult{Serial: serial}, nil
}

// Build configures the Android driver and produces the shared APK. Release is
// the default; provide signing properties before using its APK on a device.
func Build(repoRoot string, cfg config.Config, variant string) (Artifact, error) {
	variant, err := normalizeVariant(variant)
	if err != nil {
		return Artifact{}, err
	}
	if err := vcpkg.Ensure(repoRoot, cfg, false); err != nil {
		return Artifact{}, err
	}
	cmakePath, err := vcpkg.ResolveCMake(repoRoot, cfg)
	if err != nil {
		return Artifact{}, err
	}
	sdkRoot := fetcher.DiscoverVulkanSDK(repoRoot, cfg)
	if sdkRoot == "" {
		if err := fetcher.EnsureVulkanSDK(repoRoot, cfg); err != nil {
			return Artifact{}, err
		}
		sdkRoot = fetcher.DiscoverVulkanSDK(repoRoot, cfg)
	}

	buildDir := buildDirectory(repoRoot, variant)
	driverDir := filepath.Join(repoRoot, "tools", "android")
	configureArgs := []string{
		"-S", driverDir,
		"-B", buildDir,
		"-DGK_ANDROID_VARIANT=" + variant,
	}
	if err := runCommand(repoRoot, androidEnvironment(sdkRoot), cmakePath, configureArgs...); err != nil {
		return Artifact{}, err
	}
	if err := runCommand(repoRoot, androidEnvironment(sdkRoot), cmakePath, "--build", buildDir, "--target", "android-apk"); err != nil {
		return Artifact{}, err
	}
	return ReadArtifact(repoRoot, variant)
}

// Run installs a previously built APK and starts its launcher activity. An
// already-online adb device is preferred. Otherwise the selected local AVD
// (or the first one listed by the emulator) is started and awaited.
func Run(repoRoot, variant, requestedSerial, requestedAVD string) (RunResult, error) {
	variant, err := normalizeVariant(variant)
	if err != nil {
		return RunResult{}, err
	}
	artifact, err := ReadArtifact(repoRoot, variant)
	if err != nil {
		return RunResult{}, err
	}
	adbPath := androidTool(artifact.SDKRoot, "platform-tools", "adb")
	if _, err := os.Stat(adbPath); err != nil {
		return RunResult{}, fmt.Errorf("adb not found: %s", adbPath)
	}

	devices, err := onlineDevices(adbPath)
	if err != nil {
		return RunResult{}, err
	}
	serial, err := selectDevice(devices, requestedSerial)
	result := RunResult{}
	if err != nil {
		if requestedSerial != "" || len(devices) != 0 {
			return RunResult{}, err
		}
		avd, err := startAVD(artifact.SDKRoot, requestedAVD)
		if err != nil {
			return RunResult{}, err
		}
		serial, err = waitForEmulator(adbPath, 5*time.Minute)
		if err != nil {
			return RunResult{}, fmt.Errorf("wait for AVD %q: %w", avd, err)
		}
		result.EmulatorStarted = true
		result.AVD = avd
	}
	// RenderDoc's Android injection uses global GPU layer settings.  Always
	// clear them before a normal launch so a previous capture cannot affect the
	// shared APK when it is started without --renderdoc.
	resetRenderDocAndroidSettings(adbPath, serial)
	if err := installAndLaunch(adbPath, serial, artifact.APKPath); err != nil {
		return RunResult{}, err
	}
	result.Serial = serial
	return result, nil
}

// Capture installs the existing shared release APK, then asks qrenderdoc to
// perform its official Android injection and target-control capture flow.
func Capture(repoRoot string, _ config.Config, requestedSerial string) (RunResult, error) {
	const variant = "release"
	artifact, err := ReadArtifact(repoRoot, variant)
	if err != nil {
		return RunResult{}, err
	}
	renderDocRoot, err := discoverRenderDocRoot()
	if err != nil {
		return RunResult{}, err
	}
	sdkADBPath := androidTool(artifact.SDKRoot, "platform-tools", "adb")
	if _, err := os.Stat(sdkADBPath); err != nil {
		return RunResult{}, fmt.Errorf("adb not found: %s", sdkADBPath)
	}
	adbPath := sdkADBPath
	devices, err := onlineDevices(adbPath)
	if err != nil {
		return RunResult{}, err
	}
	serial, err := selectDevice(devices, requestedSerial)
	if err != nil {
		return RunResult{}, err
	}
	defer resetRenderDocAndroidSettings(adbPath, serial)

	if err := uninstallAndInstall(adbPath, serial, artifact.APKPath); err != nil {
		return RunResult{}, fmt.Errorf("launch RenderDoc capture on %s: %w", serial, err)
	}
	remoteCapture, err := runRenderDocCaptureScript(repoRoot, renderDocRoot, serial, artifact.SDKRoot)
	if err != nil {
		return RunResult{}, err
	}
	localCapture, err := pullRenderDocCapture(repoRoot, adbPath, serial, remoteCapture)
	if err != nil {
		return RunResult{}, err
	}
	if err := openRenderDocCapture(renderDocRoot, artifact.SDKRoot, localCapture); err != nil {
		return RunResult{}, err
	}
	return RunResult{Serial: serial, CapturePath: localCapture}, nil
}

// ReadArtifact locates the archived APK and Android SDK for a completed build.
func ReadArtifact(repoRoot, variant string) (Artifact, error) {
	variant, err := normalizeVariant(variant)
	if err != nil {
		return Artifact{}, err
	}
	buildDir := buildDirectory(repoRoot, variant)
	apkPath := filepath.Join(buildDir, "apk", "gkNextRenderer-"+variant+".apk")
	info, err := os.Stat(apkPath)
	if err != nil {
		if os.IsNotExist(err) {
			return Artifact{}, fmt.Errorf("Android build artifact not found: %s\nrun `gnb android build %s` first", apkPath, variant)
		}
		return Artifact{}, fmt.Errorf("inspect Android APK %s: %w", apkPath, err)
	}
	if info.IsDir() {
		return Artifact{}, fmt.Errorf("Android APK path is a directory: %s", apkPath)
	}
	sdkRoot, err := discoverSDKRootFromBuildDir(repoRoot, buildDir)
	if err != nil {
		return Artifact{}, err
	}
	return Artifact{APKPath: apkPath, SDKRoot: sdkRoot}, nil
}

func normalizeVariant(variant string) (string, error) {
	if variant == "" {
		return defaultVariant, nil
	}
	variant = strings.ToLower(variant)
	if variant != "relwithdebinfo" && variant != "debug" && variant != "release" {
		return "", fmt.Errorf("unsupported Android variant %q (expected relwithdebinfo, debug, or release)", variant)
	}
	return variant, nil
}

func buildDirectory(repoRoot, variant string) string {
	return filepath.Join(repoRoot, "out", "build", "android-"+variant)
}

func discoverSDKRoot(repoRoot, variant string) (string, error) {
	return discoverSDKRootFromBuildDir(repoRoot, buildDirectory(repoRoot, variant))
}

func discoverSDKRootFromBuildDir(repoRoot, buildDir string) (string, error) {
	cachePath := filepath.Join(buildDir, "CMakeCache.txt")
	if cache, err := os.ReadFile(cachePath); err == nil {
		if root := cmakeCacheValue(string(cache), "GK_ANDROID_SDK_ROOT"); root != "" {
			return root, nil
		}
	}
	for _, name := range []string{"ANDROID_SDK_ROOT", "ANDROID_HOME"} {
		if root := os.Getenv(name); root != "" {
			return root, nil
		}
	}
	var root string
	switch runtime.GOOS {
	case "windows":
		root = filepath.Join(os.Getenv("LOCALAPPDATA"), "Android", "Sdk")
	case "darwin":
		home, _ := os.UserHomeDir()
		root = filepath.Join(home, "Library", "Android", "sdk")
	default:
		home, _ := os.UserHomeDir()
		root = filepath.Join(home, "Android", "Sdk")
	}
	if root != "" {
		if _, err := os.Stat(root); err == nil {
			return root, nil
		}
	}
	return "", fmt.Errorf("Android SDK not found; set ANDROID_SDK_ROOT or run `gnb android build` first")
}

func cmakeCacheValue(cache, key string) string {
	prefix := key + ":"
	for _, line := range strings.Split(cache, "\n") {
		if !strings.HasPrefix(line, prefix) {
			continue
		}
		if _, value, found := strings.Cut(line, "="); found {
			return strings.TrimSpace(value)
		}
	}
	return ""
}

func androidTool(sdkRoot string, path ...string) string {
	if runtime.GOOS == "windows" {
		path[len(path)-1] += ".exe"
	}
	return filepath.Join(append([]string{sdkRoot}, path...)...)
}

func discoverADBPath(repoRoot string) (string, error) {
	sdkRoot, err := discoverSDKRoot(repoRoot, defaultVariant)
	if err != nil {
		for _, variant := range []string{"relwithdebinfo", "debug"} {
			sdkRoot, err = discoverSDKRoot(repoRoot, variant)
			if err == nil {
				break
			}
		}
		if err != nil {
			return "", err
		}
	}
	adbPath := androidTool(sdkRoot, "platform-tools", "adb")
	if _, err := os.Stat(adbPath); err != nil {
		return "", fmt.Errorf("adb not found: %s", adbPath)
	}
	return adbPath, nil
}

func discoverRenderDocRoot() (string, error) {
	if runtime.GOOS != "windows" {
		return "", fmt.Errorf("Android RenderDoc capture currently requires the Windows RenderDoc installation")
	}
	candidates := []string{}
	if programFiles := os.Getenv("ProgramFiles"); programFiles != "" {
		candidates = append(candidates, filepath.Join(programFiles, "RenderDoc"))
	}
	if programFilesX86 := os.Getenv("ProgramFiles(x86)"); programFilesX86 != "" {
		candidates = append(candidates, filepath.Join(programFilesX86, "RenderDoc"))
	}
	for _, candidate := range candidates {
		if _, err := os.Stat(filepath.Join(candidate, "qrenderdoc.exe")); err == nil {
			if _, err := os.Stat(filepath.Join(candidate, "plugins", "android", "org.renderdoc.renderdoccmd.arm64.apk")); err == nil {
				return candidate, nil
			}
		}
	}
	return "", fmt.Errorf("RenderDoc Android support not found; install RenderDoc under Program Files")
}

func installRenderDocLayer(adbPath, serial, renderDocRoot string) error {
	apkPath := filepath.Join(renderDocRoot, "plugins", "android", "org.renderdoc.renderdoccmd.arm64.apk")
	if err := runCommand("", nil, adbPath, "-s", serial, "install", "-r", "-g", "--force-queryable", apkPath); err != nil {
		return fmt.Errorf("install RenderDoc Android layer: %w", err)
	}
	return nil
}

func runRenderDocCaptureScript(repoRoot, renderDocRoot, serial, sdkRoot string) (string, error) {
	scriptPath := filepath.Join(repoRoot, "tools", "gnb", "internal", "android", "renderdoc_capture.py")
	if _, err := os.Stat(scriptPath); err != nil {
		return "", fmt.Errorf("RenderDoc capture script not found: %s", scriptPath)
	}
	resultPath := filepath.Join(repoRoot, "out", "build", "android-release", "renderdoc-capture.result")
	_ = os.Remove(resultPath)
	qrenderdocPath := filepath.Join(renderDocRoot, "qrenderdoc.exe")
	console.Command(qrenderdocPath, "--python", scriptPath)
	cmd := exec.Command(qrenderdocPath, "--python", scriptPath)
	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr
	cmd.Env = append(renderDocEnvironment(sdkRoot),
		"GK_RENDERDOC_SERIAL="+serial,
		"GK_RENDERDOC_RESULT_FILE="+resultPath,
	)
	if err := cmd.Start(); err != nil {
		return "", fmt.Errorf("start RenderDoc Android capture script: %w", err)
	}
	defer func() {
		if cmd.Process != nil {
			_ = cmd.Process.Kill()
		}
		_ = cmd.Wait()
	}()

	deadline := time.Now().Add(2 * time.Minute)
	for time.Now().Before(deadline) {
		data, err := os.ReadFile(resultPath)
		if err == nil {
			remoteCapture := strings.TrimSpace(string(data))
			if strings.HasPrefix(remoteCapture, "ERROR:") {
				return "", fmt.Errorf("RenderDoc Android capture failed: %s", remoteCapture)
			}
			if strings.HasSuffix(remoteCapture, ".rdc") {
				return remoteCapture, nil
			}
		}
		time.Sleep(500 * time.Millisecond)
	}
	return "", fmt.Errorf("timed out waiting for the RenderDoc Android capture script")
}

func pullRenderDocCapture(repoRoot, adbPath, serial, remoteCapture string) (string, error) {
	fileName := remoteCapture
	if separator := strings.LastIndexAny(fileName, "/\\"); separator >= 0 {
		fileName = fileName[separator+1:]
	}
	if fileName == "" || !strings.HasSuffix(fileName, ".rdc") {
		return "", fmt.Errorf("invalid RenderDoc capture path returned by adb: %q", remoteCapture)
	}
	localDir := filepath.Join(repoRoot, "out", "build", "android-release", "captures")
	if err := os.MkdirAll(localDir, 0o755); err != nil {
		return "", fmt.Errorf("create RenderDoc capture directory %s: %w", localDir, err)
	}
	localCapture := filepath.Join(localDir, fileName)
	if err := runCommand("", nil, adbPath, "-s", serial, "pull", remoteCapture, localCapture); err != nil {
		return "", fmt.Errorf("pull RenderDoc capture %s: %w", remoteCapture, err)
	}
	return localCapture, nil
}

func startRenderDocReplayUI(renderDocRoot, serial, sdkRoot string) error {
	qrenderdocPath := filepath.Join(renderDocRoot, "qrenderdoc.exe")
	console.Command(qrenderdocPath, "--replayhost", "adb://"+serial)
	cmd := exec.Command(qrenderdocPath, "--replayhost", "adb://"+serial)
	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr
	cmd.Env = renderDocEnvironment(sdkRoot)
	if err := cmd.Start(); err != nil {
		return fmt.Errorf("start qrenderdoc for adb://%s: %w", serial, err)
	}
	_ = cmd.Process.Release()
	return nil
}

func renderDocEnvironment(sdkRoot string) []string {
	platformTools := filepath.Join(sdkRoot, "platform-tools")
	return append(os.Environ(),
		"ANDROID_HOME="+sdkRoot,
		"ANDROID_SDK_ROOT="+sdkRoot,
		"PATH="+platformTools+string(os.PathListSeparator)+os.Getenv("PATH"),
	)
}

func openRenderDocCapture(renderDocRoot, sdkRoot, capturePath string) error {
	qrenderdocPath := filepath.Join(renderDocRoot, "qrenderdoc.exe")
	console.Command(qrenderdocPath, capturePath)
	cmd := exec.Command(qrenderdocPath, capturePath)
	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr
	cmd.Env = renderDocEnvironment(sdkRoot)
	if err := cmd.Start(); err != nil {
		return fmt.Errorf("open RenderDoc capture %s: %w", capturePath, err)
	}
	_ = cmd.Process.Release()
	return nil
}

func onlineDevices(adbPath string) ([]string, error) {
	output, err := exec.Command(adbPath, "devices").CombinedOutput()
	if err != nil {
		return nil, fmt.Errorf("adb devices: %w\n%s", err, strings.TrimSpace(string(output)))
	}
	return parseOnlineDevices(string(output)), nil
}

func parseOnlineDevices(output string) []string {
	var devices []string
	for _, line := range strings.Split(strings.ReplaceAll(output, "\r\n", "\n"), "\n") {
		fields := strings.Fields(line)
		if len(fields) >= 2 && fields[1] == "device" {
			devices = append(devices, fields[0])
		}
	}
	return devices
}

func selectDevice(devices []string, requestedSerial string) (string, error) {
	if requestedSerial != "" {
		for _, serial := range devices {
			if serial == requestedSerial {
				return serial, nil
			}
		}
		return "", fmt.Errorf("requested adb device %q is not online", requestedSerial)
	}
	if len(devices) == 0 {
		return "", fmt.Errorf("no online Android device found")
	}
	return devices[0], nil
}

func startAVD(sdkRoot, requestedAVD string) (string, error) {
	emulatorPath := androidTool(sdkRoot, "emulator", "emulator")
	if _, err := os.Stat(emulatorPath); err != nil {
		return "", fmt.Errorf("Android emulator not found: %s", emulatorPath)
	}
	output, err := exec.Command(emulatorPath, "-list-avds").CombinedOutput()
	if err != nil {
		return "", fmt.Errorf("list local Android AVDs: %w\n%s", err, strings.TrimSpace(string(output)))
	}
	avds := parseAVDs(string(output))
	avd, err := selectAVD(avds, requestedAVD)
	if err != nil {
		return "", err
	}
	console.Command(emulatorPath, "-avd", avd)
	cmd := exec.Command(emulatorPath, "-avd", avd)
	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr
	if err := cmd.Start(); err != nil {
		return "", fmt.Errorf("start Android AVD %q: %w", avd, err)
	}
	_ = cmd.Process.Release()
	return avd, nil
}

func parseAVDs(output string) []string {
	var avds []string
	for _, line := range strings.Split(strings.ReplaceAll(output, "\r\n", "\n"), "\n") {
		if avd := strings.TrimSpace(line); avd != "" {
			avds = append(avds, avd)
		}
	}
	return avds
}

func selectAVD(avds []string, requestedAVD string) (string, error) {
	if requestedAVD != "" {
		for _, avd := range avds {
			if avd == requestedAVD {
				return avd, nil
			}
		}
		return "", fmt.Errorf("requested Android AVD %q was not found", requestedAVD)
	}
	if len(avds) == 0 {
		return "", fmt.Errorf("no local Android AVD found; create one in Android Studio or pass `gnb android run --avd <name>`")
	}
	return avds[0], nil
}

func waitForEmulator(adbPath string, timeout time.Duration) (string, error) {
	deadline := time.Now().Add(timeout)
	for time.Now().Before(deadline) {
		devices, err := onlineDevices(adbPath)
		if err != nil {
			return "", err
		}
		for _, serial := range devices {
			if !strings.HasPrefix(serial, "emulator-") {
				continue
			}
			output, err := exec.Command(adbPath, "-s", serial, "shell", "getprop", "sys.boot_completed").CombinedOutput()
			if err == nil && strings.TrimSpace(string(output)) == "1" {
				return serial, nil
			}
		}
		time.Sleep(time.Second)
	}
	return "", fmt.Errorf("timed out after %s", timeout)
}

func installAndLaunch(adbPath, serial, apkPath string) error {
	if err := runCommand("", nil, adbPath, "-s", serial, "install", "-r", apkPath); err != nil {
		return fmt.Errorf("install Android APK on %s: %w", serial, err)
	}
	return launchInstalled(adbPath, serial, false)
}

func launchInstalled(adbPath, serial string, renderDoc bool) error {
	arguments := []string{"-s", serial, "shell", "am", "start", "-S", "-n", activityName}
	if renderDoc {
		arguments = append(arguments, "--ez", "gknext.renderdoc", "true")
	}
	if err := runCommand("", nil, adbPath, arguments...); err != nil {
		return fmt.Errorf("launch Android app on %s: %w", serial, err)
	}
	return nil
}

func uninstallAndInstall(adbPath, serial, apkPath string) error {
	// A previously installed APK may have been signed by another local
	// keystore. Remove only this project package so the new shared APK can be
	// installed without changing any other device state.
	_ = runCommand("", nil, adbPath, "-s", serial, "shell", "am", "force-stop", packageName)
	_ = runCommand("", nil, adbPath, "-s", serial, "uninstall", packageName)
	if err := runCommand("", nil, adbPath, "-s", serial, "install", "-r", apkPath); err != nil {
		return fmt.Errorf("install Android APK on %s: %w", serial, err)
	}
	return nil
}

func resetRenderDocAndroidSettings(adbPath, serial string) {
	// Keep this in sync with RenderDoc's Android::ResetCaptureSettings(). The
	// settings are global on the device, so leaving them behind causes every
	// later launch of this package to load the RenderDoc layer.
	settings := [][]string{
		{"shell", "setprop", "debug.vulkan.layers", ":"},
		{"shell", "settings", "delete", "global", "enable_gpu_debug_layers"},
		{"shell", "settings", "delete", "global", "gpu_debug_app"},
		{"shell", "settings", "delete", "global", "gpu_debug_layer_app"},
		{"shell", "settings", "delete", "global", "gpu_debug_layers"},
		{"shell", "settings", "delete", "global", "gpu_debug_layers_gles"},
		{"shell", "am", "force-stop", packageName},
		{"shell", "am", "force-stop", "org.renderdoc.renderdoccmd.arm64"},
		{"shell", "am", "force-stop", "org.renderdoc.renderdoccmd.arm32"},
	}
	for _, command := range settings {
		arguments := append([]string{"-s", serial}, command...)
		_ = runCommand("", nil, adbPath, arguments...)
	}
}

func runCommand(dir string, env []string, path string, args ...string) error {
	console.Command(path, args...)
	cmd := exec.Command(path, args...)
	cmd.Dir = dir
	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr
	cmd.Stdin = os.Stdin
	if env != nil {
		cmd.Env = env
	}
	return cmd.Run()
}

func androidEnvironment(vulkanSDK string) []string {
	env := os.Environ()
	if vulkanSDK == "" {
		return env
	}
	console.Label("VULKAN_SDK", vulkanSDK)
	return append(env,
		"VULKAN_SDK="+vulkanSDK,
		"PATH="+filepath.Join(vulkanSDK, "bin")+string(os.PathListSeparator)+os.Getenv("PATH"),
	)
}
