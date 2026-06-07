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
	Target      string
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

	buildArgs := []string{"--build", "--preset", preset}
	if opts.Target != "" {
		buildArgs = append(buildArgs, "--target", opts.Target)
	}
	if opts.Jobs > 0 {
		buildArgs = append(buildArgs, "--parallel", fmt.Sprintf("%d", opts.Jobs))
	}
	return run(repoRoot, opts.PrintCmd, cmakePath, buildArgs...)
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
