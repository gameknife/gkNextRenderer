package dashboard

import (
	"fmt"
	"io/fs"
	"net/http"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"time"
)

type docFileVM struct {
	RelPath   string
	Name      string
	Dir       string
	UpdatedAt time.Time
	Active    bool
}

type docsVM struct {
	Files      []docFileVM
	Selected   docFileVM
	HasDoc     bool
	Editing    bool
	Error      string
	Content    string
	EditorBody string
}

func (s *Server) buildDocsVM(selectedRel string, editing bool, errText string, draftBody string) docsVM {
	files, err := listDocsMarkdownFiles(s.opts.RepoRoot)
	vm := docsVM{
		Files:   files,
		Editing: editing,
		Error:   errText,
	}
	if err != nil {
		vm.Error = joinDocsError(vm.Error, err.Error())
		return vm
	}
	if len(files) == 0 {
		return vm
	}

	selectedPath := strings.TrimSpace(selectedRel)
	if selectedPath == "" {
		selectedPath = files[0].RelPath
	}
	normalizedRel, fullPath, err := resolveDocMarkdownPath(s.opts.RepoRoot, selectedPath)
	if err != nil {
		vm.Error = joinDocsError(vm.Error, err.Error())
		normalizedRel, fullPath, err = resolveDocMarkdownPath(s.opts.RepoRoot, files[0].RelPath)
		if err != nil {
			vm.Error = joinDocsError(vm.Error, err.Error())
			return vm
		}
	}

	found := false
	for i := range vm.Files {
		if vm.Files[i].RelPath == normalizedRel {
			vm.Files[i].Active = true
			vm.Selected = vm.Files[i]
			vm.HasDoc = true
			found = true
			break
		}
	}
	if !found {
		vm.Error = joinDocsError(vm.Error, fmt.Sprintf("文档不存在：%s", normalizedRel))
		vm.Files[0].Active = true
		vm.Selected = vm.Files[0]
		vm.HasDoc = true
		normalizedRel, fullPath, err = resolveDocMarkdownPath(s.opts.RepoRoot, vm.Selected.RelPath)
		if err != nil {
			vm.Error = joinDocsError(vm.Error, err.Error())
			return vm
		}
	}

	data, err := os.ReadFile(fullPath)
	if err != nil {
		vm.Error = joinDocsError(vm.Error, err.Error())
		return vm
	}
	vm.Content = strings.ReplaceAll(string(data), "\r\n", "\n")
	if editing && draftBody != "" {
		vm.EditorBody = strings.ReplaceAll(draftBody, "\r\n", "\n")
	} else {
		vm.EditorBody = vm.Content
	}
	return vm
}

func (s *Server) handleDocsSave(w http.ResponseWriter, r *http.Request) {
	if err := r.ParseForm(); err != nil {
		httpError(w, err)
		return
	}
	selectedPath := strings.TrimSpace(r.FormValue("path"))
	body := strings.ReplaceAll(r.FormValue("body"), "\r\n", "\n")
	normalizedRel, fullPath, err := resolveDocMarkdownPath(s.opts.RepoRoot, selectedPath)
	if err != nil {
		vm := s.buildHeader("docs")
		vm.DocsVM = s.buildDocsVM(selectedPath, true, err.Error(), body)
		s.render(w, "tab_docs", vm)
		return
	}
	info, err := os.Stat(fullPath)
	if err != nil {
		vm := s.buildHeader("docs")
		vm.DocsVM = s.buildDocsVM(normalizedRel, true, err.Error(), body)
		s.render(w, "tab_docs", vm)
		return
	}
	if !info.Mode().IsRegular() {
		vm := s.buildHeader("docs")
		vm.DocsVM = s.buildDocsVM(normalizedRel, true, "只能保存普通 markdown 文件", body)
		s.render(w, "tab_docs", vm)
		return
	}
	if body != "" && !strings.HasSuffix(body, "\n") {
		body += "\n"
	}
	if err := os.WriteFile(fullPath, []byte(body), 0644); err != nil {
		vm := s.buildHeader("docs")
		vm.DocsVM = s.buildDocsVM(normalizedRel, true, err.Error(), body)
		s.render(w, "tab_docs", vm)
		return
	}
	vm := s.buildHeader("docs")
	vm.DocsVM = s.buildDocsVM(normalizedRel, false, "", "")
	s.render(w, "tab_docs", vm)
}

func listDocsMarkdownFiles(repoRoot string) ([]docFileVM, error) {
	docsRoot := filepath.Join(repoRoot, "docs")
	info, err := os.Stat(docsRoot)
	if err != nil {
		if os.IsNotExist(err) {
			return nil, nil
		}
		return nil, err
	}
	if !info.IsDir() {
		return nil, fmt.Errorf("docs 不是目录：%s", docsRoot)
	}

	var files []docFileVM
	err = filepath.WalkDir(docsRoot, func(path string, d fs.DirEntry, walkErr error) error {
		if walkErr != nil {
			return walkErr
		}
		if d.IsDir() {
			return nil
		}
		if !strings.EqualFold(filepath.Ext(d.Name()), ".md") {
			return nil
		}
		relPath, err := filepath.Rel(repoRoot, path)
		if err != nil {
			return err
		}
		fileInfo, err := d.Info()
		if err != nil {
			return err
		}
		files = append(files, docFileVM{
			RelPath:   filepath.ToSlash(relPath),
			Name:      d.Name(),
			Dir:       filepath.ToSlash(filepath.Dir(relPath)),
			UpdatedAt: fileInfo.ModTime(),
		})
		return nil
	})
	if err != nil {
		return nil, err
	}

	sort.Slice(files, func(i, j int) bool {
		return files[i].RelPath < files[j].RelPath
	})
	return files, nil
}

func resolveDocMarkdownPath(repoRoot string, rel string) (string, string, error) {
	rel = strings.TrimSpace(rel)
	if rel == "" {
		return "", "", fmt.Errorf("缺少文档路径")
	}
	full, ok := safeRepoPath(repoRoot, rel)
	if !ok {
		return "", "", fmt.Errorf("非法文档路径：%s", rel)
	}

	docsRoot, err := filepath.Abs(filepath.Join(repoRoot, "docs"))
	if err != nil {
		return "", "", err
	}
	fullAbs, err := filepath.Abs(full)
	if err != nil {
		return "", "", err
	}
	if fullAbs == docsRoot || !strings.HasPrefix(fullAbs, docsRoot+string(os.PathSeparator)) {
		return "", "", fmt.Errorf("文档路径必须位于 docs/ 目录下")
	}
	if !strings.EqualFold(filepath.Ext(fullAbs), ".md") {
		return "", "", fmt.Errorf("目前只支持编辑 docs/ 下的 markdown 文档")
	}

	normalizedRel, err := filepath.Rel(repoRoot, fullAbs)
	if err != nil {
		return "", "", err
	}
	return filepath.ToSlash(normalizedRel), fullAbs, nil
}

func joinDocsError(existing string, next string) string {
	existing = strings.TrimSpace(existing)
	next = strings.TrimSpace(next)
	switch {
	case existing == "":
		return next
	case next == "":
		return existing
	default:
		return existing + "；" + next
	}
}
