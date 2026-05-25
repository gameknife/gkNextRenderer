package dashboard

import (
	"html/template"
	"net/http/httptest"
	"net/url"
	"os"
	"path/filepath"
	"strings"
	"testing"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/spec"
)

// setupTODORepo creates a temp dir with a .spec/TODO.md and returns a Server
// rooted there. Both the move and create-spec handlers operate on this layout.
func setupTODORepo(t *testing.T) *Server {
	t.Helper()
	dir := t.TempDir()
	if err := os.MkdirAll(filepath.Join(dir, ".spec"), 0755); err != nil {
		t.Fatal(err)
	}
	todo := `# TODO

## Milestone: 测试  <!-- status: active -->

### 下一步

- [ ] ` + "`#00001`" + ` [FEAT] 任务A
- [ ] ` + "`#00002`" + ` [BUG] 任务B

### 待规划

- [ ] ` + "`#00003`" + ` [IDEA] 任务C

### 最近完成

(暂无)
`
	if err := os.WriteFile(filepath.Join(dir, ".spec", "TODO.md"), []byte(todo), 0644); err != nil {
		t.Fatal(err)
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

func TestHandleTaskMoveBeforeAnchorReordersPanel(t *testing.T) {
	s := setupTODORepo(t)

	form := url.Values{}
	form.Set("before", "00001")
	req := httptest.NewRequest("POST", "/task/00003/move", strings.NewReader(form.Encode()))
	req.Header.Set("Content-Type", "application/x-www-form-urlencoded")
	req.SetPathValue("id", "00003")
	rec := httptest.NewRecorder()

	s.handleTaskMove(rec, req)

	if rec.Code != 200 {
		t.Fatalf("status = %d (%s), want 200", rec.Code, rec.Body.String())
	}
	doc, err := spec.Parse(spec.TODOPath(s.opts.RepoRoot))
	if err != nil {
		t.Fatal(err)
	}
	next := doc.SectionTasks(spec.SectionNext)
	if len(next) < 2 || next[0].ID != 3 || next[1].ID != 1 {
		t.Fatalf("next IDs after move = %+v, want [3, 1, …]", next)
	}
	if len(doc.SectionTasks(spec.SectionBacklog)) != 0 {
		t.Fatalf("backlog should be empty after moving 00003 out")
	}
}

func TestHandleTaskMoveToBacklogPopulatesSection(t *testing.T) {
	s := setupTODORepo(t)

	form := url.Values{}
	form.Set("to", "backlog")
	req := httptest.NewRequest("POST", "/task/00001/move", strings.NewReader(form.Encode()))
	req.Header.Set("Content-Type", "application/x-www-form-urlencoded")
	req.SetPathValue("id", "00001")
	rec := httptest.NewRecorder()

	s.handleTaskMove(rec, req)

	if rec.Code != 200 {
		t.Fatalf("status = %d (%s), want 200", rec.Code, rec.Body.String())
	}
	doc, _ := spec.Parse(spec.TODOPath(s.opts.RepoRoot))
	t1, _, ok := doc.FindTask(1)
	if !ok || t1.Section != spec.SectionBacklog {
		t.Fatalf("task 1 section = %v, want backlog", t1.Section)
	}
}

func TestHandleTaskCreateSpecWritesFileAndLinksArrow(t *testing.T) {
	s := setupTODORepo(t)

	form := url.Values{}
	form.Set("body", "## 背景\n\n来自 dashboard 的 spec。")
	req := httptest.NewRequest("POST", "/task/00001/spec", strings.NewReader(form.Encode()))
	req.Header.Set("Content-Type", "application/x-www-form-urlencoded")
	req.SetPathValue("id", "00001")
	rec := httptest.NewRecorder()

	s.handleTaskCreateSpec(rec, req)

	if rec.Code != 200 {
		t.Fatalf("status = %d (%s), want 200", rec.Code, rec.Body.String())
	}
	specPath := spec.SpecPath(s.opts.RepoRoot, 1)
	data, err := os.ReadFile(specPath)
	if err != nil {
		t.Fatalf("expected spec file at %s: %v", specPath, err)
	}
	if !strings.Contains(string(data), "来自 dashboard 的 spec") {
		t.Fatalf("spec body not written:\n%s", data)
	}
	doc, _ := spec.Parse(spec.TODOPath(s.opts.RepoRoot))
	t1, _, _ := doc.FindTask(1)
	if t1.Arrow != "specs/00001.md" {
		t.Fatalf("arrow = %q, want specs/00001.md", t1.Arrow)
	}
}

func TestHandleTaskDeleteRemovesTaskAndSpec(t *testing.T) {
	s := setupTODORepo(t)
	if _, err := spec.WriteSpecStub(s.opts.RepoRoot, spec.SpecStub{TaskID: 1, Title: "需要删"}); err != nil {
		t.Fatal(err)
	}

	req := httptest.NewRequest("POST", "/task/00001/delete", strings.NewReader(""))
	req.Header.Set("Content-Type", "application/x-www-form-urlencoded")
	req.SetPathValue("id", "00001")
	rec := httptest.NewRecorder()

	s.handleTaskDelete(rec, req)

	if rec.Code != 200 {
		t.Fatalf("status = %d (%s), want 200", rec.Code, rec.Body.String())
	}
	if got := rec.Header().Get("HX-Trigger"); got != "clear-detail" {
		t.Errorf("HX-Trigger = %q, want clear-detail", got)
	}
	doc, _ := spec.Parse(spec.TODOPath(s.opts.RepoRoot))
	if _, _, ok := doc.FindTask(1); ok {
		t.Fatal("task 1 still present after delete")
	}
	if _, err := os.Stat(spec.SpecPath(s.opts.RepoRoot, 1)); !os.IsNotExist(err) {
		t.Fatalf("spec file should be removed, err = %v", err)
	}
}

func TestHandleTaskDeleteUnknownID(t *testing.T) {
	s := setupTODORepo(t)

	req := httptest.NewRequest("POST", "/task/09999/delete", strings.NewReader(""))
	req.Header.Set("Content-Type", "application/x-www-form-urlencoded")
	req.SetPathValue("id", "09999")
	rec := httptest.NewRecorder()

	s.handleTaskDelete(rec, req)

	if rec.Code != 404 {
		t.Fatalf("status = %d, want 404", rec.Code)
	}
}

func TestHandleTaskCreateSpecRejectsDuplicate(t *testing.T) {
	s := setupTODORepo(t)
	if _, err := spec.WriteSpecStub(s.opts.RepoRoot, spec.SpecStub{TaskID: 1, Title: "已存在"}); err != nil {
		t.Fatal(err)
	}

	form := url.Values{}
	form.Set("body", "...")
	req := httptest.NewRequest("POST", "/task/00001/spec", strings.NewReader(form.Encode()))
	req.Header.Set("Content-Type", "application/x-www-form-urlencoded")
	req.SetPathValue("id", "00001")
	rec := httptest.NewRecorder()

	s.handleTaskCreateSpec(rec, req)

	if rec.Code != 409 {
		t.Fatalf("status = %d, want 409 conflict", rec.Code)
	}
}
