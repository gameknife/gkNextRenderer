// Package spec implements the .spec/ interactive workflow data model:
// parsing and rewriting TODO.md, ARCHIVE.md, and per-task journal/blocker files.
package spec

import (
	"fmt"
	"os"
	"path/filepath"
	"regexp"
	"strconv"
	"strings"
)

type Status string

const (
	StatusPending Status = " "
	StatusDoing   Status = "/"
	StatusDone    Status = "x"
	StatusBlocked Status = "!"
)

type SectionKind int

const (
	SectionUnknown SectionKind = iota
	SectionNext
	SectionBacklog
	SectionRecent
)

func (s SectionKind) Heading() string {
	switch s {
	case SectionNext:
		return "### 下一步"
	case SectionBacklog:
		return "### 待规划"
	case SectionRecent:
		return "### 最近完成"
	}
	return ""
}

func parseSectionHeading(line string) SectionKind {
	switch strings.TrimSpace(line) {
	case "### 下一步":
		return SectionNext
	case "### 待规划":
		return SectionBacklog
	case "### 最近完成":
		return SectionRecent
	}
	return SectionUnknown
}

// Task represents a single line in TODO.md.
type Task struct {
	ID       int
	Status   Status
	Priority string // "P0", "P1", "P2", or empty
	Type     string // "BUG", "FEAT", "IDEA", "SPIKE", "REFACTOR", "DOC", or empty
	Title    string
	Arrow    string // path after " → ", e.g. "journal/00017.md" or "specs/00019.md"
	Paren    string // trailing "(...)" content: a date for [x], or path for [!]
	Section  SectionKind
	LineNum  int // 1-indexed line in source
	Raw      string
}

func (t Task) FormattedID() string {
	return fmt.Sprintf("#%05d", t.ID)
}

// Document holds parsed TODO.md state plus the raw line buffer used to round-trip edits.
type Document struct {
	Path            string
	Lines           []string
	Tasks           []Task
	Milestone       string
	MilestoneStatus string
	// sectionRanges maps a section to [startLine, endLine) line indices (0-based).
	// startLine is the first line AFTER the section heading; endLine is the line
	// of the next heading (or len(Lines)). Used by inserts.
	sectionRanges map[SectionKind][2]int
}

var (
	taskHeadRE  = regexp.MustCompile("^- \\[([ /x!])\\] `#(\\d+)`\\s*(.*)$")
	tagRE       = regexp.MustCompile(`^\[([A-Z][A-Z0-9]*)\]`)
	milestoneRE = regexp.MustCompile(`^##\s+Milestone:\s*(.+?)(?:\s*<!--\s*status:\s*(\w+)\s*-->)?\s*$`)
)

// Parse reads a TODO.md file and produces a Document.
func Parse(path string) (*Document, error) {
	data, err := os.ReadFile(path)
	if err != nil {
		return nil, err
	}
	return parseBytes(path, data)
}

func parseBytes(path string, data []byte) (*Document, error) {
	text := strings.ReplaceAll(string(data), "\r\n", "\n")
	// Trim a single trailing newline so we round-trip without growing.
	trimmed := strings.TrimRight(text, "\n")
	lines := strings.Split(trimmed, "\n")
	if trimmed == "" {
		lines = nil
	}

	doc := &Document{Path: path, Lines: lines, sectionRanges: map[SectionKind][2]int{}}

	currentSection := SectionUnknown
	sectionStart := 0
	closeSection := func(endIdx int) {
		if currentSection != SectionUnknown {
			doc.sectionRanges[currentSection] = [2]int{sectionStart, endIdx}
		}
	}

	for i, line := range lines {
		if m := milestoneRE.FindStringSubmatch(line); m != nil {
			doc.Milestone = strings.TrimSpace(m[1])
			doc.MilestoneStatus = m[2]
			if doc.MilestoneStatus == "" {
				doc.MilestoneStatus = "active"
			}
			continue
		}
		if strings.HasPrefix(line, "### ") || strings.HasPrefix(line, "## ") {
			closeSection(i)
			currentSection = parseSectionHeading(line)
			sectionStart = i + 1
			continue
		}
		if currentSection == SectionUnknown {
			continue
		}
		if t, ok := parseTaskLine(line, i+1, currentSection); ok {
			doc.Tasks = append(doc.Tasks, t)
		}
	}
	closeSection(len(lines))
	return doc, nil
}

func parseTaskLine(line string, lineNum int, section SectionKind) (Task, bool) {
	m := taskHeadRE.FindStringSubmatch(line)
	if m == nil {
		return Task{}, false
	}
	id, err := strconv.Atoi(m[2])
	if err != nil {
		return Task{}, false
	}
	t := Task{
		ID:      id,
		Status:  Status(m[1]),
		Section: section,
		LineNum: lineNum,
		Raw:     line,
	}
	rest := m[3]
	// Strip leading [TAG][TAG]... — classify P\d as priority, anything else as type.
	for {
		tm := tagRE.FindStringSubmatch(rest)
		if tm == nil {
			break
		}
		tag := tm[1]
		if len(tag) == 2 && tag[0] == 'P' && tag[1] >= '0' && tag[1] <= '9' {
			t.Priority = tag
		} else {
			t.Type = tag
		}
		rest = strings.TrimLeft(rest[len(tm[0]):], " \t")
	}
	// Pull a trailing "(...)" first so it doesn't get glued to the arrow path.
	if strings.HasSuffix(rest, ")") {
		if pidx := strings.LastIndex(rest, " ("); pidx >= 0 {
			t.Paren = rest[pidx+2 : len(rest)-1]
			rest = strings.TrimRight(rest[:pidx], " \t")
		}
	}
	// Pull a trailing " → <path>".
	if idx := strings.LastIndex(rest, " → "); idx >= 0 {
		t.Arrow = strings.TrimSpace(rest[idx+len(" → "):])
		rest = strings.TrimRight(rest[:idx], " \t")
	}
	t.Title = rest
	return t, true
}

// FormatLine produces the canonical TODO.md line for a task.
func FormatLine(t Task) string {
	var b strings.Builder
	fmt.Fprintf(&b, "- [%s] `#%05d` ", string(t.Status), t.ID)
	if t.Priority != "" {
		fmt.Fprintf(&b, "[%s]", t.Priority)
	}
	if t.Type != "" {
		fmt.Fprintf(&b, "[%s]", t.Type)
	}
	if t.Priority != "" || t.Type != "" {
		b.WriteByte(' ')
	}
	b.WriteString(t.Title)
	if t.Arrow != "" {
		fmt.Fprintf(&b, " → %s", t.Arrow)
	}
	if t.Paren != "" {
		fmt.Fprintf(&b, " (%s)", t.Paren)
	}
	return b.String()
}

// Save writes the current Lines buffer back to disk with a trailing newline.
func (d *Document) Save() error {
	content := strings.Join(d.Lines, "\n")
	if !strings.HasSuffix(content, "\n") {
		content += "\n"
	}
	return os.WriteFile(d.Path, []byte(content), 0644)
}

// FindTask returns the task with the given ID and its index in d.Tasks.
func (d *Document) FindTask(id int) (*Task, int, bool) {
	for i := range d.Tasks {
		if d.Tasks[i].ID == id {
			return &d.Tasks[i], i, true
		}
	}
	return nil, -1, false
}

// MaxID returns the largest task ID in the document, or 0 if none.
func (d *Document) MaxID() int {
	max := 0
	for _, t := range d.Tasks {
		if t.ID > max {
			max = t.ID
		}
	}
	return max
}

// MarkStatus rewrites the line for the given task ID to the given status.
// For StatusDone it also fills Arrow and Paren with a journal link + date.
func (d *Document) MarkStatus(id int, status Status, opts ...EditOption) error {
	t, idx, ok := d.FindTask(id)
	if !ok {
		return fmt.Errorf("task #%05d not found in %s", id, filepath.Base(d.Path))
	}
	t.Status = status
	for _, opt := range opts {
		opt(t)
	}
	d.Tasks[idx] = *t
	d.Lines[t.LineNum-1] = FormatLine(*t)
	return nil
}

// EditOption is a small functional-options knob for MarkStatus/Edit.
type EditOption func(*Task)

func WithArrow(path string) EditOption { return func(t *Task) { t.Arrow = path } }
func WithParen(text string) EditOption { return func(t *Task) { t.Paren = text } }
func WithClearArrow() EditOption       { return func(t *Task) { t.Arrow = "" } }
func WithClearParen() EditOption       { return func(t *Task) { t.Paren = "" } }

// EditTask updates the title / type / priority of a pending task. It refuses to
// touch tasks that are not StatusPending so we never silently rewrite a done or
// blocked entry's metadata.
func (d *Document) EditTask(id int, title, taskType, priority string) error {
	t, idx, ok := d.FindTask(id)
	if !ok {
		return fmt.Errorf("task #%05d not found in %s", id, filepath.Base(d.Path))
	}
	if t.Status != StatusPending {
		return fmt.Errorf("task #%05d is not pending (status=%q); refusing to edit", id, string(t.Status))
	}
	title = strings.TrimSpace(title)
	if title == "" {
		return fmt.Errorf("title must not be empty")
	}
	t.Title = title
	t.Type = strings.ToUpper(strings.TrimSpace(taskType))
	t.Priority = strings.ToUpper(strings.TrimSpace(priority))
	d.Tasks[idx] = *t
	d.Lines[t.LineNum-1] = FormatLine(*t)
	return nil
}

// AppendTask inserts a new task line at the end of the given section, returning
// the assigned ID (max existing + 1, capped at 99999).
func (d *Document) AppendTask(section SectionKind, t Task) (int, error) {
	rng, ok := d.sectionRanges[section]
	if !ok {
		return 0, fmt.Errorf("section %q not present in %s", section.Heading(), filepath.Base(d.Path))
	}
	if t.ID == 0 {
		t.ID = d.MaxID() + 1
	}
	if t.ID > 99999 {
		return 0, fmt.Errorf("task id exceeds 5-digit range")
	}
	if t.Status == "" {
		t.Status = StatusPending
	}
	t.Section = section
	line := FormatLine(t)

	// Insert before the last trailing blank line of the section, if any; otherwise
	// at the section end. Also remove a literal "(暂无)" placeholder.
	insertAt := rng[1]
	for insertAt > rng[0] && strings.TrimSpace(d.Lines[insertAt-1]) == "" {
		insertAt--
	}
	// Remove "(暂无)" placeholder line if it's the only content.
	if insertAt-1 >= rng[0] && strings.TrimSpace(d.Lines[insertAt-1]) == "(暂无)" {
		d.Lines = append(d.Lines[:insertAt-1], d.Lines[insertAt:]...)
		d.shiftRanges(insertAt-1, -1)
		insertAt--
	}

	newLines := make([]string, 0, len(d.Lines)+1)
	newLines = append(newLines, d.Lines[:insertAt]...)
	newLines = append(newLines, line)
	newLines = append(newLines, d.Lines[insertAt:]...)
	d.Lines = newLines
	d.shiftRanges(insertAt, 1)

	t.LineNum = insertAt + 1
	t.Raw = line
	d.Tasks = append(d.Tasks, t)
	return t.ID, nil
}

// RemoveLines removes the given 1-indexed line numbers from the buffer and
// updates section ranges accordingly. Task parsing is NOT re-done; callers that
// need a fresh task slice should re-Parse.
func (d *Document) RemoveLines(lineNums []int) {
	if len(lineNums) == 0 {
		return
	}
	drop := make(map[int]bool, len(lineNums))
	for _, n := range lineNums {
		drop[n-1] = true
	}
	out := make([]string, 0, len(d.Lines))
	removedBefore := make([]int, len(d.Lines)+1)
	count := 0
	for i, line := range d.Lines {
		removedBefore[i] = count
		if drop[i] {
			count++
			continue
		}
		out = append(out, line)
	}
	removedBefore[len(d.Lines)] = count
	d.Lines = out
	for k, rng := range d.sectionRanges {
		start := rng[0] - removedBefore[rng[0]]
		end := rng[1] - removedBefore[rng[1]]
		d.sectionRanges[k] = [2]int{start, end}
	}
	// Rebuild Tasks
	kept := d.Tasks[:0]
	for _, t := range d.Tasks {
		if !drop[t.LineNum-1] {
			t.LineNum -= removedBefore[t.LineNum-1]
			kept = append(kept, t)
		}
	}
	d.Tasks = kept
}

// SectionTasks returns all tasks currently parsed in the given section.
func (d *Document) SectionTasks(section SectionKind) []Task {
	var out []Task
	for _, t := range d.Tasks {
		if t.Section == section {
			out = append(out, t)
		}
	}
	return out
}

func (d *Document) shiftRanges(insertedAt, delta int) {
	for k, rng := range d.sectionRanges {
		start, end := rng[0], rng[1]
		if insertedAt <= start {
			start += delta
		}
		if insertedAt < end || (insertedAt == end && delta > 0 && rng[1] == insertedAt) {
			end += delta
		}
		d.sectionRanges[k] = [2]int{start, end}
	}
}
