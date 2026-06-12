package dashboard

import (
	"os"
	"path/filepath"
	"reflect"
	"testing"
)

func TestDiscoverCMakeExecutables(t *testing.T) {
	root := t.TempDir()
	writeTargetFixture(t, filepath.Join(root, "CMakeLists.txt"), `
add_library(Core STATIC core.cpp)
add_executable(AppOne main.cpp)
add_executable(
    AppTwo
    second.cpp
)
`)
	writeTargetFixture(t, filepath.Join(root, "nested", "targets.cmake"), `
add_executable(AppOne duplicate.cpp)
add_executable(Tool-Three tool.cpp)
`)

	got := discoverCMakeExecutables(root)
	want := []string{"AppOne", "AppTwo", "Tool-Three"}
	if !reflect.DeepEqual(got, want) {
		t.Fatalf("discoverCMakeExecutables() = %v, want %v", got, want)
	}
}

func TestDiscoverTargetsMergesFallbackAndMarksTestsNonRunnable(t *testing.T) {
	root := t.TempDir()
	writeTargetFixture(t, filepath.Join(root, "src", "CMakeLists.txt"), `
add_executable(NewGame main.cpp)
add_executable(gkNextUnitTests tests.cpp)
`)

	targets := discoverTargets(root, "windows", []string{"LegacyTool", "NewGame"})
	if len(targets) != 3 {
		t.Fatalf("expected 3 merged targets, got %d: %+v", len(targets), targets)
	}
	if targets[0].Name != "NewGame" || !targets[0].Runnable {
		t.Fatalf("unexpected first target: %+v", targets[0])
	}
	if targets[1].Name != "gkNextUnitTests" || targets[1].Runnable {
		t.Fatalf("unit test target should not be runnable: %+v", targets[1])
	}
	if targets[2].Name != "LegacyTool" {
		t.Fatalf("fallback target missing or reordered: %+v", targets)
	}
}

func writeTargetFixture(t *testing.T, path, content string) {
	t.Helper()
	if err := os.MkdirAll(filepath.Dir(path), 0o755); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(path, []byte(content), 0o644); err != nil {
		t.Fatal(err)
	}
}
