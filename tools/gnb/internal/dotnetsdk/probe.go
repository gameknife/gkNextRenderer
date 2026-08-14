package dotnetsdk

import (
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strings"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/console"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/platform"
)

// ProbeOptions configures the Phase 0 acceptance probe.
type ProbeOptions struct {
	// BuildDir holds every probe artifact. Defaults to out/build/dotnet-probe.
	BuildDir string
	// Configuration for both the managed publish and the native probe.
	Configuration string
	// SkipAot runs only the CoreCLR half. For a fast inner loop; never for acceptance.
	SkipAot bool
}

// RunProbe answers the four Phase 0 questions end to end: bidirectional interop, identical output
// from one C# source under both backends, a collectible load context that actually swaps code and
// is actually collected, and a managed side that survives every type in the ABI.
//
// See docs/plans/dotnet-scripting-plan.md section 2. The probe is deliberately a standalone native
// program: it must be able to fail fast, without vcpkg, Vulkan or a window in the way.
func RunProbe(repoRoot string, toolchain Toolchain, options ProbeOptions) error {
	buildDir := options.BuildDir
	if buildDir == "" {
		buildDir = filepath.Join(repoRoot, "out", "build", "dotnet-probe")
	}
	configuration := configurationOrDefault(options.Configuration)

	if err := platform.EnsureMSVCEnvironment(); err != nil {
		return err
	}

	managedDir := filepath.Join(buildDir, "managed")
	console.Info("publishing managed assemblies for CoreCLR")
	if err := PublishBootstrap(repoRoot, toolchain, PublishOptions{
		OutputDir:     managedDir,
		Configuration: configuration,
	}); err != nil {
		return err
	}
	// Two behaviourally different builds of the same game project: the reload has to be shown to
	// swap code, not merely to re-run what was already loaded.
	for _, variant := range []string{"A", "B"} {
		if err := PublishGame(repoRoot, toolchain, PublishOptions{
			OutputDir:     filepath.Join(managedDir, "game-"+strings.ToLower(variant)),
			Configuration: configuration,
			GameVariant:   variant,
		}); err != nil {
			return err
		}
	}

	hostPack, err := HostPackNativeDir(toolchain)
	if err != nil {
		return err
	}

	console.Info("building the CoreCLR probe")
	coreclrDir := filepath.Join(buildDir, "coreclr")
	if err := buildProbe(repoRoot, coreclrDir, configuration, []string{
		"-DGK_DOTNET_BACKEND=CoreCLR",
		"-DGK_DOTNET_HOSTPACK=" + filepath.ToSlash(hostPack),
	}); err != nil {
		return err
	}

	coreclrTranscript := filepath.Join(buildDir, "coreclr-transcript.txt")
	console.Info("running the CoreCLR probe")
	if err := runProbeBinary(coreclrDir, []string{
		"--managed-root", managedDir,
		"--dotnet-root", toolchain.Root,
		"--game", filepath.Join(managedDir, "game-a", GameAssembly+".dll"),
		"--reload-game", filepath.Join(managedDir, "game-b", GameAssembly+".dll"),
		"--transcript", coreclrTranscript,
	}); err != nil {
		return fmt.Errorf("CoreCLR probe failed: %w", err)
	}

	if options.SkipAot {
		console.Warn("skipped the NativeAOT half: this run does not constitute Phase 0 acceptance")
		return nil
	}

	console.Info("publishing managed assemblies for NativeAOT")
	aotManagedDir := filepath.Join(buildDir, "aot")
	if err := PublishBootstrap(repoRoot, toolchain, PublishOptions{
		OutputDir:     aotManagedDir,
		Configuration: configuration,
		Aot:           true,
		NativeLib:     "Shared",
		// Must match the variant the CoreCLR probe loads first, or the shared transcript would
		// differ for a reason that has nothing to do with the backend.
		GameVariant: "A",
	}); err != nil {
		return err
	}

	nativeLibrary, err := NativeBootstrapLibrary(aotManagedDir)
	if err != nil {
		return err
	}

	console.Info("building the NativeAOT probe")
	aotProbeDir := filepath.Join(buildDir, "aot-probe")
	if err := buildProbe(repoRoot, aotProbeDir, configuration, []string{
		"-DGK_DOTNET_BACKEND=AOT",
		"-DGK_DOTNET_AOT_LIB=" + filepath.ToSlash(nativeLibrary),
	}); err != nil {
		return err
	}
	for _, runtimeFile := range NativeRuntimeFiles(aotManagedDir) {
		if err := CopyFileInto(runtimeFile, aotProbeDir); err != nil {
			return err
		}
	}

	aotTranscript := filepath.Join(buildDir, "aot-transcript.txt")
	console.Info("running the NativeAOT probe")
	if err := runProbeBinary(aotProbeDir, []string{
		"--transcript", aotTranscript,
	}); err != nil {
		return fmt.Errorf("NativeAOT probe failed: %w", err)
	}

	return compareTranscripts(coreclrTranscript, aotTranscript)
}

// compareTranscripts enforces the acceptance criterion that matters most: the shared part of the
// run must be byte-identical between backends. Only the CORE channel participates — the hot reload
// section differs by design and is recorded on INFO.
func compareTranscripts(coreclrPath string, aotPath string) error {
	coreclrLines, err := coreLines(coreclrPath)
	if err != nil {
		return err
	}
	aotLines, err := coreLines(aotPath)
	if err != nil {
		return err
	}

	if len(coreclrLines) == 0 {
		return fmt.Errorf("the CoreCLR transcript has no CORE lines: %s", coreclrPath)
	}

	if len(coreclrLines) != len(aotLines) {
		return fmt.Errorf("backends disagree: CoreCLR produced %d CORE lines, NativeAOT %d\n  %s\n  %s",
			len(coreclrLines), len(aotLines), coreclrPath, aotPath)
	}
	for index := range coreclrLines {
		if coreclrLines[index] != aotLines[index] {
			return fmt.Errorf("backends disagree at CORE line %d:\n  CoreCLR:   %s\n  NativeAOT: %s",
				index+1, coreclrLines[index], aotLines[index])
		}
	}

	console.Success("both backends produced identical output (%d CORE lines)", len(coreclrLines))
	return nil
}

func coreLines(path string) ([]string, error) {
	data, err := os.ReadFile(path)
	if err != nil {
		return nil, err
	}
	var lines []string
	for _, line := range strings.Split(strings.ReplaceAll(string(data), "\r\n", "\n"), "\n") {
		if strings.HasPrefix(line, "CORE|") {
			lines = append(lines, line)
		}
	}
	return lines, nil
}

func buildProbe(repoRoot string, buildDir string, configuration string, defines []string) error {
	sourceDir := filepath.Join(repoRoot, "src", "Modules", "NextDotNet", "Probe")

	args := []string{"-S", sourceDir, "-B", buildDir, "-DCMAKE_BUILD_TYPE=" + configuration}
	if _, err := exec.LookPath("ninja"); err == nil {
		args = append(args, "-G", "Ninja")
	}
	args = append(args, defines...)

	if err := runTool(repoRoot, "cmake", args...); err != nil {
		return err
	}
	return runTool(repoRoot, "cmake", "--build", buildDir, "--config", configuration)
}

func runProbeBinary(buildDir string, args []string) error {
	name := "gkNextDotNetProbe"
	if runtime.GOOS == "windows" {
		name += ".exe"
	}

	// Multi-config generators put the binary in a per-configuration subdirectory.
	candidates := []string{
		filepath.Join(buildDir, name),
		filepath.Join(buildDir, "Release", name),
		filepath.Join(buildDir, "Debug", name),
	}
	for _, candidate := range candidates {
		if fileExists(candidate) {
			return runTool(buildDir, candidate, args...)
		}
	}
	return fmt.Errorf("probe binary not found under %s", buildDir)
}

func runTool(dir string, name string, args ...string) error {
	console.Command(name, args...)
	cmd := exec.Command(name, args...)
	cmd.Dir = dir
	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr
	if err := cmd.Run(); err != nil {
		return fmt.Errorf("%s failed: %w", filepath.Base(name), err)
	}
	return nil
}
