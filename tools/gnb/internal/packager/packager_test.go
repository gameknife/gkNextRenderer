package packager

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func TestNormalizeTraceDeduplicatesSortsAndRejectsBuildFiles(t *testing.T) {
	path := filepath.Join(t.TempDir(), "assets.txt")
	input := strings.Join([]string{
		"assets/textures/z.png",
		"assets/models/a.glb\r",
		"assets/textures/z.png",
		"assets/models.stamp",
		"assets/paks/optional.pak",
		"../outside.txt",
		"",
	}, "\n")
	if err := os.WriteFile(path, []byte(input), 0o644); err != nil {
		t.Fatal(err)
	}
	if err := normalizeTrace(path); err != nil {
		t.Fatal(err)
	}
	raw, err := os.ReadFile(path)
	if err != nil {
		t.Fatal(err)
	}
	want := "assets/models/a.glb\nassets/textures/z.png\n"
	if string(raw) != want {
		t.Fatalf("normalized trace = %q, want %q", string(raw), want)
	}
}

func TestNormalizeTraceRejectsEmptyCoverage(t *testing.T) {
	path := filepath.Join(t.TempDir(), "assets.txt")
	if err := os.WriteFile(path, []byte("assets/paks/optional.pak\nassets/models.stamp\n"), 0o644); err != nil {
		t.Fatal(err)
	}
	if err := normalizeTrace(path); err == nil {
		t.Fatal("normalizeTrace accepted a trace without runtime assets")
	}
}

func TestReusePreciseAssetsRequiresCompleteBundle(t *testing.T) {
	repoRoot := t.TempDir()
	bundle := filepath.Join(repoRoot, "release-paks", "default")
	if err := os.MkdirAll(bundle, 0o755); err != nil {
		t.Fatal(err)
	}
	for _, name := range []string{"runtime.pak", "runtime-assets.txt", "runtime.manifest.json"} {
		if err := os.WriteFile(filepath.Join(bundle, name), []byte(name), 0o644); err != nil {
			t.Fatal(err)
		}
	}
	entries, err := reusePreciseAssets(repoRoot, Options{RuntimePak: filepath.Join("release-paks", "default")})
	if err != nil {
		t.Fatal(err)
	}
	if len(entries) != 3 || entries[0].name != "assets/paks/runtime.pak" {
		t.Fatalf("unexpected reusable precise assets: %#v", entries)
	}
	if err := os.Remove(filepath.Join(bundle, "runtime.pak")); err != nil {
		t.Fatal(err)
	}
	if _, err := reusePreciseAssets(repoRoot, Options{RuntimePak: filepath.Join("release-paks", "default")}); err == nil {
		t.Fatal("reusePreciseAssets accepted an incomplete bundle")
	}
}

func TestIncludeAllShaderBinaries(t *testing.T) {
	buildRoot := t.TempDir()
	shaderRoot := filepath.Join(buildRoot, "assets", "shaders", "nested")
	if err := os.MkdirAll(shaderRoot, 0o755); err != nil {
		t.Fatal(err)
	}
	for _, name := range []string{"active.comp.slang.spv", "inactive.frag.slang.spv", "source.slang"} {
		if err := os.WriteFile(filepath.Join(shaderRoot, name), []byte(name), 0o644); err != nil {
			t.Fatal(err)
		}
	}
	tracePath := filepath.Join(buildRoot, "trace.txt")
	if err := os.WriteFile(tracePath, []byte("assets/models/playground.glb\nassets/shaders/nested/active.comp.slang.spv\n"), 0o644); err != nil {
		t.Fatal(err)
	}
	if err := includeAllShaderBinaries(buildRoot, tracePath); err != nil {
		t.Fatal(err)
	}
	raw, err := os.ReadFile(tracePath)
	if err != nil {
		t.Fatal(err)
	}
	want := strings.Join([]string{
		"assets/models/playground.glb",
		"assets/shaders/nested/active.comp.slang.spv",
		"assets/shaders/nested/inactive.frag.slang.spv",
		"",
	}, "\n")
	if string(raw) != want {
		t.Fatalf("shader-complete trace = %q, want %q", string(raw), want)
	}
}

func TestIncludeConfiguredAssets(t *testing.T) {
	tracePath := filepath.Join(t.TempDir(), "trace.txt")
	if err := os.WriteFile(tracePath, []byte("assets/textures/base.png\n"), 0o644); err != nil {
		t.Fatal(err)
	}
	configured := []string{
		"assets/models/playground.glb",
		"assets/models/conf_room.glb",
		"assets/models/playground.glb",
	}
	if err := includeConfiguredAssets(tracePath, configured); err != nil {
		t.Fatal(err)
	}
	raw, err := os.ReadFile(tracePath)
	if err != nil {
		t.Fatal(err)
	}
	want := strings.Join([]string{
		"assets/models/conf_room.glb",
		"assets/models/playground.glb",
		"assets/textures/base.png",
		"",
	}, "\n")
	if string(raw) != want {
		t.Fatalf("configured trace = %q, want %q", string(raw), want)
	}
}

func TestIncludeConfiguredAssetsRejectsInvalidPath(t *testing.T) {
	tracePath := filepath.Join(t.TempDir(), "trace.txt")
	if err := os.WriteFile(tracePath, []byte("assets/textures/base.png\n"), 0o644); err != nil {
		t.Fatal(err)
	}
	if err := includeConfiguredAssets(tracePath, []string{"../private.glb"}); err == nil {
		t.Fatal("includeConfiguredAssets accepted a path outside assets")
	}
}

func TestGnbSidecarRequiresExplicitOptIn(t *testing.T) {
	gnbName := "gnb" + platformExeExt()
	for _, name := range []string{gnbName, "gnb-agent-manifest.json"} {
		if isRuntimeSidecar(name, false) {
			t.Fatalf("%s included without explicit opt-in", name)
		}
		if !isRuntimeSidecar(name, true) {
			t.Fatalf("%s excluded with explicit opt-in", name)
		}
	}
	if !isRuntimeSidecar("runtime.dll", false) {
		t.Fatal("ordinary runtime sidecar was excluded")
	}
}

func TestPackageArchivePathUsesPresetTemplate(t *testing.T) {
	preset := Preset{Name: "magicalego", Targets: []string{"MagicaLego"}, ArchiveName: "MagicaLego_{platform}_{version}.7z"}
	repoRoot := t.TempDir()
	got, err := packageArchivePath(repoRoot, "windows", "m1.2.3", preset)
	if err != nil {
		t.Fatal(err)
	}
	want := filepath.Join(repoRoot, "MagicaLego_win64_m1.2.3.7z")
	if got != want {
		t.Fatalf("packageArchivePath = %q, want %q", got, want)
	}
}

func TestCollectMacOSVulkanRuntimePackagesLoaderMoltenVKAndManifest(t *testing.T) {
	repoRoot := t.TempDir()
	sdkLib := filepath.Join(repoRoot, "external", "VulkanSDK", "1.4.0", "macOS", "lib")
	if err := os.MkdirAll(sdkLib, 0o755); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(repoRoot, "external", "VulkanSDK", ".current_version"), []byte("1.4.0\n"), 0o644); err != nil {
		t.Fatal(err)
	}
	for _, name := range []string{"libvulkan.1.dylib", "libMoltenVK.dylib"} {
		if err := os.WriteFile(filepath.Join(sdkLib, name), []byte(name), 0o755); err != nil {
			t.Fatal(err)
		}
	}

	buildRoot := filepath.Join(repoRoot, "out", "build", "macos-arm64")
	entries, err := collectMacOSVulkanRuntime(repoRoot, buildRoot)
	if err != nil {
		t.Fatal(err)
	}
	if len(entries) != 3 {
		t.Fatalf("runtime entries = %#v, want three entries", entries)
	}
	if entries[0].name != "bin/libvulkan.1.dylib" || entries[1].name != "bin/libMoltenVK.dylib" ||
		entries[2].name != "bin/vulkan/icd.d/MoltenVK_icd.json" {
		t.Fatalf("unexpected runtime entries: %#v", entries)
	}
	manifest, err := os.ReadFile(entries[2].source)
	if err != nil {
		t.Fatal(err)
	}
	if !strings.Contains(string(manifest), "../../libMoltenVK.dylib") {
		t.Fatalf("packaged manifest does not point at the bundled MoltenVK: %s", manifest)
	}
}

func TestValidatePresetRejectsEmptyTargets(t *testing.T) {
	err := validatePreset(Preset{Name: "empty", ArchiveName: "empty_{version}.7z"})
	if err == nil {
		t.Fatal("validatePreset accepted a preset without targets")
	}
}

func TestSmokeTargetsReadsPackageManifest(t *testing.T) {
	staging := t.TempDir()
	raw := []byte(`{"preset":"magicalego","platform":"windows","version":"m1","targets":["MagicaLego"]}`)
	if err := os.WriteFile(filepath.Join(staging, "package.manifest.json"), raw, 0o644); err != nil {
		t.Fatal(err)
	}
	targets, err := smokeTargets(staging)
	if err != nil {
		t.Fatal(err)
	}
	if len(targets) != 1 || targets[0] != "MagicaLego" {
		t.Fatalf("smoke targets = %v, want [MagicaLego]", targets)
	}
}
