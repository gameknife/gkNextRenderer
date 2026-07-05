package dashboard

import (
	"fmt"
	"io/fs"
	"net/http"
	"os"
	"path/filepath"
	"sort"
	"strconv"
	"strings"
	"time"
)

const maxDocsSourceBytes = 2 << 20

type docFileVM struct {
	RelPath   string
	Name      string
	Dir       string
	UpdatedAt time.Time
	Active    bool
}

type docFolderVM struct {
	Dir    string
	Files  []docFileVM
	Active bool
}

type docsVM struct {
	Files      []docFileVM
	Folders    []docFolderVM
	Selected   docFileVM
	HasDoc     bool
	Editing    bool
	Error      string
	Content    string
	EditorBody string
}

type docsSourceLineVM struct {
	Number int
	Text   string
	Focus  bool
}

type docsSourceVM struct {
	RelPath   string
	Name      string
	Content   string
	Language  string
	Line      int
	LineCount int
	Lines     []docsSourceLineVM
	Error     string
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

	vm.Folders = groupDocsFiles(vm.Files)

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

func (s *Server) handleDocsSource(w http.ResponseWriter, r *http.Request) {
	vm := buildDocsSourceVM(s.opts.RepoRoot, r.URL.Query().Get("path"), r.URL.Query().Get("line"))
	s.render(w, "docs_source_panel", vm)
}

func buildDocsSourceVM(repoRoot string, rel string, lineText string) docsSourceVM {
	vm := docsSourceVM{}
	normalizedRel, fullPath, err := resolveRepoDocumentPath(repoRoot, rel)
	if err != nil {
		vm.Error = err.Error()
		return vm
	}
	vm.RelPath = normalizedRel
	vm.Name = filepath.Base(fullPath)

	info, err := os.Stat(fullPath)
	if err != nil {
		vm.Error = err.Error()
		return vm
	}
	if !info.Mode().IsRegular() {
		vm.Error = fmt.Sprintf("不是普通文件：%s", normalizedRel)
		return vm
	}
	if info.Size() > maxDocsSourceBytes {
		vm.Error = fmt.Sprintf("文件过大，无法预览：%s", normalizedRel)
		return vm
	}

	data, err := os.ReadFile(fullPath)
	if err != nil {
		vm.Error = err.Error()
		return vm
	}
	if isProbablyBinary(data) {
		vm.Error = fmt.Sprintf("二进制文件无法预览：%s", normalizedRel)
		return vm
	}

	content := strings.ReplaceAll(string(data), "\r\n", "\n")
	content = strings.TrimSuffix(content, "\n")
	vm.Content = content
	vm.Language = docsSourceLanguage(normalizedRel)
	lines := strings.Split(content, "\n")
	if len(lines) == 1 && lines[0] == "" {
		lines = nil
	}
	vm.LineCount = len(lines)
	vm.Line = parseDocsSourceLine(lineText, vm.LineCount)
	vm.Lines = make([]docsSourceLineVM, 0, len(lines))
	for i, text := range lines {
		number := i + 1
		vm.Lines = append(vm.Lines, docsSourceLineVM{
			Number: number,
			Text:   text,
			Focus:  number == vm.Line,
		})
	}
	return vm
}

func docsSourceLanguage(rel string) string {
	name := strings.ToLower(filepath.Base(rel))
	switch name {
	case "cmakelists.txt":
		return "cmake"
	case "dockerfile":
		return "dockerfile"
	case "makefile":
		return "makefile"
	}

	switch strings.ToLower(filepath.Ext(name)) {
	case ".c":
		return "c"
	case ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx", ".inl", ".slang", ".hlsl":
		return "cpp"
	case ".cmake":
		return "cmake"
	case ".vert", ".frag", ".geom", ".tesc", ".tese", ".glsl":
		return "glsl"
	case ".scad":
		return "openscad"
	case ".go":
		return "go"
	case ".js", ".mjs", ".cjs":
		return "javascript"
	case ".ts", ".mts", ".cts":
		return "typescript"
	case ".json":
		return "json"
	case ".html", ".htm", ".xml", ".svg":
		return "xml"
	case ".css":
		return "css"
	case ".sh", ".bash", ".zsh":
		return "bash"
	case ".ps1", ".psm1", ".psd1":
		return "powershell"
	case ".bat", ".cmd":
		return "dos"
	case ".py":
		return "python"
	case ".rs":
		return "rust"
	case ".java":
		return "java"
	case ".kt", ".kts":
		return "kotlin"
	case ".swift":
		return "swift"
	case ".lua":
		return "lua"
	case ".sql":
		return "sql"
	case ".yaml", ".yml":
		return "yaml"
	case ".toml", ".ini":
		return "ini"
	case ".md":
		return "markdown"
	case ".diff", ".patch":
		return "diff"
	case ".proto":
		return "protobuf"
	case ".graphql", ".gql":
		return "graphql"
	}
	return ""
}

func parseDocsSourceLine(raw string, lineCount int) int {
	line, err := strconv.Atoi(strings.TrimSpace(raw))
	if err != nil || line < 1 || lineCount < 1 {
		return 0
	}
	if line > lineCount {
		return lineCount
	}
	return line
}

func resolveRepoDocumentPath(repoRoot string, rel string) (string, string, error) {
	rel = strings.TrimSpace(strings.TrimPrefix(rel, "/"))
	if rel == "" {
		return "", "", fmt.Errorf("缺少文件路径")
	}
	full, ok := safeRepoPath(repoRoot, rel)
	if !ok {
		return "", "", fmt.Errorf("非法文件路径：%s", rel)
	}

	rootAbs, err := filepath.Abs(repoRoot)
	if err != nil {
		return "", "", err
	}
	fullAbs, err := filepath.Abs(full)
	if err != nil {
		return "", "", err
	}
	rootReal, err := filepath.EvalSymlinks(rootAbs)
	if err != nil {
		return "", "", err
	}
	fullReal, err := filepath.EvalSymlinks(fullAbs)
	if err != nil {
		return "", "", err
	}
	if fullReal != rootReal && !strings.HasPrefix(fullReal, rootReal+string(os.PathSeparator)) {
		return "", "", fmt.Errorf("文件路径超出仓库目录")
	}

	normalizedRel, err := filepath.Rel(rootAbs, fullAbs)
	if err != nil {
		return "", "", err
	}
	return filepath.ToSlash(normalizedRel), fullAbs, nil
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
		leftDir := strings.ToLower(files[i].Dir)
		rightDir := strings.ToLower(files[j].Dir)
		if leftDir != rightDir {
			return leftDir < rightDir
		}
		leftName := strings.ToLower(files[i].Name)
		rightName := strings.ToLower(files[j].Name)
		if leftName != rightName {
			return leftName < rightName
		}
		return files[i].RelPath < files[j].RelPath
	})
	return files, nil
}

func groupDocsFiles(files []docFileVM) []docFolderVM {
	folders := make([]docFolderVM, 0)
	for _, file := range files {
		if len(folders) == 0 || folders[len(folders)-1].Dir != file.Dir {
			folders = append(folders, docFolderVM{Dir: file.Dir})
		}
		folder := &folders[len(folders)-1]
		folder.Files = append(folder.Files, file)
		folder.Active = folder.Active || file.Active
	}
	return folders
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
