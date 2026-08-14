package fetcher

import (
	"archive/tar"
	"archive/zip"
	"compress/gzip"
	"crypto/sha256"
	"fmt"
	"io"
	"net/http"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"slices"
	"strings"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/config"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/console"
)

type vulkanDownloadSpec struct {
	Version     string
	Platform    string
	ArchiveName string
	DownloadURL string
	SHAURL      string
}

func EnsureExternal(repoRoot string, cfg config.Config) error {
	return EnsureNamedExternal(repoRoot, cfg, nil)
}

func EnsureNamedExternal(repoRoot string, cfg config.Config, names []string) error {
	if len(names) == 0 {
		names = []string{"vulkan", "streamline", "fidelityfx"}
	}

	for _, name := range names {
		switch strings.ToLower(name) {
		case "all":
			return EnsureExternal(repoRoot, cfg)
		case "vulkan", "vulkansdk", "vulkan-sdk":
			if err := EnsureVulkanSDK(repoRoot, cfg); err != nil {
				return err
			}
		case "streamline":
			if runtime.GOOS == "windows" {
				if err := ensureArchive(repoRoot, cfg.External.Streamline.URL, filepath.Join(repoRoot, "external", "streamline-2.10.0"), "include/sl.h"); err != nil {
					return err
				}
			}
		case "fidelityfx", "fsr":
			if runtime.GOOS == "windows" {
				version := cfg.External.FidelityFX.Version
				if version == "" {
					version = "1.1.4"
				}
				dstDir := filepath.Join(repoRoot, "external", "fidelityfx-sdk-"+version)
				rootDir := "FidelityFX-SDK-" + version
				sentinel := filepath.ToSlash(filepath.Join(rootDir, "ffx-api", "include", "ffx_api", "ffx_api.h"))
				if err := ensureArchive(repoRoot, cfg.External.FidelityFX.URL, dstDir, sentinel); err != nil {
					return err
				}
			}
		default:
			return fmt.Errorf("unknown external dependency: %s", name)
		}
	}

	return nil
}

func EnsureVulkanSDK(repoRoot string, cfg config.Config) error {
	if explicit := os.Getenv("VULKAN_SDK"); explicit != "" {
		if normalizeVulkanSDKRoot(explicit) == "" {
			return fmt.Errorf("VULKAN_SDK is set to an unusable path: %s", explicit)
		}
		return nil
	}
	if sdkRoot := DiscoverVulkanSDK(repoRoot, cfg); sdkRoot != "" {
		return nil
	}

	spec, err := resolveVulkanDownloadSpec(cfg.External.VulkanSDK.Version)
	if err != nil {
		return err
	}

	installBase := externalPath(repoRoot, cfg.External.VulkanSDK.Root)
	versionRoot := filepath.Join(installBase, spec.Version)
	if sdkRoot := normalizeVulkanSDKRoot(versionRoot); sdkRoot != "" {
		return writeCurrentVulkanSDKVersion(installBase, spec.Version)
	}

	if err := os.MkdirAll(installBase, 0o755); err != nil {
		return err
	}
	if err := os.RemoveAll(versionRoot); err != nil {
		return err
	}

	tmp := filepath.Join(repoRoot, "external", ".download-"+spec.ArchiveName)
	if err := downloadAndVerify(spec, tmp); err != nil {
		return err
	}
	defer os.Remove(tmp)

	switch runtime.GOOS {
	case "darwin":
		if err := installVulkanSDKMac(tmp, versionRoot); err != nil {
			return err
		}
	case "linux":
		if err := extractTarXZ(tmp, installBase); err != nil {
			return err
		}
	case "windows":
		if err := installVulkanSDKWindows(tmp, versionRoot); err != nil {
			return err
		}
	default:
		return fmt.Errorf("automatic Vulkan SDK download is not supported on %s/%s", runtime.GOOS, runtime.GOARCH)
	}

	if sdkRoot := normalizeVulkanSDKRoot(versionRoot); sdkRoot == "" {
		return fmt.Errorf("installed Vulkan SDK at %s but could not locate a usable SDK root", versionRoot)
	}

	return writeCurrentVulkanSDKVersion(installBase, spec.Version)
}

func DiscoverVulkanSDK(repoRoot string, cfg config.Config) string {
	if sdkRoot := normalizeVulkanSDKRoot(os.Getenv("VULKAN_SDK")); sdkRoot != "" {
		return sdkRoot
	}

	installBase := externalPath(repoRoot, cfg.External.VulkanSDK.Root)
	if currentVersion := readCurrentVulkanSDKVersion(installBase); currentVersion != "" {
		if sdkRoot := normalizeVulkanSDKRoot(filepath.Join(installBase, currentVersion)); sdkRoot != "" {
			return sdkRoot
		}
	}

	if cfg.External.VulkanSDK.Version != "" && cfg.External.VulkanSDK.Version != "latest" {
		if sdkRoot := normalizeVulkanSDKRoot(filepath.Join(installBase, cfg.External.VulkanSDK.Version)); sdkRoot != "" {
			return sdkRoot
		}
	}

	matches, err := filepath.Glob(filepath.Join(installBase, "*"))
	if err != nil {
		return ""
	}
	slices.Sort(matches)
	slices.Reverse(matches)
	for _, match := range matches {
		if sdkRoot := normalizeVulkanSDKRoot(match); sdkRoot != "" {
			return sdkRoot
		}
	}

	if home, err := os.UserHomeDir(); err == nil {
		homeMatches, _ := filepath.Glob(filepath.Join(home, "VulkanSDK", "*"))
		slices.Sort(homeMatches)
		slices.Reverse(homeMatches)
		for _, match := range homeMatches {
			if sdkRoot := normalizeVulkanSDKRoot(match); sdkRoot != "" {
				return sdkRoot
			}
		}
	}

	return ""
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

// Unzip extracts a zip archive into dst (exported wrapper around unzip).
func Unzip(src string, dst string) error { return unzip(src, dst) }

// Untar extracts a tar or tar.gz archive into dst.
func Untar(src string, dst string, gz bool) error { return untar(src, dst, gz) }

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
		case tar.TypeSymlink:
			if err := os.MkdirAll(filepath.Dir(target), 0o755); err != nil {
				return err
			}
			linkTarget := filepath.Clean(filepath.Join(filepath.Dir(target), header.Linkname))
			if !strings.HasPrefix(linkTarget, filepath.Clean(dst)+string(os.PathSeparator)) {
				return fmt.Errorf("tar symlink escapes destination: %s -> %s", header.Name, header.Linkname)
			}
			_ = os.Remove(target)
			if err := os.Symlink(header.Linkname, target); err != nil {
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

func resolveVulkanDownloadSpec(requestedVersion string) (vulkanDownloadSpec, error) {
	spec := vulkanDownloadSpec{}
	switch runtime.GOOS {
	case "darwin":
		spec.Platform = "mac"
		spec.ArchiveName = "vulkan_sdk.zip"
	case "linux":
		spec.Platform = "linux"
		spec.ArchiveName = "vulkan_sdk.tar.xz"
	case "windows":
		if runtime.GOARCH == "arm64" {
			spec.Platform = "warm"
		} else {
			spec.Platform = "windows"
		}
		spec.ArchiveName = "vulkan_sdk.exe"
	default:
		return spec, fmt.Errorf("automatic Vulkan SDK download is not supported on %s/%s", runtime.GOOS, runtime.GOARCH)
	}

	spec.Version = requestedVersion
	if spec.Version == "" || spec.Version == "latest" {
		version, err := fetchText(fmt.Sprintf("https://vulkan.lunarg.com/sdk/latest/%s.txt", spec.Platform))
		if err != nil {
			return spec, err
		}
		spec.Version = strings.TrimSpace(version)
	}

	spec.DownloadURL = fmt.Sprintf("https://sdk.lunarg.com/sdk/download/%s/%s/%s?Human=true", spec.Version, spec.Platform, spec.ArchiveName)
	spec.SHAURL = fmt.Sprintf("https://sdk.lunarg.com/sdk/sha/%s/%s/%s.txt", spec.Version, spec.Platform, spec.ArchiveName)
	return spec, nil
}

func downloadAndVerify(spec vulkanDownloadSpec, dst string) error {
	if err := Download(spec.DownloadURL, dst); err != nil {
		return err
	}

	expected, err := fetchExpectedSHA(spec.SHAURL)
	if err != nil {
		return err
	}
	actual, err := fileSHA256(dst)
	if err != nil {
		return err
	}
	if !strings.EqualFold(expected, actual) {
		return fmt.Errorf("Vulkan SDK checksum mismatch for %s: expected %s, got %s", spec.Version, expected, actual)
	}
	return nil
}

func fetchExpectedSHA(url string) (string, error) {
	text, err := fetchText(url)
	if err != nil {
		return "", err
	}
	fields := strings.Fields(text)
	if len(fields) == 0 {
		return "", fmt.Errorf("empty SHA response from %s", url)
	}
	return fields[0], nil
}

func fetchText(url string) (string, error) {
	resp, err := http.Get(url)
	if err != nil {
		return "", err
	}
	defer resp.Body.Close()
	if resp.StatusCode < 200 || resp.StatusCode >= 300 {
		return "", fmt.Errorf("request failed: %s", resp.Status)
	}
	data, err := io.ReadAll(resp.Body)
	if err != nil {
		return "", err
	}
	return string(data), nil
}

func fileSHA256(path string) (string, error) {
	file, err := os.Open(path)
	if err != nil {
		return "", err
	}
	defer file.Close()

	hash := sha256.New()
	if _, err := io.Copy(hash, file); err != nil {
		return "", err
	}
	return fmt.Sprintf("%x", hash.Sum(nil)), nil
}

func installVulkanSDKMac(archivePath string, versionRoot string) error {
	stageDir := versionRoot + ".installer"
	if err := os.RemoveAll(stageDir); err != nil {
		return err
	}
	defer os.RemoveAll(stageDir)

	if err := unzip(archivePath, stageDir); err != nil {
		return err
	}
	installer, err := findMacVulkanInstaller(stageDir)
	if err != nil {
		return err
	}

	args := []string{
		"--root", versionRoot,
		"--accept-licenses",
		"--default-answer",
		"--confirm-command", "install",
		"copy_only=1",
	}
	return runCommand(stageDir, installer, args...)
}

func installVulkanSDKWindows(installerPath string, versionRoot string) error {
	args := []string{
		"--root", versionRoot,
		"--accept-licenses",
		"--default-answer",
		"--confirm-command", "install",
		"copy_only=1",
	}
	return runCommand(filepath.Dir(installerPath), installerPath, args...)
}

func extractTarXZ(archivePath string, dst string) error {
	return runCommand(filepath.Dir(archivePath), "tar", "-xf", archivePath, "-C", dst)
}

func runCommand(dir string, name string, args ...string) error {
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

func findMacVulkanInstaller(root string) (string, error) {
	var fallback string
	err := filepath.WalkDir(root, func(path string, d os.DirEntry, err error) error {
		if err != nil {
			return err
		}
		if !d.IsDir() || filepath.Ext(path) != ".app" {
			return nil
		}

		exeDir := filepath.Join(path, "Contents", "MacOS")
		entries, err := os.ReadDir(exeDir)
		if err != nil {
			return nil
		}

		appBase := strings.TrimSuffix(filepath.Base(path), ".app")
		for _, entry := range entries {
			if entry.IsDir() {
				continue
			}
			candidate := filepath.Join(exeDir, entry.Name())
			if entry.Name() == appBase {
				fallback = candidate
				return io.EOF
			}
			if fallback == "" {
				fallback = candidate
			}
		}
		return nil
	})
	if err != nil && err != io.EOF {
		return "", err
	}
	if fallback == "" {
		return "", fmt.Errorf("could not find a Vulkan SDK installer app in %s", root)
	}
	return fallback, nil
}

func normalizeVulkanSDKRoot(candidate string) string {
	if candidate == "" {
		return ""
	}

	switch runtime.GOOS {
	case "darwin":
		if fileExists(filepath.Join(candidate, "include", "vulkan", "vulkan.h")) &&
			fileExists(filepath.Join(candidate, "lib", "libvulkan.dylib")) {
			return candidate
		}
		macRoot := filepath.Join(candidate, "macOS")
		if fileExists(filepath.Join(macRoot, "include", "vulkan", "vulkan.h")) &&
			fileExists(filepath.Join(macRoot, "lib", "libvulkan.dylib")) {
			return macRoot
		}
	case "linux":
		if fileExists(filepath.Join(candidate, "include", "vulkan", "vulkan.h")) &&
			hasAnyFile(
				filepath.Join(candidate, "lib", "libvulkan.so"),
				filepath.Join(candidate, "lib", "libvulkan.so.1"),
				filepath.Join(candidate, "lib", "VulkanLoader", "lib", "libvulkan.so"),
				filepath.Join(candidate, "lib", "VulkanLoader", "lib", "libvulkan.so.1"),
			) {
			return candidate
		}
		archRoot := filepath.Join(candidate, "x86_64")
		if fileExists(filepath.Join(archRoot, "include", "vulkan", "vulkan.h")) &&
			hasAnyFile(
				filepath.Join(archRoot, "lib", "libvulkan.so"),
				filepath.Join(archRoot, "lib", "libvulkan.so.1"),
				filepath.Join(archRoot, "lib", "VulkanLoader", "lib", "libvulkan.so"),
				filepath.Join(archRoot, "lib", "VulkanLoader", "lib", "libvulkan.so.1"),
			) {
			return archRoot
		}
	case "windows":
		if hasAnyFile(
			filepath.Join(candidate, "Include", "vulkan", "vulkan.h"),
			filepath.Join(candidate, "include", "vulkan", "vulkan.h"),
		) && hasAnyFile(
			filepath.Join(candidate, "Lib", "vulkan-1.lib"),
			filepath.Join(candidate, "lib", "vulkan-1.lib"),
		) {
			return candidate
		}
	}

	return ""
}

func writeCurrentVulkanSDKVersion(installBase string, version string) error {
	return os.WriteFile(filepath.Join(installBase, ".current_version"), []byte(version+"\n"), 0o644)
}

func readCurrentVulkanSDKVersion(installBase string) string {
	data, err := os.ReadFile(filepath.Join(installBase, ".current_version"))
	if err != nil {
		return ""
	}
	return strings.TrimSpace(string(data))
}

func externalPath(repoRoot string, configured string) string {
	if filepath.IsAbs(configured) {
		return configured
	}
	return filepath.Join(repoRoot, configured)
}

func fileExists(path string) bool {
	info, err := os.Stat(path)
	return err == nil && !info.IsDir()
}

func hasAnyFile(paths ...string) bool {
	for _, path := range paths {
		if fileExists(path) {
			return true
		}
	}
	return false
}
