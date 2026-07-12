package packager

import (
	"archive/zip"
	"encoding/json"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"strings"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/console"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/platform"
)

func Package(repoRoot string, preset string, variant string, version string) error {
	if variant == "windows" || variant == "linux" || variant == "macos" {
		if err := prepareGnbSidecar(repoRoot, filepath.Join(repoRoot, "out", "build", preset), version); err != nil {
			return err
		}
	}
	switch variant {
	case "windows":
		return zipPaths(repoRoot, filepath.Join(repoRoot, "gkNextRenderer-windows.zip"), filepath.Join(repoRoot, "out", "build", preset), []string{
			"bin", "assets/locale", "assets/shaders", "assets/textures", "assets/fonts", "assets/models", "assets/paks",
		})
	case "linux":
		return zipPaths(repoRoot, filepath.Join(repoRoot, "gknextrenderer_linux64_"+fallbackVersion(version)+".zip"), filepath.Join(repoRoot, "out", "build", preset), []string{
			"bin", "assets/locale", "assets/shaders", "assets/textures", "assets/fonts", "assets/models", "assets/paks",
		})
	case "macos":
		return zipPaths(repoRoot, filepath.Join(repoRoot, "gknextrenderer_macos_"+fallbackVersion(version)+".zip"), filepath.Join(repoRoot, "out", "build", preset), []string{
			"bin", "assets/locale", "assets/shaders", "assets/textures", "assets/fonts", "assets/models", "assets/paks",
		})
	case "magicalego":
		if version == "" {
			return fmt.Errorf("gnb package magicalego requires --version")
		}
		buildRoot := filepath.Join(repoRoot, "out", "build", preset)
		return zipPaths(repoRoot, filepath.Join(repoRoot, "MagicaLego_win64_"+version+".zip"), buildRoot, []string{
			"bin/MagicaLego" + platformExeExt(), "bin/ffmpeg.exe", "bin/vulkan-1.dll", "assets/legos", "assets/paks",
		})
	default:
		return fmt.Errorf("unknown package variant %q", variant)
	}
}

func prepareGnbSidecar(repoRoot, buildRoot, version string) error {
	source := filepath.Join(repoRoot, "gnb"+platformExeExt())
	if _, err := os.Stat(source); err != nil {
		console.Warn("AI sidecar unavailable; package omits %s", source)
		return nil
	}
	destination := filepath.Join(buildRoot, "bin", "gnb"+platformExeExt())
	if err := os.MkdirAll(filepath.Dir(destination), 0o755); err != nil {
		return err
	}
	in, err := os.Open(source)
	if err != nil {
		return err
	}
	defer in.Close()
	out, err := os.Create(destination)
	if err != nil {
		return err
	}
	if _, err = io.Copy(out, in); err != nil {
		_ = out.Close()
		return err
	}
	if err := out.Close(); err != nil {
		return err
	}
	manifest := map[string]any{"protocolVersion": 1, "gnbVersion": fallbackVersion(version), "executable": filepath.Base(destination)}
	raw, _ := json.MarshalIndent(manifest, "", "  ")
	return os.WriteFile(filepath.Join(buildRoot, "bin", "gnb-agent-manifest.json"), raw, 0o644)
}

func fallbackVersion(version string) string {
	if version == "" {
		return "local"
	}
	return version
}

func platformExeExt() string {
	if platform.IsWindows() {
		return ".exe"
	}
	return ""
}

func zipPaths(repoRoot string, dst string, baseDir string, rels []string) error {
	if err := os.MkdirAll(filepath.Dir(dst), 0o755); err != nil {
		return err
	}
	out, err := os.Create(dst)
	if err != nil {
		return err
	}
	defer out.Close()
	zw := zip.NewWriter(out)
	defer zw.Close()

	for _, rel := range rels {
		path := filepath.Join(baseDir, filepath.FromSlash(rel))
		if _, err := os.Stat(path); err != nil {
			console.Warn("package skip missing %s", rel)
			continue
		}
		if err := addPath(zw, baseDir, path); err != nil {
			return err
		}
	}
	console.Success("package written: %s", dst)
	return nil
}

func addPath(zw *zip.Writer, baseDir string, path string) error {
	return filepath.WalkDir(path, func(current string, entry os.DirEntry, err error) error {
		if err != nil {
			return err
		}
		if entry.IsDir() {
			return nil
		}
		rel, err := filepath.Rel(baseDir, current)
		if err != nil {
			return err
		}
		rel = strings.ReplaceAll(rel, string(os.PathSeparator), "/")
		info, err := entry.Info()
		if err != nil {
			return err
		}
		header, err := zip.FileInfoHeader(info)
		if err != nil {
			return err
		}
		header.Name = rel
		header.Method = zip.Deflate
		writer, err := zw.CreateHeader(header)
		if err != nil {
			return err
		}
		in, err := os.Open(current)
		if err != nil {
			return err
		}
		defer in.Close()
		_, err = io.Copy(writer, in)
		return err
	})
}
