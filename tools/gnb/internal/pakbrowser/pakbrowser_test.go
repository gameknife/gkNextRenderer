package pakbrowser

import (
	"encoding/binary"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

type testEntry struct {
	name     string
	stored   uint32
	original uint32
}

func TestOpen(t *testing.T) {
	path := filepath.Join(t.TempDir(), "assets.pak")
	writeTestPak(t, path, []testEntry{
		{name: "assets/models/ship.glb", stored: 40, original: 100},
		{name: "assets/textures/ship.png", stored: 20, original: 20},
	})

	archive, err := Open(path)
	if err != nil {
		t.Fatalf("Open: %v", err)
	}
	if len(archive.Entries) != 2 {
		t.Fatalf("entries = %d, want 2", len(archive.Entries))
	}
	if archive.StoredSize != 60 {
		t.Fatalf("stored size = %d, want 60", archive.StoredSize)
	}
	if !archive.Entries[0].Compressed() || archive.Entries[1].Compressed() {
		t.Fatal("unexpected compression classification")
	}
	if archive.IndexSize >= archive.FileSize {
		t.Fatalf("index size %d should be smaller than file size %d", archive.IndexSize, archive.FileSize)
	}
}

func TestOpenRejectsInvalidPayloadRange(t *testing.T) {
	path := filepath.Join(t.TempDir(), "broken.pak")
	writeTestPak(t, path, []testEntry{{name: "assets/broken.bin", stored: 8, original: 8}})
	data, err := os.ReadFile(path)
	if err != nil {
		t.Fatal(err)
	}
	nameEnd := 7 + len("assets/broken.bin") + 1
	binary.LittleEndian.PutUint32(data[nameEnd:nameEnd+4], uint32(len(data)+1))
	if err := os.WriteFile(path, data, 0o644); err != nil {
		t.Fatal(err)
	}

	_, err = Open(path)
	if err == nil || !strings.Contains(err.Error(), "outside pak data") {
		t.Fatalf("Open error = %v, want payload range error", err)
	}
}

func writeTestPak(t *testing.T, path string, entries []testEntry) {
	t.Helper()
	indexSize := 7 + len(entries)*12
	for _, entry := range entries {
		indexSize += len(entry.name) + 1
	}
	data := make([]byte, indexSize)
	copy(data, "GNP")
	binary.LittleEndian.PutUint32(data[3:7], uint32(len(entries)))
	position := 7
	for _, entry := range entries {
		copy(data[position:], entry.name)
		position += len(entry.name) + 1
	}
	offset := uint32(indexSize)
	for _, entry := range entries {
		binary.LittleEndian.PutUint32(data[position:position+4], offset)
		binary.LittleEndian.PutUint32(data[position+4:position+8], entry.stored)
		binary.LittleEndian.PutUint32(data[position+8:position+12], entry.original)
		position += 12
		data = append(data, make([]byte, entry.stored)...)
		offset += entry.stored
	}
	if err := os.WriteFile(path, data, 0o644); err != nil {
		t.Fatal(err)
	}
}
