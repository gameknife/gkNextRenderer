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
func setupTODORepoWithContent(t *testing.T, todo string) *Server {
	t.Helper()
	dir := t.TempDir()
	if err := os.MkdirAll(filepath.Join(dir, ".spec"), 0755); err != nil {
		t.Fatal(err)
	}
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

func setupTODORepo(t *testing.T) *Server {
	t.Helper()
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
	return setupTODORepoWithContent(t, todo)
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

func TestHandleTodoCleanupMovesDoneTasksAndCapsRecentDisplay(t *testing.T) {
	todo := `# TODO

## Milestone: 测试  <!-- status: active -->

### 下一步

- [x] ` + "`#00001`" + ` [BUG] 刚做完 → journal/00001.md (2026-05-20)
- [ ] ` + "`#00002`" + ` [FEAT] 还没做

### 待规划

(暂无)

### 最近完成

- [x] ` + "`#00003`" + ` [BUG] 任务3 → journal/00003.md (2026-05-03)
- [x] ` + "`#00004`" + ` [BUG] 任务4 → journal/00004.md (2026-05-04)
- [x] ` + "`#00005`" + ` [BUG] 任务5 → journal/00005.md (2026-05-05)
- [x] ` + "`#00006`" + ` [BUG] 任务6 → journal/00006.md (2026-05-06)
- [x] ` + "`#00007`" + ` [BUG] 任务7 → journal/00007.md (2026-05-07)
- [x] ` + "`#00008`" + ` [BUG] 任务8 → journal/00008.md (2026-05-08)
- [x] ` + "`#00009`" + ` [BUG] 任务9 → journal/00009.md (2026-05-09)
- [x] ` + "`#00010`" + ` [BUG] 任务10 → journal/00010.md (2026-05-10)
- [x] ` + "`#00011`" + ` [BUG] 任务11 → journal/00011.md (2026-05-11)
- [x] ` + "`#00012`" + ` [BUG] 任务12 → journal/00012.md (2026-05-12)
- [x] ` + "`#00013`" + ` [BUG] 任务13 → journal/00013.md (2026-05-13)
- [x] ` + "`#00014`" + ` [BUG] 任务14 → journal/00014.md (2026-05-14)
`
	s := setupTODORepoWithContent(t, todo)

	req := httptest.NewRequest("POST", "/todo/cleanup", strings.NewReader(""))
	req.Header.Set("Content-Type", "application/x-www-form-urlencoded")
	rec := httptest.NewRecorder()

	s.handleTodoCleanup(rec, req)

	if rec.Code != 200 {
		t.Fatalf("status = %d (%s), want 200", rec.Code, rec.Body.String())
	}
	doc, err := spec.Parse(spec.TODOPath(s.opts.RepoRoot))
	if err != nil {
		t.Fatal(err)
	}
	t1, _, ok := doc.FindTask(1)
	if !ok || t1.Section != spec.SectionRecent {
		t.Fatalf("task 1 section = %v, want recent", t1.Section)
	}
	next := doc.SectionTasks(spec.SectionNext)
	if len(next) != 1 || next[0].ID != 2 {
		t.Fatalf("next tasks = %+v, want only #00002", next)
	}

	body := rec.Body.String()
	if !strings.Contains(body, `hx-post="/todo/cleanup"`) {
		t.Fatalf("cleanup button missing from panel:\n%s", body)
	}
	for _, hidden := range []string{"data-id=\"00003\"", "data-id=\"00004\"", "data-id=\"00005\""} {
		if strings.Contains(body, hidden) {
			t.Fatalf("panel should hide older recent task %s:\n%s", hidden, body)
		}
	}
	for _, visible := range []string{"data-id=\"00001\"", "data-id=\"00014\""} {
		if !strings.Contains(body, visible) {
			t.Fatalf("panel missing recent task %s:\n%s", visible, body)
		}
	}
}

func TestHandleTaskDoneAutoMovesTaskToRecent(t *testing.T) {
	s := setupTODORepo(t)

	req := httptest.NewRequest("POST", "/task/00001/done", strings.NewReader(""))
	req.Header.Set("Content-Type", "application/x-www-form-urlencoded")
	req.SetPathValue("id", "00001")
	rec := httptest.NewRecorder()

	s.handleTaskDone(rec, req)

	if rec.Code != 200 {
		t.Fatalf("status = %d (%s), want 200", rec.Code, rec.Body.String())
	}
	doc, err := spec.Parse(spec.TODOPath(s.opts.RepoRoot))
	if err != nil {
		t.Fatal(err)
	}
	t1, _, ok := doc.FindTask(1)
	if !ok {
		t.Fatal("task 1 missing after mark done")
	}
	if t1.Section != spec.SectionRecent {
		t.Fatalf("task 1 section = %v, want recent", t1.Section)
	}
	if t1.Status != spec.StatusDone {
		t.Fatalf("task 1 status = %q, want done", t1.Status)
	}
	next := doc.SectionTasks(spec.SectionNext)
	if len(next) != 1 || next[0].ID != 2 {
		t.Fatalf("next tasks = %+v, want only #00002", next)
	}
}

func TestHandleTaskSpecEditFormRendersTextarea(t *testing.T) {
	s := setupTODORepo(t)
	if _, err := spec.WriteSpecStub(s.opts.RepoRoot, spec.SpecStub{
		TaskID: 1,
		Title:  "可编辑 spec",
		Body:   "## 背景\n\n旧内容",
	}); err != nil {
		t.Fatal(err)
	}

	req := httptest.NewRequest("GET", "/task/00001/spec/edit", nil)
	req.SetPathValue("id", "00001")
	rec := httptest.NewRecorder()

	s.handleTaskSpecEditForm(rec, req)

	if rec.Code != 200 {
		t.Fatalf("status = %d (%s), want 200", rec.Code, rec.Body.String())
	}
	body := rec.Body.String()
	if !strings.Contains(body, `hx-post="/task/00001/spec/save"`) {
		t.Fatalf("edit form missing save action:\n%s", body)
	}
	if !strings.Contains(body, "<textarea") {
		t.Fatalf("edit form missing textarea:\n%s", body)
	}
}

func TestHandleTaskSpecSaveWritesFile(t *testing.T) {
	s := setupTODORepo(t)
	if _, err := spec.WriteSpecStub(s.opts.RepoRoot, spec.SpecStub{
		TaskID: 1,
		Title:  "可编辑 spec",
		Body:   "旧内容",
	}); err != nil {
		t.Fatal(err)
	}

	form := url.Values{}
	form.Set("body", "## 背景\r\n\r\n新内容")
	req := httptest.NewRequest("POST", "/task/00001/spec/save", strings.NewReader(form.Encode()))
	req.Header.Set("Content-Type", "application/x-www-form-urlencoded")
	req.SetPathValue("id", "00001")
	rec := httptest.NewRecorder()

	s.handleTaskSpecSave(rec, req)

	if rec.Code != 200 {
		t.Fatalf("status = %d (%s), want 200", rec.Code, rec.Body.String())
	}
	data, err := os.ReadFile(spec.SpecPath(s.opts.RepoRoot, 1))
	if err != nil {
		t.Fatal(err)
	}
	if string(data) != "## 背景\n\n新内容\n" {
		t.Fatalf("saved spec = %q, want normalized markdown", string(data))
	}
	if !strings.Contains(rec.Body.String(), "新内容") {
		t.Fatalf("response did not re-render updated spec:\n%s", rec.Body.String())
	}
}
