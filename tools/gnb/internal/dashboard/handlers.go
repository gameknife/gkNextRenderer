package dashboard

import (
	"context"
	"fmt"
	"html"
	"html/template"
	"net/http"
	"os"
	"path/filepath"
	"runtime"
	"strconv"
	"strings"
	"time"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/gitops"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/llm"
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
	mux.HandleFunc("POST /git/switch", s.handleGitSwitch)
	mux.HandleFunc("POST /git/switch-remote", s.handleGitSwitchRemote)
	mux.HandleFunc("POST /git/create-branch", s.handleGitCreateBranch)
	mux.HandleFunc("POST /git/pull", s.handleGitPull)
	mux.HandleFunc("POST /git/fetch", s.handleGitFetch)
	mux.HandleFunc("POST /git/reset", s.handleGitReset)
	mux.HandleFunc("GET /git/local-changes", s.handleGitLocalChanges)
	mux.HandleFunc("POST /git/stage", s.handleGitStage)
	mux.HandleFunc("POST /git/unstage", s.handleGitUnstage)
	mux.HandleFunc("POST /git/stash/push", s.handleGitStashPush)
	mux.HandleFunc("POST /git/stash/{action}", s.handleGitStashAction)
	mux.HandleFunc("GET /git/commit/{ref}", s.handleGitCommit)
	mux.HandleFunc("GET /git/panel", s.handleGitPanel)
	mux.HandleFunc("POST /git/commit-message", s.handleGitCommitMessage)
	mux.HandleFunc("POST /git/commit", s.handleGitCommitCreate)
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
	ActiveTab  string // "todo" | "build" | "run" | "test" | "git"
	BuildVM    buildVM
	RunVM      runVM
	TestVM     testVM
	GitVM      gitVM
}

type gitVM struct {
	Status         gitops.Status
	Branches       []gitops.Branch
	RemoteBranches []gitops.RemoteBranch
	RemoteCommits  []gitops.Commit
	Commits        []gitops.Commit
	Stashes        []gitops.Stash
	Error          string
	Flash          string // success / info message after an action
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
	case "git":
		vm := s.buildHeader("git")
		vm.GitVM = s.buildGitVM("")
		s.render(w, "tab_git", vm)
	default:
		http.Error(w, "unknown tab "+kind, http.StatusNotFound)
	}
}

// ----- git handlers ---------------------------------------------------

func (s *Server) buildGitVM(flash string) gitVM {
	vm := gitVM{Flash: flash}
	st, err := gitops.GetStatus(s.opts.RepoRoot)
	if err != nil {
		vm.Error = err.Error()
		return vm
	}
	vm.Status = st
	if branches, err := gitops.Branches(s.opts.RepoRoot); err != nil {
		vm.Error = err.Error()
	} else {
		vm.Branches = branches
	}
	if remotes, err := gitops.RemoteBranches(s.opts.RepoRoot); err != nil {
		if vm.Error == "" {
			vm.Error = err.Error()
		}
	} else {
		vm.RemoteBranches = remotes
	}
	if commits, err := gitops.Log(s.opts.RepoRoot, 30); err != nil {
		if vm.Error == "" {
			vm.Error = err.Error()
		}
	} else {
		vm.Commits = commits
	}
	if st.Upstream != "" {
		if commits, err := gitops.LogRange(s.opts.RepoRoot, "HEAD.."+st.Upstream, 12); err != nil {
			if vm.Error == "" {
				vm.Error = err.Error()
			}
		} else {
			vm.RemoteCommits = commits
		}
	}
	if stashes, err := gitops.StashList(s.opts.RepoRoot); err != nil {
		if vm.Error == "" {
			vm.Error = err.Error()
		}
	} else {
		vm.Stashes = stashes
	}
	return vm
}

// renderGitBody re-renders the whole git tab body (left column + commits +
// stash). All destructive actions go through this so both columns stay in
// sync (e.g. reset moves HEAD which changes the log).
func (s *Server) renderGitBody(w http.ResponseWriter, flash string) {
	vm := s.buildHeader("git")
	vm.GitVM = s.buildGitVM(flash)
	s.render(w, "git_body", vm)
}

func (s *Server) handleGitPanel(w http.ResponseWriter, r *http.Request) {
	s.renderGitBody(w, r.URL.Query().Get("flash"))
}

func (s *Server) handleGitSwitch(w http.ResponseWriter, r *http.Request) {
	if err := r.ParseForm(); err != nil {
		httpError(w, err)
		return
	}
	branch := strings.TrimSpace(r.FormValue("branch"))
	if branch == "" {
		http.Error(w, "branch is required", http.StatusBadRequest)
		return
	}
	if err := gitops.Checkout(s.opts.RepoRoot, branch, false); err != nil {
		s.renderGitBody(w, "切换分支失败: "+err.Error())
		return
	}
	s.renderGitBody(w, "已切换到 "+branch)
}

func (s *Server) handleGitPull(w http.ResponseWriter, r *http.Request) {
	out, err := gitops.Pull(s.opts.RepoRoot)
	if err != nil {
		s.renderGitBody(w, "Pull 失败: "+err.Error())
		return
	}
	flash := "Pull 完成"
	if first := firstLine(out); first != "" {
		flash = flash + " (" + first + ")"
	}
	s.renderGitBody(w, flash)
}

func (s *Server) handleGitFetch(w http.ResponseWriter, r *http.Request) {
	if _, err := gitops.Fetch(s.opts.RepoRoot); err != nil {
		s.renderGitBody(w, "Fetch 失败: "+err.Error())
		return
	}
	s.renderGitBody(w, "Fetch 完成")
}

func (s *Server) handleGitSwitchRemote(w http.ResponseWriter, r *http.Request) {
	if err := r.ParseForm(); err != nil {
		httpError(w, err)
		return
	}
	ref := strings.TrimSpace(r.FormValue("ref"))
	if ref == "" {
		http.Error(w, "ref required", http.StatusBadRequest)
		return
	}
	if err := gitops.CheckoutRemote(s.opts.RepoRoot, ref, false); err != nil {
		s.renderGitBody(w, "切换远程分支失败: "+err.Error())
		return
	}
	s.renderGitBody(w, "已基于 "+ref+" 创建本地跟踪分支")
}

func (s *Server) handleGitCreateBranch(w http.ResponseWriter, r *http.Request) {
	if err := r.ParseForm(); err != nil {
		httpError(w, err)
		return
	}
	name := strings.TrimSpace(r.FormValue("name"))
	startPoint := strings.TrimSpace(r.FormValue("start"))
	if name == "" {
		http.Error(w, "name required", http.StatusBadRequest)
		return
	}
	if err := gitops.CreateBranch(s.opts.RepoRoot, name, startPoint, false); err != nil {
		s.renderGitBody(w, "创建分支失败: "+err.Error())
		return
	}
	s.renderGitBody(w, "已创建并切换到 "+name)
}

func (s *Server) handleGitReset(w http.ResponseWriter, r *http.Request) {
	if err := r.ParseForm(); err != nil {
		httpError(w, err)
		return
	}
	ref := strings.TrimSpace(r.FormValue("ref"))
	if ref == "" {
		http.Error(w, "ref required", http.StatusBadRequest)
		return
	}
	if err := gitops.ResetHard(s.opts.RepoRoot, ref); err != nil {
		s.renderGitBody(w, "Reset 失败: "+err.Error())
		return
	}
	s.renderGitBody(w, "已 reset --hard 到 "+ref)
}

func (s *Server) handleGitStashPush(w http.ResponseWriter, r *http.Request) {
	if err := r.ParseForm(); err != nil {
		httpError(w, err)
		return
	}
	msg := strings.TrimSpace(r.FormValue("message"))
	includeUntracked := r.FormValue("untracked") == "1"
	out, err := gitops.StashPush(s.opts.RepoRoot, msg, includeUntracked)
	if err != nil {
		s.renderGitBody(w, "Stash 失败: "+err.Error())
		return
	}
	flash := "已 stash"
	if first := firstLine(out); first != "" {
		flash = flash + " (" + first + ")"
	}
	s.renderGitBody(w, flash)
}

func (s *Server) handleGitStashAction(w http.ResponseWriter, r *http.Request) {
	action := r.PathValue("action")
	if err := r.ParseForm(); err != nil {
		httpError(w, err)
		return
	}
	ref := strings.TrimSpace(r.FormValue("ref"))
	out, err := gitops.StashAction(s.opts.RepoRoot, action, ref)
	if err != nil {
		s.renderGitBody(w, "stash "+action+" 失败: "+err.Error())
		return
	}
	flash := "stash " + action + " 完成"
	if first := firstLine(out); first != "" {
		flash = flash + " (" + first + ")"
	}
	s.renderGitBody(w, flash)
}

func (s *Server) handleGitCommit(w http.ResponseWriter, r *http.Request) {
	ref := r.PathValue("ref")
	if ref == "" {
		http.Error(w, "ref required", http.StatusBadRequest)
		return
	}
	c, err := gitops.ShowCommit(s.opts.RepoRoot, ref)
	if err != nil {
		http.Error(w, err.Error(), http.StatusNotFound)
		return
	}
	s.render(w, "git_commit_detail", c)
}

func (s *Server) handleGitLocalChanges(w http.ResponseWriter, r *http.Request) {
	vm := s.buildHeader("git")
	vm.GitVM = s.buildGitVM(r.URL.Query().Get("flash"))
	s.render(w, "git_commit_card", vm)
}

func (s *Server) handleGitStage(w http.ResponseWriter, r *http.Request) {
	if err := r.ParseForm(); err != nil {
		httpError(w, err)
		return
	}
	path := strings.TrimSpace(r.FormValue("path"))
	if path == "" {
		if err := gitops.AddAll(s.opts.RepoRoot); err != nil {
			s.renderGitBody(w, "Stage 失败: "+err.Error())
			return
		}
		s.renderGitBody(w, "已 stage 全部改动")
		return
	}
	if err := gitops.AddPath(s.opts.RepoRoot, path); err != nil {
		s.renderGitBody(w, "Stage 失败: "+err.Error())
		return
	}
	s.renderGitBody(w, "已 stage: "+path)
}

func (s *Server) handleGitUnstage(w http.ResponseWriter, r *http.Request) {
	if err := r.ParseForm(); err != nil {
		httpError(w, err)
		return
	}
	path := strings.TrimSpace(r.FormValue("path"))
	if path == "" {
		if err := gitops.UnstageAll(s.opts.RepoRoot); err != nil {
			s.renderGitBody(w, "Unstage 失败: "+err.Error())
			return
		}
		s.renderGitBody(w, "已 unstage 全部改动")
		return
	}
	if err := gitops.UnstagePath(s.opts.RepoRoot, path); err != nil {
		s.renderGitBody(w, "Unstage 失败: "+err.Error())
		return
	}
	s.renderGitBody(w, "已 unstage: "+path)
}

func firstLine(s string) string {
	if i := strings.IndexByte(s, '\n'); i >= 0 {
		return s[:i]
	}
	return s
}

// handleGitCommitMessage runs the LLM commit-message generator and returns
// an HTMX fragment that replaces the textarea inside the commit card.
//
// The endpoint takes the LLM round-trip in the request goroutine (can take
// 30-60s on first call while the model loads). HTMX's hx-indicator handles
// the spinner; the client times out at 5 min just like the CLI.
func (s *Server) handleGitCommitMessage(w http.ResponseWriter, r *http.Request) {
	ctx, cancel := context.WithTimeout(r.Context(), 5*time.Minute)
	defer cancel()
	res, err := llm.GenerateCommitMessage(ctx, s.opts.RepoRoot, s.opts.Config.External.LLM, 16000, 0.2)
	if err != nil {
		// Surface error inside the textarea so the user can see what went wrong
		// without breaking the rest of the page.
		writeCommitTextarea(w, "[LLM error] "+err.Error())
		return
	}
	writeCommitTextarea(w, res.Message)
}

// handleGitCommitCreate stages all (optional) and commits with the provided
// message. Re-renders the whole git body so the recent-commits list and dirty
// state reflect the new HEAD.
func (s *Server) handleGitCommitCreate(w http.ResponseWriter, r *http.Request) {
	if err := r.ParseForm(); err != nil {
		httpError(w, err)
		return
	}
	message := strings.TrimSpace(r.FormValue("message"))
	if message == "" {
		s.renderGitBody(w, "Commit 失败: 提交消息为空")
		return
	}
	if r.FormValue("stage_all") == "1" {
		if err := gitops.AddAll(s.opts.RepoRoot); err != nil {
			s.renderGitBody(w, "git add -A 失败: "+err.Error())
			return
		}
	}
	if _, err := gitops.CreateCommit(s.opts.RepoRoot, message); err != nil {
		s.renderGitBody(w, "Commit 失败: "+err.Error())
		return
	}
	subject := firstLine(message)
	if len(subject) > 60 {
		subject = subject[:60] + "..."
	}
	s.renderGitBody(w, "已提交: "+subject)
}

// writeCommitTextarea emits the textarea HTML fragment used as the htmx swap
// target. Keeping the markup here (rather than a separate template) keeps the
// fragment trivially small and lets the caller embed it without a render
// round-trip.
func writeCommitTextarea(w http.ResponseWriter, content string) {
	w.Header().Set("Content-Type", "text/html; charset=utf-8")
	fmt.Fprintf(w,
		`<textarea id="commit-msg-textarea" name="message" class="input commit-msg-textarea" rows="10" placeholder="commit message">%s</textarea>`,
		html.EscapeString(content),
	)
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
		reconfigure := r.FormValue("reconfigure") == "1"
		spec = s.buildJobSpec(target, reconfigure)
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

func (s *Server) buildJobSpec(target string, reconfigure bool) JobSpec {
	label := target
	if target == "" || target == "all" {
		label = "all"
	}
	args := []string{"--build", "--preset", s.opts.Preset}
	if target != "" && target != "all" {
		args = append(args, "--target", target)
	}
	env := []string{"CLICOLOR_FORCE=1", "FORCE_COLOR=1"}
	if !reconfigure {
		return JobSpec{
			Kind:    JobBuild,
			Target:  label,
			Name:    "cmake",
			Args:    args,
			WorkDir: s.opts.RepoRoot,
			Env:     env,
		}
	}
	// Two-step run: configure first, then build. We model that as a shell
	// invocation so the user gets one combined log stream. On Windows we
	// fall back to PowerShell because cmd.exe lacks `&&` in CommandContext
	// without a /c wrapper.
	configureArgs := []string{"--preset", s.opts.Preset}
	if runtime.GOOS == "windows" {
		ps := fmt.Sprintf("cmake %s; if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }; cmake %s",
			joinShell(configureArgs), joinShell(args))
		return JobSpec{
			Kind:    JobBuild,
			Target:  label,
			Name:    "powershell",
			Args:    []string{"-NoProfile", "-Command", ps},
			WorkDir: s.opts.RepoRoot,
			Env:     env,
		}
	}
	sh := fmt.Sprintf("cmake %s && cmake %s",
		joinShell(configureArgs), joinShell(args))
	return JobSpec{
		Kind:    JobBuild,
		Target:  label,
		Name:    "sh",
		Args:    []string{"-c", sh},
		WorkDir: s.opts.RepoRoot,
		Env:     env,
	}
}

// joinShell quotes args naively for human-readable shell strings. Inputs come
// from preset name + target name, neither of which contains shell metacharacters
// in this project, so we keep it simple.
func joinShell(args []string) string {
	out := ""
	for i, a := range args {
		if i > 0 {
			out += " "
		}
		out += a
	}
	return out
}

func (s *Server) runJobSpec(target string, extraArgs []string) (JobSpec, error) {
	if target == "" {
		return JobSpec{}, fmt.Errorf("请选择要运行的 target")
	}
	binDir := platform.BinDir(s.opts.RepoRoot, s.opts.Preset)
	exe := platform.ExecutablePath(binDir, target)
	return JobSpec{
		Kind:       JobRun,
		Target:     target,
		Name:       exe,
		Args:       extraArgs,
		WorkDir:    filepath.Dir(exe),
		Env:        []string{"FORCE_COLOR=1"},
		AfterStart: runActivationHook,
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
