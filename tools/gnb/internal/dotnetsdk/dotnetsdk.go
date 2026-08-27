// Package dotnetsdk locates, installs and drives the .NET toolchain used by the managed scripting
// layer.
//
// Follows the same shape as the bundled tsc and llama.cpp dependencies: a pinned version in
// gnb.toml, downloaded into external/ on demand, so a fresh checkout never requires a machine-wide
// install. Unlike those, a matching .NET SDK is commonly already present, and a 300 MB download to
// duplicate it would be hostile — so an installed SDK that satisfies the pin is accepted, and the
// download is the fallback rather than the rule.
package dotnetsdk

import (
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"sort"
	"strconv"
	"strings"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/config"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/console"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/fetcher"
)

// SourceExternal marks a toolchain installed by gnb under external/dotnet.
const SourceExternal = "external"

// SourceSystem marks a machine-wide toolchain that satisfies the pinned version.
const SourceSystem = "system"

// Toolchain is a resolved .NET installation.
type Toolchain struct {
	// Root is the .NET installation root: the directory holding dotnet(.exe), sdk/ and packs/.
	Root string
	// Exe is the dotnet driver executable.
	Exe string
	// SDKVersion is the newest SDK found under Root.
	SDKVersion string
	// Source is SourceExternal or SourceSystem.
	Source string
}

// InstallDir is where gnb puts a downloaded SDK.
func InstallDir(repoRoot string) string {
	return filepath.Join(repoRoot, "external", "dotnet")
}

// Resolve returns a usable toolchain without downloading anything.
//
// Order is external install, then DOTNET_ROOT, then dotnet on PATH, then the platform's default
// install location. A machine-wide install is accepted when its newest SDK is at least the pinned
// version's feature band, so a developer with a current SDK never pays for the download.
func Resolve(repoRoot string, cfg config.DotNetConfig) (Toolchain, error) {
	var reasons []string

	for _, candidate := range candidateRoots(repoRoot) {
		toolchain, err := inspect(candidate.root, candidate.source)
		if err != nil {
			reasons = append(reasons, fmt.Sprintf("%s: %v", candidate.root, err))
			continue
		}
		if toolchain.Source == SourceExternal {
			return toolchain, nil
		}
		if satisfies(toolchain.SDKVersion, cfg.Version) {
			return toolchain, nil
		}
		reasons = append(reasons, fmt.Sprintf("%s: SDK %s is older than the pinned %s",
			toolchain.Root, toolchain.SDKVersion, cfg.Version))
	}

	detail := ""
	if len(reasons) > 0 {
		detail = "\n  " + strings.Join(reasons, "\n  ")
	}
	return Toolchain{}, fmt.Errorf("no usable .NET SDK found; run 'gnb dotnet setup'%s", detail)
}

// Ensure resolves a toolchain, downloading the pinned SDK into external/dotnet when nothing
// suitable is installed. With force, the pinned SDK is installed even if a system SDK would do.
func Ensure(repoRoot string, cfg config.DotNetConfig, force bool) (Toolchain, error) {
	if !force {
		if toolchain, err := Resolve(repoRoot, cfg); err == nil {
			return toolchain, nil
		}
	}

	url, err := DownloadURL(cfg)
	if err != nil {
		return Toolchain{}, err
	}

	installDir := InstallDir(repoRoot)
	console.Info("installing .NET SDK %s into %s", cfg.Version, installDir)
	if err := os.MkdirAll(installDir, 0o755); err != nil {
		return Toolchain{}, err
	}

	archive := filepath.Join(repoRoot, "external", ".download-dotnet-sdk"+archiveExtension())
	if err := fetcher.Download(url, archive); err != nil {
		return Toolchain{}, err
	}
	defer os.Remove(archive)

	// SDK archives are rooted at the install directory itself, so they extract in place.
	if strings.HasSuffix(archive, ".zip") {
		err = fetcher.Unzip(archive, installDir)
	} else {
		err = fetcher.Untar(archive, installDir, true)
	}
	if err != nil {
		return Toolchain{}, err
	}

	if runtime.GOOS != "windows" {
		if err := os.Chmod(filepath.Join(installDir, "dotnet"), 0o755); err != nil {
			return Toolchain{}, err
		}
	}

	toolchain, err := inspect(installDir, SourceExternal)
	if err != nil {
		return Toolchain{}, fmt.Errorf("installed .NET SDK at %s but it is not usable: %w", installDir, err)
	}
	return toolchain, nil
}

// DownloadURL renders the configured template for the host platform.
func DownloadURL(cfg config.DotNetConfig) (string, error) {
	rid, err := HostRID()
	if err != nil {
		return "", err
	}
	template := cfg.URLTemplate
	if template == "" {
		return "", fmt.Errorf("external.dotnet.url_template is not configured")
	}
	url := strings.ReplaceAll(template, "{version}", cfg.Version)
	url = strings.ReplaceAll(url, "{rid}", rid)
	url = strings.ReplaceAll(url, "{ext}", strings.TrimPrefix(archiveExtension(), "."))
	return url, nil
}

// HostRID is the .NET runtime identifier for the host platform.
func HostRID() (string, error) {
	arch := ""
	switch runtime.GOARCH {
	case "amd64":
		arch = "x64"
	case "arm64":
		arch = "arm64"
	default:
		return "", fmt.Errorf(".NET is not supported on %s/%s", runtime.GOOS, runtime.GOARCH)
	}

	switch runtime.GOOS {
	case "windows":
		return "win-" + arch, nil
	case "linux":
		return "linux-" + arch, nil
	case "darwin":
		return "osx-" + arch, nil
	default:
		return "", fmt.Errorf(".NET is not supported on %s/%s", runtime.GOOS, runtime.GOARCH)
	}
}

// HostPackNativeDir returns the directory holding hostfxr.h and coreclr_delegates.h for the host
// platform. The CoreCLR backend compiles against these headers; it links nothing from the pack.
func HostPackNativeDir(toolchain Toolchain) (string, error) {
	rid, err := HostRID()
	if err != nil {
		return "", err
	}

	packRoot := filepath.Join(toolchain.Root, "packs", "Microsoft.NETCore.App.Host."+rid)
	versions, err := sortedVersionDirs(packRoot)
	if err != nil || len(versions) == 0 {
		return "", fmt.Errorf("no host pack found under %s; the .NET installation at %s looks incomplete",
			packRoot, toolchain.Root)
	}

	for i := len(versions) - 1; i >= 0; i-- {
		candidate := filepath.Join(packRoot, versions[i], "runtimes", rid, "native")
		if fileExists(filepath.Join(candidate, "hostfxr.h")) {
			return candidate, nil
		}
	}
	return "", fmt.Errorf("no hostfxr.h found under %s", packRoot)
}

// Command builds a dotnet invocation bound to this toolchain. DOTNET_ROOT is pinned so a nested
// build cannot silently fall back to a different installation.
func (t Toolchain) Command(dir string, args ...string) *exec.Cmd {
	cmd := exec.Command(t.Exe, args...)
	cmd.Dir = dir
	cmd.Env = append(os.Environ(),
		"DOTNET_ROOT="+t.Root,
		"DOTNET_CLI_TELEMETRY_OPTOUT=1",
		"DOTNET_NOLOGO=1",
	)
	return cmd
}

// Run executes a dotnet command, streaming its output.
func (t Toolchain) Run(dir string, args ...string) error {
	console.Command("dotnet", args...)
	cmd := t.Command(dir, args...)
	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr
	if err := cmd.Run(); err != nil {
		return fmt.Errorf("dotnet %s failed: %w", strings.Join(args, " "), err)
	}
	return nil
}

type rootCandidate struct {
	root   string
	source string
}

func candidateRoots(repoRoot string) []rootCandidate {
	candidates := []rootCandidate{{root: InstallDir(repoRoot), source: SourceExternal}}

	if fromEnv := strings.TrimSpace(os.Getenv("DOTNET_ROOT")); fromEnv != "" {
		candidates = append(candidates, rootCandidate{root: fromEnv, source: SourceSystem})
	}
	if onPath, err := exec.LookPath("dotnet"); err == nil {
		if resolved, err := filepath.EvalSymlinks(onPath); err == nil {
			onPath = resolved
		}
		candidates = append(candidates, rootCandidate{root: filepath.Dir(onPath), source: SourceSystem})
	}
	switch runtime.GOOS {
	case "windows":
		if programFiles := os.Getenv("ProgramFiles"); programFiles != "" {
			candidates = append(candidates, rootCandidate{root: filepath.Join(programFiles, "dotnet"), source: SourceSystem})
		}
	case "darwin":
		candidates = append(candidates,
			rootCandidate{root: "/usr/local/share/dotnet", source: SourceSystem},
			rootCandidate{root: "/opt/homebrew/share/dotnet", source: SourceSystem})
	default:
		candidates = append(candidates,
			rootCandidate{root: "/usr/share/dotnet", source: SourceSystem},
			rootCandidate{root: "/usr/lib/dotnet", source: SourceSystem})
	}

	seen := map[string]bool{}
	unique := candidates[:0]
	for _, candidate := range candidates {
		key := strings.ToLower(filepath.Clean(candidate.root))
		if seen[key] {
			continue
		}
		seen[key] = true
		unique = append(unique, candidate)
	}
	return unique
}

func inspect(root string, source string) (Toolchain, error) {
	exe := filepath.Join(root, "dotnet")
	if runtime.GOOS == "windows" {
		exe += ".exe"
	}
	if !fileExists(exe) {
		return Toolchain{}, fmt.Errorf("no dotnet executable")
	}

	versions, err := sortedVersionDirs(filepath.Join(root, "sdk"))
	if err != nil || len(versions) == 0 {
		return Toolchain{}, fmt.Errorf("no SDK installed")
	}

	return Toolchain{
		Root:       root,
		Exe:        exe,
		SDKVersion: versions[len(versions)-1],
		Source:     source,
	}, nil
}

// satisfies reports whether an installed SDK can build what the pinned version can. Compared on
// major.minor.band: a newer patch inside the same feature band is always acceptable, and so is any
// newer band, since the managed projects target a framework rather than an exact SDK.
func satisfies(installed string, pinned string) bool {
	if pinned == "" {
		return true
	}
	return compareVersions(installed, pinned) >= 0
}

func compareVersions(left string, right string) int {
	leftParts := versionParts(left)
	rightParts := versionParts(right)
	for i := 0; i < len(leftParts) || i < len(rightParts); i++ {
		leftPart, rightPart := 0, 0
		if i < len(leftParts) {
			leftPart = leftParts[i]
		}
		if i < len(rightParts) {
			rightPart = rightParts[i]
		}
		if leftPart != rightPart {
			if leftPart > rightPart {
				return 1
			}
			return -1
		}
	}
	return 0
}

func versionParts(version string) []int {
	// Drop any prerelease suffix: "10.0.100-preview.3" compares as 10.0.100.
	if dash := strings.IndexByte(version, '-'); dash >= 0 {
		version = version[:dash]
	}
	fields := strings.Split(version, ".")
	parts := make([]int, 0, len(fields))
	for _, field := range fields {
		value, err := strconv.Atoi(strings.TrimSpace(field))
		if err != nil {
			break
		}
		parts = append(parts, value)
	}
	return parts
}

func sortedVersionDirs(dir string) ([]string, error) {
	entries, err := os.ReadDir(dir)
	if err != nil {
		return nil, err
	}
	versions := make([]string, 0, len(entries))
	for _, entry := range entries {
		if entry.IsDir() && len(versionParts(entry.Name())) > 0 {
			versions = append(versions, entry.Name())
		}
	}
	sort.Slice(versions, func(i, j int) bool { return compareVersions(versions[i], versions[j]) < 0 })
	return versions, nil
}

func archiveExtension() string {
	if runtime.GOOS == "windows" {
		return ".zip"
	}
	return ".tar.gz"
}

func fileExists(path string) bool {
	info, err := os.Stat(path)
	return err == nil && !info.IsDir()
}
