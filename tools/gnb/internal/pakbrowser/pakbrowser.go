// Package pakbrowser reads the index of gkNextEngine GNP package files.
// It deliberately does not decompress payloads: dashboard analysis only needs
// the names, offsets, stored sizes, and original sizes kept in the index.
package pakbrowser

import (
	"bufio"
	"encoding/binary"
	"fmt"
	"io"
	"os"
	"strings"
)

const (
	magic          = "GNP"
	maxEntryCount  = 1_000_000
	maxEntryName   = 16 * 1024
	entryIndexSize = 3 * 4
)

// Entry describes one file stored in a pak.
type Entry struct {
	Name             string
	Offset           uint64
	StoredSize       uint64
	UncompressedSize uint64
}

// Compressed reports whether the payload occupies less space than its source.
func (e Entry) Compressed() bool {
	return e.StoredSize < e.UncompressedSize
}

// Archive is the validated, read-only index of a pak file.
type Archive struct {
	Path       string
	FileSize   uint64
	IndexSize  uint64
	StoredSize uint64
	Entries    []Entry
}

// Open parses and validates the index in path.
func Open(path string) (*Archive, error) {
	file, err := os.Open(path)
	if err != nil {
		return nil, fmt.Errorf("open pak: %w", err)
	}
	defer file.Close()

	info, err := file.Stat()
	if err != nil {
		return nil, fmt.Errorf("stat pak: %w", err)
	}
	if info.Size() < 7 {
		return nil, fmt.Errorf("pak is too small (%d bytes)", info.Size())
	}

	reader := bufio.NewReader(file)
	header := make([]byte, len(magic))
	if _, err := io.ReadFull(reader, header); err != nil {
		return nil, fmt.Errorf("read pak header: %w", err)
	}
	if string(header) != magic {
		return nil, fmt.Errorf("invalid pak magic %q", string(header))
	}

	var entryCount uint32
	if err := binary.Read(reader, binary.LittleEndian, &entryCount); err != nil {
		return nil, fmt.Errorf("read entry count: %w", err)
	}
	if entryCount > maxEntryCount {
		return nil, fmt.Errorf("entry count %d exceeds safety limit", entryCount)
	}

	names := make([]string, entryCount)
	seen := make(map[string]struct{}, entryCount)
	for i := range names {
		name, err := readName(reader)
		if err != nil {
			return nil, fmt.Errorf("read entry %d name: %w", i, err)
		}
		name = strings.ReplaceAll(name, "\\", "/")
		name = strings.TrimPrefix(name, "./")
		if name == "" {
			return nil, fmt.Errorf("entry %d has an empty name", i)
		}
		if _, exists := seen[name]; exists {
			return nil, fmt.Errorf("duplicate entry name %q", name)
		}
		seen[name] = struct{}{}
		names[i] = name
	}

	indexSize := uint64(len(magic) + 4)
	for _, name := range names {
		indexSize += uint64(len(name) + 1)
	}
	indexSize += uint64(entryCount) * entryIndexSize
	fileSize := uint64(info.Size())
	if indexSize > fileSize {
		return nil, fmt.Errorf("pak index ends at %d beyond file size %d", indexSize, fileSize)
	}

	archive := &Archive{
		Path:      path,
		FileSize:  fileSize,
		IndexSize: indexSize,
		Entries:   make([]Entry, entryCount),
	}
	for i, name := range names {
		var offset, storedSize, uncompressedSize uint32
		if err := binary.Read(reader, binary.LittleEndian, &offset); err != nil {
			return nil, fmt.Errorf("read entry %q offset: %w", name, err)
		}
		if err := binary.Read(reader, binary.LittleEndian, &storedSize); err != nil {
			return nil, fmt.Errorf("read entry %q stored size: %w", name, err)
		}
		if err := binary.Read(reader, binary.LittleEndian, &uncompressedSize); err != nil {
			return nil, fmt.Errorf("read entry %q original size: %w", name, err)
		}
		end := uint64(offset) + uint64(storedSize)
		if uint64(offset) < indexSize || end > fileSize {
			return nil, fmt.Errorf("entry %q payload [%d, %d) is outside pak data [%d, %d)", name, offset, end, indexSize, fileSize)
		}
		archive.Entries[i] = Entry{
			Name:             name,
			Offset:           uint64(offset),
			StoredSize:       uint64(storedSize),
			UncompressedSize: uint64(uncompressedSize),
		}
		archive.StoredSize += uint64(storedSize)
	}
	return archive, nil
}

func readName(reader *bufio.Reader) (string, error) {
	name := make([]byte, 0, 128)
	for len(name) <= maxEntryName {
		value, err := reader.ReadByte()
		if err != nil {
			return "", err
		}
		if value == 0 {
			return string(name), nil
		}
		name = append(name, value)
	}
	return "", fmt.Errorf("name exceeds %d bytes", maxEntryName)
}
