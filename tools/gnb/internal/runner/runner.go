package runner

import (
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"strings"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/console"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/platform"
)

type Options struct {
	Target       string
	Preset       string
	BinDir       string
	List         bool
	DryRun       bool
	PresentModes []string
	Scenes       []string
	Args         []string
}

func Run(repoRoot string, opts Options) error {
	binDir := opts.BinDir
	if binDir == "" {
		binDir = platform.BinDir(repoRoot, opts.Preset)
	}
	if _, err := os.Stat(binDir); err != nil {
		return fmt.Errorf("bin directory not found: %s\n请先运行 `gnb build %s`", binDir, opts.Target)
	}
	if opts.List {
		entries, err := os.ReadDir(binDir)
		if err != nil {
			return err
		}
		for _, entry := range entries {
			fmt.Println(entry.Name())
		}
		return nil
	}
	exe := platform.ExecutablePath(binDir, opts.Target)
	if _, err := os.Stat(exe); err != nil {
		return fmt.Errorf("executable not found: %s\n请先运行 `gnb build %s`", exe, opts.Target)
	}
	args := make([]string, 0, len(opts.PresentModes)+len(opts.Scenes)+len(opts.Args))
	for _, mode := range opts.PresentModes {
		args = append(args, "--present-mode="+mode)
	}
	for _, scene := range opts.Scenes {
		args = append(args, "--load-scene="+scene)
	}
	args = append(args, opts.Args...)
	console.CommandLine(strings.TrimSpace(exe + " " + strings.Join(args, " ")))
	if opts.DryRun {
		return nil
	}
	cmd := exec.Command(exe, args...)
	cmd.Dir = filepath.Dir(exe)
	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr
	cmd.Stdin = os.Stdin
	return cmd.Run()
}
