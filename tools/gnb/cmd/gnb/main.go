package main

import (
	"context"
	"encoding/json"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"runtime/debug"
	"sort"
	"strings"
	"time"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/android"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/cmakerun"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/config"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/console"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/fetcher"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/ios"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/mobileapps"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/loc"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/packager"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/paks"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/platform"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/runner"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/targetgraph"
	validatepkg "github.com/gameknife/gknextrenderer/tools/gnb/internal/validate"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/vcpkg"
	"github.com/spf13/cobra"
)

var version = ""

type appContext struct {
	repoRoot string
	cfg      config.Config
	preset   string
}

func main() {
	repoRootCandidates := []string{"."}
	if explicit := explicitRepoRoot(); explicit != "" {
		repoRootCandidates = []string{explicit}
	}
	if executable, err := os.Executable(); err == nil {
		if resolvedExecutable, resolveErr := filepath.EvalSymlinks(executable); resolveErr == nil {
			executable = resolvedExecutable
		}
		repoRootCandidates = append(repoRootCandidates, filepath.Dir(executable))
	}
	repoRoot, repoErr := config.FindRepoRootFromCandidates(repoRootCandidates...)
	var cfg config.Config
	if repoErr == nil {
		var err error
		cfg, err = config.Load(repoRoot)
		if err != nil {
			fatal(err)
		}
	}
	preset, _ := cmakerun.DefaultPreset()
	if explicit := explicitPreset(); explicit != "" {
		preset = explicit
	}
	ctx := appContext{repoRoot: repoRoot, cfg: cfg, preset: preset}

	root := &cobra.Command{
		Use:           "gnb",
		Short:         "gkNextRenderer build helper",
		SilenceUsage:  true,
		SilenceErrors: true,
		RunE: func(cmd *cobra.Command, args []string) error {
			// Bare `gnb` launches the dashboard.
			return runDashboard(ctx, dashboardCmdOpts{})
		},
	}
	var repoRootFlag string
	root.PersistentFlags().StringVar(&repoRootFlag, "repo-root", "", "explicit repository root (also GNB_REPO_ROOT)")
	var presetFlag string
	root.PersistentFlags().StringVar(&presetFlag, "preset", "", "CMake preset to use instead of the host default")
	// Commands listed here run without a discovered repository — everything
	// else fails fast with a friendly hint instead of crashing inside a
	// command implementation that assumed a repo root.
	repolessCommands := map[string]bool{
		"init":       true,
		"help":       true,
		"version":    true,
		"completion": true,
	}
	root.PersistentPreRunE = func(cmd *cobra.Command, args []string) error {
		if repoErr == nil {
			return nil
		}
		name := cmd.Name()
		parentName := ""
		if p := cmd.Parent(); p != nil {
			parentName = p.Name()
		}
		if repolessCommands[name] || repolessCommands[parentName] {
			return nil
		}
		console.Error("%s", repoErr)
		fmt.Println()
		console.Info("cd into a gkNextEngine checkout, or run `gnb init` to clone one.")
		return repoErr
	}
	root.AddCommand(newInfoCommand(ctx))
	root.AddCommand(newDoctorCommand(ctx))
	root.AddCommand(newSetupCommand(ctx))
	root.AddCommand(newDepsCommand(ctx))
	root.AddCommand(newBuildCommand(ctx))
	root.AddCommand(newGraphCommand(ctx))
	root.AddCommand(newRunCommand(ctx))
	root.AddCommand(newRemoteCommand(ctx))
	root.AddCommand(newTestCommand(ctx))
	root.AddCommand(newVisualCommand(ctx))
	root.AddCommand(newShotCommand(ctx))
	root.AddCommand(newScadCommand(ctx))
	root.AddCommand(newGeoCommand(ctx))
	root.AddCommand(newValidateCommand(ctx))
	root.AddCommand(newTuiCommand(ctx))
	root.AddCommand(newEditorCommand(ctx))
	root.AddCommand(newAndroidCommand(ctx))
	root.AddCommand(newIOSCommand(ctx))
	root.AddCommand(newPaksCommand(ctx))
	root.AddCommand(newPackageCommand(ctx))
	root.AddCommand(newSmokeCommand(ctx))
	root.AddCommand(newCleanCommand(ctx))
	root.AddCommand(newInstallCommand(ctx))
	root.AddCommand(newTodoCommand(ctx))
	root.AddCommand(newDashboardCommand(ctx))
	root.AddCommand(newLocCommand(ctx))
	root.AddCommand(newTyposCommand(ctx))
	root.AddCommand(newGitCommand(ctx))
	root.AddCommand(newDotNetCommand(ctx))
	root.AddCommand(newCSharpGenCommand(ctx))
	root.AddCommand(newLLMCommand(ctx))
	root.AddCommand(newAICommand(ctx))
	root.AddCommand(newTracyCommand(ctx))
	root.AddCommand(newRiderCommand(ctx))
	root.AddCommand(newVisualStudioCommand(ctx))
	root.AddCommand(newWebsiteCommand(ctx))
	root.AddCommand(newLegacyAgentCommand(ctx))
	root.AddCommand(newInitCommand())

	if err := root.Execute(); err != nil {
		fatal(err)
	}
}

func explicitRepoRoot() string {
	if value := strings.TrimSpace(os.Getenv("GNB_REPO_ROOT")); value != "" {
		return value
	}
	for i, arg := range os.Args[1:] {
		if strings.HasPrefix(arg, "--repo-root=") {
			return strings.TrimPrefix(arg, "--repo-root=")
		}
		if arg == "--repo-root" && i+2 < len(os.Args) {
			return os.Args[i+2]
		}
	}
	return ""
}

func explicitPreset() string {
	for i, arg := range os.Args[1:] {
		if strings.HasPrefix(arg, "--preset=") {
			return strings.TrimSpace(strings.TrimPrefix(arg, "--preset="))
		}
		if arg == "--preset" && i+2 < len(os.Args) {
			return strings.TrimSpace(os.Args[i+2])
		}
	}
	return ""
}

func newInfoCommand(ctx appContext) *cobra.Command {
	binCacheKey := false
	cmd := &cobra.Command{
		Use:   "info",
		Short: "Print build environment information",
		RunE: func(cmd *cobra.Command, args []string) error {
			if binCacheKey {
				fmt.Println(config.BinCacheKey(ctx.repoRoot, ctx.cfg, runtime.GOOS))
				return nil
			}
			console.Label("gnb", resolvedVersion())
			console.Label("repo", ctx.repoRoot)
			console.Label("platform", runtime.GOOS+"/"+runtime.GOARCH)
			console.Label("preset", ctx.preset)
			console.Label("bin", platform.BinDir(ctx.repoRoot, ctx.preset))
			console.Label("vcpkg", vcpkg.Root(ctx.repoRoot, ctx.cfg))
			console.Label("bincache", filepath.Join(ctx.repoRoot, ctx.cfg.Vcpkg.BinaryCache))
			console.Label("cache-key", config.BinCacheKey(ctx.repoRoot, ctx.cfg, runtime.GOOS))
			if sha, err := gitCommit(ctx.repoRoot); err == nil {
				console.Label("git", sha)
			}
			return nil
		},
	}
	cmd.Flags().BoolVar(&binCacheKey, "bincache-key", false, "print CI binary cache key only")
	return cmd
}

func resolvedVersion() string {
	if version != "" {
		return version
	}
	if buildInfo, ok := debug.ReadBuildInfo(); ok {
		for _, setting := range buildInfo.Settings {
			if setting.Key == "vcs.revision" && setting.Value != "" {
				return setting.Value
			}
		}
	}
	return "dev"
}

func newDoctorCommand(ctx appContext) *cobra.Command {
	return &cobra.Command{
		Use:   "doctor",
		Short: "Check required build tools",
		RunE: func(cmd *cobra.Command, args []string) error {
			checks := []string{"git"}
			if runtime.GOOS == "linux" {
				checks = append(checks, "pkg-config")
			}
			if runtime.GOOS == "darwin" {
				checks = append(checks, "xcodebuild")
			}
			failed := false
			for _, name := range checks {
				if platform.CommandExists(name) {
					console.Success(name)
				} else {
					console.Warn("missing %s", name)
					failed = true
				}
			}
			if cmakePath, err := vcpkg.ResolveCMake(ctx.repoRoot, ctx.cfg); err == nil {
				if platform.CommandExists("cmake") {
					console.Success("cmake")
				} else {
					console.Success("cmake (bundled)")
					console.Label("cmake-path", cmakePath)
				}
			} else {
				console.Warn("missing cmake")
				failed = true
			}
			if runtime.GOOS == "linux" || runtime.GOOS == "darwin" {
				if ninjaPath, err := vcpkg.ResolveNinja(ctx.repoRoot, ctx.cfg); err == nil {
					if platform.CommandExists("ninja") || platform.CommandExists("ninja-build") {
						console.Success("ninja")
					} else {
						console.Success("ninja (bundled)")
						console.Label("ninja-path", ninjaPath)
					}
				} else {
					console.Warn("missing ninja")
					failed = true
				}
			}
			if runtime.GOOS == "windows" || runtime.GOOS == "linux" || runtime.GOOS == "darwin" {
				if sdkRoot := fetcher.DiscoverVulkanSDK(ctx.repoRoot, ctx.cfg); sdkRoot != "" {
					console.Success("Vulkan SDK")
					console.Label("vulkan-sdk-path", sdkRoot)
				} else {
					console.Warn("missing Vulkan SDK (run `gnb setup`)")
					if envRoot := os.Getenv("VULKAN_SDK"); envRoot != "" {
						console.Label("VULKAN_SDK", envRoot)
					}
					failed = true
				}
			}
			if err := platform.EnsureLinuxDesktopPackages(); err != nil {
				console.Warn("%s", err)
				failed = true
			}
			if _, err := os.Stat(vcpkg.Toolchain(ctx.repoRoot, ctx.cfg)); err == nil {
				console.Success("vcpkg toolchain")
			} else {
				console.Warn("missing vcpkg toolchain (run `gnb setup`)")
			}
			if failed {
				return fmt.Errorf("doctor found missing requirements")
			}
			return nil
		},
	}
}

func newSetupCommand(ctx appContext) *cobra.Command {
	skipPaks := false
	vcpkgOnly := false
	refresh := false
	cmd := &cobra.Command{
		Use:   "setup",
		Short: "Prepare vcpkg, external SDKs, and optional paks",
		RunE: func(cmd *cobra.Command, args []string) error {
			if err := platform.EnsureLinuxPreparePackages(); err != nil {
				return err
			}
			if err := vcpkg.Ensure(ctx.repoRoot, ctx.cfg, refresh); err != nil {
				return err
			}
			if vcpkgOnly {
				return nil
			}
			if err := platform.EnsureLinuxDesktopPackages(); err != nil {
				return err
			}
			if err := fetcher.EnsureExternal(ctx.repoRoot, ctx.cfg); err != nil {
				return err
			}
			if !skipPaks {
				return paks.Fetch(ctx.repoRoot, ctx.cfg, nil, false)
			}
			return nil
		},
	}
	cmd.Flags().BoolVar(&skipPaks, "skip-paks", false, "skip optional pak downloads")
	cmd.Flags().BoolVar(&vcpkgOnly, "vcpkg-only", false, "only prepare vcpkg")
	cmd.Flags().BoolVar(&refresh, "refresh", false, "update vcpkg instead of pinning configured ref")
	return cmd
}

func newDepsCommand(ctx appContext) *cobra.Command {
	root := &cobra.Command{
		Use:   "deps",
		Short: "Fetch project-managed external toolchains",
	}

	fetch := &cobra.Command{
		Use:   "fetch [all|vulkan|streamline|fidelityfx]",
		Short: "Fetch one or more external dependencies",
		RunE: func(cmd *cobra.Command, args []string) error {
			return fetcher.EnsureNamedExternal(ctx.repoRoot, ctx.cfg, args)
		},
	}

	root.AddCommand(fetch)
	return root
}

func newBuildCommand(ctx appContext) *cobra.Command {
	opts := cmakerun.BuildOptions{}
	skipSetup := false
	allTargets := false
	tracyMode := ""
	cmd := &cobra.Command{
		Use:   "build [targets...]",
		Short: "Configure and build the native project",
		Args:  cobra.ArbitraryArgs,
		RunE: func(cmd *cobra.Command, args []string) error {
			if tracyMode != "" {
				mode := strings.ToLower(strings.TrimSpace(tracyMode))
				if mode != "on" && mode != "off" {
					return fmt.Errorf("--tracy expects on or off, got %q", tracyMode)
				}
				opts.ConfigureArgs = append(opts.ConfigureArgs, "-DGK_ENABLE_TRACY="+strings.ToUpper(mode))
			}
			startTime := time.Now()
			if len(args) == 0 && !allTargets {
				opts.Targets = []string{"gkNextRenderer", "gkNextUnitTests"}
				if !opts.PrintCmd {
					console.Info("默认仅构建核心目标: gkNextRenderer, gkNextUnitTests (使用 --all 构建全部项目)")
				}
			} else {
				opts.Targets = append([]string(nil), args...)
			}
			if !skipSetup {
				if _, err := os.Stat(vcpkg.Toolchain(ctx.repoRoot, ctx.cfg)); err != nil {
					console.Info("首次构建：自动执行 setup（如需跳过用 --skip-setup）")
					if err := platform.EnsureLinuxPreparePackages(); err != nil {
						return err
					}
					if err := vcpkg.Ensure(ctx.repoRoot, ctx.cfg, false); err != nil {
						return err
					}
					if err := fetcher.EnsureExternal(ctx.repoRoot, ctx.cfg); err != nil {
						return err
					}
				}
			}
			if !skipSetup {
				if err := vcpkg.EnsureBootstrapped(ctx.repoRoot, ctx.cfg); err != nil {
					return err
				}
			}
			if err := platform.EnsureLinuxDesktopPackages(); err != nil {
				return err
			}
			if err := platform.EnsureMSVCEnvironment(); err != nil {
				return err
			}
			cmakePath, err := vcpkg.EnsureBundledCMake(ctx.repoRoot, ctx.cfg)
			if err != nil {
				return err
			}
			if usesNinjaPreset(ctx.preset) {
				ninjaPath, err := vcpkg.EnsureBundledNinja(ctx.repoRoot, ctx.cfg)
				if err != nil {
					return err
				}
				opts.MakeProgram = ninjaPath
			}
			buildErr := cmakerun.BuildWithCMake(ctx.repoRoot, cmakePath, ctx.preset, opts)
			if !opts.PrintCmd {
				elapsed := time.Since(startTime)
				console.Info("Build completed in %s", formatDuration(elapsed))
			}
			return buildErr
		},
	}
	cmd.Flags().BoolVar(&allTargets, "all", false, "build all targets in the project")
	cmd.Flags().StringVar(&tracyMode, "tracy", "", "override Tracy client for this configure (on or off)")
	cmd.Flags().BoolVar(&opts.Clean, "clean", false, "delete the CMake build directory before building")
	cmd.Flags().BoolVar(&opts.Reconfigure, "reconfigure", false, "force CMake configure")
	cmd.Flags().IntVar(&opts.Jobs, "jobs", 0, "parallel build jobs")
	cmd.Flags().BoolVar(&opts.NoUnity, "no-unity", false, "configure with -DENABLE_UNITY_BUILD=OFF")
	cmd.Flags().BoolVar(&opts.LTO, "lto", false, "configure with -DENABLE_LTO=ON")
	cmd.Flags().BoolVar(&opts.PrintCmd, "print-cmd", false, "print cmake commands without executing")
	cmd.Flags().BoolVar(&skipSetup, "skip-setup", false, "do not auto-bootstrap vcpkg/external dependencies")
	return cmd
}

func usesNinjaPreset(preset string) bool {
	switch preset {
	case "windows", "windows-no-unity", "windows-asan", "windows-ninja",
		"linux", "linux-asan", "linux-arm64", "macos-arm64", "macos-arm64-asan":
		return true
	default:
		return false
	}
}

func newGraphCommand(ctx appContext) *cobra.Command {
	opts := targetgraph.Options{}
	all := false
	cmd := &cobra.Command{
		Use:   "graph [target]",
		Short: "Export a CMake target dependency graph",
		Long: "Export CMake's target dependency graph as SVG/PNG/PDF/DOT.\n\n" +
			"Examples:\n" +
			"  gnb graph gkNextEditor\n" +
			"  gnb graph gkNextRenderer --format dot\n" +
			"  gnb graph gkNextEngine --dependers\n" +
			"  gnb graph --all --format svg",
		Args: cobra.MaximumNArgs(1),
		RunE: func(cmd *cobra.Command, args []string) error {
			if all && len(args) == 1 {
				return fmt.Errorf("--all cannot be combined with a target")
			}
			if len(args) == 1 {
				opts.Target = args[0]
			}
			opts.RepoRoot = ctx.repoRoot
			opts.Preset = ctx.preset
			cmakePath, err := vcpkg.EnsureBundledCMake(ctx.repoRoot, ctx.cfg)
			if err != nil {
				return err
			}
			opts.CMakePath = cmakePath
			return targetgraph.Run(opts)
		},
	}
	cmd.Flags().StringVar(&opts.Format, "format", "svg", "output format: svg, png, pdf, or dot")
	cmd.Flags().StringVarP(&opts.Output, "out", "o", "", "output file path")
	cmd.Flags().BoolVar(&opts.Dependers, "dependers", false, "show targets that depend on the target instead of its dependencies")
	cmd.Flags().BoolVar(&opts.PrintCmd, "print-cmd", false, "print commands without executing")
	cmd.Flags().BoolVar(&all, "all", false, "export the full target graph; this is the default when no target is given")
	return cmd
}

func newRunCommand(ctx appContext) *cobra.Command {
	cmd := &cobra.Command{
		Use:   "run [gnb-flags] [target] [app-args]",
		Short: "List runnable applications or run a built target",
		Long: "List runnable applications or run a built target.\n\n" +
			"Arguments after the target are passed to the target executable, so `gnb run gkNextRenderer --help` prints the application help.",
		Args:               cobra.ArbitraryArgs,
		DisableFlagParsing: true,
		RunE: func(cmd *cobra.Command, args []string) error {
			opts, showHelp, err := parseRunArgs(ctx.preset, args)
			if err != nil {
				return err
			}
			if showHelp {
				return cmd.Help()
			}
			if opts.Target == "" && len(opts.Args) == 0 && !opts.List {
				printRunnableTargets(ctx)
				return nil
			}
			return runner.Run(ctx.repoRoot, opts)
		},
	}
	var opts runner.Options
	cmd.Flags().StringVar(&opts.BinDir, "bin-dir", "", "override binary directory")
	cmd.Flags().BoolVar(&opts.List, "list", false, "list binary directory entries")
	cmd.Flags().BoolVar(&opts.DryRun, "dry-run", false, "print command without running")
	cmd.Flags().StringArrayVar(&opts.PresentModes, "present-mode", nil, "append --present-mode=value")
	cmd.Flags().StringArrayVar(&opts.Scenes, "scene", nil, "append --load-scene=value")
	return cmd
}

func parseRunArgs(preset string, args []string) (runner.Options, bool, error) {
	opts := runner.Options{Preset: preset}
	for i := 0; i < len(args); i++ {
		arg := args[i]
		if arg == "--" {
			opts.Args = append(opts.Args, args[i+1:]...)
			return opts, false, nil
		}
		if !strings.HasPrefix(arg, "-") {
			opts.Target = arg
			opts.Args = append(opts.Args, args[i+1:]...)
			return opts, false, nil
		}

		switch {
		case arg == "-h" || arg == "--help":
			return opts, true, nil
		case arg == "--dry-run":
			opts.DryRun = true
		case arg == "--list":
			opts.List = true
		case arg == "--bin-dir":
			i++
			if i >= len(args) {
				return opts, false, fmt.Errorf("--bin-dir requires a value")
			}
			opts.BinDir = args[i]
		case strings.HasPrefix(arg, "--bin-dir="):
			opts.BinDir = strings.TrimPrefix(arg, "--bin-dir=")
		case arg == "--present-mode":
			i++
			if i >= len(args) {
				return opts, false, fmt.Errorf("--present-mode requires a value")
			}
			opts.PresentModes = append(opts.PresentModes, args[i])
		case strings.HasPrefix(arg, "--present-mode="):
			opts.PresentModes = append(opts.PresentModes, strings.TrimPrefix(arg, "--present-mode="))
		case arg == "--scene":
			i++
			if i >= len(args) {
				return opts, false, fmt.Errorf("--scene requires a value")
			}
			opts.Scenes = append(opts.Scenes, args[i])
		case strings.HasPrefix(arg, "--scene="):
			opts.Scenes = append(opts.Scenes, strings.TrimPrefix(arg, "--scene="))
		default:
			opts.Args = append(opts.Args, args[i:]...)
			return opts, false, nil
		}
	}
	return opts, false, nil
}

func printRunnableTargets(ctx appContext) {
	console.Header("Runnable applications")
	for _, target := range ctx.cfg.Targets.All {
		if target == "gkNextUnitTests" {
			continue
		}
		fmt.Printf("  %s\n", target)
	}
	fmt.Println()
	console.Info("Run one with: gnb run <target>")
}

func newTestCommand(ctx appContext) *cobra.Command {
	listTests := false
	listTags := false
	cmd := &cobra.Command{
		Use:   "test [filter]",
		Short: "Run Catch2 unit tests",
		Args:  cobra.ArbitraryArgs,
		RunE: func(cmd *cobra.Command, args []string) error {
			runArgs := append([]string{}, args...)
			if listTests {
				runArgs = append(runArgs, "--list-tests")
			}
			if listTags {
				runArgs = append(runArgs, "--list-tags")
			}
			return runner.Run(ctx.repoRoot, runner.Options{Target: "gkNextUnitTests", Preset: ctx.preset, Args: runArgs})
		},
	}
	cmd.Flags().BoolVar(&listTests, "list-tests", false, "list Catch2 tests")
	cmd.Flags().BoolVar(&listTags, "list-tags", false, "list Catch2 tags")
	return cmd
}

func newVisualCommand(ctx appContext) *cobra.Command {
	return &cobra.Command{
		Use:   "visual",
		Short: "Run visual tests",
		RunE: func(cmd *cobra.Command, args []string) error {
			return runner.Run(ctx.repoRoot, runner.Options{Target: "gkNextVisualTest", Preset: ctx.preset, Args: args})
		},
	}
}

func newShotCommand(ctx appContext) *cobra.Command {
	var scene string
	var target string
	var frames int
	var includeUI bool
	var headless bool
	cmd := &cobra.Command{
		Use:   "shot [--scene <path>] [--target <name>] [--frames N] [--ui] [--headless]",
		Short: "Capture one validation screenshot, then auto-exit (no focus-stealing window)",
		Long: "Render a scene to a stable frame, capture a single screenshot to a fixed path, then exit.\n\n" +
			"The window is hidden so it never pops to the foreground or steals focus during an agent\n" +
			"dev loop, and the app exits on its own. Pass --ui to include ImGui in the capture.\n" +
			"The screenshot path is printed when finished.\n\n" +
			"Examples:\n" +
			"  gnb shot --scene assets/models/playground.glb\n" +
			"  gnb shot --target ScadStudio --scene assets/scad/source/beer_cup.scad --frames 60\n" +
			"  gnb shot --target AirportSim --ui",
		RunE: func(cmd *cobra.Command, args []string) error {
			out := filepath.Join(filepath.Dir(platform.BinDir(ctx.repoRoot, ctx.preset)), "screenshots", "agent_validation")
			if headless {
				args = append(args, "--headless-surface")
			}
			opts := validatepkg.Options{RepoRoot: ctx.repoRoot, Preset: ctx.preset, Target: target, Scene: scene, Args: args}
			if err := validatepkg.Shot(context.Background(), opts, frames, includeUI, out); err != nil {
				return err
			}
			shot := out + ".jpg"
			console.Info("screenshot: " + shot)
			return nil
		},
	}
	cmd.Flags().StringVar(&scene, "scene", "", "scene to load (file path or built-in .proc name)")
	cmd.Flags().StringVar(&target, "target", "gkNextRenderer", "target executable to run")
	cmd.Flags().IntVar(&frames, "frames", 0, "frames to render before capture (0 = engine default)")
	cmd.Flags().BoolVar(&includeUI, "ui", false, "include ImGui UI in the screenshot")
	cmd.Flags().BoolVar(&headless, "headless", false, "force VK_EXT_headless_surface (including on macOS)")
	return cmd
}

func shotRunArgs(frames int, includeUI bool, trailingArgs []string) []string {
	runArgs := []string{"--agent-validation"}
	if frames > 0 {
		runArgs = append(runArgs, fmt.Sprintf("--agent-validation-frames=%d", frames))
	}
	if includeUI {
		runArgs = append(runArgs, "--agent-validation-ui")
	}
	return append(runArgs, trailingArgs...)
}

type validateScriptHints struct {
	Name     string `json:"name"`
	Target   string `json:"target"`
	Scene    string `json:"scene"`
	Viewport struct {
		Width  int `json:"width"`
		Height int `json:"height"`
	} `json:"viewport"`
}

func newValidateCommand(ctx appContext) *cobra.Command {
	var script string
	var target string
	var scene string
	var report string
	var width int
	var height int
	var visible bool
	var syncValidation bool
	cmd := &cobra.Command{
		Use:   "validate --script <path> [--target <name>] [--scene <path>]",
		Short: "Run an agent input validation script and write a JSON report",
		Args:  cobra.ArbitraryArgs,
		RunE: func(cmd *cobra.Command, args []string) error {
			if script == "" {
				return fmt.Errorf("--script is required")
			}
			scriptPath := script
			if !filepath.IsAbs(scriptPath) {
				scriptPath = filepath.Join(ctx.repoRoot, scriptPath)
			}
			scriptPath, _ = filepath.Abs(scriptPath)

			hints := loadValidateScriptHints(scriptPath)
			if target == "" {
				target = hints.Target
			}
			if target == "" {
				target = "gkNextRenderer"
			}
			if scene == "" {
				scene = hints.Scene
			}
			if width == 0 {
				width = hints.Viewport.Width
			}
			if height == 0 {
				height = hints.Viewport.Height
			}

			reportPath := report
			if reportPath == "" {
				reportName := hints.Name
				if reportName == "" {
					reportName = strings.TrimSuffix(filepath.Base(scriptPath), filepath.Ext(scriptPath))
				}
				reportPath = filepath.Join(filepath.Dir(platform.BinDir(ctx.repoRoot, ctx.preset)),
					"agent_reports", reportName+".json")
			} else if !filepath.IsAbs(reportPath) {
				reportPath = filepath.Join(filepath.Dir(platform.BinDir(ctx.repoRoot, ctx.preset)), reportPath)
			}
			opts := validatepkg.Options{RepoRoot: ctx.repoRoot, Preset: ctx.preset, Target: target, Scene: scene,
				Script: scriptPath, Report: reportPath, Width: width, Height: height, Visible: visible, SyncValidation: syncValidation, Args: args}
			err := validatepkg.Run(context.Background(), opts)
			console.Info("agent report: " + reportPath)
			return err
		},
	}
	cmd.Flags().StringVar(&script, "script", "", "agent script JSON path")
	cmd.Flags().StringVar(&target, "target", "", "target executable to run (default: script target or gkNextRenderer)")
	cmd.Flags().StringVar(&scene, "scene", "", "scene to load (overrides script scene)")
	cmd.Flags().StringVar(&report, "report", "", "report JSON output path")
	cmd.Flags().IntVar(&width, "width", 0, "window width (overrides script viewport.width)")
	cmd.Flags().IntVar(&height, "height", 0, "window height (overrides script viewport.height)")
	cmd.Flags().BoolVar(&visible, "visible", false, "show the desktop window while replaying the agent script")
	cmd.Flags().BoolVar(&syncValidation, "sync-validation", false, "enable Vulkan core and synchronization validation")
	return cmd
}

func loadValidateScriptHints(scriptPath string) validateScriptHints {
	var hints validateScriptHints
	data, err := os.ReadFile(scriptPath)
	if err != nil {
		return hints
	}
	_ = json.Unmarshal(data, &hints)
	return hints
}

func newTuiCommand(ctx appContext) *cobra.Command {
	var scene string
	var target string
	var fps int
	var maxCols int
	var maxRows int
	var ssaa int
	var noInput bool
	cmd := &cobra.Command{
		Use:   "tui [--scene <path>] [--target <name>]",
		Short: "Run a target in terminal TUI mode (hidden window + truecolor terminal blit)",
		Long: "Render a target into a hidden swapchain and continuously blit the frames into the\n" +
			"current terminal using truecolor half-block characters.\n\n" +
			"Examples:\n" +
			"  gnb tui --scene assets/models/playground.glb\n" +
			"  gnb tui --target ScadStudio --scene assets/scad/source/beer_cup.scad\n" +
			"  gnb tui --target gkNextRenderer --tui-fps 20",
		RunE: func(cmd *cobra.Command, args []string) error {
			runArgs := []string{"--tui"}
			if fps > 0 {
				runArgs = append(runArgs, fmt.Sprintf("--tui-fps=%d", fps))
			}
			if maxCols > 0 {
				runArgs = append(runArgs, fmt.Sprintf("--tui-max-cols=%d", maxCols))
			}
			if maxRows > 0 {
				runArgs = append(runArgs, fmt.Sprintf("--tui-max-rows=%d", maxRows))
			}
			if ssaa > 0 {
				runArgs = append(runArgs, fmt.Sprintf("--tui-ssaa=%d", ssaa))
			}
			if noInput {
				runArgs = append(runArgs, "--tui-no-input")
			}
			runArgs = append(runArgs, args...)

			opts := runner.Options{Target: target, Preset: ctx.preset, Args: runArgs}
			if scene != "" {
				opts.Scenes = append(opts.Scenes, scene)
			}
			return runner.Run(ctx.repoRoot, opts)
		},
	}
	cmd.Flags().StringVar(&scene, "scene", "", "scene to load (file path or built-in .proc name)")
	cmd.Flags().StringVar(&target, "target", "gkNextRenderer", "target executable to run")
	cmd.Flags().IntVar(&fps, "tui-fps", 0, "terminal refresh cap (0 = engine default)")
	cmd.Flags().IntVar(&maxCols, "tui-max-cols", 0, "optional terminal column cap")
	cmd.Flags().IntVar(&maxRows, "tui-max-rows", 0, "optional terminal row cap")
	cmd.Flags().IntVar(&ssaa, "tui-ssaa", 0, "hidden render supersample factor (0 = engine default)")
	cmd.Flags().BoolVar(&noInput, "tui-no-input", false, "do not capture stdin in TUI mode")
	return cmd
}

func newEditorCommand(ctx appContext) *cobra.Command {
	return &cobra.Command{
		Use:   "editor",
		Short: "Run gkNextEditor",
		RunE: func(cmd *cobra.Command, args []string) error {
			return runner.Run(ctx.repoRoot, runner.Options{Target: "gkNextEditor", Preset: ctx.preset, Args: args})
		},
	}
}

func newAndroidCommand(ctx appContext) *cobra.Command {
	root := &cobra.Command{
		Use:   "android",
		Short: "Build and launch the Android app",
		Args:  cobra.NoArgs,
		RunE: func(cmd *cobra.Command, args []string) error {
			return cmd.Help()
		},
	}

	buildApp := ""
	build := &cobra.Command{
		Use:   "build [relwithdebinfo|debug|release]",
		Short: "Build an Android APK (default: release)",
		Args:  cobra.MaximumNArgs(1),
		RunE: func(cmd *cobra.Command, args []string) error {
			variant := ""
			if len(args) == 1 {
				variant = args[0]
			}
			artifact, err := android.Build(ctx.repoRoot, ctx.cfg, variant, buildApp)
			if err != nil {
				return err
			}
			fmt.Fprintf(cmd.OutOrStdout(), "[android] built %s\n", artifact.APKPath)
			fmt.Fprintln(cmd.OutOrStdout(), "[android] install and launch it with `gnb android run`")
			return nil
		},
	}
	build.Flags().StringVar(&buildApp, "app", "",
		mobileapps.FlagHelp(ctx.repoRoot, mobileapps.Android, "application to package"))
	root.AddCommand(build)

	serial := ""
	avd := ""
	runApp := ""
	run := &cobra.Command{
		Use:   "run [relwithdebinfo|debug|release]",
		Short: "Install and launch a built Android APK on adb or a local AVD",
		Args:  cobra.MaximumNArgs(1),
		RunE: func(cmd *cobra.Command, args []string) error {
			variant := ""
			if len(args) == 1 {
				variant = args[0]
			}
			result, err := android.Run(ctx.repoRoot, variant, runApp, serial, avd)
			if err != nil {
				return err
			}
			if result.EmulatorStarted {
				fmt.Fprintf(cmd.OutOrStdout(), "[android] started AVD %s\n", result.AVD)
			}
			fmt.Fprintf(cmd.OutOrStdout(), "[android] installed and launched on %s\n", result.Serial)
			return nil
		},
	}
	run.Flags().StringVar(&serial, "serial", "", "use this online adb device serial")
	run.Flags().StringVar(&avd, "avd", "", "start this local AVD when no adb device is online")
	run.Flags().StringVar(&runApp, "app", "",
		mobileapps.FlagHelp(ctx.repoRoot, mobileapps.Android, "application to install"))
	root.AddCommand(run)

	captureSerial := ""
	capture := &cobra.Command{
		Use:   "capture",
		Short: "Capture the existing shared release APK through RenderDoc and open the first capture",
		Args:  cobra.NoArgs,
		RunE: func(cmd *cobra.Command, args []string) error {
			result, err := android.Capture(ctx.repoRoot, ctx.cfg, captureSerial)
			if err != nil {
				return err
			}
			fmt.Fprintf(cmd.OutOrStdout(), "[android] RenderDoc captured and opened on %s: %s\n", result.Serial, result.CapturePath)
			return nil
		},
	}
	capture.Flags().StringVar(&captureSerial, "serial", "", "use this online adb device serial")
	root.AddCommand(capture)

	renderDocSerial := ""
	renderDoc := &cobra.Command{
		Use:   "renderdoc",
		Short: "Open RenderDoc connected to an adb Android device",
		Args:  cobra.NoArgs,
		RunE: func(cmd *cobra.Command, args []string) error {
			result, err := android.OpenRenderDoc(ctx.repoRoot, renderDocSerial)
			if err != nil {
				return err
			}
			fmt.Fprintf(cmd.OutOrStdout(), "[android] RenderDoc opened for %s; select this device in Replay Context\n", result.Serial)
			return nil
		},
	}
	renderDoc.Flags().StringVar(&renderDocSerial, "serial", "", "use this online adb device serial")
	root.AddCommand(renderDoc)

	connect := &cobra.Command{
		Use:   "connect <host>:<port>",
		Short: "Connect adb to a remote Android device",
		Args:  cobra.ExactArgs(1),
		RunE: func(cmd *cobra.Command, args []string) error {
			return android.Connect(ctx.repoRoot, args[0])
		},
	}
	root.AddCommand(connect)

	devices := &cobra.Command{
		Use:   "devices",
		Short: "List adb-connected Android devices",
		Args:  cobra.NoArgs,
		RunE: func(cmd *cobra.Command, args []string) error {
			return android.ListDevices(ctx.repoRoot, cmd.OutOrStdout())
		},
	}
	root.AddCommand(devices)
	return root
}

func newIOSCommand(ctx appContext) *cobra.Command {
	root := &cobra.Command{
		Use:   "ios",
		Short: "Build the CMake-generated iOS device app",
		Args:  cobra.NoArgs,
		RunE: func(cmd *cobra.Command, args []string) error {
			return cmd.Help()
		},
	}

	buildOpts := cmakerun.BuildOptions{}
	teamID := ""
	verbose := false
	buildApp := ""
	build := &cobra.Command{
		Use:   "build",
		Short: "Build an application for an arm64 iOS device",
		Args:  cobra.NoArgs,
		RunE: func(cmd *cobra.Command, args []string) error {
			if _, err := os.Stat(vcpkg.Toolchain(ctx.repoRoot, ctx.cfg)); err != nil {
				if err := vcpkg.Ensure(ctx.repoRoot, ctx.cfg, false); err != nil {
					return err
				}
			}
			if err := vcpkg.EnsureBootstrapped(ctx.repoRoot, ctx.cfg); err != nil {
				return err
			}
			if err := fetcher.EnsureExternal(ctx.repoRoot, ctx.cfg); err != nil {
				return err
			}
			cmakePath, err := vcpkg.EnsureBundledCMake(ctx.repoRoot, ctx.cfg)
			if err != nil {
				return err
			}
			staged, err := ios.Build(ctx.repoRoot, cmakePath, teamID, buildApp, !verbose, buildOpts)
			if err != nil {
				return err
			}
			if staged.WrapperPath == "" {
				fmt.Fprintf(cmd.OutOrStdout(), "[ios] built an unsigned bundle; pass --team-id <TEAM_ID> to produce a launchable app\n")
				return nil
			}
			fmt.Fprintf(cmd.OutOrStdout(), "[ios] Designed-for-iPad app staged: %s\n", staged.WrapperPath)
			fmt.Fprintf(cmd.OutOrStdout(), "[ios] launch it with `gnb ios run`\n")
			if staged.Restaged {
				fmt.Fprintf(cmd.OutOrStdout(), "[ios] current bundle replaced inside the persistent Designed-for-iPad wrapper\n")
			}
			return nil
		},
	}
	build.Flags().BoolVar(&buildOpts.Clean, "clean", false, "delete the selected iOS build directory first")
	build.Flags().BoolVar(&buildOpts.Reconfigure, "reconfigure", false, "force CMake configure")
	build.Flags().IntVar(&buildOpts.Jobs, "jobs", 0, "parallel build jobs")
	build.Flags().StringVar(&teamID, "team-id", "", "Apple Developer Team ID for automatic device signing")
	build.Flags().BoolVar(&verbose, "verbose", false, "show normal Xcode build progress output")
	build.Flags().StringVar(&buildApp, "app", "",
		mobileapps.FlagHelp(ctx.repoRoot, mobileapps.IOS, "application to bundle"))

	root.AddCommand(build)
	devices := &cobra.Command{
		Use:   "device",
		Short: "List available iOS run devices",
		Args:  cobra.NoArgs,
		RunE: func(cmd *cobra.Command, args []string) error {
			return ios.ListDevices(cmd.OutOrStdout())
		},
	}
	root.AddCommand(devices)

	requestedDevice := ""
	run := &cobra.Command{
		Use:   "run",
		Short: "Install and launch the signed iOS app on a selected device",
		Args:  cobra.NoArgs,
		RunE: func(cmd *cobra.Command, args []string) error {
			artifact, device, err := ios.Run(ctx.repoRoot, requestedDevice, cmd.InOrStdin(), cmd.OutOrStdout())
			if err != nil {
				return err
			}
			if device.IsMac() {
				fmt.Fprintf(cmd.OutOrStdout(), "[ios] launched %s on %s (%s)\n", artifact.BundleID, device.Name, artifact.WrapperPath)
			} else {
				fmt.Fprintf(cmd.OutOrStdout(), "[ios] installed and launched %s on %s (%s)\n", artifact.BundleID, device.Name, device.Identifier)
			}
			if device.IsMac() && artifact.Restaged {
				fmt.Fprintf(cmd.OutOrStdout(), "[ios] launched the current bundle from the persistent Designed-for-iPad wrapper\n")
			}
			return nil
		},
	}
	run.Flags().StringVar(&requestedDevice, "device", "", "device ID, UDID, or name; prompts when multiple devices are available")

	root.AddCommand(run)
	teams := &cobra.Command{
		Use:   "teams",
		Short: "List locally provisioned Apple Developer teams",
		Args:  cobra.NoArgs,
		RunE: func(cmd *cobra.Command, args []string) error {
			homeDir, err := os.UserHomeDir()
			if err != nil {
				return fmt.Errorf("locate home directory: %w", err)
			}
			teams, warnings := ios.Teams(homeDir)
			for _, warning := range warnings {
				fmt.Fprintf(cmd.ErrOrStderr(), "[warning] %v\n", warning)
			}
			if len(teams) == 0 {
				fmt.Fprintln(cmd.OutOrStdout(), "No local iOS provisioning profiles found. Sign an iOS app once in Xcode, then run this command again.")
				return nil
			}

			fmt.Fprintln(cmd.OutOrStdout(), "Apple Developer teams available from local provisioning profiles:")
			for _, team := range teams {
				name := team.Name
				if name == "" {
					name = "(unnamed team)"
				}
				fmt.Fprintf(cmd.OutOrStdout(), "  %s\t%s\n", name, team.ID)
			}
			return nil
		},
	}

	root.AddCommand(teams)
	return root
}

func newPaksCommand(ctx appContext) *cobra.Command {
	root := &cobra.Command{Use: "paks", Short: "Fetch, publish, or list optional pak assets"}
	force := false
	fetch := &cobra.Command{
		Use:   "fetch [groups...]",
		Short: "Fetch optional pak assets",
		RunE: func(cmd *cobra.Command, args []string) error {
			return paks.Fetch(ctx.repoRoot, ctx.cfg, args, force)
		},
	}
	fetch.Flags().BoolVar(&force, "force", false, "redownload existing files")
	token := ""
	dryRun := false
	publish := &cobra.Command{
		Use:   "publish [groups...]",
		Short: "Publish optional pak assets to GitHub Releases",
		RunE: func(cmd *cobra.Command, args []string) error {
			return paks.Publish(ctx.repoRoot, ctx.cfg, args, dryRun, token)
		},
	}
	publish.Flags().StringVar(&token, "token", "", "GitHub token; defaults to GITHUB_TOKEN")
	publish.Flags().BoolVar(&dryRun, "dry-run", false, "print upload plan")
	list := &cobra.Command{
		Use:   "list",
		Short: "List pak manifest status",
		RunE: func(cmd *cobra.Command, args []string) error {
			return paks.List(ctx.repoRoot, ctx.cfg)
		},
	}
	root.AddCommand(fetch, publish, list)
	return root
}

func newPackageCommand(ctx appContext) *cobra.Command {
	versionFlag := ""
	packagePresetName := ""
	traceAssets := false
	assetTrace := ""
	runtimePak := ""
	traceFrames := 120
	includeGNB := false
	cmd := &cobra.Command{
		Use:   "package <windows|linux|macos>",
		Short: "Create a high-compression 7z release archive",
		Args:  cobra.ExactArgs(1),
		RunE: func(cmd *cobra.Command, args []string) error {
			preset, err := resolvePackagePreset(ctx.cfg, packagePresetName)
			if err != nil {
				return err
			}
			return packager.Package(ctx.repoRoot, ctx.preset, args[0], preset, packager.Options{
				Version: versionFlag, TraceAssets: traceAssets, AssetTrace: assetTrace, TraceFrames: traceFrames,
				RuntimePak: runtimePak, IncludeGNB: includeGNB,
			})
		},
	}
	cmd.Flags().StringVar(&versionFlag, "version", "", "package version")
	cmd.Flags().StringVar(&packagePresetName, "package-preset", "", "package preset from gnb.toml (defaults to package.default_preset)")
	cmd.Flags().BoolVar(&traceAssets, "trace-assets", false, "run the package preset targets and package only observed runtime assets")
	cmd.Flags().StringVar(&assetTrace, "asset-trace", "", "reuse a newline-delimited runtime asset trace instead of launching targets")
	cmd.Flags().StringVar(&runtimePak, "runtime-pak", "", "reuse a directory containing runtime.pak, runtime-assets.txt, and runtime.manifest.json")
	cmd.Flags().IntVar(&traceFrames, "trace-frames", 120, "stable frames to sample per target with --trace-assets")
	cmd.Flags().BoolVar(&includeGNB, "include-gnb", false, "include the gnb executable and agent manifest in the desktop package")
	return cmd
}

func resolvePackagePreset(cfg config.Config, name string) (packager.Preset, error) {
	if name == "" {
		name = cfg.Package.DefaultPreset
	}
	configured, ok := cfg.Package.Presets[name]
	if !ok {
		available := make([]string, 0, len(cfg.Package.Presets))
		for candidate := range cfg.Package.Presets {
			available = append(available, candidate)
		}
		sort.Strings(available)
		return packager.Preset{}, fmt.Errorf("unknown package preset %q (available: %s)", name, strings.Join(available, ", "))
	}
	return packager.Preset{
		Name:                name,
		Targets:             configured.Targets,
		ArchiveName:         configured.ArchiveName,
		AlwaysIncludeAssets: configured.AlwaysIncludeAssets,
		ExtraFiles:          configured.ExtraFiles,
	}, nil
}

func newSmokeCommand(ctx appContext) *cobra.Command {
	opts := packager.SmokeOptions{}
	timeoutSeconds := 90
	cmd := &cobra.Command{
		Use:   "smoke <package.7z>",
		Short: "Extract a release archive into a clean directory and verify it runs out of the box",
		Args:  cobra.ExactArgs(1),
		RunE: func(cmd *cobra.Command, args []string) error {
			archive := args[0]
			if !filepath.IsAbs(archive) {
				archive = filepath.Join(ctx.repoRoot, archive)
			}
			opts.LaunchTimeout = time.Duration(timeoutSeconds) * time.Second
			return packager.Smoke(archive, opts)
		},
	}
	cmd.Flags().BoolVar(&opts.Launch, "launch", false, "also launch each target and wait for the scene-ready log (needs a Vulkan device)")
	cmd.Flags().BoolVar(&opts.Keep, "keep", false, "keep the extracted staging directory")
	cmd.Flags().StringVar(&opts.StagingDir, "staging", "", "extraction directory (default: a temp directory)")
	cmd.Flags().IntVar(&timeoutSeconds, "timeout", 90, "per-target launch timeout in seconds")
	return cmd
}

func newCleanCommand(ctx appContext) *cobra.Command {
	return &cobra.Command{
		Use:   "clean [target]",
		Short: "Clean build output",
		Args:  cobra.MaximumNArgs(1),
		RunE: func(cmd *cobra.Command, args []string) error {
			target := ""
			if len(args) == 1 {
				target = args[0]
			}
			return cmakerun.Clean(ctx.repoRoot, ctx.preset, target)
		},
	}
}

func newInstallCommand(ctx appContext) *cobra.Command {
	return &cobra.Command{
		Use:   "install",
		Short: "Install gnb to a user bin directory",
		RunE: func(cmd *cobra.Command, args []string) error {
			exe, err := os.Executable()
			if err != nil {
				return err
			}
			home, err := os.UserHomeDir()
			if err != nil {
				return err
			}
			dir := filepath.Join(home, ".local", "bin")
			name := "gnb"
			if runtime.GOOS == "windows" {
				dir = filepath.Join(home, "bin")
				name = "gnb.exe"
			}
			if err := os.MkdirAll(dir, 0o755); err != nil {
				return err
			}
			dst := filepath.Join(dir, name)
			data, err := os.ReadFile(exe)
			if err != nil {
				return err
			}
			if err := os.WriteFile(dst, data, 0o755); err != nil {
				return err
			}
			console.Success("installed to %s", dst)
			return nil
		},
	}
}

func newLocCommand(ctx appContext) *cobra.Command {
	var (
		includeThirdParty bool
		extensions        []string
	)
	cmd := &cobra.Command{
		Use:   "loc",
		Short: "Print a line-of-code summary of src/, grouped by category and subproject",
		RunE: func(cmd *cobra.Command, args []string) error {
			return loc.Run(loc.Options{
				Root:              ctx.repoRoot,
				Extensions:        extensions,
				IncludeThirdParty: includeThirdParty,
			})
		},
	}
	cmd.Flags().BoolVar(&includeThirdParty, "thirdparty", false, "include src/ThirdParty in the report")
	cmd.Flags().StringSliceVar(&extensions, "ext", nil, "override file extensions (e.g. --ext .cpp,.h)")
	return cmd
}

func newTyposCommand(ctx appContext) *cobra.Command {
	return &cobra.Command{
		Use:                "typos [flags]",
		Short:              "Check first-party files for spelling mistakes",
		DisableFlagParsing: true,
		RunE: func(cmd *cobra.Command, args []string) error {
			typosPath, err := exec.LookPath("typos")
			if err != nil {
				return fmt.Errorf("typos is not installed; see https://github.com/crate-ci/typos#install")
			}

			check := exec.Command(typosPath, args...)
			check.Dir = ctx.repoRoot
			check.Stdin = os.Stdin
			check.Stdout = os.Stdout
			check.Stderr = os.Stderr
			return check.Run()
		},
	}
}

func gitCommit(repoRoot string) (string, error) {
	cmd := exec.Command("git", "rev-parse", "--short", "HEAD")
	cmd.Dir = repoRoot
	data, err := cmd.Output()
	if err != nil {
		return "", err
	}
	return strings.TrimSpace(string(data)), nil
}

func fatal(err error) {
	console.Error("%s", err)
	os.Exit(1)
}

func formatDuration(d time.Duration) string {
	d = d.Round(time.Second)
	min := int(d / time.Minute)
	sec := int((d % time.Minute) / time.Second)
	if min > 0 {
		if sec == 0 {
			return fmt.Sprintf("%d min", min)
		}
		return fmt.Sprintf("%d min %d sec", min, sec)
	}
	return fmt.Sprintf("%d sec", sec)
}
