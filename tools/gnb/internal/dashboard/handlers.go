package dashboard

import (
	"fmt"
	"net/http"
	"runtime"
	"strconv"
	"strings"
	"time"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/gitops"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/llm"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/loc"
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
	mux.HandleFunc("GET /task/{id}/spec/edit", s.handleTaskSpecEditForm)
	mux.HandleFunc("POST /task/{id}/spec", s.handleTaskCreateSpec)
	mux.HandleFunc("POST /task/{id}/spec/save", s.handleTaskSpecSave)
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
	mux.HandleFunc("POST /chat/send", s.handleChatSend)
	mux.HandleFunc("POST /chat/send-stream", s.handleChatSendStream)
	mux.HandleFunc("GET /chat/session", s.handleChatSession)
	mux.HandleFunc("POST /chat/new", s.handleChatNew)
	mux.HandleFunc("POST /chat/archive", s.handleChatArchive)
	mux.HandleFunc("POST /chat/clear", s.handleChatClear)
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
	ChatVM     chatVM
	LocVM      locVM
}

type locVM struct {
	Snapshot          *loc.Snapshot
	IncludeThirdParty bool
	Error             string
	MaxCategoryLines  int
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

type chatVM struct {
	SessionID     string
	Models        []chatModelVM
	Sessions      []chatSessionVM
	SelectedModel string
	Messages      []llm.ChatMessage
	Context       chatContextVM
	Error         string
	ServerRunning bool
	RunningModel  string
	Endpoint      string
}

type chatSessionVM struct {
	ID             string
	Title          string
	ModelID        string
	UpdatedAt      time.Time
	RelativeTime   string
	MessageCount   int
	Active         bool
	ContextUsed    int
	ContextLimit   int
	ContextPercent int
}

type chatContextVM struct {
	Used    int
	Limit   int
	Percent int
}

type chatModelVM struct {
	ID         string
	ContextN   int
	Downloaded bool
	Active     bool
	Running    bool
}

const (
	defaultChatMaxTokens = 4096
	minChatMaxTokens     = 256
	maxChatMaxTokens     = 32768
)

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
	EditingSpec bool
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

func (s *Server) buildLocVM(includeThirdParty bool) locVM {
	snap, err := loc.Scan(loc.Options{
		Root:              s.opts.RepoRoot,
		IncludeThirdParty: includeThirdParty,
	})
	vm := locVM{IncludeThirdParty: includeThirdParty}
	if err != nil {
		vm.Error = err.Error()
		return vm
	}
	vm.Snapshot = snap
	for _, c := range snap.Categories {
		if c.Lines > vm.MaxCategoryLines {
			vm.MaxCategoryLines = c.Lines
		}
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
	case "chat":
		vm := s.buildHeader("chat")
		vm.ChatVM = s.buildChatVM("", "")
		s.render(w, "tab_chat", vm)
	case "loc":
		vm := s.buildHeader("loc")
		vm.LocVM = s.buildLocVM(r.URL.Query().Get("thirdparty") == "1")
		s.render(w, "tab_loc", vm)
	default:
		http.Error(w, "unknown tab "+kind, http.StatusNotFound)
	}
}

// ----- misc helpers --------------------------------------------------

func relativeTime(t time.Time) string {
	if t.IsZero() {
		return "now"
	}
	d := time.Since(t)
	if d < 0 {
		d = 0
	}
	switch {
	case d < time.Minute:
		return "now"
	case d < time.Hour:
		return fmt.Sprintf("%dm", int(d.Minutes()))
	case d < 24*time.Hour:
		return fmt.Sprintf("%dh", int(d.Hours()))
	case d < 7*24*time.Hour:
		return fmt.Sprintf("%dd", int(d.Hours()/24))
	case d < 30*24*time.Hour:
		return fmt.Sprintf("%dweek", int(d.Hours()/(24*7)))
	case d < 365*24*time.Hour:
		return fmt.Sprintf("%dmonth", int(d.Hours()/(24*30)))
	default:
		return fmt.Sprintf("%dy", int(d.Hours()/(24*365)))
	}
}
