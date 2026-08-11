package android

import (
	"fmt"
	"os"
	"os/exec"
	"path/filepath"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/config"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/console"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/fetcher"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/vcpkg"
)

func Run(repoRoot string, cfg config.Config, mode string) error {
	if mode == "" {
		mode = "debug"
	}
	if mode != "debug" && mode != "release" {
		return fmt.Errorf("unsupported Android variant %q (expected debug or release)", mode)
	}
	if err := vcpkg.Ensure(repoRoot, cfg, false); err != nil {
		return err
	}
	cmakePath, err := vcpkg.ResolveCMake(repoRoot, cfg)
	if err != nil {
		return err
	}
	sdkRoot := fetcher.DiscoverVulkanSDK(repoRoot, cfg)
	if sdkRoot == "" {
		if err := fetcher.EnsureVulkanSDK(repoRoot, cfg); err != nil {
			return err
		}
		sdkRoot = fetcher.DiscoverVulkanSDK(repoRoot, cfg)
	}

	buildDir := filepath.Join(repoRoot, "out", "build", "android-"+mode)
	driverDir := filepath.Join(repoRoot, "tools", "android")
	configureArgs := []string{
		"-S", driverDir,
		"-B", buildDir,
		"-DGK_ANDROID_VARIANT=" + mode,
	}
	console.Command(cmakePath, configureArgs...)
	configure := exec.Command(cmakePath, configureArgs...)
	configure.Dir = repoRoot
	configure.Stdout = os.Stdout
	configure.Stderr = os.Stderr
	configure.Stdin = os.Stdin
	configure.Env = androidEnvironment(sdkRoot)
	if err := configure.Run(); err != nil {
		return err
	}

	target := "android-run"
	if mode == "release" {
		target = "android-apk"
	}
	buildArgs := []string{"--build", buildDir, "--target", target}
	console.Command(cmakePath, buildArgs...)
	cmd := exec.Command(cmakePath, buildArgs...)
	cmd.Dir = repoRoot
	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr
	cmd.Stdin = os.Stdin
	cmd.Env = androidEnvironment(sdkRoot)
	return cmd.Run()
}

func androidEnvironment(vulkanSDK string) []string {
	env := os.Environ()
	if vulkanSDK == "" {
		return env
	}
	console.Label("VULKAN_SDK", vulkanSDK)
	return append(env,
		"VULKAN_SDK="+vulkanSDK,
		"PATH="+filepath.Join(vulkanSDK, "bin")+string(os.PathListSeparator)+os.Getenv("PATH"),
	)
}
