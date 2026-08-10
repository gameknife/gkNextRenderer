package packager

import (
	"os"
	"path/filepath"
	"slices"
	"testing"
)

func TestSafeArchiveName(t *testing.T) {
	for _, good := range []string{"README.txt", "bin/gkNextRenderer.exe", "assets/paks/runtime.pak"} {
		if got, err := safeArchiveName(good); err != nil || got != good {
			t.Fatalf("safeArchiveName(%q) = %q, %v", good, got, err)
		}
	}
	for _, bad := range []string{"", ".", "../outside", "assets/../../outside", "/absolute"} {
		if _, err := safeArchiveName(bad); err == nil {
			t.Fatalf("safeArchiveName accepted %q", bad)
		}
	}
}

func TestWriteAndExtract7zArchive(t *testing.T) {
	if _, err := resolve7Zip(); err != nil {
		t.Skip(err)
	}
	root := t.TempDir()
	source := filepath.Join(root, "source.txt")
	if err := os.WriteFile(source, []byte("runtime asset\n"), 0o644); err != nil {
		t.Fatal(err)
	}
	archive := filepath.Join(root, "package.7z")
	if err := write7zArchive(archive, []entry{{source: source, name: "assets/test/source.txt"}}); err != nil {
		t.Fatal(err)
	}
	destination := filepath.Join(root, "extracted")
	if err := os.MkdirAll(destination, 0o755); err != nil {
		t.Fatal(err)
	}
	names, err := extractArchive(archive, destination)
	if err != nil {
		t.Fatal(err)
	}
	if !slices.Contains(names, "assets/test/source.txt") {
		t.Fatalf("archive entries = %#v", names)
	}
	raw, err := os.ReadFile(filepath.Join(destination, "assets", "test", "source.txt"))
	if err != nil {
		t.Fatal(err)
	}
	if string(raw) != "runtime asset\n" {
		t.Fatalf("extracted content = %q", string(raw))
	}
}
