package llm

import (
	"os"
	"path/filepath"
	"runtime"
	"testing"
)

func TestRepairLlamaRuntimeCreatesMajorVersionAliases(t *testing.T) {
	if runtime.GOOS != "darwin" {
		t.Skip("darwin-only dylib alias behavior")
	}

	tmpDir := t.TempDir()
	layout := Layout{BinDir: tmpDir}
	realName := "libllama-common.0.0.9296.dylib"
	realPath := filepath.Join(tmpDir, realName)
	if err := os.WriteFile(realPath, []byte("x"), 0o644); err != nil {
		t.Fatal(err)
	}

	if err := repairLlamaRuntime(layout); err != nil {
		t.Fatal(err)
	}

	aliasPath := filepath.Join(tmpDir, "libllama-common.0.dylib")
	info, err := os.Lstat(aliasPath)
	if err != nil {
		t.Fatal(err)
	}
	if info.Mode()&os.ModeSymlink == 0 {
		t.Fatalf("%s is not a symlink", aliasPath)
	}
	target, err := os.Readlink(aliasPath)
	if err != nil {
		t.Fatal(err)
	}
	if target != realName {
		t.Fatalf("alias target = %q, want %q", target, realName)
	}
}
