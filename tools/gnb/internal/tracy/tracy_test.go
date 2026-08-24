package tracy

import (
	"encoding/json"
	"os"
	"path/filepath"
	"testing"
)

func TestReleaseAsset(t *testing.T) {
	tests := []struct {
		goos string
		want string
	}{
		{goos: "windows", want: "windows"},
		{goos: "darwin", want: "macos"},
		{goos: "linux", want: "linux"},
	}

	for _, test := range tests {
		t.Run(test.goos, func(t *testing.T) {
			got, err := releaseAsset(test.goos, "0.14.1")
			if err != nil {
				t.Fatalf("releaseAsset() returned error: %v", err)
			}
			if got != test.want {
				t.Fatalf("releaseAsset() = %q, want %q", got, test.want)
			}
		})
	}
}

func TestReleaseAssetUnsupportedPlatform(t *testing.T) {
	if _, err := releaseAsset("freebsd", "0.14.1"); err == nil {
		t.Fatal("releaseAsset() unexpectedly accepted an unsupported platform")
	}
}

func TestFindProfilerInMacOSBundle(t *testing.T) {
	root := t.TempDir()
	profiler := filepath.Join(root, "tracy-profiler.app", "Contents", "MacOS", "tracy-profiler")
	if err := os.MkdirAll(filepath.Dir(profiler), 0o755); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(profiler, []byte("test"), 0o755); err != nil {
		t.Fatal(err)
	}

	if got := findProfiler(root); got != profiler {
		t.Fatalf("findProfiler() = %q, want %q", got, profiler)
	}
}

func TestVerifyClientVersionUsesRepositoryOverlay(t *testing.T) {
	root := t.TempDir()
	manifestPath := filepath.Join(root, "cmake", "vcpkg-overlays", "tracy", "vcpkg.json")
	if err := os.MkdirAll(filepath.Dir(manifestPath), 0o755); err != nil {
		t.Fatal(err)
	}
	manifest, err := json.Marshal(portManifest{Version: "0.14.1"})
	if err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(manifestPath, manifest, 0o644); err != nil {
		t.Fatal(err)
	}

	if err := verifyClientVersion(root, "0.14.1"); err != nil {
		t.Fatalf("verifyClientVersion() returned error: %v", err)
	}
}
