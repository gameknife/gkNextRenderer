package cmakerun

import (
	"bufio"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"strings"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/console"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/platform"
)

type BuildOptions struct {
	Targets     []string
	Clean       bool
	Reconfigure bool
	Jobs        int
	NoUnity     bool
	LTO         bool
	MakeProgram string
	PrintCmd    bool
}

func Build(repoRoot string, preset string, opts BuildOptions) error {
	return BuildWithCMake(repoRoot, "cmake", preset, opts)
}

func BuildWithCMake(repoRoot string, cmakePath string, preset string, opts BuildOptions) error {
	buildDir := filepath.Join(repoRoot, "out", "build", preset)
	cache := filepath.Join(buildDir, "CMakeCache.txt")
	if opts.Clean {
		console.Info("cleaning %s", buildDir)
		if err := os.RemoveAll(buildDir); err != nil {
			return err
		}
	}

	configureArgs := []string{"--preset", preset}
	if opts.NoUnity {
		configureArgs = append(configureArgs, "-DENABLE_UNITY_BUILD=OFF")
	}
	if opts.LTO {
		configureArgs = append(configureArgs, "-DENABLE_LTO=ON")
	}
	if opts.MakeProgram != "" {
		configureArgs = append(configureArgs, "-DCMAKE_MAKE_PROGRAM="+opts.MakeProgram)
	}

	needsConfigure := opts.Clean || opts.Reconfigure || opts.NoUnity || opts.LTO
	if _, err := os.Stat(cache); os.IsNotExist(err) {
		needsConfigure = true
	}
	if !needsConfigure && requiresMakeProgramRefresh(cache, opts.MakeProgram) {
		console.Info("reconfigure required to refresh CMAKE_MAKE_PROGRAM")
		needsConfigure = true
	}
	if needsConfigure {
		if err := run(repoRoot, opts.PrintCmd, cmakePath, configureArgs...); err != nil {
			return err
		}
	} else {
		console.Info("configure skipped; use --reconfigure to force")
	}

	if handled, err := tryBuildWindowsMultiTarget(repoRoot, buildDir, preset, opts); handled {
		return err
	}

	return run(repoRoot, opts.PrintCmd, cmakePath, makeBuildArgs(preset, opts)...)
}

func makeBuildArgs(preset string, opts BuildOptions) []string {
	args := []string{"--build", "--preset", preset}
	if len(opts.Targets) > 0 {
		args = append(args, "--target")
		args = append(args, opts.Targets...)
	}
	if opts.Jobs > 0 {
		args = append(args, "--parallel", fmt.Sprintf("%d", opts.Jobs))
	} else {
		args = append(args, "--parallel")
	}
	return args
}

func ListPresets(repoRoot string) error {
	return run(repoRoot, false, "cmake", "--list-presets=configure")
}

func Clean(repoRoot string, preset string, target string) error {
	if target == "" || target == "out" {
		return os.RemoveAll(filepath.Join(repoRoot, "out"))
	}
	return run(repoRoot, false, "cmake", "--build", "--preset", preset, "--target", "clean")
}

func DefaultPreset() (string, error) {
	host, err := platform.Detect()
	if err != nil {
		return "", err
	}
	return host.Preset, nil
}

func run(dir string, printOnly bool, name string, args ...string) error {
	console.CommandLine(strings.TrimSpace(name + " " + strings.Join(args, " ")))
	if printOnly {
		return nil
	}
	cmd := exec.Command(name, args...)
	cmd.Dir = dir
	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr
	cmd.Stdin = os.Stdin
	if err := cmd.Run(); err != nil {
		return fmt.Errorf("%s failed: %w", name, err)
	}
	return nil
}

func tryBuildWindowsMultiTarget(repoRoot string, buildDir string, preset string, opts BuildOptions) (bool, error) {
	if preset != "windows" || len(opts.Targets) <= 1 {
		return false, nil
	}

	msbuild, ok := findMSBuild()
	if !ok {
		console.Info("MSBuild not found; falling back to cmake --build")
		return false, nil
	}

	projects := make([]string, 0, len(opts.Targets))
	for _, target := range opts.Targets {
		project, ok := findVSProjectForTarget(buildDir, target)
		if !ok {
			console.Info("project for target %s not found; falling back to cmake --build", target)
			return false, nil
		}
		projects = append(projects, project)
	}

	projectFile := filepath.Join(buildDir, "gnb_multi_target.proj")
	if !opts.PrintCmd {
		if err := os.WriteFile(projectFile, []byte(buildTraversalProject(projects)), 0o644); err != nil {
			return true, err
		}
	}

	args := []string{
		projectFile,
		msbuildParallelArg(opts.Jobs),
		"/p:Configuration=RelWithDebInfo",
		"/p:Platform=x64",
		"/verbosity:minimal",
	}
	return true, run(repoRoot, opts.PrintCmd, msbuild, args...)
}

func findMSBuild() (string, bool) {
	if path, err := exec.LookPath("MSBuild.exe"); err == nil {
		return path, true
	}

	if programFilesX86 := os.Getenv("ProgramFiles(x86)"); programFilesX86 != "" {
		vswhere := filepath.Join(programFilesX86, "Microsoft Visual Studio", "Installer", "vswhere.exe")
		if _, err := os.Stat(vswhere); err == nil {
			out, err := exec.Command(vswhere, "-latest", "-requires", "Microsoft.Component.MSBuild", "-find", `MSBuild\**\Bin\MSBuild.exe`).Output()
			if err == nil {
				for _, line := range strings.Split(string(out), "\n") {
					path := strings.TrimSpace(line)
					if path == "" {
						continue
					}
					if _, err := os.Stat(path); err == nil {
						return path, true
					}
				}
			}
		}
	}

	for _, root := range []string{os.Getenv("ProgramFiles"), os.Getenv("ProgramFiles(x86)")} {
		if root == "" {
			continue
		}
		for _, version := range []string{"18", "17"} {
			for _, edition := range []string{"Community", "Professional", "Enterprise", "BuildTools"} {
				path := filepath.Join(root, "Microsoft Visual Studio", version, edition, "MSBuild", "Current", "Bin", "MSBuild.exe")
				if _, err := os.Stat(path); err == nil {
					return path, true
				}
			}
		}
	}

	return "", false
}

func findVSProjectForTarget(buildDir string, target string) (string, bool) {
	want := target + ".vcxproj"
	var found string
	_ = filepath.WalkDir(buildDir, func(path string, entry os.DirEntry, err error) error {
		if err != nil || entry.IsDir() || entry.Name() != want {
			return nil
		}
		found = path
		return filepath.SkipAll
	})
	return found, found != ""
}

func buildTraversalProject(projects []string) string {
	var b strings.Builder
	b.WriteString("<Project DefaultTargets=\"Build\" xmlns=\"http://schemas.microsoft.com/developer/msbuild/2003\">\n")
	b.WriteString("  <ItemGroup>\n")
	for _, project := range projects {
		b.WriteString("    <ProjectToBuild Include=\"")
		b.WriteString(escapeXML(project))
		b.WriteString("\" />\n")
	}
	b.WriteString("  </ItemGroup>\n")
	b.WriteString("  <Target Name=\"Build\">\n")
	b.WriteString("    <MSBuild Projects=\"@(ProjectToBuild)\" Targets=\"Build\" BuildInParallel=\"true\" Properties=\"Configuration=RelWithDebInfo;Platform=x64\" />\n")
	b.WriteString("  </Target>\n")
	b.WriteString("</Project>\n")
	return b.String()
}

func msbuildParallelArg(jobs int) string {
	if jobs > 0 {
		return fmt.Sprintf("/m:%d", jobs)
	}
	return "/m"
}

func escapeXML(value string) string {
	replacer := strings.NewReplacer(
		"&", "&amp;",
		"\"", "&quot;",
		"'", "&apos;",
		"<", "&lt;",
		">", "&gt;",
	)
	return replacer.Replace(value)
}

func requiresMakeProgramRefresh(cachePath string, want string) bool {
	if want == "" {
		return false
	}

	file, err := os.Open(cachePath)
	if err != nil {
		return true
	}
	defer file.Close()

	scanner := bufio.NewScanner(file)
	for scanner.Scan() {
		line := scanner.Text()
		if !strings.HasPrefix(line, "CMAKE_MAKE_PROGRAM:") {
			continue
		}
		parts := strings.SplitN(line, "=", 2)
		if len(parts) != 2 {
			return true
		}
		current := filepath.Clean(parts[1])
		expected := filepath.Clean(want)
		if current != expected {
			return true
		}
		if _, err := os.Stat(current); err != nil {
			return true
		}
		return false
	}

	return true
}
