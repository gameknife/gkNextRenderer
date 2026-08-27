package csharpgen

import (
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
)

// Refresh re-dumps the reflection manifest from a built engine binary.
//
// This is the one step of code generation that needs the engine: entt::meta only exists at
// runtime, so nothing outside the process can enumerate it. Keeping it a separate, explicit
// command is what lets Run — and therefore `--check` — work from the committed snapshot alone.
func Refresh(repoRoot string, executable string) error {
	if _, err := os.Stat(executable); err != nil {
		return fmt.Errorf("cannot refresh the reflection manifest: %s is not built (%w)", executable, err)
	}

	manifestPath := filepath.Join(repoRoot, filepath.FromSlash(ManifestPath))
	command := exec.Command(executable, "--dump-reflection", manifestPath)
	command.Dir = repoRoot
	if output, err := command.CombinedOutput(); err != nil {
		return fmt.Errorf("%s --dump-reflection failed: %w\n%s", filepath.Base(executable), err, output)
	}
	return nil
}
