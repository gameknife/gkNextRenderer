package dashboard

import (
	"encoding/binary"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func TestPaksTabRendersArchiveAnalysis(t *testing.T) {
	repoRoot := t.TempDir()
	pakDir := filepath.Join(repoRoot, "assets", "paks")
	if err := os.MkdirAll(pakDir, 0o755); err != nil {
		t.Fatal(err)
	}
	writeDashboardTestPak(t, filepath.Join(pakDir, "sample.pak"))

	server, err := New(Options{RepoRoot: repoRoot, Preset: "windows"})
	if err != nil {
		t.Fatalf("New: %v", err)
	}
	recorder := httptest.NewRecorder()
	request := httptest.NewRequest(http.MethodGet, "/tab/paks", nil)
	server.routes().ServeHTTP(recorder, request)

	if recorder.Code != http.StatusOK {
		t.Fatalf("status = %d, body = %s", recorder.Code, recorder.Body.String())
	}
	body := recorder.Body.String()
	for _, want := range []string{"sample.pak", "assets/models", "ship.glb", "文件组织结构", "文件类型", "60 B"} {
		if !strings.Contains(body, want) {
			t.Errorf("response does not contain %q", want)
		}
	}
}

func TestBuildPaksVMRejectsUndiscoveredPath(t *testing.T) {
	repoRoot := t.TempDir()
	pakDir := filepath.Join(repoRoot, "assets", "paks")
	if err := os.MkdirAll(pakDir, 0o755); err != nil {
		t.Fatal(err)
	}
	writeDashboardTestPak(t, filepath.Join(pakDir, "sample.pak"))
	server := &Server{opts: Options{RepoRoot: repoRoot, Preset: "windows"}}
	vm := server.buildPaksVM("../secret.pak")
	if !strings.Contains(vm.Error, "不在可浏览范围") || len(vm.Files) != 1 {
		t.Fatalf("unexpected result: error = %q, files = %d", vm.Error, len(vm.Files))
	}
}

func writeDashboardTestPak(t *testing.T, path string) {
	t.Helper()
	names := []string{"assets/models/ship.glb", "assets/textures/ship.png"}
	stored := []uint32{40, 20}
	original := []uint32{100, 20}
	indexSize := 7 + len(names)*12
	for _, name := range names {
		indexSize += len(name) + 1
	}
	data := make([]byte, indexSize)
	copy(data, "GNP")
	binary.LittleEndian.PutUint32(data[3:7], uint32(len(names)))
	position := 7
	for _, name := range names {
		copy(data[position:], name)
		position += len(name) + 1
	}
	offset := uint32(indexSize)
	for index := range names {
		binary.LittleEndian.PutUint32(data[position:position+4], offset)
		binary.LittleEndian.PutUint32(data[position+4:position+8], stored[index])
		binary.LittleEndian.PutUint32(data[position+8:position+12], original[index])
		position += 12
		data = append(data, make([]byte, stored[index])...)
		offset += stored[index]
	}
	if err := os.WriteFile(path, data, 0o644); err != nil {
		t.Fatal(err)
	}
}
