package android

import (
	"reflect"
	"testing"
)

func TestNormalizeVariantDefaultsToRelease(t *testing.T) {
	variant, err := normalizeVariant("")
	if err != nil {
		t.Fatal(err)
	}
	if variant != "release" {
		t.Fatalf("variant = %q, want release", variant)
	}
}

func TestNormalizeVariantAcceptsKnownVariantsCaseInsensitively(t *testing.T) {
	for _, input := range []string{"RelWithDebInfo", "DEBUG", "release"} {
		if _, err := normalizeVariant(input); err != nil {
			t.Fatalf("normalizeVariant(%q): %v", input, err)
		}
	}
	if _, err := normalizeVariant("profile"); err == nil {
		t.Fatal("normalizeVariant(profile) succeeded, want error")
	}
}

func TestParseOnlineDevicesIgnoresOfflineDevices(t *testing.T) {
	output := "List of devices attached\r\nphone-1\tdevice product:foo\r\nemulator-5554\toffline\r\nphone-2\tunauthorized\r\n"
	got := parseOnlineDevices(output)
	want := []string{"phone-1"}
	if !reflect.DeepEqual(got, want) {
		t.Fatalf("parseOnlineDevices() = %#v, want %#v", got, want)
	}
}

func TestSelectDeviceUsesFirstOnlineDeviceUnlessExplicit(t *testing.T) {
	devices := []string{"phone-1", "emulator-5554"}
	serial, err := selectDevice(devices, "")
	if err != nil || serial != "phone-1" {
		t.Fatalf("select default = %q, %v; want phone-1, nil", serial, err)
	}
	serial, err = selectDevice(devices, "emulator-5554")
	if err != nil || serial != "emulator-5554" {
		t.Fatalf("select explicit = %q, %v; want emulator-5554, nil", serial, err)
	}
}

func TestSelectAVDDefaultsToFirstAvailable(t *testing.T) {
	avds := parseAVDs("Pixel_8_API_35\nTablet_API_35\n")
	avd, err := selectAVD(avds, "")
	if err != nil || avd != "Pixel_8_API_35" {
		t.Fatalf("selectAVD() = %q, %v; want Pixel_8_API_35, nil", avd, err)
	}
	if _, err := selectAVD(avds, "missing"); err == nil {
		t.Fatal("selectAVD(missing) succeeded, want error")
	}
}

func TestCMakeCacheValue(t *testing.T) {
	cache := "GK_ANDROID_SDK_ROOT:PATH=C:/Android/Sdk\nOTHER:STRING=value\n"
	if got := cmakeCacheValue(cache, "GK_ANDROID_SDK_ROOT"); got != "C:/Android/Sdk" {
		t.Fatalf("cmakeCacheValue() = %q, want C:/Android/Sdk", got)
	}
}
