package platform

import (
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strings"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/console"
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

func EnsureLinuxPreparePackages() error {
	if runtime.GOOS != "linux" {
		return nil
	}

	if CommandExists("apt-get") && CommandExists("dpkg-query") {
		return ensureLinuxAptPackages()
	}
	if CommandExists("pacman") {
		return ensureLinuxPacmanPackages()
	}

	return EnsureLinuxDesktopPackages()
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

func ensureLinuxAptPackages() error {
	// System packages required on Debian/Ubuntu.
	//
	// Grouping:
	//   - build toolchain: vcpkg bootstrap + cmake/ninja
	//   - X11/Wayland dev headers: required at vcpkg *compile time* for the sdl3
	//     (X11/Wayland backends) and vulkan-loader (xcb/xlib/wayland) ports.
	//     The engine itself only links SDL3 and never touches Xlib/Wayland
	//     directly; these dev packages exist so vcpkg can build SDL3 against
	//     the system display stack. xorg-dev is the metapackage that covers the
	//     full X11 dev header set SDL3's X11 backend pulls in at configure time;
	//     the discrete libx11/libxft/libxext subset listed by the upstream sdl3
	//     portfile is NOT sufficient in practice.
	//
	// Notably NOT included (vs. historical list):
	//   - autoconf / autoconf-archive / automake / libtool: every Linux port in
	//     vcpkg.json is cmake-based; no autoreconf-style port remains.
	//   - libsystemd-dev: no reference anywhere in src/, cmake/, or gnb; the
	//     vcpkg dbus port pulls its own deps.
	packages := []string{
		"build-essential",
		"cmake",
		"ninja-build",
		"curl",
		"zip",
		"unzip",
		"tar",
		"pkg-config",
		"libxi-dev",
		"libxinerama-dev",
		"libxcursor-dev",
		"libxrandr-dev",
		"libxtst-dev",
		"wayland-protocols",
		"libxkbcommon-dev",
		"xorg-dev",
	}
	missing := make([]string, 0)
	for _, pkg := range packages {
		cmd := exec.Command("dpkg-query", "-W", "-f=${Status}", pkg)
		data, err := cmd.Output()
		if err != nil || !strings.Contains(string(data), "install ok installed") {
			missing = append(missing, pkg)
		}
	}
	if len(missing) == 0 {
		return nil
	}

	args := append([]string{"apt", "install"}, packages...)
	return runSystemPrepare("sudo", args...)
}

func ensureLinuxPacmanPackages() error {
	// See ensureLinuxAptPackages for the rationale on what is/isn't included.
	// base-devel already covers autotools on Arch; libxrandr pulls the rest of
	// the X11 dev headers transitively.
	packages := []string{
		"base-devel",
		"cmake",
		"ninja",
		"curl",
		"zip",
		"unzip",
		"tar",
		"pkgconf",
		"libxrandr",
		"libxtst",
		"wayland-protocols",
		"libxkbcommon",
	}
	missing := make([]string, 0)
	for _, pkg := range packages {
		if err := exec.Command("pacman", "-Q", pkg).Run(); err != nil {
			missing = append(missing, pkg)
		}
	}
	if len(missing) == 0 {
		return nil
	}

	args := append([]string{"pacman", "-S", "--needed"}, packages...)
	return runSystemPrepare("sudo", args...)
}

func runSystemPrepare(name string, args ...string) error {
	if runtime.GOOS == "linux" && name == "sudo" && os.Geteuid() == 0 && len(args) > 0 {
		name = args[0]
		args = args[1:]
	}
	if !CommandExists(name) {
		return fmt.Errorf("missing %s; install required Linux packages manually", name)
	}
	console.Info("preparing Linux packages")
	console.Command(name, args...)
	cmd := exec.Command(name, args...)
	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr
	cmd.Stdin = os.Stdin
	if err := cmd.Run(); err != nil {
		return fmt.Errorf("%s failed: %w", name, err)
	}
	return nil
}
