package ios

import "testing"

func TestDeviceBuildConstants(t *testing.T) {
	if preset != "ios-device" {
		t.Fatalf("preset = %q, want ios-device", preset)
	}
	if target != "gkNextRenderer" {
		t.Fatalf("target = %q, want gkNextRenderer", target)
	}
}
