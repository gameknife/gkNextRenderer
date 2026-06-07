package dashboard

import (
	"html/template"
	"net/http/httptest"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func setupDocsRepo(t *testing.T) *Server {
	t.Helper()
	dir := t.TempDir()
	for path, body := range map[string]string{
		"docs/alpha.md":            "# Alpha\n\n第一篇文档。\n",
		"docs/projects/guide.md":   "# Guide\n\n项目文档。\n",
		"docs/gallery/ignore.avif": "not-markdown",
		"docs/projects/ignore.txt": "ignore me",
		".spec/TODO.md":            "# TODO\n\n## Milestone: 测试  <!-- status: active -->\n\n### 下一步\n\n(暂无)\n\n### 待规划\n\n(暂无)\n\n### 最近完成\n\n(暂无)\n",
	} {
		full := filepath.Join(dir, filepath.FromSlash(path))
		if err := os.MkdirAll(filepath.Dir(full), 0o755); err != nil {
			t.Fatal(err)
		}
		if err := os.WriteFile(full, []byte(body), 0o644); err != nil {
			t.Fatal(err)
		}
	}
	tpl, err := template.New("dashboard").
		Funcs(templateFuncs()).
		ParseFS(templateFS, "templates/*.html")
	if err != nil {
		t.Fatal(err)
	}
	return &Server{
		opts:  Options{RepoRoot: dir},
		tpl:   tpl,
		jobs:  NewJobManager(),
		chats: NewChatStore(),
	}
}

func TestBuildDocsVMListsMarkdownFilesOnly(t *testing.T) {
	s := setupDocsRepo(t)

	vm := s.buildDocsVM("", false, "", "")

	if len(vm.Files) != 2 {
		t.Fatalf("len(files) = %d, want 2", len(vm.Files))
	}
	if vm.Files[0].RelPath != "docs/alpha.md" || vm.Files[1].RelPath != "docs/projects/guide.md" {
		t.Fatalf("files = %+v, want markdown files only", vm.Files)
	}
	if !vm.HasDoc || vm.Selected.RelPath != "docs/alpha.md" {
		t.Fatalf("selected = %+v, want docs/alpha.md", vm.Selected)
	}
	if !strings.Contains(vm.Content, "第一篇文档") {
		t.Fatalf("content = %q, want alpha markdown", vm.Content)
	}
}

func TestResolveDocMarkdownPathRejectsTraversalAndNonMarkdown(t *testing.T) {
	s := setupDocsRepo(t)

	if _, _, err := resolveDocMarkdownPath(s.opts.RepoRoot, "../AGENTS.md"); err == nil {
		t.Fatal("expected traversal path to fail")
	}
	if _, _, err := resolveDocMarkdownPath(s.opts.RepoRoot, "docs/gallery/ignore.avif"); err == nil {
		t.Fatal("expected non-markdown path to fail")
	}
	if rel, _, err := resolveDocMarkdownPath(s.opts.RepoRoot, "docs/projects/guide.md"); err != nil || rel != "docs/projects/guide.md" {
		t.Fatalf("resolve nested markdown = (%q, %v), want docs/projects/guide.md", rel, err)
	}
}

func TestHandleTabDocsRendersEditView(t *testing.T) {
	s := setupDocsRepo(t)

	req := httptest.NewRequest("GET", "/tab/docs?file=docs/projects/guide.md&edit=1", nil)
	req.SetPathValue("kind", "docs")
	rec := httptest.NewRecorder()

	s.handleTab(rec, req)

	if rec.Code != 200 {
		t.Fatalf("status = %d (%s), want 200", rec.Code, rec.Body.String())
	}
	body := rec.Body.String()
	if !strings.Contains(body, `name="path" value="docs/projects/guide.md"`) {
		t.Fatalf("rendered body missing selected path:\n%s", body)
	}
	if !strings.Contains(body, "<textarea") {
		t.Fatalf("rendered body missing textarea:\n%s", body)
	}
	if !strings.Contains(body, "项目文档") {
		t.Fatalf("rendered body missing markdown text:\n%s", body)
	}
}

func TestHandleDocsSaveWritesFileAndReturnsPreview(t *testing.T) {
	s := setupDocsRepo(t)

	form := strings.NewReader("path=docs%2Falpha.md&body=%23%23+Updated%0A%0A%E4%BF%AE%E6%94%B9%E5%90%8E")
	req := httptest.NewRequest("POST", "/docs/save", form)
	req.Header.Set("Content-Type", "application/x-www-form-urlencoded")
	rec := httptest.NewRecorder()

	s.handleDocsSave(rec, req)

	if rec.Code != 200 {
		t.Fatalf("status = %d (%s), want 200", rec.Code, rec.Body.String())
	}
	data, err := os.ReadFile(filepath.Join(s.opts.RepoRoot, "docs", "alpha.md"))
	if err != nil {
		t.Fatal(err)
	}
	if string(data) != "## Updated\n\n修改后\n" {
		t.Fatalf("saved doc = %q, want normalized markdown", string(data))
	}
	body := rec.Body.String()
	if strings.Contains(body, "<textarea") {
		t.Fatalf("save should return preview mode, got edit form:\n%s", body)
	}
	if !strings.Contains(body, "修改后") {
		t.Fatalf("preview missing saved body:\n%s", body)
	}
}
