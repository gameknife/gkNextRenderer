package dashboard

import (
	"fmt"
	"html/template"
	"net/http"
	"os"
	"path/filepath"
	"runtime"
	"strconv"
	"strings"
	"time"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/platform"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/spec"
)

func (s *Server) routes() http.Handler {
	mux := http.NewServeMux()
	mux.HandleFunc("GET /{$}", s.handleIndex)
	mux.HandleFunc("GET /todo-panel", s.handleTodoPanel)
	mux.HandleFunc("GET /task/{id}", s.handleTaskDetail)
	mux.HandleFunc("POST /task/add", s.handleTaskAdd)
	mux.HandleFunc("POST /task/{id}/done", s.handleTaskDone)
	mux.HandleFunc("POST /task/{id}/block", s.handleTaskBlock)
	mux.HandleFunc("GET /task/{id}/edit", s.handleTaskEditForm)
	mux.HandleFunc("POST /task/{id}/edit", s.handleTaskEdit)
	mux.HandleFunc("POST /task/{id}/move", s.handleTaskMove)
	mux.HandleFunc("POST /task/{id}/spec", s.handleTaskCreateSpec)
	mux.HandleFunc("POST /task/{id}/delete", s.handleTaskDelete)
	mux.HandleFunc("GET /tab/{kind}", s.handleTab)
	mux.HandleFunc("POST /jobs/{kind}", s.handleJobStart)
	mux.HandleFunc("POST /jobs/{id}/cancel", s.handleJobCancel)
	mux.HandleFunc("GET /jobs/{id}/stream", s.handleJobStream)
	return logRequests(mux)
}

func logRequests(next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		start := time.Now()
		next.ServeHTTP(w, r)
		fmt.Printf("[%s] %s %s  %s\n", time.Now().Format("15:04:05"), r.Method, r.URL.Path, time.Since(start))
	})
}

// ----- view models ----------------------------------------------------

type sectionVM struct {
	Kind    spec.SectionKind
	Heading string
	Tasks   []spec.Task
}

type indexVM struct {
	RepoRoot   string
	Milestone  string
	Status     string
	Sections   []sectionVM
	Journals   []journalSummary
	Version    string
	Preset     string
	OS         string
	RecentSize int
	ActiveTab  string // "todo" | "build" | "run" | "test"
	BuildVM    buildVM
	RunVM      runVM
	TestVM     testVM
}

type buildVM struct {
	Targets []string
	Latest  JobSnapshot
	HasJob  bool
}

type runVM struct {
	Targets []string
	Latest  JobSnapshot
	HasJob  bool
}

type testVM struct {
	Tests     []TestCase
	ListErr   string
	BinPath   string
	BinExists bool
	Latest    JobSnapshot
	HasJob    bool
}

type journalSummary struct {
	ID      int
	Title   string
	Date    string
	Type    string
	Section string
}

type taskDetailVM struct {
	Task        spec.Task
	SectionName string
	SpecBody    string
	JournalBody string
	BlockerBody string
	HasSpec     bool
	HasJournal  bool
	HasBlocker  bool
}

func (s *Server) buildIndex() (indexVM, error) {
	doc, err := spec.Parse(spec.TODOPath(s.opts.RepoRoot))
	if err != nil {
		return indexVM{}, err
	}
	vm := indexVM{
		RepoRoot:  s.opts.RepoRoot,
		Milestone: doc.Milestone,
		Status:    doc.MilestoneStatus,
		Version:   s.opts.Version,
		Preset:    s.opts.Preset,
		OS:        runtime.GOOS + "/" + runtime.GOARCH,
		ActiveTab: "todo",
	}
	for _, kind := range []spec.SectionKind{spec.SectionNext, spec.SectionBacklog, spec.SectionRecent} {
		vm.Sections = append(vm.Sections, sectionVM{
			Kind:    kind,
			Heading: kind.Heading(),
			Tasks:   doc.SectionTasks(kind),
		})
	}
	vm.RecentSize = len(doc.SectionTasks(spec.SectionRecent))
	vm.Journals = collectJournals(s.opts.RepoRoot, doc, 10)
	return vm, nil
}

// buildHeader returns a minimal indexVM with just the header fields populated.
// Tab content handlers use this as a base so each tab can be rendered without
// re-parsing TODO.md when not needed.
func (s *Server) buildHeader(activeTab string) indexVM {
	return indexVM{
		RepoRoot:  s.opts.RepoRoot,
		Version:   s.opts.Version,
		Preset:    s.opts.Preset,
		OS:        runtime.GOOS + "/" + runtime.GOARCH,
		ActiveTab: activeTab,
	}
}

func (s *Server) buildBuildVM() buildVM {
	vm := buildVM{Targets: append([]string(nil), s.opts.Config.Targets.All...)}
	if snap, ok := s.jobs.LatestSnapshot(JobBuild); ok {
		vm.Latest = snap
		vm.HasJob = true
	}
	return vm
}

func (s *Server) buildRunVM() runVM {
	vm := runVM{}
	for _, t := range s.opts.Config.Targets.All {
		if t == "gkNextUnitTests" {
			continue // tests live in the Test tab
		}
		vm.Targets = append(vm.Targets, t)
	}
	if snap, ok := s.jobs.LatestSnapshot(JobRun); ok {
		vm.Latest = snap
		vm.HasJob = true
	}
	return vm
}

func (s *Server) buildTestVM() testVM {
	binDir := platform.BinDir(s.opts.RepoRoot, s.opts.Preset)
	binPath := platform.ExecutablePath(binDir, "gkNextUnitTests")
	vm := testVM{BinPath: binPath}
	cases, err := ListCatch2Tests(binPath)
	if err != nil {
		vm.ListErr = err.Error()
	} else {
		vm.Tests = cases
		vm.BinExists = true
	}
	if snap, ok := s.jobs.LatestSnapshot(JobTest); ok {
		vm.Latest = snap
		vm.HasJob = true
	}
	return vm
}

func sectionName(s spec.SectionKind) string {
	switch s {
	case spec.SectionNext:
		return "下一步"
	case spec.SectionBacklog:
		return "待规划"
	case spec.SectionRecent:
		return "最近完成"
	}
	return "?"
}

// collectJournals walks the parsed tasks and pulls a summary entry for each
// task whose Arrow points to journal/<id>.md, capped at limit.
func collectJournals(repoRoot string, doc *spec.Document, limit int) []journalSummary {
	var out []journalSummary
	for i := len(doc.Tasks) - 1; i >= 0 && len(out) < limit; i-- {
		t := doc.Tasks[i]
		if !strings.HasPrefix(t.Arrow, "journal/") {
			continue
		}
		out = append(out, journalSummary{
			ID:      t.ID,
			Title:   t.Title,
			Date:    t.Paren,
			Type:    t.Type,
			Section: sectionName(t.Section),
		})
	}
	return out
}

// ----- handlers -------------------------------------------------------

func (s *Server) handleIndex(w http.ResponseWriter, r *http.Request) {
	vm, err := s.buildIndex()
	if err != nil {
		httpError(w, err)
		return
	}
	s.render(w, "layout.html", vm)
}

func (s *Server) handleTodoPanel(w http.ResponseWriter, r *http.Request) {
	vm, err := s.buildIndex()
	if err != nil {
		httpError(w, err)
		return
	}
	s.render(w, "todo_panel", vm)
}

func (s *Server) handleTaskDetail(w http.ResponseWriter, r *http.Request) {
	id, err := parsePathID(r)
	if err != nil {
		httpError(w, err)
		return
	}
	doc, err := spec.Parse(spec.TODOPath(s.opts.RepoRoot))
	if err != nil {
		httpError(w, err)
		return
	}
	t, _, ok := doc.FindTask(id)
	if !ok {
		http.Error(w, fmt.Sprintf("task #%05d not found", id), http.StatusNotFound)
		return
	}
	specBody, hasSpec := spec.ReadIfExists(spec.SpecPath(s.opts.RepoRoot, id))
	jBody, hasJ := spec.ReadIfExists(spec.JournalPath(s.opts.RepoRoot, id))
	bBody, hasB := spec.ReadIfExists(spec.BlockerPath(s.opts.RepoRoot, id))
	vm := taskDetailVM{
		Task:        *t,
		SectionName: sectionName(t.Section),
		SpecBody:    specBody, HasSpec: hasSpec,
		JournalBody: jBody, HasJournal: hasJ,
		BlockerBody: bBody, HasBlocker: hasB,
	}
	s.render(w, "task_detail", vm)
}

func (s *Server) handleTaskAdd(w http.ResponseWriter, r *http.Request) {
	if err := r.ParseForm(); err != nil {
		httpError(w, err)
		return
	}
	title := strings.TrimSpace(r.FormValue("title"))
	typeUp := strings.ToUpper(strings.TrimSpace(r.FormValue("type")))
	priUp := strings.ToUpper(strings.TrimSpace(r.FormValue("priority")))
	section := spec.SectionNext
	if r.FormValue("section") == "backlog" {
		section = spec.SectionBacklog
	}
	if title == "" || typeUp == "" {
		http.Error(w, "title 和 type 必填", http.StatusBadRequest)
		return
	}
	doc, err := spec.Parse(spec.TODOPath(s.opts.RepoRoot))
	if err != nil {
		httpError(w, err)
		return
	}
	if _, err := doc.AppendTask(section, spec.Task{
		Priority: priUp, Type: typeUp, Title: title,
	}); err != nil {
		httpError(w, err)
		return
	}
	if err := doc.Save(); err != nil {
		httpError(w, err)
		return
	}
	s.respondTodoPanel(w, r)
}

func (s *Server) handleTaskDone(w http.ResponseWriter, r *http.Request) {
	id, err := parsePathID(r)
	if err != nil {
		httpError(w, err)
		return
	}
	doc, err := spec.Parse(spec.TODOPath(s.opts.RepoRoot))
	if err != nil {
		httpError(w, err)
		return
	}
	date := time.Now().Format("2006-01-02")
	if err := doc.MarkStatus(id, spec.StatusDone,
		spec.WithArrow(spec.JournalRel(id)),
		spec.WithParen(date),
	); err != nil {
		httpError(w, err)
		return
	}
	if err := doc.Save(); err != nil {
		httpError(w, err)
		return
	}
	// Best-effort journal stub. Ignore os.ErrExist.
	_, _ = spec.WriteJournalStub(s.opts.RepoRoot, spec.JournalStub{TaskID: id})
	s.respondTodoPanel(w, r)
}

func (s *Server) handleTaskEditForm(w http.ResponseWriter, r *http.Request) {
	id, err := parsePathID(r)
	if err != nil {
		httpError(w, err)
		return
	}
	doc, err := spec.Parse(spec.TODOPath(s.opts.RepoRoot))
	if err != nil {
		httpError(w, err)
		return
	}
	t, _, ok := doc.FindTask(id)
	if !ok {
		http.Error(w, fmt.Sprintf("task #%05d not found", id), http.StatusNotFound)
		return
	}
	if t.Status != spec.StatusPending {
		http.Error(w, "只能编辑未启动的任务", http.StatusBadRequest)
		return
	}
	s.render(w, "task_edit_form", *t)
}

func (s *Server) handleTaskEdit(w http.ResponseWriter, r *http.Request) {
	id, err := parsePathID(r)
	if err != nil {
		httpError(w, err)
		return
	}
	if err := r.ParseForm(); err != nil {
		httpError(w, err)
		return
	}
	doc, err := spec.Parse(spec.TODOPath(s.opts.RepoRoot))
	if err != nil {
		httpError(w, err)
		return
	}
	if err := doc.EditTask(id,
		r.FormValue("title"),
		r.FormValue("type"),
		r.FormValue("priority"),
	); err != nil {
		http.Error(w, err.Error(), http.StatusBadRequest)
		return
	}
	if err := doc.Save(); err != nil {
		httpError(w, err)
		return
	}
	s.respondTodoPanel(w, r)
}

func (s *Server) handleTaskBlock(w http.ResponseWriter, r *http.Request) {
	id, err := parsePathID(r)
	if err != nil {
		httpError(w, err)
		return
	}
	reason := strings.TrimSpace(r.FormValue("reason"))
	doc, err := spec.Parse(spec.TODOPath(s.opts.RepoRoot))
	if err != nil {
		httpError(w, err)
		return
	}
	if err := doc.MarkStatus(id, spec.StatusBlocked,
		spec.WithClearArrow(),
		spec.WithParen(spec.BlockerRel(id)),
	); err != nil {
		httpError(w, err)
		return
	}
	if err := doc.Save(); err != nil {
		httpError(w, err)
		return
	}
	_, _ = spec.WriteBlockerStub(s.opts.RepoRoot, spec.BlockerStub{TaskID: id, Reason: reason})
	s.respondTodoPanel(w, r)
}

func (s *Server) handleTaskMove(w http.ResponseWriter, r *http.Request) {
	id, err := parsePathID(r)
	if err != nil {
		httpError(w, err)
		return
	}
	if err := r.ParseForm(); err != nil {
		httpError(w, err)
		return
	}
	toSection := spec.SectionUnknown
	switch strings.ToLower(strings.TrimSpace(r.FormValue("to"))) {
	case "":
		// no explicit target; resolved via anchor below
	case "next", "下一步":
		toSection = spec.SectionNext
	case "backlog", "待规划":
		toSection = spec.SectionBacklog
	default:
		http.Error(w, "to must be next or backlog", http.StatusBadRequest)
		return
	}
	beforeID, err := optionalFormID(r.FormValue("before"))
	if err != nil {
		http.Error(w, err.Error(), http.StatusBadRequest)
		return
	}
	afterID, err := optionalFormID(r.FormValue("after"))
	if err != nil {
		http.Error(w, err.Error(), http.StatusBadRequest)
		return
	}
	doc, err := spec.Parse(spec.TODOPath(s.opts.RepoRoot))
	if err != nil {
		httpError(w, err)
		return
	}
	if err := doc.MoveTask(id, spec.MovePlacement{
		ToSection: toSection,
		BeforeID:  beforeID,
		AfterID:   afterID,
	}); err != nil {
		http.Error(w, err.Error(), http.StatusBadRequest)
		return
	}
	if err := doc.Save(); err != nil {
		httpError(w, err)
		return
	}
	s.respondTodoPanel(w, r)
}

func (s *Server) handleTaskCreateSpec(w http.ResponseWriter, r *http.Request) {
	id, err := parsePathID(r)
	if err != nil {
		httpError(w, err)
		return
	}
	if err := r.ParseForm(); err != nil {
		httpError(w, err)
		return
	}
	body := r.FormValue("body")
	doc, err := spec.Parse(spec.TODOPath(s.opts.RepoRoot))
	if err != nil {
		httpError(w, err)
		return
	}
	t, _, ok := doc.FindTask(id)
	if !ok {
		http.Error(w, fmt.Sprintf("task #%05d not found", id), http.StatusNotFound)
		return
	}
	if _, err := os.Stat(spec.SpecPath(s.opts.RepoRoot, id)); err == nil {
		http.Error(w, "spec 文件已存在", http.StatusConflict)
		return
	}
	if _, err := spec.WriteSpecStub(s.opts.RepoRoot, spec.SpecStub{
		TaskID:   id,
		Title:    t.Title,
		Type:     t.Type,
		Priority: t.Priority,
		Body:     body,
	}); err != nil {
		httpError(w, err)
		return
	}
	if t.Arrow == "" {
		if err := doc.MarkStatus(id, t.Status, spec.WithArrow(spec.SpecRel(id))); err != nil {
			httpError(w, err)
			return
		}
		if err := doc.Save(); err != nil {
			httpError(w, err)
			return
		}
	}
	// Re-render the detail panel so the new spec card shows up immediately.
	r2 := r.Clone(r.Context())
	r2.SetPathValue("id", r.PathValue("id"))
	s.handleTaskDetail(w, r2)
}

func (s *Server) handleTaskDelete(w http.ResponseWriter, r *http.Request) {
	id, err := parsePathID(r)
	if err != nil {
		httpError(w, err)
		return
	}
	if err := r.ParseForm(); err != nil {
		httpError(w, err)
		return
	}
	alsoFiles := r.FormValue("also_files") == "1"
	doc, err := spec.Parse(spec.TODOPath(s.opts.RepoRoot))
	if err != nil {
		httpError(w, err)
		return
	}
	if _, err := doc.DeleteTask(id); err != nil {
		http.Error(w, err.Error(), http.StatusNotFound)
		return
	}
	if err := doc.Save(); err != nil {
		httpError(w, err)
		return
	}
	if _, err := spec.RemoveIfExists(spec.SpecPath(s.opts.RepoRoot, id)); err != nil {
		httpError(w, err)
		return
	}
	if alsoFiles {
		if _, err := spec.RemoveIfExists(spec.JournalPath(s.opts.RepoRoot, id)); err != nil {
			httpError(w, err)
			return
		}
		if _, err := spec.RemoveIfExists(spec.BlockerPath(s.opts.RepoRoot, id)); err != nil {
			httpError(w, err)
			return
		}
	}
	// Tell the page to also clear the detail panel; htmx-trigger fires the
	// `clear-detail` event in the browser which our layout.html script handles.
	w.Header().Set("HX-Trigger", "clear-detail")
	s.respondTodoPanel(w, r)
}

func optionalFormID(raw string) (int, error) {
	raw = strings.TrimSpace(raw)
	if raw == "" {
		return 0, nil
	}
	raw = strings.TrimPrefix(raw, "#")
	id, err := strconv.Atoi(raw)
	if err != nil || id <= 0 {
		return 0, fmt.Errorf("invalid id %q", raw)
	}
	return id, nil
}

func (s *Server) respondTodoPanel(w http.ResponseWriter, r *http.Request) {
	vm, err := s.buildIndex()
	if err != nil {
		httpError(w, err)
		return
	}
	s.render(w, "todo_panel", vm)
}

// ----- helpers --------------------------------------------------------

func (s *Server) render(w http.ResponseWriter, name string, data any) {
	w.Header().Set("Content-Type", "text/html; charset=utf-8")
	w.Header().Set("Cache-Control", "no-store")
	var err error
	if strings.HasSuffix(name, ".html") {
		err = s.tpl.ExecuteTemplate(w, name, data)
	} else {
		err = s.tpl.ExecuteTemplate(w, name, data)
	}
	if err != nil {
		// Headers may already be flushed; best we can do is log.
		fmt.Printf("template %s error: %v\n", name, err)
	}
}

func parsePathID(r *http.Request) (int, error) {
	raw := r.PathValue("id")
	raw = strings.TrimPrefix(raw, "#")
	id, err := strconv.Atoi(raw)
	if err != nil || id <= 0 {
		return 0, fmt.Errorf("invalid id %q", r.PathValue("id"))
	}
	return id, nil
}

func httpError(w http.ResponseWriter, err error) {
	http.Error(w, err.Error(), http.StatusInternalServerError)
}

// Keep an unused reference so go vet doesn't flag template.HTML imports left
// behind by future refactors.
var _ = template.HTML("")

// ----- tab handlers ---------------------------------------------------

// handleTab returns the inner content for one tab. Used by the left-side tab
// strip's htmx clicks; the outer layout (header + tab strip) stays put.
func (s *Server) handleTab(w http.ResponseWriter, r *http.Request) {
	kind := r.PathValue("kind")
	switch kind {
	case "todo":
		vm, err := s.buildIndex()
		if err != nil {
			httpError(w, err)
			return
		}
		s.render(w, "tab_todo", vm)
	case "build":
		vm := s.buildHeader("build")
		vm.BuildVM = s.buildBuildVM()
		s.render(w, "tab_build", vm)
	case "run":
		vm := s.buildHeader("run")
		vm.RunVM = s.buildRunVM()
		s.render(w, "tab_run", vm)
	case "test":
		vm := s.buildHeader("test")
		vm.TestVM = s.buildTestVM()
		s.render(w, "tab_test", vm)
	default:
		http.Error(w, "unknown tab "+kind, http.StatusNotFound)
	}
}

// ----- job handlers ---------------------------------------------------

func (s *Server) handleJobStart(w http.ResponseWriter, r *http.Request) {
	kind := JobKind(r.PathValue("kind"))
	if err := r.ParseForm(); err != nil {
		httpError(w, err)
		return
	}
	target := strings.TrimSpace(r.FormValue("target"))
	var spec JobSpec
	switch kind {
	case JobBuild:
		spec = s.buildJobSpec(target)
	case JobRun:
		var err error
		extraArgs := strings.Fields(r.FormValue("extraArgs"))
		spec, err = s.runJobSpec(target, extraArgs)
		if err != nil {
			http.Error(w, err.Error(), http.StatusBadRequest)
			return
		}
	case JobTest:
		var err error
		spec, err = s.testJobSpec(target)
		if err != nil {
			http.Error(w, err.Error(), http.StatusBadRequest)
			return
		}
	default:
		http.Error(w, "unknown job kind "+string(kind), http.StatusBadRequest)
		return
	}
	job, err := s.jobs.Start(spec)
	if err != nil {
		http.Error(w, err.Error(), http.StatusInternalServerError)
		return
	}
	s.render(w, "log_panel", job.snapshot())
}

func (s *Server) handleJobCancel(w http.ResponseWriter, r *http.Request) {
	id := r.PathValue("id")
	if err := s.jobs.Cancel(id); err != nil {
		http.Error(w, err.Error(), http.StatusNotFound)
		return
	}
	job, ok := s.jobs.Get(id)
	if !ok {
		http.Error(w, "job not found", http.StatusNotFound)
		return
	}
	s.render(w, "log_panel", job.snapshot())
}

// handleJobStream serves Server-Sent Events for one job. The initial buffered
// output is replayed before the live channel takes over. The connection ends
// when the job terminates or the client disconnects.
func (s *Server) handleJobStream(w http.ResponseWriter, r *http.Request) {
	id := r.PathValue("id")
	job, ok := s.jobs.Get(id)
	if !ok {
		http.Error(w, "job not found", http.StatusNotFound)
		return
	}
	from := 0
	if raw := strings.TrimSpace(r.URL.Query().Get("from")); raw != "" {
		n, err := strconv.Atoi(raw)
		if err != nil || n < 0 {
			http.Error(w, "invalid stream offset", http.StatusBadRequest)
			return
		}
		from = n
	}
	ch, snap := job.subscribe()
	defer job.unsubscribe(ch)
	if from > len(snap.Lines) {
		from = len(snap.Lines)
	}
	if snap.Status != StatusRunning && from >= len(snap.Lines) {
		w.WriteHeader(http.StatusNoContent)
		return
	}
	flusher, ok := w.(http.Flusher)
	if !ok {
		http.Error(w, "streaming unsupported", http.StatusInternalServerError)
		return
	}
	w.Header().Set("Content-Type", "text/event-stream")
	w.Header().Set("Cache-Control", "no-cache, no-transform")
	w.Header().Set("Connection", "keep-alive")
	w.Header().Set("X-Accel-Buffering", "no")
	w.WriteHeader(http.StatusOK)

	// SSE protocol: `data:` lines may not contain raw newlines. Each log
	// line is already a single HTML row, but defend against embedded \n
	// just in case (some build tools embed them in escape sequences).
	// Line events get a block wrapper so each row renders on its own
	// row in the log body (spans alone would all flow inline).
	emit := func(name, data string) {
		if name == "line" {
			data = `<div class="log-line">` + data + `</div>`
		}
		safe := strings.ReplaceAll(data, "\n", " ")
		fmt.Fprintf(w, "event: %s\ndata: %s\n\n", name, safe)
		flusher.Flush()
	}

	// Replay buffer.
	for _, line := range snap.Lines[from:] {
		emit("line", line)
	}
	if snap.Status != StatusRunning {
		emit("status", statusBadgeHTML(snap.Status, snap.ExitNote))
		emit("done", fmt.Sprintf("%d", snap.FinishedAt.Unix()))
		return
	}
	emit("status", statusBadgeHTML(snap.Status, snap.ExitNote))

	ctx := r.Context()
	for {
		select {
		case <-ctx.Done():
			return
		case ev, ok := <-ch:
			if !ok {
				return
			}
			emit(ev.Name, ev.Data)
			if ev.Name == "done" {
				return
			}
		}
	}
}

// ----- job spec builders ----------------------------------------------

func (s *Server) buildJobSpec(target string) JobSpec {
	args := []string{"--build", "--preset", s.opts.Preset}
	label := target
	if target == "" || target == "all" {
		label = "all"
	} else {
		args = append(args, "--target", target)
	}
	return JobSpec{
		Kind:    JobBuild,
		Target:  label,
		Name:    "cmake",
		Args:    args,
		WorkDir: s.opts.RepoRoot,
		Env:     []string{"CLICOLOR_FORCE=1", "FORCE_COLOR=1"},
	}
}

func (s *Server) runJobSpec(target string, extraArgs []string) (JobSpec, error) {
	if target == "" {
		return JobSpec{}, fmt.Errorf("请选择要运行的 target")
	}
	binDir := platform.BinDir(s.opts.RepoRoot, s.opts.Preset)
	exe := platform.ExecutablePath(binDir, target)
	return JobSpec{
		Kind:    JobRun,
		Target:  target,
		Name:    exe,
		Args:    extraArgs,
		WorkDir: filepath.Dir(exe),
		Env:     []string{"FORCE_COLOR=1"},
	}, nil
}

func (s *Server) testJobSpec(name string) (JobSpec, error) {
	binDir := platform.BinDir(s.opts.RepoRoot, s.opts.Preset)
	exe := platform.ExecutablePath(binDir, "gkNextUnitTests")
	args := []string{"--use-colour", "yes"}
	label := name
	if name == "" || name == "all" {
		label = "all"
	} else {
		args = append([]string{name}, args...)
	}
	return JobSpec{
		Kind:    JobTest,
		Target:  label,
		Name:    exe,
		Args:    args,
		WorkDir: filepath.Dir(exe),
		Env:     []string{"FORCE_COLOR=1"},
	}, nil
}
