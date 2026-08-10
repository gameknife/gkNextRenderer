package config

import (
	"os"
	"path/filepath"
	"testing"

	"github.com/BurntSushi/toml"
)

func TestPackageAlwaysIncludeAssetsConfig(t *testing.T) {
	var cfg Config
	if _, err := toml.Decode(`[package]
default_preset = "default"
[package.presets.default]
targets = ["gkNextRenderer", "gkNextEditor"]
archive_name = "gknextrenderer_{platform}_{version}.7z"
always_include_assets = ["assets/models/conf_room.glb", "assets/models/pbr.glb"]
[package.presets.magicalego]
targets = ["MagicaLego"]
archive_name = "MagicaLego_{platform}_{version}.7z"
extra_files = ["bin/ffmpeg.exe"]
`, &cfg); err != nil {
		t.Fatal(err)
	}
	want := []string{"assets/models/conf_room.glb", "assets/models/pbr.glb"}
	preset := cfg.Package.Presets["default"]
	if len(preset.AlwaysIncludeAssets) != len(want) {
		t.Fatalf("always_include_assets = %v, want %v", preset.AlwaysIncludeAssets, want)
	}
	for i := range want {
		if preset.AlwaysIncludeAssets[i] != want[i] {
			t.Fatalf("always_include_assets[%d] = %q, want %q", i, preset.AlwaysIncludeAssets[i], want[i])
		}
	}
	magicalego := cfg.Package.Presets["magicalego"]
	if len(magicalego.Targets) != 1 || magicalego.Targets[0] != "MagicaLego" {
		t.Fatalf("magicalego targets = %v", magicalego.Targets)
	}
	if len(magicalego.ExtraFiles) != 1 || magicalego.ExtraFiles[0] != "bin/ffmpeg.exe" {
		t.Fatalf("magicalego extra_files = %v", magicalego.ExtraFiles)
	}
}

func TestFindRepoRootFromCandidatesFallsBackToExecutableDirectory(t *testing.T) {
	repoRoot := t.TempDir()
	if err := os.WriteFile(filepath.Join(repoRoot, "gnb.toml"), nil, 0644); err != nil {
		t.Fatal(err)
	}
	executableDir := filepath.Join(repoRoot, "tools", "gnb-bin", "windows-amd64")
	if err := os.MkdirAll(executableDir, 0755); err != nil {
		t.Fatal(err)
	}

	got, err := FindRepoRootFromCandidates(t.TempDir(), executableDir)
	if err != nil {
		t.Fatal(err)
	}
	if got != repoRoot {
		t.Fatalf("FindRepoRootFromCandidates() = %q, want %q", got, repoRoot)
	}
}

func TestFindRepoRootFromCandidatesPrefersWorkingDirectory(t *testing.T) {
	workingRepo := t.TempDir()
	executableRepo := t.TempDir()
	for _, repoRoot := range []string{workingRepo, executableRepo} {
		if err := os.WriteFile(filepath.Join(repoRoot, "gnb.toml"), nil, 0644); err != nil {
			t.Fatal(err)
		}
	}

	got, err := FindRepoRootFromCandidates(workingRepo, executableRepo)
	if err != nil {
		t.Fatal(err)
	}
	if got != workingRepo {
		t.Fatalf("FindRepoRootFromCandidates() = %q, want %q", got, workingRepo)
	}
}
