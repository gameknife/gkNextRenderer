// TODO tab: render the task panel and mutate .spec/TODO.md (add/edit/move/
// done/block/delete) plus per-task spec creation. See handlers.go for shared
// view models and routing.
package dashboard

import (
	"fmt"
	"net/http"
	"os"
	"strconv"
	"strings"
	"time"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/spec"
)

// loadTODO parses .spec/TODO.md. On failure it writes an HTTP 500 and returns
// ok=false, so handlers can bail with `doc, ok := s.loadTODO(w); if !ok { return }`.
func (s *Server) loadTODO(w http.ResponseWriter) (*spec.Document, bool) {
	doc, err := spec.Parse(spec.TODOPath(s.opts.RepoRoot))
	if err != nil {
		httpError(w, err)
		return nil, false
	}
	return doc, true
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
	doc, ok := s.loadTODO(w)
	if !ok {
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
	doc, ok := s.loadTODO(w)
	if !ok {
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
	doc, ok := s.loadTODO(w)
	if !ok {
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
	doc, ok := s.loadTODO(w)
	if !ok {
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
	doc, ok := s.loadTODO(w)
	if !ok {
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
	doc, ok := s.loadTODO(w)
	if !ok {
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
	doc, ok := s.loadTODO(w)
	if !ok {
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
	doc, ok := s.loadTODO(w)
	if !ok {
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
	doc, ok := s.loadTODO(w)
	if !ok {
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
