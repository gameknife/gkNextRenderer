package ios

import (
	"fmt"
	"os"
	"os/exec"
	"strings"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/console"
)

func Build(repoRoot string, cmakePath string, skipCodeSign bool) error {
	if err := run(repoRoot, cmakePath, configureArgs(skipCodeSign)...); err != nil {
		return err
	}

	return run(repoRoot, cmakePath, buildArgs("gkNextRenderer")...)
}

func configureArgs(skipCodeSign bool) []string {
	return []string{"--preset", "ios", fmt.Sprintf("-DIOS_SKIP_CODE_SIGN=%s", onOff(skipCodeSign))}
}

func buildArgs(target string) []string {
	return []string{"--build", "--preset", "ios", "--target", target}
}

func onOff(enabled bool) string {
	if enabled {
		return "ON"
	}
	return "OFF"
}

func run(dir string, name string, args ...string) error {
	console.CommandLine(strings.TrimSpace(name + " " + strings.Join(args, " ")))

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
