package dotnetsdk

import (
	"os"
	"path/filepath"
	"runtime"
	"strings"
	"testing"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/config"
)

func makeDir(parent string, name string) error {
	return os.Mkdir(filepath.Join(parent, name), 0o755)
}

func TestDownloadURLRendersHostPlatform(t *testing.T) {
	cfg := config.DotNetConfig{
		Version:     "10.0.300",
		URLTemplate: "https://example.invalid/Sdk/{version}/dotnet-sdk-{version}-{rid}.{ext}",
	}

	url, err := DownloadURL(cfg)
	if err != nil {
		t.Fatalf("DownloadURL: %v", err)
	}

	rid, err := HostRID()
	if err != nil {
		t.Fatalf("HostRID: %v", err)
	}

	wantExt := "tar.gz"
	if runtime.GOOS == "windows" {
		wantExt = "zip"
	}
	want := "https://example.invalid/Sdk/10.0.300/dotnet-sdk-10.0.300-" + rid + "." + wantExt
	if url != want {
		t.Fatalf("DownloadURL = %q, want %q", url, want)
	}
	if strings.Contains(url, "{") {
		t.Fatalf("DownloadURL left placeholders unrendered: %q", url)
	}
}

func TestDownloadURLRequiresTemplate(t *testing.T) {
	if _, err := DownloadURL(config.DotNetConfig{Version: "10.0.300"}); err == nil {
		t.Fatal("expected an error when url_template is unset")
	}
}

func TestSatisfies(t *testing.T) {
	cases := []struct {
		installed string
		pinned    string
		want      bool
	}{
		// An installed SDK at or beyond the pin is accepted, so a developer with a current SDK
		// never pays for the 300 MB download.
		{"10.0.300", "10.0.300", true},
		{"10.0.301", "10.0.300", true},
		{"10.0.400", "10.0.300", true},
		{"11.0.100", "10.0.300", true},
		// Lexicographic comparison would get this one wrong.
		{"9.0.400", "10.0.300", false},
		{"10.0.200", "10.0.300", false},
		{"8.0.100", "10.0.300", false},
		// Prerelease suffixes compare on their numeric part.
		{"10.0.300-preview.2", "10.0.300", true},
		// An unset pin accepts anything.
		{"8.0.100", "", true},
	}

	for _, testCase := range cases {
		if got := satisfies(testCase.installed, testCase.pinned); got != testCase.want {
			t.Errorf("satisfies(%q, %q) = %v, want %v",
				testCase.installed, testCase.pinned, got, testCase.want)
		}
	}
}

func TestSortedVersionDirsOrdersNumerically(t *testing.T) {
	dir := t.TempDir()
	for _, name := range []string{"9.0.400", "10.0.100", "10.0.300", "not-a-version"} {
		if err := makeDir(dir, name); err != nil {
			t.Fatalf("mkdir %s: %v", name, err)
		}
	}

	versions, err := sortedVersionDirs(dir)
	if err != nil {
		t.Fatalf("sortedVersionDirs: %v", err)
	}
	if len(versions) != 3 {
		t.Fatalf("sortedVersionDirs returned %v, want the three version directories", versions)
	}
	if newest := versions[len(versions)-1]; newest != "10.0.300" {
		t.Fatalf("newest = %q, want 10.0.300", newest)
	}
}
