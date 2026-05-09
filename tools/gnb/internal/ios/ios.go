package ios

import (
	"os"
	"os/exec"
	"path/filepath"
)

func Build(repoRoot string, skipCodeSign bool) error {
	args := []string{"-project", filepath.Join(repoRoot, "ios", "gkNextRenderer.xcodeproj"), "-scheme", "gkNextRenderer", "-configuration", "RelWithDebInfo", "build"}
	if skipCodeSign {
		args = append(args, "CODE_SIGNING_ALLOWED=NO", "CODE_SIGNING_REQUIRED=NO")
	}
	cmd := exec.Command("xcodebuild", args...)
	cmd.Dir = repoRoot
	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr
	cmd.Stdin = os.Stdin
	return cmd.Run()
}
