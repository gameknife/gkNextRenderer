package dashboard

import (
	"fmt"
	"io/fs"
	"os"
	"path/filepath"
	"sort"
	"strings"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/pakbrowser"
)

type pakFileVM struct {
	RelPath  string
	Name     string
	Location string
	Size     uint64
	Selected bool
}

type pakTreeRowVM struct {
	Name             string
	Path             string
	Depth            int
	Directory        bool
	FileCount        int
	StoredSize       uint64
	UncompressedSize uint64
}

type pakTypeVM struct {
	Extension        string
	FileCount        int
	StoredSize       uint64
	UncompressedSize uint64
}

type paksVM struct {
	Files              []pakFileVM
	SelectedPath       string
	Archive            *pakbrowser.Archive
	Rows               []pakTreeRowVM
	Types              []pakTypeVM
	UncompressedSize   uint64
	CompressedFiles    int
	CompressionSavings float64
	Error              string
}

func (s *Server) buildPaksVM(selectedPath string) paksVM {
	vm := paksVM{}
	paths, err := discoverPakFiles(s.opts.RepoRoot, s.opts.Preset)
	if err != nil {
		vm.Error = err.Error()
		return vm
	}
	if len(paths) == 0 {
		return vm
	}

	selectedPath = filepath.ToSlash(filepath.Clean(selectedPath))
	if selectedPath == "." || selectedPath == "" {
		selectedPath = defaultPakPath(s.opts.RepoRoot, paths)
	}
	selectedAbs := ""
	for _, path := range paths {
		info, statErr := os.Stat(path)
		if statErr != nil {
			continue
		}
		rel, relErr := filepath.Rel(s.opts.RepoRoot, path)
		if relErr != nil {
			continue
		}
		rel = filepath.ToSlash(rel)
		fileVM := pakFileVM{
			RelPath:  rel,
			Name:     filepath.Base(path),
			Location: "source",
			Size:     uint64(info.Size()),
			Selected: rel == selectedPath,
		}
		if strings.HasPrefix(rel, "out/") {
			fileVM.Location = "build"
		}
		if fileVM.Selected {
			selectedAbs = path
		}
		vm.Files = append(vm.Files, fileVM)
	}
	vm.SelectedPath = selectedPath
	if selectedAbs == "" {
		vm.Error = fmt.Sprintf("Pak 不在可浏览范围内：%s", selectedPath)
		return vm
	}

	archive, err := pakbrowser.Open(selectedAbs)
	if err != nil {
		vm.Error = fmt.Sprintf("解析 %s 失败：%v", selectedPath, err)
		return vm
	}
	vm.Archive = archive
	vm.Rows = buildPakTreeRows(archive.Entries)
	vm.Types = buildPakTypes(archive.Entries)
	for _, entry := range archive.Entries {
		vm.UncompressedSize += entry.UncompressedSize
		if entry.Compressed() {
			vm.CompressedFiles++
		}
	}
	if vm.UncompressedSize > 0 {
		vm.CompressionSavings = (1 - float64(archive.StoredSize)/float64(vm.UncompressedSize)) * 100
	}
	return vm
}

func discoverPakFiles(repoRoot, preset string) ([]string, error) {
	roots := []string{
		filepath.Join(repoRoot, "assets", "paks"),
		filepath.Join(repoRoot, "out", "build", preset, "assets", "paks"),
		filepath.Join(repoRoot, "out", "build", preset, "bin", "assets", "paks"),
	}
	seen := make(map[string]struct{})
	var paths []string
	for _, root := range roots {
		info, err := os.Stat(root)
		if err != nil {
			if os.IsNotExist(err) {
				continue
			}
			return nil, fmt.Errorf("scan pak directory %s: %w", root, err)
		}
		if !info.IsDir() {
			continue
		}
		err = filepath.WalkDir(root, func(path string, entry fs.DirEntry, walkErr error) error {
			if walkErr != nil {
				return walkErr
			}
			if entry.IsDir() || !strings.EqualFold(filepath.Ext(entry.Name()), ".pak") {
				return nil
			}
			clean := filepath.Clean(path)
			if _, exists := seen[clean]; exists {
				return nil
			}
			seen[clean] = struct{}{}
			paths = append(paths, clean)
			return nil
		})
		if err != nil {
			return nil, fmt.Errorf("scan pak directory %s: %w", root, err)
		}
	}
	sort.Slice(paths, func(i, j int) bool {
		left, _ := filepath.Rel(repoRoot, paths[i])
		right, _ := filepath.Rel(repoRoot, paths[j])
		return strings.ToLower(filepath.ToSlash(left)) < strings.ToLower(filepath.ToSlash(right))
	})
	return paths, nil
}

func defaultPakPath(repoRoot string, paths []string) string {
	selected := paths[0]
	for _, path := range paths {
		if strings.EqualFold(filepath.Base(path), "runtime.pak") {
			selected = path
			break
		}
	}
	rel, err := filepath.Rel(repoRoot, selected)
	if err != nil {
		return filepath.ToSlash(selected)
	}
	return filepath.ToSlash(rel)
}

type pakTreeNode struct {
	name             string
	path             string
	directory        bool
	fileCount        int
	storedSize       uint64
	uncompressedSize uint64
	children         map[string]*pakTreeNode
}

func buildPakTreeRows(entries []pakbrowser.Entry) []pakTreeRowVM {
	root := &pakTreeNode{directory: true, children: make(map[string]*pakTreeNode)}
	for _, entry := range entries {
		parts := strings.Split(strings.Trim(entry.Name, "/"), "/")
		if len(parts) == 0 {
			continue
		}
		node := root
		node.fileCount++
		node.storedSize += entry.StoredSize
		node.uncompressedSize += entry.UncompressedSize
		for index, part := range parts {
			if part == "" || part == "." {
				continue
			}
			isDirectory := index < len(parts)-1
			key := part
			if !isDirectory {
				key = "\x00" + part
			}
			child := node.children[key]
			if child == nil {
				childPath := part
				if node.path != "" {
					childPath = node.path + "/" + part
				}
				child = &pakTreeNode{name: part, path: childPath, directory: isDirectory, children: make(map[string]*pakTreeNode)}
				node.children[key] = child
			}
			child.fileCount++
			child.storedSize += entry.StoredSize
			child.uncompressedSize += entry.UncompressedSize
			node = child
		}
	}

	rows := make([]pakTreeRowVM, 0, len(entries)*2)
	var appendChildren func(*pakTreeNode, int)
	appendChildren = func(parent *pakTreeNode, depth int) {
		children := make([]*pakTreeNode, 0, len(parent.children))
		for _, child := range parent.children {
			children = append(children, child)
		}
		sort.Slice(children, func(i, j int) bool {
			if children[i].directory != children[j].directory {
				return children[i].directory
			}
			return strings.ToLower(children[i].name) < strings.ToLower(children[j].name)
		})
		for _, child := range children {
			rows = append(rows, pakTreeRowVM{
				Name:             child.name,
				Path:             child.path,
				Depth:            depth,
				Directory:        child.directory,
				FileCount:        child.fileCount,
				StoredSize:       child.storedSize,
				UncompressedSize: child.uncompressedSize,
			})
			if child.directory {
				appendChildren(child, depth+1)
			}
		}
	}
	appendChildren(root, 0)
	return rows
}

func buildPakTypes(entries []pakbrowser.Entry) []pakTypeVM {
	byType := make(map[string]*pakTypeVM)
	for _, entry := range entries {
		extension := strings.ToLower(filepath.Ext(entry.Name))
		if extension == "" {
			extension = "(无扩展名)"
		}
		item := byType[extension]
		if item == nil {
			item = &pakTypeVM{Extension: extension}
			byType[extension] = item
		}
		item.FileCount++
		item.StoredSize += entry.StoredSize
		item.UncompressedSize += entry.UncompressedSize
	}
	types := make([]pakTypeVM, 0, len(byType))
	for _, item := range byType {
		types = append(types, *item)
	}
	sort.Slice(types, func(i, j int) bool {
		if types[i].StoredSize != types[j].StoredSize {
			return types[i].StoredSize > types[j].StoredSize
		}
		return types[i].Extension < types[j].Extension
	})
	return types
}
