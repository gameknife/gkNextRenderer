package fetcher

import (
	"archive/tar"
	"archive/zip"
	"compress/gzip"
	"fmt"
	"io"
	"net/http"
	"os"
	"path/filepath"
	"runtime"
	"strings"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/config"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/console"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/platform"
)

func EnsureExternal(repoRoot string, cfg config.Config) error {
	if err := ensureTSC(repoRoot, cfg); err != nil {
		return err
	}
	key := platform.PlatformKey()
	if key == "linux" || key == "macos_arm64" {
		url := cfg.External.Slang.Linux
		name := "slang-2025.6.1-linux-x86_64"
		if key == "macos_arm64" {
			url = cfg.External.Slang.MacOSArm64
			name = "slang-2025.6.1-macos-aarch64"
		}
		if err := ensureArchive(repoRoot, url, filepath.Join(repoRoot, "external", name), "bin/slangc"); err != nil {
			return err
		}
	}
	if runtime.GOOS == "windows" {
		if err := ensureArchive(repoRoot, cfg.External.Streamline.URL, filepath.Join(repoRoot, "external", "streamline-2.10.0"), "include/sl.h"); err != nil {
			return err
		}
	}
	return nil
}

func EnsureIOSExternal(repoRoot string, cfg config.Config) error {
	return ensureArchive(repoRoot, cfg.External.MoltenVK.URL, filepath.Join(repoRoot, "external", "moltenvk-1.4.0"), "MoltenVK/static/MoltenVK.xcframework/ios-arm64/libMoltenVK.a")
}

func ensureTSC(repoRoot string, cfg config.Config) error {
	filename := "tsc"
	url := cfg.External.TSC.Linux
	if runtime.GOOS == "windows" {
		filename = "tsc.exe"
		url = cfg.External.TSC.Windows
	} else if runtime.GOOS == "darwin" {
		url = cfg.External.TSC.MacOSArm64
	}
	dir := filepath.Join(repoRoot, "tools", "tsc")
	dst := filepath.Join(dir, filename)
	versionFile := filepath.Join(dir, ".tsc_version")
	if data, err := os.ReadFile(versionFile); err == nil && strings.TrimSpace(string(data)) == cfg.External.TSC.Version {
		if _, err := os.Stat(dst); err == nil {
			return nil
		}
	}
	if err := os.MkdirAll(dir, 0o755); err != nil {
		return err
	}
	if err := Download(url, dst); err != nil {
		return err
	}
	if runtime.GOOS != "windows" {
		if err := os.Chmod(dst, 0o755); err != nil {
			return err
		}
	}
	return os.WriteFile(versionFile, []byte(cfg.External.TSC.Version+"\n"), 0o644)
}

func ensureArchive(repoRoot string, url string, dstDir string, sentinel string) error {
	if url == "" {
		return nil
	}
	if _, err := os.Stat(filepath.Join(dstDir, filepath.FromSlash(sentinel))); err == nil {
		return nil
	}
	if err := os.MkdirAll(dstDir, 0o755); err != nil {
		return err
	}
	tmp := filepath.Join(repoRoot, "external", ".download-"+filepath.Base(url))
	if err := Download(url, tmp); err != nil {
		return err
	}
	defer os.Remove(tmp)
	if strings.HasSuffix(url, ".zip") {
		return unzip(tmp, dstDir)
	}
	if strings.HasSuffix(url, ".tar") || strings.HasSuffix(url, ".tar.gz") || strings.HasSuffix(url, ".tgz") {
		return untar(tmp, dstDir, strings.HasSuffix(url, ".gz") || strings.HasSuffix(url, ".tgz"))
	}
	return fmt.Errorf("unsupported archive URL: %s", url)
}

func Download(url string, dst string) error {
	console.Info("download %s", url)
	resp, err := http.Get(url)
	if err != nil {
		return err
	}
	defer resp.Body.Close()
	if resp.StatusCode < 200 || resp.StatusCode >= 300 {
		return fmt.Errorf("download failed: %s", resp.Status)
	}
	if err := os.MkdirAll(filepath.Dir(dst), 0o755); err != nil {
		return err
	}
	part := dst + ".part"
	out, err := os.Create(part)
	if err != nil {
		return err
	}
	_, copyErr := io.Copy(out, resp.Body)
	closeErr := out.Close()
	if copyErr != nil {
		_ = os.Remove(part)
		return copyErr
	}
	if closeErr != nil {
		_ = os.Remove(part)
		return closeErr
	}
	return os.Rename(part, dst)
}

func unzip(src string, dst string) error {
	reader, err := zip.OpenReader(src)
	if err != nil {
		return err
	}
	defer reader.Close()
	for _, file := range reader.File {
		target := filepath.Join(dst, file.Name)
		if !strings.HasPrefix(filepath.Clean(target), filepath.Clean(dst)+string(os.PathSeparator)) {
			return fmt.Errorf("zip entry escapes destination: %s", file.Name)
		}
		if file.FileInfo().IsDir() {
			if err := os.MkdirAll(target, 0o755); err != nil {
				return err
			}
			continue
		}
		if err := os.MkdirAll(filepath.Dir(target), 0o755); err != nil {
			return err
		}
		in, err := file.Open()
		if err != nil {
			return err
		}
		out, err := os.OpenFile(target, os.O_CREATE|os.O_WRONLY|os.O_TRUNC, file.Mode())
		if err != nil {
			in.Close()
			return err
		}
		_, err = io.Copy(out, in)
		in.Close()
		if closeErr := out.Close(); err == nil {
			err = closeErr
		}
		if err != nil {
			return err
		}
	}
	return nil
}

func untar(src string, dst string, gz bool) error {
	file, err := os.Open(src)
	if err != nil {
		return err
	}
	defer file.Close()
	var reader io.Reader = file
	if gz {
		gzReader, err := gzip.NewReader(file)
		if err != nil {
			return err
		}
		defer gzReader.Close()
		reader = gzReader
	}
	tr := tar.NewReader(reader)
	for {
		header, err := tr.Next()
		if err == io.EOF {
			return nil
		}
		if err != nil {
			return err
		}
		target := filepath.Join(dst, header.Name)
		if !strings.HasPrefix(filepath.Clean(target), filepath.Clean(dst)+string(os.PathSeparator)) {
			return fmt.Errorf("tar entry escapes destination: %s", header.Name)
		}
		switch header.Typeflag {
		case tar.TypeDir:
			if err := os.MkdirAll(target, 0o755); err != nil {
				return err
			}
		case tar.TypeReg:
			if err := os.MkdirAll(filepath.Dir(target), 0o755); err != nil {
				return err
			}
			out, err := os.OpenFile(target, os.O_CREATE|os.O_WRONLY|os.O_TRUNC, os.FileMode(header.Mode))
			if err != nil {
				return err
			}
			_, err = io.Copy(out, tr)
			if closeErr := out.Close(); err == nil {
				err = closeErr
			}
			if err != nil {
				return err
			}
		}
	}
}
