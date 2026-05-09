package cmakerun

import (
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"strings"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/platform"
)

type BuildOptions struct {
	Target      string
	Clean       bool
	Reconfigure bool
	Jobs        int
	NoUnity     bool
	LTO         bool
	PrintCmd    bool
}

func Build(repoRoot string, preset string, opts BuildOptions) error {
	buildDir := filepath.Join(repoRoot, "out", "build", preset)
	cache := filepath.Join(buildDir, "CMakeCache.txt")
	if opts.Clean {
		fmt.Printf("[gnb] cleaning %s\n", buildDir)
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

	needsConfigure := opts.Clean || opts.Reconfigure || opts.NoUnity || opts.LTO
	if _, err := os.Stat(cache); os.IsNotExist(err) {
		needsConfigure = true
	}
	if needsConfigure {
		if err := run(repoRoot, opts.PrintCmd, "cmake", configureArgs...); err != nil {
			return err
		}
	} else {
		fmt.Println("[gnb] configure skipped; use --reconfigure to force")
	}

	buildArgs := []string{"--build", "--preset", preset}
	if opts.Target != "" {
		buildArgs = append(buildArgs, "--target", opts.Target)
	}
	if opts.Jobs > 0 {
		buildArgs = append(buildArgs, "--parallel", fmt.Sprintf("%d", opts.Jobs))
	}
	return run(repoRoot, opts.PrintCmd, "cmake", buildArgs...)
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
	fmt.Printf("[gnb] %s %s\n", name, strings.Join(args, " "))
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
