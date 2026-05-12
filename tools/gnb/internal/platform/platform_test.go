package platform

import (
	"runtime"
	"testing"
)

func TestDetectReturnsMacOSArm64PresetOnDarwin(t *testing.T) {
	if runtime.GOOS != "darwin" {
		t.Skip("darwin-only preset selection")
	}

	host, err := Detect()
	if err != nil {
		t.Fatalf("Detect() returned error: %v", err)
	}
	if host.Preset != "macos-arm64" {
		t.Fatalf("Detect() preset = %q, want %q", host.Preset, "macos-arm64")
	}
}
