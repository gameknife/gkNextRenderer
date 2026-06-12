package config

import (
	"os"
	"path/filepath"
	"testing"
)

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
