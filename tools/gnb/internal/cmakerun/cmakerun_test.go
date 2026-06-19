package cmakerun

import (
	"os"
	"path/filepath"
	"reflect"
	"testing"
)

func TestMakeBuildArgs(t *testing.T) {
	t.Run("multiple targets", func(t *testing.T) {
		got := makeBuildArgs("windows", BuildOptions{
			Targets: []string{"gkNextRenderer", "gkNextUnitTests"},
			Jobs:    8,
		})
		want := []string{
			"--build", "--preset", "windows",
			"--target", "gkNextRenderer", "gkNextUnitTests",
			"--parallel", "8",
		}
		if !reflect.DeepEqual(got, want) {
			t.Fatalf("makeBuildArgs() = %#v, want %#v", got, want)
		}
	})

	t.Run("default target", func(t *testing.T) {
		got := makeBuildArgs("linux", BuildOptions{})
		want := []string{"--build", "--preset", "linux"}
		if !reflect.DeepEqual(got, want) {
			t.Fatalf("makeBuildArgs() = %#v, want %#v", got, want)
		}
	})
}

func TestRequiresMakeProgramRefresh(t *testing.T) {
	t.Run("missing want does not force refresh", func(t *testing.T) {
		if requiresMakeProgramRefresh("missing-cache", "") {
			t.Fatalf("expected empty make program to skip refresh")
		}
	})

	t.Run("missing cache forces refresh", func(t *testing.T) {
		if !requiresMakeProgramRefresh("missing-cache", "/tmp/ninja") {
			t.Fatalf("expected missing cache to force refresh")
		}
	})

	t.Run("missing cached entry forces refresh", func(t *testing.T) {
		cachePath := writeCacheFile(t, "SOME_OTHER_KEY:STRING=value\n")
		if !requiresMakeProgramRefresh(cachePath, "/tmp/ninja") {
			t.Fatalf("expected missing CMAKE_MAKE_PROGRAM entry to force refresh")
		}
	})

	t.Run("mismatched cached path forces refresh", func(t *testing.T) {
		current := writeExecutable(t, "current-ninja")
		want := writeExecutable(t, "want-ninja")
		cachePath := writeCacheFile(t, "CMAKE_MAKE_PROGRAM:FILEPATH="+current+"\n")
		if !requiresMakeProgramRefresh(cachePath, want) {
			t.Fatalf("expected mismatched CMAKE_MAKE_PROGRAM to force refresh")
		}
	})

	t.Run("missing cached executable forces refresh", func(t *testing.T) {
		cachePath := writeCacheFile(t, "CMAKE_MAKE_PROGRAM:FILEPATH=/tmp/does-not-exist-ninja\n")
		if !requiresMakeProgramRefresh(cachePath, "/tmp/does-not-exist-ninja") {
			t.Fatalf("expected missing cached executable to force refresh")
		}
	})

	t.Run("matching existing executable keeps cache", func(t *testing.T) {
		ninjaPath := writeExecutable(t, "ninja")
		cachePath := writeCacheFile(t, "CMAKE_MAKE_PROGRAM:FILEPATH="+ninjaPath+"\n")
		if requiresMakeProgramRefresh(cachePath, ninjaPath) {
			t.Fatalf("expected matching CMAKE_MAKE_PROGRAM to keep cache")
		}
	})
}

func writeCacheFile(t *testing.T, contents string) string {
	t.Helper()
	dir := t.TempDir()
	path := filepath.Join(dir, "CMakeCache.txt")
	if err := os.WriteFile(path, []byte(contents), 0o644); err != nil {
		t.Fatalf("write cache: %v", err)
	}
	return path
}

func writeExecutable(t *testing.T, name string) string {
	t.Helper()
	dir := t.TempDir()
	path := filepath.Join(dir, name)
	if err := os.WriteFile(path, []byte("#!/bin/sh\n"), 0o755); err != nil {
		t.Fatalf("write executable: %v", err)
	}
	return path
}
