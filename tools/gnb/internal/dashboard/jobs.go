package dashboard

import (
	"bufio"
	"context"
	"crypto/rand"
	"encoding/hex"
	"errors"
	"fmt"
	"io"
	"os/exec"
	"strings"
	"sync"
	"time"
)

// JobKind tags a job so the UI can pick the right "latest" entry per tab.
type JobKind string

const (
	JobBuild JobKind = "build"
	JobRun   JobKind = "run"
	JobTest  JobKind = "test"
)

// JobStatus tracks the lifecycle of a Job.
type JobStatus string

const (
	StatusRunning  JobStatus = "running"
	StatusSuccess  JobStatus = "success"
	StatusFailed   JobStatus = "failed"
	StatusCanceled JobStatus = "canceled"
)

const (
	maxBufferLines = 5000
	subBufferSize  = 256
)

// JobEvent is a single SSE message destined for a subscriber. Name maps to the
// SSE `event:` field, Data is already HTML-escaped / ANSI-converted.
type JobEvent struct {
	Name string
	Data string
}

// Job is one in-flight or terminated subprocess. It is safe to read fields via
// Snapshot(); raw fields must only be touched while holding mu.
type Job struct {
	ID      string
	Kind    JobKind
	Target  string // descriptive label ("all", a target name, or "all tests")
	Command string // human-readable command line for the header

	mu         sync.Mutex
	status     JobStatus
	startedAt  time.Time
	finishedAt time.Time
	exitNote   string // exit code or error message
	lines      []string
	subs       map[chan JobEvent]struct{}
	cmd        *exec.Cmd
	cancel     context.CancelFunc
}

// JobSnapshot is a lightweight copy used by templates and the catch-up SSE
// replay path. It owns its slice so callers can mutate freely.
type JobSnapshot struct {
	ID         string
	Kind       JobKind
	Target     string
	Command    string
	StreamBase string
	Status     JobStatus
	StartedAt  time.Time
	FinishedAt time.Time
	ExitNote   string
	Lines      []string
}

func (j *Job) snapshot() JobSnapshot {
	j.mu.Lock()
	defer j.mu.Unlock()
	lines := make([]string, len(j.lines))
	copy(lines, j.lines)
	return JobSnapshot{
		ID:         j.ID,
		Kind:       j.Kind,
		Target:     j.Target,
		Command:    j.Command,
		Status:     j.status,
		StartedAt:  j.startedAt,
		FinishedAt: j.finishedAt,
		ExitNote:   j.exitNote,
		Lines:      lines,
	}
}

func (j *Job) appendLine(htmlLine string) {
	j.mu.Lock()
	j.lines = append(j.lines, htmlLine)
	if over := len(j.lines) - maxBufferLines; over > 0 {
		j.lines = j.lines[over:]
	}
	subs := make([]chan JobEvent, 0, len(j.subs))
	for ch := range j.subs {
		subs = append(subs, ch)
	}
	j.mu.Unlock()
	for _, ch := range subs {
		// Non-blocking send: if the subscriber is slow we drop. The next
		// page reload still gets the buffered output via snapshot.
		select {
		case ch <- JobEvent{Name: "line", Data: htmlLine}:
		default:
		}
	}
}

func (j *Job) finalize(status JobStatus, note string) {
	j.mu.Lock()
	j.status = status
	j.finishedAt = time.Now()
	donePayload := fmt.Sprintf("%d", j.finishedAt.Unix())
	j.exitNote = note
	subs := make([]chan JobEvent, 0, len(j.subs))
	for ch := range j.subs {
		subs = append(subs, ch)
	}
	j.mu.Unlock()

	// Append a completion summary line so the user doesn't miss the result.
	switch status {
	case StatusSuccess:
		j.appendLine(`<span style="color:#22c55e;font-weight:600">✓ 完成 (` + note + `)</span>`)
	case StatusFailed:
		j.appendLine(`<span style="color:#ef4444;font-weight:600">✗ 失败 (` + note + `)</span>`)
	case StatusCanceled:
		j.appendLine(`<span style="color:#9aa3b2">⊘ 已取消</span>`)
	}

	statusPayload := statusBadgeHTML(status, note)
	for _, ch := range subs {
		// Send status & done events synchronously where possible; we
		// don't want to drop the terminal signal.
		select {
		case ch <- JobEvent{Name: "status", Data: statusPayload}:
		default:
		}
		select {
		case ch <- JobEvent{Name: "done", Data: donePayload}:
		default:
		}
	}
}

func (j *Job) subscribe() (chan JobEvent, JobSnapshot) {
	ch := make(chan JobEvent, subBufferSize)
	j.mu.Lock()
	if j.subs == nil {
		j.subs = map[chan JobEvent]struct{}{}
	}
	j.subs[ch] = struct{}{}
	snap := JobSnapshot{
		ID:         j.ID,
		Kind:       j.Kind,
		Target:     j.Target,
		Command:    j.Command,
		Status:     j.status,
		StartedAt:  j.startedAt,
		FinishedAt: j.finishedAt,
		ExitNote:   j.exitNote,
		Lines:      append([]string(nil), j.lines...),
	}
	j.mu.Unlock()
	return ch, snap
}

func (j *Job) unsubscribe(ch chan JobEvent) {
	j.mu.Lock()
	delete(j.subs, ch)
	j.mu.Unlock()
}

// JobSpec describes how to spawn a subprocess. WorkDir, Env, and forcing of
// color output are managed centrally so build/run/test all behave the same.
type JobSpec struct {
	Kind       JobKind
	Target     string
	Name       string // executable
	Args       []string
	WorkDir    string
	Env        []string // extra env entries, appended to os.Environ()
	AfterStart func(*exec.Cmd)
}

// JobManager owns the active and historical jobs. Per kind only one job is
// kept as "latest"; an older one is left running but loses the slot. The user
// can cancel the previous explicitly if they want to stop it.
type JobManager struct {
	mu     sync.Mutex
	jobs   map[string]*Job
	latest map[JobKind]string
}

func NewJobManager() *JobManager {
	return &JobManager{
		jobs:   map[string]*Job{},
		latest: map[JobKind]string{},
	}
}

// Start spawns a subprocess based on spec and returns the new Job ID.
func (m *JobManager) Start(spec JobSpec) (*Job, error) {
	if spec.Name == "" {
		return nil, errors.New("job: empty executable")
	}
	id := newJobID()
	ctx, cancel := context.WithCancel(context.Background())
	cmd := exec.CommandContext(ctx, spec.Name, spec.Args...)
	cmd.Dir = spec.WorkDir
	cmd.Env = append(cmd.Environ(), spec.Env...)
	stdout, err := cmd.StdoutPipe()
	if err != nil {
		cancel()
		return nil, err
	}
	stderr, err := cmd.StderrPipe()
	if err != nil {
		cancel()
		return nil, err
	}
	if err := cmd.Start(); err != nil {
		cancel()
		return nil, err
	}
	if spec.AfterStart != nil {
		go spec.AfterStart(cmd)
	}

	job := &Job{
		ID:        id,
		Kind:      spec.Kind,
		Target:    spec.Target,
		Command:   strings.TrimSpace(spec.Name + " " + strings.Join(spec.Args, " ")),
		status:    StatusRunning,
		startedAt: time.Now(),
		subs:      map[chan JobEvent]struct{}{},
		cmd:       cmd,
		cancel:    cancel,
	}

	m.mu.Lock()
	m.jobs[id] = job
	m.latest[spec.Kind] = id
	m.mu.Unlock()

	// Banner line so the user can see what was run.
	job.appendLine(`<span style="color:#9aa3b2">$ ` + escapeHTML(job.Command) + `</span>`)

	var wg sync.WaitGroup
	wg.Add(2)
	go pumpLines(stdout, job, &wg, false)
	go pumpLines(stderr, job, &wg, true)

	go func() {
		wg.Wait()
		err := cmd.Wait()
		cancel()
		switch {
		case err == nil:
			job.finalize(StatusSuccess, "exit 0")
		case ctx.Err() == context.Canceled && job.statusIs(StatusRunning):
			// Treat as canceled even if the OS surfaced an error.
			job.finalize(StatusCanceled, "canceled")
		default:
			var exitErr *exec.ExitError
			if errors.As(err, &exitErr) {
				job.finalize(StatusFailed, fmt.Sprintf("exit %d", exitErr.ExitCode()))
			} else {
				job.finalize(StatusFailed, err.Error())
			}
		}
	}()

	return job, nil
}

func (j *Job) statusIs(s JobStatus) bool {
	j.mu.Lock()
	defer j.mu.Unlock()
	return j.status == s
}

// Cancel terminates a running job; it's a no-op for already-finished jobs.
func (m *JobManager) Cancel(id string) error {
	m.mu.Lock()
	job, ok := m.jobs[id]
	m.mu.Unlock()
	if !ok {
		return fmt.Errorf("job %s not found", id)
	}
	job.mu.Lock()
	if job.status != StatusRunning {
		job.mu.Unlock()
		return nil
	}
	cancel := job.cancel
	job.status = StatusCanceled
	job.mu.Unlock()
	if cancel != nil {
		cancel()
	}
	return nil
}

// Get returns a snapshot of a single job by ID.
func (m *JobManager) Get(id string) (*Job, bool) {
	m.mu.Lock()
	defer m.mu.Unlock()
	j, ok := m.jobs[id]
	return j, ok
}

// Latest returns a snapshot of the most-recent job for a kind, if any.
func (m *JobManager) Latest(kind JobKind) (*Job, bool) {
	m.mu.Lock()
	defer m.mu.Unlock()
	id, ok := m.latest[kind]
	if !ok {
		return nil, false
	}
	job, ok := m.jobs[id]
	return job, ok
}

// LatestSnapshot is a convenience for templates that only need read-only data.
func (m *JobManager) LatestSnapshot(kind JobKind) (JobSnapshot, bool) {
	job, ok := m.Latest(kind)
	if !ok {
		return JobSnapshot{}, false
	}
	return job.snapshot(), true
}

func newJobID() string {
	var b [6]byte
	_, _ = rand.Read(b[:])
	return hex.EncodeToString(b[:])
}

// pumpLines scans a stream line-by-line, converts ANSI, and appends to the job.
// errPrefix flags stderr so we can tint stderr lines a touch differently.
func pumpLines(r io.Reader, job *Job, wg *sync.WaitGroup, isStderr bool) {
	defer wg.Done()
	scanner := bufio.NewScanner(r)
	scanner.Buffer(make([]byte, 0, 8192), 1024*1024)
	for scanner.Scan() {
		raw := scanner.Text()
		htmlLine := ansiToHTML(raw)
		if isStderr && htmlLine == "" {
			continue
		}
		if isStderr && !strings.Contains(htmlLine, "color:") {
			// Lightly tint untagged stderr so warnings/errors stand out.
			htmlLine = `<span style="color:#fca5a5">` + htmlLine + `</span>`
		}
		job.appendLine(htmlLine)
	}
}

// statusBadgeHTML renders the status pill HTML used by the SSE "status" event
// and by the initial server-side render of the log header.
func statusBadgeHTML(status JobStatus, note string) string {
	var label, color string
	switch status {
	case StatusRunning:
		label, color = "运行中", "#eab308"
	case StatusSuccess:
		label, color = "成功", "#22c55e"
	case StatusFailed:
		label, color = "失败", "#ef4444"
	case StatusCanceled:
		label, color = "已取消", "#9aa3b2"
	default:
		label, color = string(status), "#9aa3b2"
	}
	var sb strings.Builder
	sb.WriteString(`<span class="job-pill" style="color:`)
	sb.WriteString(color)
	sb.WriteString(`;border-color:`)
	sb.WriteString(color)
	sb.WriteString(`33;background:`)
	sb.WriteString(color)
	sb.WriteString(`14">`)
	sb.WriteString(escapeHTML(label))
	if note != "" {
		sb.WriteString(`<span class="job-pill-note">`)
		sb.WriteString(escapeHTML(note))
		sb.WriteString(`</span>`)
	}
	sb.WriteString(`</span>`)
	return sb.String()
}

func escapeHTML(s string) string {
	r := strings.NewReplacer(
		"&", "&amp;",
		"<", "&lt;",
		">", "&gt;",
		`"`, "&quot;",
	)
	return r.Replace(s)
}
