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
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/remoteplay"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/spec"
)

func (s *Server) routes() http.Handler {
	mux := http.NewServeMux()
	mux.HandleFunc("GET /{$}", s.handleIndex)
	mux.HandleFunc("GET /remote/client", s.handleRemoteClient)
	mux.HandleFunc("GET /todo-panel", s.handleTodoPanel)
	mux.HandleFunc("POST /todo/cleanup", s.handleTodoCleanup)
	mux.HandleFunc("POST /docs/save", s.handleDocsSave)
	mux.HandleFunc("GET /docs/source", s.handleDocsSource)
	mux.HandleFunc("GET /graph/data", s.handleGraphData)
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
	mux.HandleFunc("OPTIONS /jobs/{id}/stream", s.handleJobStreamOptions)
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
	mux.HandleFunc("OPTIONS /chat/send-stream", s.handleChatSendStreamOptions)
	mux.HandleFunc("POST /chat/send-stream", s.handleChatSendStream)
	mux.HandleFunc("GET /chat/session", s.handleChatSession)
	mux.HandleFunc("POST /chat/new", s.handleChatNew)
	mux.HandleFunc("POST /chat/archive", s.handleChatArchive)
	mux.HandleFunc("POST /chat/clear", s.handleChatClear)
	mux.HandleFunc("POST /chat/serve", s.handleChatServe)
	mux.HandleFunc("POST /chat/stop", s.handleChatStop)
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
	StreamBase string
	Milestone  string
	Status     string
	Sections   []sectionVM
	Journals   []journalSummary
	Version    string
	Preset     string
	OS         string
	RecentSize int
	ActiveTab  string // "todo" | "docs" | "build" | "remote" | "test" | "git" | "chat" | "loc" | "paks" | "graph" | "settings"
	BuildVM    buildRunVM
	RemoteVM   remoteVM
	TestVM     testVM
	GitVM      gitVM
	ChatVM     chatVM
	LocVM      locVM
	PaksVM     paksVM
	DocsVM     docsVM
	GraphVM    graphVM
}

type locVM struct {
	Snapshot          *loc.Snapshot
	IncludeThirdParty bool
	Error             string
	MaxCategoryLines  int
	Contributions     contributionGraphVM
	HourlyCommits     hourlyCommitGraphVM
	DepthOptions      []locDepthOption
	SelectedDepth     string
	TableRows         []locTableRowVM
}

type locDepthOption struct {
	Value    string
	Label    string
	Selected bool
}

type locTableRowVM struct {
	Name  string
	Path  string
	Files int
	Lines int
	Depth int
}

type contributionDayVM struct {
	Date   string
	Label  string
	Count  int
	Level  int
	Future bool
}

type contributionWeekVM struct {
	Days []contributionDayVM
}

type contributionMonthVM struct {
	Label  string
	Column int
}

type contributionGraphVM struct {
	Weeks  []contributionWeekVM
	Months []contributionMonthVM
	Total  int
	Max    int
	Error  string
}

type hourlyCommitVM struct {
	Hour       int
	Label      string
	RangeLabel string
	Count      int
}

type hourlyCommitGraphVM struct {
	Hours []hourlyCommitVM
	Total int
	Max   int
	Mid   int
	Error string
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

type buildRunVM struct {
	Targets []targetVM
	Latest  JobSnapshot
	HasJob  bool
}

type remoteVM struct {
	Targets        []targetVM
	Latest         JobSnapshot
	HasJob         bool
	LocalHosts     []string
	URLPreview     []string
	DefaultTarget  string
	DefaultBind    string
	DefaultScene   string
	DefaultRes     string
	DefaultEncoder string
	HTTPPort       uint32
	SignalingPort  uint32
	BitrateKbps    uint32
	Fps            uint32
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
	SessionID        string
	Models           []chatModelVM
	Sessions         []chatSessionVM
	SelectedModel    string
	SelectedProvider string
	SelectedProfile  string
	Providers        []chatProviderVM
	Messages         []llm.ChatMessage
	Context          chatContextVM
	Error            string
	Flash            string
	ServerRunning    bool
	RunningModel     string
	Endpoint         string
}

type chatSessionVM struct {
	ID             string
	Title          string
	ModelID        string
	ProviderID     string
	ProfileID      string
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

type chatProviderVM struct {
	ID          string
	DisplayName string
	Kind        string
	Configured  bool
	Active      bool
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

const recentDisplayLimit = 10

func (s *Server) buildIndex() (indexVM, error) {
	doc, err := spec.Parse(spec.TODOPath(s.opts.RepoRoot))
	if err != nil {
		return indexVM{}, err
	}
	vm := indexVM{
		RepoRoot:   s.opts.RepoRoot,
		StreamBase: s.streamBaseURL,
		Milestone:  doc.Milestone,
		Status:     doc.MilestoneStatus,
		Version:    s.opts.Version,
		Preset:     s.opts.Preset,
		OS:         runtime.GOOS + "/" + runtime.GOARCH,
		ActiveTab:  "todo",
	}
	for _, kind := range []spec.SectionKind{spec.SectionNext, spec.SectionBacklog, spec.SectionRecent} {
		tasks := doc.SectionTasks(kind)
		if kind == spec.SectionRecent && len(tasks) > recentDisplayLimit {
			tasks = append([]spec.Task(nil), tasks[len(tasks)-recentDisplayLimit:]...)
		}
		vm.Sections = append(vm.Sections, sectionVM{
			Kind:    kind,
			Heading: kind.Heading(),
			Tasks:   tasks,
		})
	}
	vm.RecentSize = len(doc.SectionTasks(spec.SectionRecent))
	vm.Journals = collectJournals(s.opts.RepoRoot, doc, recentDisplayLimit)
	return vm, nil
}

// buildHeader returns a minimal indexVM with just the header fields populated.
// Tab content handlers use this as a base so each tab can be rendered without
// re-parsing TODO.md when not needed.
func (s *Server) buildHeader(activeTab string) indexVM {
	return indexVM{
		RepoRoot:   s.opts.RepoRoot,
		StreamBase: s.streamBaseURL,
		Version:    s.opts.Version,
		Preset:     s.opts.Preset,
		OS:         runtime.GOOS + "/" + runtime.GOARCH,
		ActiveTab:  activeTab,
	}
}

func (s *Server) buildBuildRunVM() buildRunVM {
	vm := buildRunVM{
		Targets: discoverTargets(s.opts.RepoRoot, s.opts.Preset, s.opts.Config.Targets.All),
	}
	build, hasBuild := s.jobs.LatestSnapshot(JobBuild)
	run, hasRun := s.jobs.LatestSnapshot(JobRun)
	build.StreamBase = s.streamBaseURL
	run.StreamBase = s.streamBaseURL
	switch {
	case hasBuild && hasRun:
		if run.StartedAt.After(build.StartedAt) {
			vm.Latest = run
		} else {
			vm.Latest = build
		}
		vm.HasJob = true
	case hasBuild:
		vm.Latest = build
		vm.HasJob = true
	case hasRun:
		vm.Latest = run
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
		snap.StreamBase = s.streamBaseURL
		vm.Latest = snap
		vm.HasJob = true
	}
	return vm
}

func (s *Server) buildRemoteVM() remoteVM {
	targets := discoverTargets(s.opts.RepoRoot, s.opts.Preset, s.opts.Config.Targets.All)
	runnable := make([]targetVM, 0, len(targets))
	defaultTarget := "gkNextRenderer"
	hasDefaultTarget := false
	for _, target := range targets {
		if !target.Runnable || target.Name == "Packager" {
			continue
		}
		if target.Name == defaultTarget {
			hasDefaultTarget = true
		}
		runnable = append(runnable, target)
	}
	if !hasDefaultTarget && len(runnable) > 0 {
		defaultTarget = runnable[0].Name
	}
	vm := remoteVM{
		Targets:        runnable,
		LocalHosts:     remoteplay.LocalIPv4Hosts(),
		DefaultTarget:  defaultTarget,
		DefaultBind:    "0.0.0.0",
		DefaultEncoder: "auto",
		HTTPPort:       8088,
		SignalingPort:  8089,
		Fps:            30,
	}
	vm.URLPreview = remoteplay.BuildAccessURLs(vm.DefaultBind, vm.HTTPPort, vm.LocalHosts)
	if snap, ok := s.jobs.LatestSnapshot(JobRemote); ok {
		snap.StreamBase = s.streamBaseURL
		vm.Latest = snap
		vm.HasJob = true
	}
	return vm
}

func (s *Server) buildLocVM(includeThirdParty bool, selectedDepth string) locVM {
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
	maxDepth := locMaxSelectableDepth(snap)
	vm.SelectedDepth = normalizeLocDepth(selectedDepth, maxDepth)
	vm.DepthOptions = buildLocDepthOptions(maxDepth, vm.SelectedDepth)
	vm.TableRows = buildLocTableRows(snap, vm.SelectedDepth)
	today := time.Now()
	chartStart := contributionChartStart(today)
	counts, err := gitops.DailyCommitCounts(s.opts.RepoRoot, chartStart)
	if err != nil {
		vm.Contributions.Error = err.Error()
	} else {
		vm.Contributions = buildContributionGraph(counts, today)
	}
	hourlyCounts, err := gitops.HourlyCommitCounts(s.opts.RepoRoot, chartStart)
	if err != nil {
		vm.HourlyCommits.Error = err.Error()
	} else {
		vm.HourlyCommits = buildHourlyCommitGraph(hourlyCounts)
	}
	return vm
}

func locMaxSelectableDepth(snap *loc.Snapshot) int {
	if snap == nil {
		return 1
	}
	maxDepth := snap.MaxFolderDepth + 1
	if maxDepth < 1 {
		maxDepth = 1
	}
	return maxDepth
}

func normalizeLocDepth(value string, maxDepth int) string {
	if maxDepth < 1 {
		maxDepth = 1
	}
	defaultDepth := 3
	if maxDepth < defaultDepth {
		defaultDepth = maxDepth
	}
	if value == "" {
		return strconv.Itoa(defaultDepth)
	}
	depth, err := strconv.Atoi(value)
	if err != nil {
		return strconv.Itoa(defaultDepth)
	}
	if depth < 1 {
		depth = 1
	}
	if depth > maxDepth {
		depth = maxDepth
	}
	return strconv.Itoa(depth)
}

func buildLocDepthOptions(maxDepth int, selected string) []locDepthOption {
	if maxDepth < 1 {
		maxDepth = 1
	}
	options := make([]locDepthOption, 0, maxDepth)
	for depth := 1; depth <= maxDepth; depth++ {
		options = append(options, locDepthOption{
			Value:    strconv.Itoa(depth),
			Label:    fmt.Sprintf("展开 %d 层", depth),
			Selected: selected == strconv.Itoa(depth),
		})
	}
	return options
}

func buildLocTableRows(snap *loc.Snapshot, selectedDepth string) []locTableRowVM {
	if snap == nil || snap.Tree == nil {
		return nil
	}
	rows := []locTableRowVM{}
	depthLimit, _ := strconv.Atoi(selectedDepth)
	if depthLimit < 1 {
		depthLimit = 1
	}
	for _, category := range snap.Tree.Children {
		appendLocTableRows(&rows, category, depthLimit)
	}
	return rows
}

func appendLocTableRows(rows *[]locTableRowVM, node *loc.TreeNodeSummary, depthLimit int) {
	if node == nil || node.IsFile {
		return
	}
	*rows = append(*rows, locTableRowVM{
		Name:  node.Name,
		Path:  node.Path,
		Files: node.Files,
		Lines: node.Lines,
		Depth: max(0, node.Depth-1),
	})
	if node.Depth >= depthLimit {
		return
	}
	for _, child := range node.Children {
		appendLocTableRows(rows, child, depthLimit)
	}
}

func contributionChartStart(today time.Time) time.Time {
	today = dateOnly(today)
	currentWeekStart := today.AddDate(0, 0, -int(today.Weekday()))
	return currentWeekStart.AddDate(0, 0, -52*7)
}

func buildContributionGraph(counts map[string]int, today time.Time) contributionGraphVM {
	today = dateOnly(today)
	start := contributionChartStart(today)
	vm := contributionGraphVM{
		Weeks: make([]contributionWeekVM, 53),
	}
	for offset := 0; offset < 53*7; offset++ {
		date := start.AddDate(0, 0, offset)
		if date.After(today) {
			break
		}
		if count := counts[date.Format("2006-01-02")]; count > vm.Max {
			vm.Max = count
		}
	}

	lastMonth := time.Month(0)
	for week := 0; week < 53; week++ {
		weekStart := start.AddDate(0, 0, week*7)
		if weekStart.Month() != lastMonth {
			vm.Months = append(vm.Months, contributionMonthVM{
				Label:  weekStart.Format("Jan"),
				Column: week + 1,
			})
			lastMonth = weekStart.Month()
		}
		days := make([]contributionDayVM, 0, 7)
		for day := 0; day < 7; day++ {
			date := weekStart.AddDate(0, 0, day)
			future := date.After(today)
			count := 0
			if !future {
				count = counts[date.Format("2006-01-02")]
				vm.Total += count
			}
			days = append(days, contributionDayVM{
				Date:   date.Format("2006-01-02"),
				Label:  date.Format("Jan 2, 2006"),
				Count:  count,
				Level:  contributionLevel(count, vm.Max),
				Future: future,
			})
		}
		vm.Weeks[week] = contributionWeekVM{Days: days}
	}
	return vm
}

func contributionLevel(count, maxCount int) int {
	if count <= 0 || maxCount <= 0 {
		return 0
	}
	level := (count*4 + maxCount - 1) / maxCount
	if level < 1 {
		return 1
	}
	if level > 4 {
		return 4
	}
	return level
}

func buildHourlyCommitGraph(counts map[int]int) hourlyCommitGraphVM {
	vm := hourlyCommitGraphVM{
		Hours: make([]hourlyCommitVM, 0, 24),
	}
	for offset := 0; offset < 24; offset++ {
		hour := (6 + offset) % 24
		count := counts[hour]
		vm.Hours = append(vm.Hours, hourlyCommitVM{
			Hour:       hour,
			Label:      formatHourLabel(hour),
			RangeLabel: fmt.Sprintf("%02d:00–%02d:00", hour, (hour+1)%24),
			Count:      count,
		})
		vm.Total += count
		if count > vm.Max {
			vm.Max = count
		}
	}
	if vm.Max > 0 {
		vm.Mid = (vm.Max + 1) / 2
	}
	return vm
}

func formatHourLabel(hour int) string {
	if hour < 0 || hour > 23 {
		return ""
	}
	period := "AM"
	displayHour := hour
	if hour >= 12 {
		period = "PM"
		if displayHour > 12 {
			displayHour -= 12
		}
	}
	if displayHour == 0 {
		displayHour = 12
	}
	return fmt.Sprintf("%d %s", displayHour, period)
}

func dateOnly(value time.Time) time.Time {
	year, month, day := value.Date()
	return time.Date(year, month, day, 0, 0, 0, 0, value.Location())
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
	if r.URL.Query().Get("tab") == "paks" {
		vm := s.buildHeader("paks")
		vm.PaksVM = s.buildPaksVM(r.URL.Query().Get("file"))
		s.render(w, "layout.html", vm)
		return
	}
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
		vm.BuildVM = s.buildBuildRunVM()
		s.render(w, "tab_build", vm)
	case "remote":
		vm := s.buildHeader("remote")
		vm.RemoteVM = s.buildRemoteVM()
		s.render(w, "tab_remote", vm)
	case "docs":
		vm := s.buildHeader("docs")
		vm.DocsVM = s.buildDocsVM(r.URL.Query().Get("file"), r.URL.Query().Get("edit") == "1", "", "")
		s.render(w, "tab_docs", vm)
	case "run":
		vm := s.buildHeader("build")
		vm.BuildVM = s.buildBuildRunVM()
		s.render(w, "tab_build", vm)
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
		vm.ChatVM = s.buildChatVM("", "", "", "")
		s.render(w, "tab_chat", vm)
	case "loc":
		vm := s.buildHeader("loc")
		vm.LocVM = s.buildLocVM(r.URL.Query().Get("thirdparty") != "", r.URL.Query().Get("depth"))
		s.render(w, "tab_loc", vm)
	case "paks":
		vm := s.buildHeader("paks")
		vm.PaksVM = s.buildPaksVM(r.URL.Query().Get("file"))
		s.render(w, "tab_paks", vm)
	case "graph":
		vm := s.buildHeader("graph")
		vm.GraphVM = s.buildGraphVM()
		s.render(w, "tab_graph", vm)
	case "settings":
		vm := s.buildHeader("settings")
		s.render(w, "tab_settings", vm)
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
