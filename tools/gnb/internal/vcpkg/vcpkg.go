package vcpkg

import (
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"sort"
	"strings"
	"time"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/config"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/console"
)

func Root(repoRoot string, cfg config.Config) string {
	if env := os.Getenv("VCPKG_ROOT"); env != "" {
		return env
	}
	if cfg.Vcpkg.Root == "" {
		return filepath.Join(repoRoot, ".vcpkg")
	}
	return filepath.Join(repoRoot, cfg.Vcpkg.Root)
}

func Toolchain(repoRoot string, cfg config.Config) string {
	return filepath.Join(Root(repoRoot, cfg), "scripts", "buildsystems", "vcpkg.cmake")
}

func Exe(repoRoot string, cfg config.Config) string {
	name := "vcpkg"
	if runtime.GOOS == "windows" {
		name = "vcpkg.exe"
	}
	return filepath.Join(Root(repoRoot, cfg), name)
}

func ResolveCMake(repoRoot string, cfg config.Config) (string, error) {
	if path, err := exec.LookPath("cmake"); err == nil {
		return path, nil
	}
	return resolveDownloadedTool(repoRoot, cfg, cmakeToolName(), func(path string) bool {
		binComponent := string(filepath.Separator) + "bin" + string(filepath.Separator)
		return strings.Contains(path, binComponent)
	})
}

func ResolveNinja(repoRoot string, cfg config.Config) (string, error) {
	for _, name := range []string{"ninja", "ninja-build"} {
		if path, err := exec.LookPath(name); err == nil {
			return path, nil
		}
	}
	return resolveDownloadedTool(repoRoot, cfg, ninjaToolName(), nil)
}

func EnsureBundledCMake(repoRoot string, cfg config.Config) (string, error) {
	if path, err := exec.LookPath("cmake"); err == nil {
		return path, nil
	}
	if path, err := resolveDownloadedTool(repoRoot, cfg, cmakeToolName(), func(path string) bool {
		binComponent := string(filepath.Separator) + "bin" + string(filepath.Separator)
		return strings.Contains(path, binComponent)
	}); err == nil {
		return path, nil
	}
	if err := Ensure(repoRoot, cfg, false); err != nil {
		return "", err
	}
	if path, err := fetchTool(repoRoot, cfg, "cmake"); err == nil && path != "" {
		return path, nil
	} else if err != nil {
		return "", err
	}
	return resolveDownloadedTool(repoRoot, cfg, cmakeToolName(), func(path string) bool {
		binComponent := string(filepath.Separator) + "bin" + string(filepath.Separator)
		return strings.Contains(path, binComponent)
	})
}

func EnsureBundledNinja(repoRoot string, cfg config.Config) (string, error) {
	for _, name := range []string{"ninja", "ninja-build"} {
		if path, err := exec.LookPath(name); err == nil {
			return path, nil
		}
	}
	if path, err := resolveDownloadedTool(repoRoot, cfg, ninjaToolName(), nil); err == nil {
		return path, nil
	}
	if err := Ensure(repoRoot, cfg, false); err != nil {
		return "", err
	}
	if path, err := fetchTool(repoRoot, cfg, "ninja"); err == nil && path != "" {
		return path, nil
	} else if err != nil {
		return "", err
	}
	return resolveDownloadedTool(repoRoot, cfg, ninjaToolName(), nil)
}

func Ensure(repoRoot string, cfg config.Config, refresh bool) error {
	cache := filepath.Join(repoRoot, cfg.Vcpkg.BinaryCache)
	if cfg.Vcpkg.BinaryCache == "" {
		cache = filepath.Join(repoRoot, ".vcpkg_bincache")
	}
	if err := os.MkdirAll(cache, 0o755); err != nil {
		return err
	}

	root := Root(repoRoot, cfg)
	if _, err := os.Stat(filepath.Join(root, ".git")); os.IsNotExist(err) {
		if err := run(repoRoot, "git", "clone", "https://github.com/microsoft/vcpkg", root); err != nil {
			return err
		}
	}

	if refresh {
		if err := run(root, "git", "checkout", "master"); err != nil {
			_ = run(root, "git", "checkout", "-b", "master", "origin/master")
		}
		if err := run(root, "git", "pull", "--ff-only"); err != nil {
			return err
		}
	} else if cfg.Vcpkg.Ref != "" {
		if err := run(root, "git", "fetch", "origin", "--tags", "--force"); err != nil {
			return err
		}
		if err := run(root, "git", "-c", "advice.detachedHead=false", "checkout", "--force", cfg.Vcpkg.Ref); err != nil {
			return err
		}
		if err := run(root, "git", "reset", "--hard", cfg.Vcpkg.Ref); err != nil {
			return err
		}
	}

	needsBootstrap, err := NeedsBootstrap(repoRoot, cfg)
	if err != nil {
		return err
	}
	if needsBootstrap {
		return Bootstrap(repoRoot, cfg)
	}
	return nil
}

func NeedsBootstrap(repoRoot string, cfg config.Config) (bool, error) {
	exe := Exe(repoRoot, cfg)
	if _, err := os.Stat(exe); err != nil {
		if os.IsNotExist(err) {
			return true, nil
		}
		return false, err
	}

	root := Root(repoRoot, cfg)
	expectedTag, err := toolReleaseTag(root)
	if err != nil {
		return false, err
	}
	if expectedTag == "" {
		return false, nil
	}

	output, err := runCapture(root, exe, "version", "--disable-metrics")
	if err != nil {
		console.Warn("vcpkg version check failed; re-running bootstrap: %s", err)
		return true, nil
	}
	if !strings.Contains(output, expectedTag) {
		console.Info("vcpkg tool is %q but checkout expects %q; re-running bootstrap",
			strings.TrimSpace(firstLine(output)), expectedTag)
		return true, nil
	}
	return false, nil
}

func Bootstrap(repoRoot string, cfg config.Config) error {
	root := Root(repoRoot, cfg)
	if runtime.GOOS == "windows" {
		return run(root, "cmd", "/c", "bootstrap-vcpkg.bat", "-disableMetrics")
	}
	return run(root, "./bootstrap-vcpkg.sh", "-disableMetrics")
}

func toolReleaseTag(root string) (string, error) {
	data, err := os.ReadFile(filepath.Join(root, "scripts", "vcpkg-tool-metadata.txt"))
	if err != nil {
		if os.IsNotExist(err) {
			return "", nil
		}
		return "", err
	}
	for _, line := range strings.Split(string(data), "\n") {
		key, value, ok := strings.Cut(strings.TrimSpace(line), "=")
		if ok && key == "VCPKG_TOOL_RELEASE_TAG" {
			return strings.TrimSpace(value), nil
		}
	}
	return "", nil
}

func firstLine(s string) string {
	line, _, _ := strings.Cut(s, "\n")
	return line
}

func run(dir string, name string, args ...string) error {
	console.Command(name, args...)
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

func cmakeToolName() string {
	if runtime.GOOS == "windows" {
		return "cmake.exe"
	}
	return "cmake"
}

func ninjaToolName() string {
	if runtime.GOOS == "windows" {
		return "ninja.exe"
	}
	return "ninja"
}

func resolveDownloadedTool(repoRoot string, cfg config.Config, toolName string, filter func(string) bool) (string, error) {
	toolsRoot := filepath.Join(Root(repoRoot, cfg), "downloads", "tools")
	type toolMatch struct {
		path    string
		modTime time.Time
	}
	var matches []toolMatch
	_ = filepath.WalkDir(toolsRoot, func(path string, d os.DirEntry, err error) error {
		if err != nil || d == nil || d.IsDir() {
			return nil
		}
		if filepath.Base(path) != toolName {
			return nil
		}
		if filter != nil && !filter(path) {
			return nil
		}
		info, statErr := d.Info()
		if statErr != nil {
			return nil
		}
		matches = append(matches, toolMatch{path: path, modTime: info.ModTime()})
		return nil
	})

	if len(matches) == 0 {
		return "", fmt.Errorf("%s not found in PATH and no bundled vcpkg copy was found under %s", toolName, toolsRoot)
	}

	sort.Slice(matches, func(i int, j int) bool {
		if matches[i].modTime.Equal(matches[j].modTime) {
			return matches[i].path < matches[j].path
		}
		return matches[i].modTime.Before(matches[j].modTime)
	})
	return matches[len(matches)-1].path, nil
}

func fetchTool(repoRoot string, cfg config.Config, tool string) (string, error) {
	output, err := runCapture(repoRoot, Exe(repoRoot, cfg), "fetch", tool)
	if err != nil {
		return "", err
	}

	lines := strings.Split(strings.TrimSpace(output), "\n")
	for i := len(lines) - 1; i >= 0; i-- {
		line := strings.TrimSpace(lines[i])
		if line == "" {
			continue
		}
		if filepath.IsAbs(line) {
			return line, nil
		}
	}
	return "", nil
}

func runCapture(dir string, name string, args ...string) (string, error) {
	console.Command(name, args...)
	cmd := exec.Command(name, args...)
	cmd.Dir = dir
	cmd.Stderr = os.Stderr
	cmd.Stdin = os.Stdin
	output, err := cmd.Output()
	if err != nil {
		return "", fmt.Errorf("%s failed: %w", name, err)
	}
	return string(output), nil
}
