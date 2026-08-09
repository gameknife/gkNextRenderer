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

// releaseTargets is the explicit list of executables shipped in the desktop release
// package. Anything not listed here stays out of the zip, so build-only tools and
// sample subprojects never leak into a public download.
var releaseTargets = []string{
	"gkNextRenderer",
	"gkNextEditor",
	"gkNextMotionBenchmark",
}

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

func Package(repoRoot string, preset string, variant string, version string) error {
	buildRoot := filepath.Join(repoRoot, "out", "build", preset)

	switch variant {
	case "windows", "linux", "macos":
		if err := prepareGnbSidecar(repoRoot, buildRoot, version); err != nil {
			return err
		}
		entries, err := collectDesktopEntries(repoRoot, buildRoot, variant, version)
		if err != nil {
			return err
		}
		return zipEntries(desktopArchivePath(repoRoot, variant, version), entries)
	case "magicalego":
		if version == "" {
			return fmt.Errorf("gnb package magicalego requires --version")
		}
		entries, err := collectPaths(buildRoot, []string{
			"bin/MagicaLego" + platformExeExt(), "bin/ffmpeg.exe", "bin/vulkan-1.dll", "assets/legos", "assets/paks",
		})
		if err != nil {
			return err
		}
		return zipEntries(filepath.Join(repoRoot, "MagicaLego_win64_"+version+".zip"), entries)
	default:
		return fmt.Errorf("unknown package variant %q", variant)
	}
}

func desktopArchivePath(repoRoot string, variant string, version string) string {
	switch variant {
	case "windows":
		return filepath.Join(repoRoot, "gknextrenderer_win64_"+fallbackVersion(version)+".zip")
	case "linux":
		return filepath.Join(repoRoot, "gknextrenderer_linux64_"+fallbackVersion(version)+".zip")
	default:
		return filepath.Join(repoRoot, "gknextrenderer_macos_"+fallbackVersion(version)+".zip")
	}
}

func collectDesktopEntries(repoRoot, buildRoot, variant, version string) ([]entry, error) {
	var entries []entry

	binDir := filepath.Join(buildRoot, "bin")
	found := map[string]bool{}
	for _, target := range releaseTargets {
		name := target + platformExeExt()
		path := filepath.Join(binDir, name)
		if _, err := os.Stat(path); err != nil {
			return nil, fmt.Errorf("release target %q missing from %s: build it before packaging", name, binDir)
		}
		entries = append(entries, entry{source: path, name: "bin/" + name})
		found[name] = true
	}

	// Runtime sidecars: shared libraries, vendor license files and the gnb agent
	// manifest. Debug artifacts (.pdb/.ilk/.exp/.lib) are excluded by construction.
	sidecars, err := os.ReadDir(binDir)
	if err != nil {
		return nil, err
	}
	for _, item := range sidecars {
		if item.IsDir() {
			continue
		}
		name := item.Name()
		if found[name] || !isRuntimeSidecar(name) {
			continue
		}
		entries = append(entries, entry{source: filepath.Join(binDir, name), name: "bin/" + name})
	}

	assets, err := collectPaths(buildRoot, releaseAssetDirs)
	if err != nil {
		return nil, err
	}
	entries = append(entries, assets...)

	docs, err := collectReleaseDocs(repoRoot, buildRoot, variant, version)
	if err != nil {
		return nil, err
	}
	return append(entries, docs...), nil
}

// isRuntimeSidecar reports whether a file next to the executables must ship with
// the release. It deliberately whitelists extensions so debug symbols and import
// libraries can never slip in.
func isRuntimeSidecar(name string) bool {
	lower := strings.ToLower(name)
	switch {
	case strings.HasSuffix(lower, ".dll"), strings.HasSuffix(lower, ".dylib"):
		return true
	case strings.Contains(lower, ".so."), strings.HasSuffix(lower, ".so"):
		return true
	case strings.HasSuffix(lower, ".license.txt"):
		return true
	case lower == "gnb"+platformExeExt() || lower == "gnb-agent-manifest.json":
		return true
	}
	return false
}

func collectReleaseDocs(repoRoot, buildRoot, variant, version string) ([]entry, error) {
	readme := filepath.Join(buildRoot, "RELEASE-README.txt")
	if err := os.WriteFile(readme, []byte(releaseReadme(variant, version)), 0o644); err != nil {
		return nil, err
	}
	entries := []entry{{source: readme, name: "README.txt"}}

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

func releaseReadme(variant string, version string) string {
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

	lines := []string{
		"gkNextRenderer " + fallbackVersion(version),
		"====================================",
		"",
		"Platform: " + platformName,
		"",
		"What is in this package",
		"-----------------------",
		"  bin/gkNextRenderer" + exeExt + "        Real-time renderer / viewer. Start here.",
		"  bin/gkNextEditor" + exeExt + "          Scene and material editor.",
		"  bin/gkNextMotionBenchmark" + exeExt + " Automated benchmark; writes a CSV report and exits.",
		"  assets/                        Runtime assets. Keep next to bin/.",
		"",
		"How to run",
		"----------",
		"  1. Extract the whole archive; bin/ and assets/ must stay side by side.",
		"  2. Launch bin/gkNextRenderer" + exeExt + ".",
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
		"  - GIF recording requires ffmpeg and is unavailable in this package;",
		"    animated WebP recording works.",
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
	}
	return strings.Join(lines, "\r\n")
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

func zipEntries(dst string, entries []entry) error {
	if err := os.MkdirAll(filepath.Dir(dst), 0o755); err != nil {
		return err
	}
	out, err := os.Create(dst)
	if err != nil {
		return err
	}
	defer out.Close()
	zw := zip.NewWriter(out)

	var total int64
	for _, item := range entries {
		size, err := addEntry(zw, item)
		if err != nil {
			_ = zw.Close()
			return err
		}
		total += size
	}
	if err := zw.Close(); err != nil {
		return err
	}
	console.Success("package written: %s (%d files, %.1f MB uncompressed)", dst, len(entries), float64(total)/(1024*1024))
	return nil
}

func addEntry(zw *zip.Writer, item entry) (int64, error) {
	info, err := os.Stat(item.source)
	if err != nil {
		return 0, err
	}
	header, err := zip.FileInfoHeader(info)
	if err != nil {
		return 0, err
	}
	header.Name = item.name
	header.Method = zip.Deflate
	writer, err := zw.CreateHeader(header)
	if err != nil {
		return 0, err
	}
	in, err := os.Open(item.source)
	if err != nil {
		return 0, err
	}
	defer in.Close()
	return io.Copy(writer, in)
}
