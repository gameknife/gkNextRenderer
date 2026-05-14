package dashboard

import (
	"fmt"
	"html/template"
	"net/http"
	"runtime"
	"strconv"
	"strings"
	"time"

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
}

type journalSummary struct {
	ID      int
	Title   string
	Date    string
	Type    string
	Section string
}

type taskDetailVM struct {
	Task         spec.Task
	SectionName  string
	SpecBody     string
	JournalBody  string
	BlockerBody  string
	HasSpec      bool
	HasJournal   bool
	HasBlocker   bool
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
