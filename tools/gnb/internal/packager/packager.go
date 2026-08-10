package packager

import (
	"context"
	"encoding/json"
	"fmt"
	"io"
	"os"
	"os/exec"
	"path/filepath"
	"sort"
	"strings"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/console"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/platform"
	validatepkg "github.com/gameknife/gknextrenderer/tools/gnb/internal/validate"
)

// releaseAssetDirs is the runtime asset whitelist. Every entry here is read by at
// least one shipped target at runtime; keep it in sync when a new asset root gains
// a runtime consumer.
var releaseAssetDirs = []string{
	"assets/anims",
	"assets/brand",
	"assets/configs",
	"assets/fonts",
	"assets/locale",
	"assets/models",
	"assets/paks",
	"assets/remote",
	"assets/scripts",
	"assets/shaders",
	"assets/sounds",
	"assets/textures",
}

// entry is one file to place into the archive.
type entry struct {
	source string
	name   string
}

// Preset describes one data-driven release shape from gnb.toml. Every preset
// travels through the same target collection, asset trace and archive pipeline.
type Preset struct {
	Name                string
	Targets             []string
	ArchiveName         string
	AlwaysIncludeAssets []string
	ExtraFiles          []string
}

type Options struct {
	Version     string
	TraceAssets bool
	AssetTrace  string
	TraceFrames int
	IncludeGNB  bool
}

func Package(repoRoot string, buildPreset string, variant string, packagePreset Preset, opts Options) error {
	if err := validatePreset(packagePreset); err != nil {
		return err
	}
	buildRoot := filepath.Join(repoRoot, "out", "build", buildPreset)
	version := opts.Version

	switch variant {
	case "windows", "linux", "macos":
		if opts.IncludeGNB {
			if err := prepareGnbSidecar(repoRoot, buildRoot, version); err != nil {
				return err
			}
		}
		var preciseAssets []entry
		if opts.TraceAssets || opts.AssetTrace != "" {
			tracePath, err := preparePreciseAssets(repoRoot, buildPreset, buildRoot, packagePreset, opts)
			if err != nil {
				return err
			}
			preciseAssets = []entry{
				{source: filepath.Join(buildRoot, "assets", "paks", "runtime.pak"), name: "assets/paks/runtime.pak"},
				{source: tracePath, name: "assets/paks/runtime-assets.txt"},
				{source: filepath.Join(buildRoot, "assets", "paks", "runtime.manifest.json"), name: "assets/paks/runtime.manifest.json"},
			}
		}
		entries, err := collectDesktopEntries(repoRoot, buildRoot, variant, version, packagePreset, preciseAssets, opts.IncludeGNB)
		if err != nil {
			return err
		}
		archivePath, err := packageArchivePath(repoRoot, variant, version, packagePreset)
		if err != nil {
			return err
		}
		return write7zArchive(archivePath, entries)
	default:
		return fmt.Errorf("unknown package platform %q", variant)
	}
}

func validatePreset(preset Preset) error {
	if preset.Name == "" {
		return fmt.Errorf("package preset name is empty")
	}
	for _, r := range preset.Name {
		if (r < 'a' || r > 'z') && (r < 'A' || r > 'Z') && (r < '0' || r > '9') && r != '-' && r != '_' && r != '.' {
			return fmt.Errorf("package preset name %q contains an unsupported character", preset.Name)
		}
	}
	if len(preset.Targets) == 0 {
		return fmt.Errorf("package preset %q has no targets", preset.Name)
	}
	for _, target := range preset.Targets {
		if target == "" || filepath.Base(target) != target {
			return fmt.Errorf("package preset %q has invalid target %q", preset.Name, target)
		}
	}
	if preset.ArchiveName == "" {
		return fmt.Errorf("package preset %q has no archive_name", preset.Name)
	}
	for _, rel := range preset.ExtraFiles {
		clean := filepath.Clean(filepath.FromSlash(strings.TrimSpace(rel)))
		if rel == "" || filepath.IsAbs(clean) || clean == ".." || strings.HasPrefix(clean, ".."+string(filepath.Separator)) {
			return fmt.Errorf("package preset %q has invalid extra_files entry %q", preset.Name, rel)
		}
	}
	return nil
}

func packageArchivePath(repoRoot string, variant string, version string, preset Preset) (string, error) {
	platformName := ""
	switch variant {
	case "windows":
		platformName = "win64"
	case "linux":
		platformName = "linux64"
	case "macos":
		platformName = "macos"
	default:
		return "", fmt.Errorf("unknown package platform %q", variant)
	}
	name := strings.NewReplacer(
		"{platform}", platformName,
		"{version}", fallbackVersion(version),
		"{preset}", preset.Name,
	).Replace(preset.ArchiveName)
	if filepath.Base(name) != name || !strings.EqualFold(filepath.Ext(name), ".7z") {
		return "", fmt.Errorf("package preset %q produced invalid archive name %q", preset.Name, name)
	}
	return filepath.Join(repoRoot, name), nil
}

func collectDesktopEntries(repoRoot, buildRoot, variant, version string, preset Preset, preciseAssets []entry, includeGnb bool) ([]entry, error) {
	var entries []entry

	binDir := filepath.Join(buildRoot, "bin")
	found := map[string]bool{}
	for _, target := range preset.Targets {
		name := target + platformExeExt()
		path := filepath.Join(binDir, name)
		if _, err := os.Stat(path); err != nil {
			return nil, fmt.Errorf("release target %q missing from %s: build it before packaging", name, binDir)
		}
		entries = append(entries, entry{source: path, name: "bin/" + name})
		found[name] = true
	}

	// Runtime sidecars: shared libraries and vendor license files. The optional
	// gnb agent executable and manifest are included only through --include-gnb.
	// Debug artifacts (.pdb/.ilk/.exp/.lib) are excluded by construction.
	sidecars, err := os.ReadDir(binDir)
	if err != nil {
		return nil, err
	}
	for _, item := range sidecars {
		if item.IsDir() {
			continue
		}
		name := item.Name()
		if found[name] || !isRuntimeSidecar(name, includeGnb) {
			continue
		}
		entries = append(entries, entry{source: filepath.Join(binDir, name), name: "bin/" + name})
		found[name] = true
	}

	extraFiles, err := collectPaths(buildRoot, preset.ExtraFiles)
	if err != nil {
		return nil, err
	}
	for _, item := range extraFiles {
		if found[filepath.Base(item.name)] {
			continue
		}
		entries = append(entries, item)
		found[filepath.Base(item.name)] = true
	}

	if len(preciseAssets) > 0 {
		entries = append(entries, preciseAssets...)
	} else {
		assets, err := collectPaths(buildRoot, releaseAssetDirs)
		if err != nil {
			return nil, err
		}
		entries = append(entries, assets...)
	}

	docs, err := collectReleaseDocs(repoRoot, buildRoot, variant, version, preset)
	if err != nil {
		return nil, err
	}
	return append(entries, docs...), nil
}

func preparePreciseAssets(repoRoot, buildPreset, buildRoot string, packagePreset Preset, opts Options) (string, error) {
	if opts.TraceAssets && opts.AssetTrace != "" {
		return "", fmt.Errorf("--trace-assets and --asset-trace are mutually exclusive")
	}
	pakDir := filepath.Join(buildRoot, "assets", "paks")
	pakPath := filepath.Join(pakDir, "runtime.pak")
	manifestPath := filepath.Join(pakDir, "runtime.manifest.json")
	if err := os.MkdirAll(pakDir, 0o755); err != nil {
		return "", err
	}
	// Never let a previous precise package contribute stale mounted entries to
	// either a fresh coverage run or a reused trace.
	for _, stale := range []string{pakPath, manifestPath} {
		if err := os.Remove(stale); err != nil && !os.IsNotExist(err) {
			return "", err
		}
	}
	tracePath := opts.AssetTrace
	if tracePath == "" {
		tracePath = filepath.Join(buildRoot, "asset-traces", packagePreset.Name+"-assets.txt")
		if err := os.MkdirAll(filepath.Dir(tracePath), 0o755); err != nil {
			return "", err
		}
		if err := os.Remove(tracePath); err != nil && !os.IsNotExist(err) {
			return "", err
		}
		for _, target := range packagePreset.Targets {
			console.Header("asset trace: " + target)
			err := validatepkg.Trace(context.Background(), validatepkg.Options{
				RepoRoot: repoRoot,
				Preset:   buildPreset,
				Target:   target,
				Args:     []string{"--asset-trace=" + tracePath},
			}, opts.TraceFrames)
			if err != nil {
				return "", fmt.Errorf("asset trace %s: %w", target, err)
			}
		}
	} else if !filepath.IsAbs(tracePath) {
		tracePath = filepath.Join(repoRoot, tracePath)
	}
	if err := normalizeTrace(tracePath); err != nil {
		return "", err
	}
	if err := includeConfiguredAssets(tracePath, packagePreset.AlwaysIncludeAssets); err != nil {
		return "", err
	}
	if err := includeAllShaderBinaries(buildRoot, tracePath); err != nil {
		return "", err
	}

	packer := platform.ExecutablePath(filepath.Join(buildRoot, "bin"), "Packager")
	if _, err := os.Stat(packer); err != nil {
		return "", fmt.Errorf("precise packaging requires %s; run `gnb build Packager` first", packer)
	}
	cmd := exec.Command(packer,
		"--out="+pakPath,
		"--root=",
		"--list="+tracePath,
		"--manifest="+manifestPath,
	)
	cmd.Dir = filepath.Join(buildRoot, "bin")
	cmd.Stdout, cmd.Stderr = os.Stdout, os.Stderr
	if err := cmd.Run(); err != nil {
		return "", fmt.Errorf("build precise runtime pak: %w", err)
	}
	return tracePath, nil
}

func normalizeTrace(path string) error {
	raw, err := os.ReadFile(path)
	if err != nil {
		return fmt.Errorf("read asset trace: %w", err)
	}
	unique := map[string]bool{}
	for _, line := range strings.Split(strings.ReplaceAll(string(raw), "\r\n", "\n"), "\n") {
		line = filepath.ToSlash(filepath.Clean(strings.TrimSpace(line)))
		rel := strings.TrimPrefix(line, "assets/")
		if strings.HasPrefix(line, "assets/") && strings.Contains(rel, "/") &&
			!strings.HasSuffix(strings.ToLower(line), ".pak") && !strings.HasSuffix(strings.ToLower(line), ".stamp") &&
			!strings.Contains(line, "../") {
			unique[line] = true
		}
	}
	if len(unique) == 0 {
		return fmt.Errorf("asset trace contains no usable assets: %s", path)
	}
	lines := make([]string, 0, len(unique))
	for line := range unique {
		lines = append(lines, line)
	}
	sort.Strings(lines)
	return os.WriteFile(path, []byte(strings.Join(lines, "\n")+"\n"), 0o644)
}

// includeConfiguredAssets merges release-critical assets from gnb.toml into a
// runtime trace. PakFromList performs the final availability check against both
// disk assets and mounted source paks.
func includeConfiguredAssets(tracePath string, configured []string) error {
	if len(configured) == 0 {
		return nil
	}
	raw, err := os.ReadFile(tracePath)
	if err != nil {
		return err
	}
	entries := map[string]bool{}
	for _, line := range strings.Split(strings.TrimSpace(string(raw)), "\n") {
		if line = strings.TrimSpace(line); line != "" {
			entries[filepath.ToSlash(line)] = true
		}
	}
	for _, asset := range configured {
		asset = filepath.ToSlash(filepath.Clean(strings.TrimSpace(asset)))
		rel := strings.TrimPrefix(asset, "assets/")
		if !strings.HasPrefix(asset, "assets/") || !strings.Contains(rel, "/") ||
			strings.Contains(asset, "../") || strings.HasSuffix(strings.ToLower(asset), ".pak") ||
			strings.HasSuffix(strings.ToLower(asset), ".stamp") {
			return fmt.Errorf("invalid package preset always_include_assets entry %q", asset)
		}
		entries[asset] = true
	}
	lines := make([]string, 0, len(entries))
	for line := range entries {
		lines = append(lines, line)
	}
	sort.Strings(lines)
	return os.WriteFile(tracePath, []byte(strings.Join(lines, "\n")+"\n"), 0o644)
}

// includeAllShaderBinaries adds every compiled Slang shader to a precise package.
// Renderer selection is runtime state, so coverage from the currently active
// renderer cannot prove which SPIR-V modules a user may select after shipping.
func includeAllShaderBinaries(buildRoot, tracePath string) error {
	raw, err := os.ReadFile(tracePath)
	if err != nil {
		return err
	}
	entries := map[string]bool{}
	for _, line := range strings.Split(strings.TrimSpace(string(raw)), "\n") {
		if line = strings.TrimSpace(line); line != "" {
			entries[filepath.ToSlash(line)] = true
		}
	}

	shaderRoot := filepath.Join(buildRoot, "assets", "shaders")
	err = filepath.WalkDir(shaderRoot, func(path string, item os.DirEntry, walkErr error) error {
		if walkErr != nil {
			return walkErr
		}
		if item.IsDir() || !strings.EqualFold(filepath.Ext(item.Name()), ".spv") {
			return nil
		}
		relative, relErr := filepath.Rel(buildRoot, path)
		if relErr != nil {
			return relErr
		}
		entries[filepath.ToSlash(relative)] = true
		return nil
	})
	if err != nil {
		return fmt.Errorf("collect compiled shaders: %w", err)
	}

	lines := make([]string, 0, len(entries))
	for line := range entries {
		lines = append(lines, line)
	}
	sort.Strings(lines)
	return os.WriteFile(tracePath, []byte(strings.Join(lines, "\n")+"\n"), 0o644)
}

// isRuntimeSidecar reports whether a file next to the executables must ship with
// the release. It deliberately whitelists extensions so debug symbols and import
// libraries can never slip in.
func isRuntimeSidecar(name string, includeGnb bool) bool {
	lower := strings.ToLower(name)
	switch {
	case strings.HasSuffix(lower, ".dll"), strings.HasSuffix(lower, ".dylib"):
		return true
	case strings.Contains(lower, ".so."), strings.HasSuffix(lower, ".so"):
		return true
	case strings.HasSuffix(lower, ".license.txt"):
		return true
	case lower == "gnb"+platformExeExt() || lower == "gnb-agent-manifest.json":
		return includeGnb
	}
	return false
}

type packageManifest struct {
	Preset   string   `json:"preset"`
	Platform string   `json:"platform"`
	Version  string   `json:"version"`
	Targets  []string `json:"targets"`
}

func collectReleaseDocs(repoRoot, buildRoot, variant, version string, preset Preset) ([]entry, error) {
	readme := filepath.Join(buildRoot, "RELEASE-README.txt")
	if err := os.WriteFile(readme, []byte(releaseReadme(variant, version, preset)), 0o644); err != nil {
		return nil, err
	}
	entries := []entry{{source: readme, name: "README.txt"}}
	manifestPath := filepath.Join(buildRoot, "RELEASE-PACKAGE-MANIFEST.json")
	manifest := packageManifest{Preset: preset.Name, Platform: variant, Version: fallbackVersion(version), Targets: preset.Targets}
	raw, err := json.MarshalIndent(manifest, "", "  ")
	if err != nil {
		return nil, err
	}
	if err := os.WriteFile(manifestPath, raw, 0o644); err != nil {
		return nil, err
	}
	entries = append(entries, entry{source: manifestPath, name: "package.manifest.json"})

	for _, doc := range []struct{ rel, name string }{
		{"LICENSE", "LICENSE"},
		{"THIRD-PARTY-NOTICES.md", "THIRD-PARTY-NOTICES.md"},
	} {
		path := filepath.Join(repoRoot, doc.rel)
		if _, err := os.Stat(path); err != nil {
			console.Warn("package skip missing %s", doc.rel)
			continue
		}
		entries = append(entries, entry{source: path, name: doc.name})
	}
	return entries, nil
}

func releaseReadme(variant string, version string, preset Preset) string {
	exeExt := ".exe"
	platformName := "Windows 10/11 x64"
	requirements := "" +
		"  - GPU: Vulkan 1.3 capable. Hardware ray tracing (RTX 20-series / RX 6000 or newer)\r\n" +
		"    unlocks the PathTracing renderer; other GPUs fall back to the software renderers.\r\n" +
		"  - Driver: keep the vendor driver up to date; Vulkan 1.3 support is required.\r\n"
	switch variant {
	case "linux":
		exeExt = ""
		platformName = "Linux x86_64 (glibc 2.35+)"
		requirements += "  - Install the distro Vulkan loader (libvulkan1) and your GPU vendor driver.\r\n"
	case "macos":
		exeExt = ""
		platformName = "macOS on Apple Silicon (arm64)"
		requirements += "  - Runs on MoltenVK; hardware ray tracing is unavailable, software renderers are used.\r\n"
	}

	title := preset.Targets[0]
	lines := []string{
		title + " " + fallbackVersion(version),
		"====================================",
		"",
		"Platform: " + platformName,
		"",
		"What is in this package",
		"-----------------------",
	}
	for _, target := range preset.Targets {
		lines = append(lines, "  bin/"+target+exeExt)
	}
	lines = append(lines,
		"  assets/                        Runtime assets. Keep next to bin/.",
		"",
		"How to run",
		"----------",
		"  1. Extract the whole archive; bin/ and assets/ must stay side by side.",
		"  2. Launch bin/"+preset.Targets[0]+exeExt+".",
		"  3. Pass --help to any executable for the command line options.",
		"",
		"System requirements",
		"-------------------",
		requirements,
		"Where files are written",
		"-----------------------",
		"  Logs, settings, screenshots and cooked data go to the per-user application",
		"  data directory. The log path is shown in the About dialog (Help > About).",
		"",
		"Known issues",
		"------------",
	)
	if !presetIncludesFile(preset, "ffmpeg.exe") {
		lines = append(lines,
			"  - GIF recording requires ffmpeg and is unavailable in this package;",
			"    animated WebP recording works.",
		)
	}
	lines = append(lines,
		"  - The user interface is English. A partial Chinese translation is",
		"    available with --locale zhCN.",
		"",
		"License",
		"-------",
		"  See LICENSE and THIRD-PARTY-NOTICES.md in this folder.",
		"",
		"Feedback",
		"--------",
		"  https://github.com/gameknife/gkNextRenderer/issues",
		"",
	)
	return strings.Join(lines, "\r\n")
}

func presetIncludesFile(preset Preset, name string) bool {
	for _, rel := range preset.ExtraFiles {
		if strings.EqualFold(filepath.Base(rel), name) {
			return true
		}
	}
	return false
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

// collectPaths expands relative files/directories under baseDir into archive entries.
func collectPaths(baseDir string, rels []string) ([]entry, error) {
	var entries []entry
	for _, rel := range rels {
		path := filepath.Join(baseDir, filepath.FromSlash(rel))
		info, err := os.Stat(path)
		if err != nil {
			console.Warn("package skip missing %s", rel)
			continue
		}
		if !info.IsDir() {
			entries = append(entries, entry{source: path, name: rel})
			continue
		}
		err = filepath.WalkDir(path, func(current string, item os.DirEntry, err error) error {
			if err != nil {
				return err
			}
			if item.IsDir() {
				return nil
			}
			name, err := filepath.Rel(baseDir, current)
			if err != nil {
				return err
			}
			entries = append(entries, entry{source: current, name: filepath.ToSlash(name)})
			return nil
		})
		if err != nil {
			return nil, err
		}
	}
	return entries, nil
}
