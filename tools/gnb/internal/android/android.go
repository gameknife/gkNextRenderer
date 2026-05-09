package android

import (
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
)

func Run(repoRoot string, mode string) error {
	if mode == "" {
		mode = "debug"
	}
	task := "installAndLaunch"
	if mode == "release" {
		task = "build"
	}
	gradle := "./gradlew"
	if runtime.GOOS == "windows" {
		gradle = "gradlew.bat"
	}
	dir := filepath.Join(repoRoot, "android")
	fmt.Printf("[gnb] %s %s\n", gradle, task)
	cmd := exec.Command(gradle, task)
	cmd.Dir = dir
	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr
	cmd.Stdin = os.Stdin
	return cmd.Run()
}
