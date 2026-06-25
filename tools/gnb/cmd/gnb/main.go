package main

import (
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"runtime/debug"
	"strings"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/android"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/cmakerun"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/config"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/console"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/fetcher"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/ios"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/loc"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/packager"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/paks"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/platform"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/runner"
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
	root.AddCommand(newRunCommand(ctx))
	root.AddCommand(newTestCommand(ctx))
	root.AddCommand(newVisualCommand(ctx))
	root.AddCommand(newShotCommand(ctx))
	root.AddCommand(newTuiCommand(ctx))
	root.AddCommand(newEditorCommand(ctx))
	root.AddCommand(newAndroidCommand(ctx))
	root.AddCommand(newIOSCommand(ctx))
	root.AddCommand(newPaksCommand(ctx))
	root.AddCommand(newPackageCommand(ctx))
	root.AddCommand(newCleanCommand(ctx))
	root.AddCommand(newInstallCommand(ctx))
	root.AddCommand(newTodoCommand(ctx))
	root.AddCommand(newDashboardCommand(ctx))
	root.AddCommand(newLocCommand(ctx))
	root.AddCommand(newGitCommand(ctx))
	root.AddCommand(newLLMCommand(ctx))
	root.AddCommand(newInitCommand())

	if err := root.Execute(); err != nil {
		fatal(err)
	}
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
		Use:   "fetch [all|tsc|vulkan|streamline]",
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
	cmd := &cobra.Command{
		Use:   "build [targets...]",
		Short: "Configure and build the native project",
		Args:  cobra.ArbitraryArgs,
		RunE: func(cmd *cobra.Command, args []string) error {
			opts.Targets = append([]string(nil), args...)
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
			if !opts.PrintCmd {
				if err := fetcher.EnsureHostBuildTools(ctx.repoRoot, ctx.cfg); err != nil {
					return err
				}
			}
			if err := platform.EnsureLinuxDesktopPackages(); err != nil {
				return err
			}
			cmakePath, err := vcpkg.EnsureBundledCMake(ctx.repoRoot, ctx.cfg)
			if err != nil {
				return err
			}
			if ctx.preset == "linux" || ctx.preset == "macos-arm64" {
				ninjaPath, err := vcpkg.EnsureBundledNinja(ctx.repoRoot, ctx.cfg)
				if err != nil {
					return err
				}
				opts.MakeProgram = ninjaPath
			}
			return cmakerun.BuildWithCMake(ctx.repoRoot, cmakePath, ctx.preset, opts)
		},
	}
	cmd.Flags().BoolVar(&opts.Clean, "clean", false, "delete the CMake build directory before building")
	cmd.Flags().BoolVar(&opts.Reconfigure, "reconfigure", false, "force CMake configure")
	cmd.Flags().IntVar(&opts.Jobs, "jobs", 0, "parallel build jobs")
	cmd.Flags().BoolVar(&opts.NoUnity, "no-unity", false, "configure with -DENABLE_UNITY_BUILD=OFF")
	cmd.Flags().BoolVar(&opts.LTO, "lto", false, "configure with -DENABLE_LTO=ON")
	cmd.Flags().BoolVar(&opts.PrintCmd, "print-cmd", false, "print cmake commands without executing")
	cmd.Flags().BoolVar(&skipSetup, "skip-setup", false, "do not auto-bootstrap vcpkg/external dependencies")
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
	cmd := &cobra.Command{
		Use:   "shot [--scene <path>] [--target <name>] [--frames N] [--ui]",
		Short: "Capture one validation screenshot, then auto-exit (no focus-stealing window)",
		Long: "Render a scene to a stable frame, capture a single screenshot to a fixed path, then exit.\n\n" +
			"The window is hidden so it never pops to the foreground or steals focus during an agent\n" +
			"dev loop, and the app exits on its own. Pass --ui to include ImGui in the capture.\n" +
			"The screenshot path is printed when finished.\n\n" +
			"Examples:\n" +
			"  gnb shot --scene assets/models/playground.glb\n" +
			"  gnb shot --target ScadStudio --scene assets/scad/beer_cup.scad --frames 60\n" +
			"  gnb shot --target AirportSim --ui",
		RunE: func(cmd *cobra.Command, args []string) error {
			runArgs := shotRunArgs(frames, includeUI, args)
			opts := runner.Options{Target: target, Preset: ctx.preset, Args: runArgs}
			if scene != "" {
				opts.Scenes = append(opts.Scenes, scene)
			}
			if err := runner.Run(ctx.repoRoot, opts); err != nil {
				return err
			}
			shot := filepath.Join(filepath.Dir(platform.BinDir(ctx.repoRoot, ctx.preset)),
				"screenshots", "agent_validation.jpg")
			console.Info("screenshot: " + shot)
			return nil
		},
	}
	cmd.Flags().StringVar(&scene, "scene", "", "scene to load (file path or built-in .proc name)")
	cmd.Flags().StringVar(&target, "target", "gkNextRenderer", "target executable to run")
	cmd.Flags().IntVar(&frames, "frames", 0, "frames to render before capture (0 = engine default)")
	cmd.Flags().BoolVar(&includeUI, "ui", false, "include ImGui UI in the screenshot")
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
			"  gnb tui --target ScadStudio --scene assets/scad/beer_cup.scad\n" +
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
	return &cobra.Command{
		Use:   "android [debug|release]",
		Short: "Run Android Gradle build/install",
		Args:  cobra.MaximumNArgs(1),
		RunE: func(cmd *cobra.Command, args []string) error {
			mode := "debug"
			if len(args) == 1 {
				mode = args[0]
			}
			return android.Run(ctx.repoRoot, ctx.cfg, mode)
		},
	}
}

func newIOSCommand(ctx appContext) *cobra.Command {
	skipCodeSign := true
	codeSign := false
	cmd := &cobra.Command{
		Use:   "ios",
		Short: "Build iOS target with CMake preset",
		RunE: func(cmd *cobra.Command, args []string) error {
			if err := fetcher.EnsureExternal(ctx.repoRoot, ctx.cfg); err != nil {
				return err
			}
			if err := fetcher.EnsureIOSExternal(ctx.repoRoot, ctx.cfg); err != nil {
				return err
			}
			skip, err := resolveIOSSkipCodeSign(cmd, skipCodeSign, codeSign)
			if err != nil {
				return err
			}
			cmakePath, err := vcpkg.ResolveCMake(ctx.repoRoot, ctx.cfg)
			if err != nil {
				return err
			}
			return ios.Build(ctx.repoRoot, cmakePath, skip)
		},
	}
	cmd.Flags().BoolVar(&skipCodeSign, "skip-codesign", true, "disable code signing")
	cmd.Flags().BoolVar(&codeSign, "codesign", false, "enable code signing")
	return cmd
}

func resolveIOSSkipCodeSign(cmd *cobra.Command, skipCodeSign bool, codeSign bool) (bool, error) {
	if cmd.Flags().Changed("skip-codesign") && cmd.Flags().Changed("codesign") {
		return false, fmt.Errorf("cannot use --skip-codesign and --codesign together")
	}
	if cmd.Flags().Changed("codesign") {
		return !codeSign, nil
	}
	return skipCodeSign, nil
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
	cmd := &cobra.Command{
		Use:   "package <windows|linux|macos|magicalego>",
		Short: "Create a release zip",
		Args:  cobra.ExactArgs(1),
		RunE: func(cmd *cobra.Command, args []string) error {
			return packager.Package(ctx.repoRoot, ctx.preset, args[0], versionFlag)
		},
	}
	cmd.Flags().StringVar(&versionFlag, "version", "", "package version")
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
