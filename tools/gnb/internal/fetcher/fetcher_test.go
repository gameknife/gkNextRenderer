package fetcher

import (
	"archive/tar"
	"compress/gzip"
	"os"
	"path/filepath"
	"testing"
)

func TestUntarPreservesSymlink(t *testing.T) {
	tmpDir := t.TempDir()
	archivePath := filepath.Join(tmpDir, "archive.tar.gz")
	outDir := filepath.Join(tmpDir, "out")

	file, err := os.Create(archivePath)
	if err != nil {
		t.Fatal(err)
	}
	gz := gzip.NewWriter(file)
	tw := tar.NewWriter(gz)

	writeHeader := func(hdr *tar.Header, body string) {
		t.Helper()
		if err := tw.WriteHeader(hdr); err != nil {
			t.Fatal(err)
		}
		if body != "" {
			if _, err := tw.Write([]byte(body)); err != nil {
				t.Fatal(err)
			}
		}
	}

	writeHeader(&tar.Header{
		Name:     "./",
		Typeflag: tar.TypeDir,
		Mode:     0o755,
	}, "")
	writeHeader(&tar.Header{
		Name:     "pkg",
		Typeflag: tar.TypeDir,
		Mode:     0o755,
	}, "")
	writeHeader(&tar.Header{
		Name:     "pkg/libfoo.1.2.3.dylib",
		Typeflag: tar.TypeReg,
		Mode:     0o644,
		Size:     int64(len("payload")),
	}, "payload")
	writeHeader(&tar.Header{
		Name:     "pkg/libfoo.1.dylib",
		Typeflag: tar.TypeSymlink,
		Mode:     0o755,
		Linkname: "libfoo.1.2.3.dylib",
	}, "")

	if err := tw.Close(); err != nil {
		t.Fatal(err)
	}
	if err := gz.Close(); err != nil {
		t.Fatal(err)
	}
	if err := file.Close(); err != nil {
		t.Fatal(err)
	}

	if err := Untar(archivePath, outDir, true); err != nil {
		t.Fatal(err)
	}

	linkPath := filepath.Join(outDir, "pkg", "libfoo.1.dylib")
	info, err := os.Lstat(linkPath)
	if err != nil {
		t.Fatal(err)
	}
	if info.Mode()&os.ModeSymlink == 0 {
		t.Fatalf("%s is not a symlink", linkPath)
	}
	target, err := os.Readlink(linkPath)
	if err != nil {
		t.Fatal(err)
	}
	if target != "libfoo.1.2.3.dylib" {
		t.Fatalf("symlink target = %q, want %q", target, "libfoo.1.2.3.dylib")
	}
}

func TestArchiveEntryPathAllowsRootAndRejectsTraversal(t *testing.T) {
	dst := filepath.Join("relative", "out")
	root, err := archiveEntryPath(dst, "./")
	if err != nil {
		t.Fatalf("archiveEntryPath root: %v", err)
	}
	if root != filepath.Clean(dst) {
		t.Fatalf("archiveEntryPath root = %q, want %q", root, filepath.Clean(dst))
	}

	for _, name := range []string{"../outside", "/tmp/outside"} {
		if _, err := archiveEntryPath(dst, name); err == nil {
			t.Fatalf("archiveEntryPath accepted escaping entry %q", name)
		}
	}
}
