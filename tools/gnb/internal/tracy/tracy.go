package tracy

import (
	"encoding/json"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strings"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/config"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/console"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/fetcher"
)

const defaultVersion = "0.14.1"

type portManifest struct {
	Version string `json:"version"`
}

// Ensure downloads the official Tracy GUI without enabling vcpkg's gui-tools
// feature. The client version is checked against the repository vcpkg overlay before
// the GUI is launched so a protocol mismatch is reported early.
func Ensure(repoRoot string, cfg config.Config) (string, error) {
	version := strings.TrimSpace(cfg.External.Tracy.Version)
	if version == "" {
		version = defaultVersion
	}
	if err := verifyClientVersion(repoRoot, version); err != nil {
		return "", err
	}
	root := cfg.External.Tracy.Root
	if root == "" {
		root = filepath.Join("external", "tracy")
	}
	root = filepath.Join(repoRoot, filepath.FromSlash(root))
	if profiler := findProfiler(root); profiler != "" {
		if marker, err := os.ReadFile(filepath.Join(root, ".version")); err == nil && strings.TrimSpace(string(marker)) != version {
			return "", fmt.Errorf("Tracy GUI at %s is version %s, but client is %s; remove it and run `gnb tracy fetch`", root, strings.TrimSpace(string(marker)), version)
		}
		return profiler, nil
	}
	url := strings.TrimSpace(cfg.External.Tracy.URL)
	if url == "" {
		asset, err := releaseAsset(runtime.GOOS, version)
		if err != nil {
			return "", err
		}
		url = fmt.Sprintf("https://github.com/wolfpld/tracy/releases/download/v%s/%s-%s.zip", version, asset, version)
	}
	archive := filepath.Join(repoRoot, "external", ".download-tracy-"+version+".zip")
	if err := fetcher.Download(url, archive); err != nil {
		return "", err
	}
	defer os.Remove(archive)
	if err := os.MkdirAll(root, 0o755); err != nil {
		return "", err
	}
	if err := fetcher.Unzip(archive, root); err != nil {
		return "", fmt.Errorf("extract Tracy GUI: %w", err)
	}
	profiler := findProfiler(root)
	if profiler == "" {
		return "", fmt.Errorf("Tracy archive did not contain a profiler executable: %s", url)
	}
	if err := os.WriteFile(filepath.Join(root, ".version"), []byte(version+"\n"), 0o644); err != nil {
		return "", err
	}
	return profiler, nil
}

func releaseAsset(goos string, version string) (string, error) {
	switch goos {
	case "windows":
		return "windows", nil
	case "darwin":
		return "macos", nil
	case "linux":
		return "linux", nil
	default:
		return "", fmt.Errorf("Tracy %s has no official prebuilt GUI for %s; build the matching source package manually", version, goos)
	}
}

func Launch(repoRoot string, cfg config.Config) error {
	profiler, err := Ensure(repoRoot, cfg)
	if err != nil {
		return err
	}
	console.Command(profiler)
	cmd := exec.Command(profiler)
	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr
	if err := cmd.Start(); err != nil {
		return fmt.Errorf("start Tracy GUI: %w", err)
	}
	_ = cmd.Process.Release()
	return nil
}

func verifyClientVersion(repoRoot, expected string) error {
	path := filepath.Join(repoRoot, "cmake", "vcpkg-overlays", "tracy", "vcpkg.json")
	data, err := os.ReadFile(path)
	if err != nil {
		return fmt.Errorf("read Tracy vcpkg overlay %s: %w", path, err)
	}
	var manifest portManifest
	if err := json.Unmarshal(data, &manifest); err != nil {
		return fmt.Errorf("parse Tracy vcpkg overlay %s: %w", path, err)
	}
	if strings.TrimSpace(manifest.Version) != expected {
		return fmt.Errorf("Tracy client version %s does not match GUI version %s; update gnb.toml and the vcpkg overlay together", manifest.Version, expected)
	}
	return nil
}

func findProfiler(root string) string {
	var found string
	_ = filepath.WalkDir(root, func(path string, entry os.DirEntry, err error) error {
		if err != nil || found != "" || entry.IsDir() {
			return nil
		}
		name := strings.ToLower(entry.Name())
		if name == "tracy.exe" || name == "tracy-profiler.exe" || name == "tracy-profiler" {
			found = path
		}
		return nil
	})
	return found
}
