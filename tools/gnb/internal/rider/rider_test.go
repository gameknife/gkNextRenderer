package rider

import (
	"os"
	"path/filepath"
	"testing"
)

func TestRemoveProjectMetadata(t *testing.T) {
	repoRoot := t.TempDir()
	ideaPath := filepath.Join(repoRoot, ".idea")
	if err := os.MkdirAll(filepath.Join(ideaPath, "workspace"), 0o755); err != nil {
		t.Fatalf("create metadata directory: %v", err)
	}
	if err := os.WriteFile(filepath.Join(ideaPath, "workspace", "workspace.xml"), []byte("stale"), 0o644); err != nil {
		t.Fatalf("create metadata file: %v", err)
	}

	if err := RemoveProjectMetadata(repoRoot); err != nil {
		t.Fatalf("RemoveProjectMetadata() failed: %v", err)
	}
	if _, err := os.Stat(ideaPath); !os.IsNotExist(err) {
		t.Fatalf(".idea still exists, stat error = %v", err)
	}
}

func TestRemoveProjectMetadataRejectsFile(t *testing.T) {
	repoRoot := t.TempDir()
	ideaPath := filepath.Join(repoRoot, ".idea")
	if err := os.WriteFile(ideaPath, []byte("not a directory"), 0o644); err != nil {
		t.Fatalf("create metadata file: %v", err)
	}

	if err := RemoveProjectMetadata(repoRoot); err == nil {
		t.Fatal("RemoveProjectMetadata() accepted a file path")
	}
	if _, err := os.Stat(ideaPath); err != nil {
		t.Fatalf("unexpectedly removed metadata file: %v", err)
	}
}
