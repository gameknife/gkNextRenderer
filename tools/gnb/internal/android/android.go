package android

import (
	"os"
	"os/exec"
	"path/filepath"
	"runtime"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/config"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/console"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/fetcher"
)

func Run(repoRoot string, cfg config.Config, mode string) error {
	if mode == "" {
		mode = "debug"
	}
	sdkRoot := fetcher.DiscoverVulkanSDK(repoRoot, cfg)
	if sdkRoot == "" {
		if err := fetcher.EnsureVulkanSDK(repoRoot, cfg); err != nil {
			return err
		}
		sdkRoot = fetcher.DiscoverVulkanSDK(repoRoot, cfg)
	}

	task := "installAndLaunch"
	if mode == "release" {
		task = "build"
	}
	dir := filepath.Join(repoRoot, "android")
	gradle := filepath.Join(dir, "gradlew")
	if runtime.GOOS == "windows" {
		gradle = filepath.Join(dir, "gradlew.bat")
	}
	console.Command(gradle, task)
	cmd := exec.Command(gradle, task)
	cmd.Dir = dir
	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr
	cmd.Stdin = os.Stdin
	if sdkRoot != "" {
		console.Label("VULKAN_SDK", sdkRoot)
		cmd.Env = append(os.Environ(),
			"VULKAN_SDK="+sdkRoot,
			"PATH="+filepath.Join(sdkRoot, "bin")+string(os.PathListSeparator)+os.Getenv("PATH"),
		)
	}
	return cmd.Run()
}
