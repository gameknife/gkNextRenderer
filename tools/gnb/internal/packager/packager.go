package packager

import (
	"archive/zip"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"strings"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/console"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/platform"
)

func Package(repoRoot string, preset string, variant string, version string) error {
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
