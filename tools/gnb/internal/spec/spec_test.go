package spec

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
)

const sampleTODO = `# TODO

## Milestone: 测试  <!-- status: active -->

里程碑目标：测试。

### 下一步

- [ ] ` + "`#00018`" + ` [P0][BUG] 修复贴图采样越界
- [/] ` + "`#00019`" + ` [P1][FEAT] 体积雾 → specs/00019.md
- [!] ` + "`#00020`" + ` [SPIKE] work graphs (blockers/00020.md)

### 待规划

- [ ] ` + "`#00021`" + ` [IDEA] 试试 NRD 降噪

### 最近完成

- [x] ` + "`#00017`" + ` [BUG] 修复贴图过滤 → journal/00017.md (2026-05-13)
`

func TestParseStatuses(t *testing.T) {
	doc, err := parseBytes("TODO.md", []byte(sampleTODO))
	if err != nil {
		t.Fatalf("parse: %v", err)
	}
	if doc.Milestone != "测试" {
		t.Errorf("milestone = %q, want 测试", doc.Milestone)
	}
	if doc.MilestoneStatus != "active" {
		t.Errorf("status = %q, want active", doc.MilestoneStatus)
	}
	if got := len(doc.Tasks); got != 5 {
		t.Fatalf("tasks = %d, want 5", got)
	}
	cases := []struct {
		idx     int
		id      int
		status  Status
		pri     string
		typ     string
		title   string
		arrow   string
		paren   string
		section SectionKind
	}{
		{0, 18, StatusPending, "P0", "BUG", "修复贴图采样越界", "", "", SectionNext},
		{1, 19, StatusDoing, "P1", "FEAT", "体积雾", "specs/00019.md", "", SectionNext},
		{2, 20, StatusBlocked, "", "SPIKE", "work graphs", "", "blockers/00020.md", SectionNext},
		{3, 21, StatusPending, "", "IDEA", "试试 NRD 降噪", "", "", SectionBacklog},
		{4, 17, StatusDone, "", "BUG", "修复贴图过滤", "journal/00017.md", "2026-05-13", SectionRecent},
	}
	for _, c := range cases {
		got := doc.Tasks[c.idx]
		if got.ID != c.id || got.Status != c.status || got.Priority != c.pri ||
			got.Type != c.typ || got.Title != c.title || got.Arrow != c.arrow ||
			got.Paren != c.paren || got.Section != c.section {
			t.Errorf("task[%d] = %+v, want id=%d status=%q pri=%q typ=%q title=%q arrow=%q paren=%q section=%d",
				c.idx, got, c.id, c.status, c.pri, c.typ, c.title, c.arrow, c.paren, c.section)
		}
	}
}

func TestRoundTripFormatLine(t *testing.T) {
	doc, err := parseBytes("TODO.md", []byte(sampleTODO))
	if err != nil {
		t.Fatalf("parse: %v", err)
	}
	for _, task := range doc.Tasks {
		got := FormatLine(task)
		if got != task.Raw {
			t.Errorf("FormatLine round-trip: got %q want %q", got, task.Raw)
		}
	}
}

func TestMarkDone(t *testing.T) {
	doc, _ := parseBytes("TODO.md", []byte(sampleTODO))
	if err := doc.MarkStatus(18, StatusDone, WithArrow(JournalRel(18)), WithParen("2026-05-14")); err != nil {
		t.Fatalf("MarkStatus: %v", err)
	}
	task, _, _ := doc.FindTask(18)
	if task.Status != StatusDone {
		t.Errorf("status = %q, want x", task.Status)
	}
	want := "- [x] `#00018` [P0][BUG] 修复贴图采样越界 → journal/00018.md (2026-05-14)"
	if doc.Lines[task.LineNum-1] != want {
		t.Errorf("line = %q\nwant   %q", doc.Lines[task.LineNum-1], want)
	}
}

func TestMarkBlocked(t *testing.T) {
	doc, _ := parseBytes("TODO.md", []byte(sampleTODO))
	if err := doc.MarkStatus(18, StatusBlocked, WithParen(BlockerRel(18))); err != nil {
		t.Fatalf("MarkStatus: %v", err)
	}
	task, _, _ := doc.FindTask(18)
	want := "- [!] `#00018` [P0][BUG] 修复贴图采样越界 (blockers/00018.md)"
	if doc.Lines[task.LineNum-1] != want {
		t.Errorf("line = %q\nwant   %q", doc.Lines[task.LineNum-1], want)
	}
}

func TestAppendTaskAssignsNextID(t *testing.T) {
	doc, _ := parseBytes("TODO.md", []byte(sampleTODO))
	id, err := doc.AppendTask(SectionBacklog, Task{Type: "FEAT", Priority: "P1", Title: "测试新任务"})
	if err != nil {
		t.Fatalf("AppendTask: %v", err)
	}
	if id != 22 { // max in sample is 21
		t.Errorf("new id = %d, want 22", id)
	}
	// Verify the line ended up inside the backlog section.
	doc2, _ := parseBytes("TODO.md", []byte(strings.Join(doc.Lines, "\n")+"\n"))
	task, _, ok := doc2.FindTask(22)
	if !ok {
		t.Fatal("new task not found after re-parse")
	}
	if task.Section != SectionBacklog {
		t.Errorf("section = %d, want %d (backlog)", task.Section, SectionBacklog)
	}
}

func TestAppendReplacesPlaceholder(t *testing.T) {
	empty := `# TODO

## Milestone: 空  <!-- status: active -->

### 下一步

(暂无)

### 待规划

### 最近完成

(暂无)
`
	doc, err := parseBytes("TODO.md", []byte(empty))
	if err != nil {
		t.Fatalf("parse: %v", err)
	}
	id, err := doc.AppendTask(SectionNext, Task{Type: "BUG", Title: "首个任务"})
	if err != nil {
		t.Fatalf("AppendTask: %v", err)
	}
	if id != 1 {
		t.Errorf("first id = %d, want 1", id)
	}
	joined := strings.Join(doc.Lines, "\n")
	if strings.Contains(joined, "### 下一步\n\n(暂无)") {
		t.Errorf("placeholder still present:\n%s", joined)
	}
	if !strings.Contains(joined, "- [ ] `#00001` [BUG] 首个任务") {
		t.Errorf("new task line missing:\n%s", joined)
	}
}

func TestRemoveLines(t *testing.T) {
	doc, _ := parseBytes("TODO.md", []byte(sampleTODO))
	taskBefore, _, _ := doc.FindTask(17)
	doc.RemoveLines([]int{taskBefore.LineNum})
	if _, _, ok := doc.FindTask(17); ok {
		t.Fatal("task 17 should be removed")
	}
	// task 21's line number should have shifted up by one
	t21, _, _ := doc.FindTask(21)
	if t21.LineNum != 17 && t21.LineNum != 18 {
		// loose check: just make sure FormatLine at that line matches.
		if FormatLine(*t21) != doc.Lines[t21.LineNum-1] {
			t.Errorf("task 21 line stale: %q vs %q", FormatLine(*t21), doc.Lines[t21.LineNum-1])
		}
	}
}

func TestArchiveByKeep(t *testing.T) {
	dir := t.TempDir()
	specDir := filepath.Join(dir, ".spec")
	if err := os.MkdirAll(specDir, 0755); err != nil {
		t.Fatal(err)
	}
	todo := `# TODO

## Milestone: 测试

### 下一步

(暂无)

### 待规划

(暂无)

### 最近完成

- [x] ` + "`#00001`" + ` [BUG] 一 → journal/00001.md (2026-05-10)
- [x] ` + "`#00002`" + ` [BUG] 二 → journal/00002.md (2026-05-11)
- [x] ` + "`#00003`" + ` [BUG] 三 → journal/00003.md (2026-05-12)
- [x] ` + "`#00004`" + ` [BUG] 四 → journal/00004.md (2026-05-13)
`
	if err := os.WriteFile(filepath.Join(specDir, "TODO.md"), []byte(todo), 0644); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(specDir, "ARCHIVE.md"), []byte("# Archive\n"), 0644); err != nil {
		t.Fatal(err)
	}
	res, err := Archive(dir, ArchiveOptions{Keep: 2, Bucket: "2026-05"})
	if err != nil {
		t.Fatalf("Archive: %v", err)
	}
	if len(res.Moved) != 2 {
		t.Fatalf("moved = %d, want 2", len(res.Moved))
	}
	if res.Moved[0].ID != 1 || res.Moved[1].ID != 2 {
		t.Errorf("moved ids = %v, want [1 2]", []int{res.Moved[0].ID, res.Moved[1].ID})
	}
	// TODO.md should retain tasks 3 and 4.
	todoAfter, _ := os.ReadFile(filepath.Join(specDir, "TODO.md"))
	if strings.Contains(string(todoAfter), "`#00001`") || strings.Contains(string(todoAfter), "`#00002`") {
		t.Errorf("archived tasks still in TODO.md:\n%s", todoAfter)
	}
	if !strings.Contains(string(todoAfter), "`#00003`") || !strings.Contains(string(todoAfter), "`#00004`") {
		t.Errorf("kept tasks missing from TODO.md:\n%s", todoAfter)
	}
	// ARCHIVE.md should have a "## 2026-05" bucket with tasks 1 and 2.
	archAfter, _ := os.ReadFile(filepath.Join(specDir, "ARCHIVE.md"))
	if !strings.Contains(string(archAfter), "## 2026-05") {
		t.Errorf("bucket missing:\n%s", archAfter)
	}
	if !strings.Contains(string(archAfter), "`#00001`") || !strings.Contains(string(archAfter), "`#00002`") {
		t.Errorf("archived tasks missing from ARCHIVE.md:\n%s", archAfter)
	}
}

func TestArchiveOlderThan(t *testing.T) {
	dir := t.TempDir()
	specDir := filepath.Join(dir, ".spec")
	_ = os.MkdirAll(specDir, 0755)
	todo := `# TODO

## Milestone: x

### 下一步

(暂无)

### 待规划

(暂无)

### 最近完成

- [x] ` + "`#00001`" + ` [BUG] old → journal/00001.md (2020-01-01)
- [x] ` + "`#00002`" + ` [BUG] new → journal/00002.md (2099-01-01)
`
	_ = os.WriteFile(filepath.Join(specDir, "TODO.md"), []byte(todo), 0644)
	_ = os.WriteFile(filepath.Join(specDir, "ARCHIVE.md"), []byte("# Archive\n"), 0644)
	res, err := Archive(dir, ArchiveOptions{OlderThanDays: 7, Bucket: "2026-05"})
	if err != nil {
		t.Fatalf("Archive: %v", err)
	}
	if len(res.Moved) != 1 || res.Moved[0].ID != 1 {
		t.Errorf("moved = %+v, want [#00001]", res.Moved)
	}
}

func TestWriteJournalStub(t *testing.T) {
	dir := t.TempDir()
	path, err := WriteJournalStub(dir, JournalStub{TaskID: 42, BuildOK: true, Summary: "test", Files: []string{"a.go"}})
	if err != nil {
		t.Fatalf("WriteJournalStub: %v", err)
	}
	data, _ := os.ReadFile(path)
	if !strings.Contains(string(data), "task: 00042") {
		t.Errorf("missing task id: %s", data)
	}
	if !strings.Contains(string(data), "build_ok: true") {
		t.Errorf("missing build_ok: %s", data)
	}
	// Re-writing should refuse.
	if _, err := WriteJournalStub(dir, JournalStub{TaskID: 42}); err == nil {
		t.Error("expected error on overwrite")
	}
}
