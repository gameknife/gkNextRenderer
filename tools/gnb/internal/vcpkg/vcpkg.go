package vcpkg

import (
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"

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

	if _, err := os.Stat(Exe(repoRoot, cfg)); os.IsNotExist(err) {
		if runtime.GOOS == "windows" {
			return run(root, "cmd", "/c", "bootstrap-vcpkg.bat", "-disableMetrics")
		}
		return run(root, "./bootstrap-vcpkg.sh", "-disableMetrics")
	}
	return nil
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
