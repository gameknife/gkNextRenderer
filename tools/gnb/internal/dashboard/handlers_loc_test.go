package dashboard

import (
	"bytes"
	"html/template"
	"net/http/httptest"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/loc"
)

func TestBuildContributionGraph(t *testing.T) {
	today := time.Date(2026, time.June, 12, 15, 30, 0, 0, time.Local)
	counts := map[string]int{
		"2025-06-08": 1,
		"2026-06-11": 4,
		"2026-06-12": 8,
		"2026-06-13": 16,
	}

	graph := buildContributionGraph(counts, today)

	if len(graph.Weeks) != 53 {
		t.Fatalf("expected 53 weeks, got %d", len(graph.Weeks))
	}
	if graph.Weeks[0].Days[0].Date != "2025-06-08" {
		t.Fatalf("unexpected chart start: %s", graph.Weeks[0].Days[0].Date)
	}
	if graph.Total != 13 {
		t.Fatalf("expected 13 commits through today, got %d", graph.Total)
	}
	if graph.Max != 8 {
		t.Fatalf("expected max daily count 8, got %d", graph.Max)
	}

	var halfDay contributionDayVM
	var maxDay contributionDayVM
	var futureDay contributionDayVM
	for _, week := range graph.Weeks {
		if len(week.Days) != 7 {
			t.Fatalf("expected 7 days per week, got %d", len(week.Days))
		}
		for _, day := range week.Days {
			switch day.Date {
			case "2026-06-11":
				halfDay = day
			case "2026-06-12":
				maxDay = day
			case "2026-06-13":
				futureDay = day
			}
		}
	}

	if halfDay.Level != 2 {
		t.Fatalf("expected half-max day at level 2, got %d", halfDay.Level)
	}
	if maxDay.Level != 4 {
		t.Fatalf("expected max day at level 4, got %d", maxDay.Level)
	}
	if !futureDay.Future || futureDay.Count != 0 || futureDay.Level != 0 {
		t.Fatalf("future day should be empty, got %+v", futureDay)
	}
	if len(graph.Months) < 12 {
		t.Fatalf("expected month labels across the year, got %d", len(graph.Months))
	}
}

func TestContributionLevel(t *testing.T) {
	tests := []struct {
		count int
		max   int
		want  int
	}{
		{count: 0, max: 10, want: 0},
		{count: 1, max: 10, want: 1},
		{count: 5, max: 10, want: 2},
		{count: 10, max: 10, want: 4},
	}

	for _, test := range tests {
		if got := contributionLevel(test.count, test.max); got != test.want {
			t.Fatalf("contributionLevel(%d, %d) = %d, want %d", test.count, test.max, got, test.want)
		}
	}
}

func TestBuildHourlyCommitGraphStartsAtSixAM(t *testing.T) {
	graph := buildHourlyCommitGraph(map[int]int{
		0:  3,
		5:  1,
		6:  4,
		7:  2,
		23: 7,
	})

	if len(graph.Hours) != 24 {
		t.Fatalf("expected 24 hourly buckets, got %d", len(graph.Hours))
	}
	if graph.Hours[0].Hour != 6 || graph.Hours[0].Label != "6 AM" {
		t.Fatalf("unexpected first bucket: %+v", graph.Hours[0])
	}
	if graph.Hours[17].Hour != 23 || graph.Hours[18].Hour != 0 || graph.Hours[23].Hour != 5 {
		t.Fatalf("hour buckets should wrap at midnight from 23 to 0: %+v", graph.Hours)
	}
	if graph.Hours[0].RangeLabel != "06:00–07:00" || graph.Hours[18].RangeLabel != "00:00–01:00" {
		t.Fatalf("unexpected range labels: first=%q midnight=%q", graph.Hours[0].RangeLabel, graph.Hours[18].RangeLabel)
	}
	if graph.Total != 17 || graph.Max != 7 || graph.Mid != 4 {
		t.Fatalf("unexpected graph summary: total=%d max=%d mid=%d", graph.Total, graph.Max, graph.Mid)
	}
}

func TestTabLocRendersHourlyCommitGraph(t *testing.T) {
	graph := buildHourlyCommitGraph(map[int]int{6: 4, 23: 2, 0: 1})
	var out bytes.Buffer
	err := parseDashboardTemplates(t).ExecuteTemplate(&out, "tab_loc", indexVM{
		LocVM: locVM{
			Snapshot:      &loc.Snapshot{},
			HourlyCommits: graph,
		},
	})
	if err != nil {
		t.Fatal(err)
	}

	body := out.String()
	for _, want := range []string{
		"每日提交时段热度",
		"06:00 → 次日 06:00",
		"title=\"06:00–07:00（当地时间）：4 次提交\"",
		"title=\"00:00–01:00（当地时间）：1 次提交\"",
	} {
		if !strings.Contains(body, want) {
			t.Fatalf("hourly contribution chart missing %q:\n%s", want, body)
		}
	}
}

func TestBuildLocVMProvidesFolderDepthOptionsOnly(t *testing.T) {
	s := setupLocRepo(t)

	vm := s.buildLocVM(false, "file")

	if vm.SelectedDepth != "3" {
		t.Fatalf("SelectedDepth = %q, want fallback depth 3", vm.SelectedDepth)
	}
	if len(vm.DepthOptions) != 4 {
		t.Fatalf("len(DepthOptions) = %d, want 4", len(vm.DepthOptions))
	}
	if containsLocRow(vm.TableRows, "Engine/Runtime/Scripting/ScriptContext.cpp") {
		t.Fatalf("folder rows unexpectedly contain a file: %+v", vm.TableRows)
	}
	if !containsLocRow(vm.TableRows, "Engine/Runtime/Scripting") {
		t.Fatalf("folder rows missing Scripting: %+v", vm.TableRows)
	}
}

func TestBuildLocVMNumericDepthStillExcludesFiles(t *testing.T) {
	s := setupLocRepo(t)

	vm := s.buildLocVM(false, "4")

	if containsLocRow(vm.TableRows, "Engine/Runtime/Scripting/ScriptContext.cpp") ||
		containsLocRow(vm.TableRows, "Engine/Runtime/Reflection/Meta.cpp") {
		t.Fatalf("depth=4 should not include files: %+v", vm.TableRows)
	}
}

func TestHandleTabLocRendersFolderDepthSelector(t *testing.T) {
	s := setupLocRepo(t)
	req := httptest.NewRequest("GET", "/tab/loc?depth=file&thirdparty=1", nil)
	req.SetPathValue("kind", "loc")
	rec := httptest.NewRecorder()

	s.handleTab(rec, req)

	if rec.Code != 200 {
		t.Fatalf("status = %d (%s), want 200", rec.Code, rec.Body.String())
	}
	body := rec.Body.String()
	for _, want := range []string{
		`name="depth"`,
		`<option value="3" selected>展开 3 层</option>`,
		`name="thirdparty" value="1" checked`,
		`<th>目录</th>`,
	} {
		if !strings.Contains(body, want) {
			t.Fatalf("loc response missing %q:\n%s", want, body)
		}
	}
	if strings.Contains(body, "ScriptContext.cpp") || strings.Contains(body, `value="file"`) {
		t.Fatalf("loc response should render folders only:\n%s", body)
	}
}

func setupLocRepo(t *testing.T) *Server {
	t.Helper()
	dir := t.TempDir()
	for path, body := range map[string]string{
		"src/Engine/Runtime/Scripting/ScriptContext.cpp": "int a = 1;\nint b = 2;\n",
		"src/Engine/Runtime/Reflection/Meta.cpp":         "int c = 3;\n",
		"src/Application/Game/Flappy/main.cpp":           "int game = 1;\n",
		"src/ThirdParty/lib/foo.cpp":                     "int third = 1;\n",
		".spec/TODO.md":                                  "# TODO\n\n## Milestone: 测试  <!-- status: active -->\n\n### 下一步\n\n(暂无)\n\n### 待规划\n\n(暂无)\n\n### 最近完成\n\n(暂无)\n",
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

func containsLocRow(rows []locTableRowVM, path string) bool {
	for _, row := range rows {
		if row.Path == path {
			return true
		}
	}
	return false
}
