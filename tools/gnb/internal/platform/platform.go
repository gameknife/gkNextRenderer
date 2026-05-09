package platform

import (
	"errors"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
)

type Host struct {
	OS     string
	Arch   string
	Preset string
	ExeExt string
}

func Detect() (Host, error) {
	host := Host{OS: runtime.GOOS, Arch: runtime.GOARCH}
	if runtime.GOOS == "windows" {
		host.Preset = "windows"
		host.ExeExt = ".exe"
		return host, nil
	}
	if runtime.GOOS == "linux" {
		host.Preset = "linux"
		return host, nil
	}
	if runtime.GOOS == "darwin" {
		if runtime.GOARCH != "arm64" {
			return host, errors.New("macOS Intel is not a configured CMake preset; use macos-arm64 on Apple Silicon")
		}
		host.Preset = "macos-arm64"
		return host, nil
	}
	return host, fmt.Errorf("unsupported host platform: %s/%s", runtime.GOOS, runtime.GOARCH)
}

func PlatformKey() string {
	switch runtime.GOOS {
	case "windows":
		return "windows"
	case "linux":
		return "linux"
	case "darwin":
		if runtime.GOARCH == "arm64" {
			return "macos_arm64"
		}
		return "macos_amd64"
	default:
		return runtime.GOOS + "_" + runtime.GOARCH
	}
}

func IsWindows() bool {
	return runtime.GOOS == "windows"
}

func BinDir(repoRoot string, preset string) string {
	return filepath.Join(repoRoot, "out", "build", preset, "bin")
}

func ExecutablePath(binDir string, target string) string {
	path := filepath.Join(binDir, target)
	if runtime.GOOS == "windows" && filepath.Ext(path) == "" {
		path += ".exe"
	}
	return path
}

func CommandExists(name string) bool {
	_, err := exec.LookPath(name)
	return err == nil
}

func EnsureLinuxDesktopPackages() error {
	if runtime.GOOS != "linux" {
		return nil
	}
	missing := make([]string, 0)
	for _, module := range []string{"xrandr", "wayland-protocols", "xkbcommon"} {
		cmd := exec.Command("pkg-config", "--exists", module)
		if err := cmd.Run(); err != nil {
			missing = append(missing, module)
		}
	}
	if len(missing) == 0 {
		return nil
	}

	hint := "Install packages that provide pkg-config modules: xrandr wayland-protocols xkbcommon"
	if _, err := os.Stat("/etc/arch-release"); err == nil {
		hint = "sudo pacman -S --needed libxrandr wayland-protocols libxkbcommon"
	} else if CommandExists("apt-get") {
		hint = "sudo apt install libxrandr-dev wayland-protocols libxkbcommon-dev"
	}

	return fmt.Errorf("missing Linux desktop packages: %v\n%s", missing, hint)
}
