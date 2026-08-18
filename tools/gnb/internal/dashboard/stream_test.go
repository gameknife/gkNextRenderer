package dashboard

import (
	"net/http/httptest"
	"strings"
	"testing"
	"time"
)

func TestHandleJobStreamReturnsNoContentForCompletedCaughtUpClient(t *testing.T) {
	server := &Server{jobs: NewJobManager()}
	job := &Job{
		ID:       "done-job",
		Kind:     JobBuild,
		Target:   "all",
		Command:  "cmake --build out/build/windows",
		status:   StatusSuccess,
		exitNote: "exit 0",
		lines: []string{
			`<span>$ cmake --build out/build/windows</span>`,
			`done`,
		},
		subs: map[chan JobEvent]struct{}{},
	}
	server.jobs.jobs[job.ID] = job

	req := httptest.NewRequest("GET", "/jobs/"+job.ID+"/stream?from=2", nil)
	req.SetPathValue("id", job.ID)
	rec := httptest.NewRecorder()

	server.handleJobStream(rec, req)

	if rec.Code != 204 {
		t.Fatalf("status = %d, want 204", rec.Code)
	}
	if body := rec.Body.String(); body != "" {
		t.Fatalf("expected empty body for caught-up completed stream, got %q", body)
	}
}

func TestHandleJobStreamOnlyReplaysMissingLines(t *testing.T) {
	server := &Server{jobs: NewJobManager()}
	finishedAt := time.Unix(90, 0)
	job := &Job{
		ID:         "partial-job",
		Kind:       JobBuild,
		Target:     "all",
		Command:    "cmake --build out/build/windows",
		status:     StatusSuccess,
		finishedAt: finishedAt,
		exitNote:   "exit 0",
		lines: []string{
			`<span>line-0</span>`,
			`<span>line-1</span>`,
			`<span>line-2</span>`,
		},
		subs: map[chan JobEvent]struct{}{},
	}
	server.jobs.jobs[job.ID] = job

	req := httptest.NewRequest("GET", "/jobs/"+job.ID+"/stream?from=1", nil)
	req.SetPathValue("id", job.ID)
	rec := httptest.NewRecorder()

	server.handleJobStream(rec, req)

	body := rec.Body.String()
	if rec.Code != 200 {
		t.Fatalf("status = %d, want 200", rec.Code)
	}
	if strings.Contains(body, "line-0") {
		t.Fatalf("unexpected replay of already-rendered line: %q", body)
	}
	if !strings.Contains(body, "line-1") || !strings.Contains(body, "line-2") {
		t.Fatalf("missing expected replay lines: %q", body)
	}
	if !strings.Contains(body, "event: done\ndata: 90") {
		t.Fatalf("missing finished timestamp in done event: %q", body)
	}
}

func TestLogPanelTemplateRendersBufferedLinesAndOnlyStreamsRunningJobs(t *testing.T) {
	server, err := New(Options{})
	if err != nil {
		t.Fatalf("New() error = %v", err)
	}

	render := func(snap JobSnapshot) string {
		var sb strings.Builder
		if err := server.tpl.ExecuteTemplate(&sb, "log_panel", snap); err != nil {
			t.Fatalf("ExecuteTemplate() error = %v", err)
		}
		return sb.String()
	}

	runningHTML := render(JobSnapshot{
		ID:        "running-job",
		Kind:      JobBuild,
		Target:    "all",
		Status:    StatusRunning,
		Lines:     []string{`<span>line-a</span>`, `<span>line-b</span>`},
		StartedAt: jobTime(),
	})
	if !strings.Contains(runningHTML, `data-stream="/jobs/running-job/stream?from=2"`) {
		t.Fatalf("running job missing stream URL with offset: %q", runningHTML)
	}
	if !strings.Contains(runningHTML, `data-started-at="1"`) {
		t.Fatalf("running job missing started-at attribute: %q", runningHTML)
	}
	if !strings.Contains(runningHTML, `data-elapsed>`) || !strings.Contains(runningHTML, ` min `) || !strings.Contains(runningHTML, ` sec</span>`) {
		t.Fatalf("running job missing formatted elapsed text: %q", runningHTML)
	}
	if !strings.Contains(runningHTML, `<div class="log-line"><span>line-a</span></div>`) {
		t.Fatalf("running job missing buffered line render: %q", runningHTML)
	}

	finishedHTML := render(JobSnapshot{
		ID:         "finished-job",
		Kind:       JobBuild,
		Target:     "all",
		Status:     StatusSuccess,
		ExitNote:   "exit 0",
		Lines:      []string{`<span>line-x</span>`},
		StartedAt:  jobTime(),
		FinishedAt: jobTime().Add(125 * time.Second),
	})
	if strings.Contains(finishedHTML, `data-stream="/jobs/finished-job/stream`) {
		t.Fatalf("finished job should not keep stream endpoint attached: %q", finishedHTML)
	}
	if !strings.Contains(finishedHTML, `data-finished-at="126"`) {
		t.Fatalf("finished job missing finished-at attribute: %q", finishedHTML)
	}
	if !strings.Contains(finishedHTML, `data-elapsed>2 min 5 sec</span>`) {
		t.Fatalf("finished job missing formatted elapsed text: %q", finishedHTML)
	}
	if !strings.Contains(finishedHTML, `<div class="log-line"><span>line-x</span></div>`) {
		t.Fatalf("finished job missing buffered line render: %q", finishedHTML)
	}
}

func TestFormatElapsedDuration(t *testing.T) {
	tests := []struct {
		name string
		in   time.Duration
		want string
	}{
		{name: "sub-second", in: 900 * time.Millisecond, want: "0 min 0 sec"},
		{name: "seconds only", in: 59 * time.Second, want: "0 min 59 sec"},
		{name: "minutes and seconds", in: 125 * time.Second, want: "2 min 5 sec"},
		{name: "negative", in: -3 * time.Second, want: "0 min 0 sec"},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			if got := formatElapsedDuration(tt.in); got != tt.want {
				t.Fatalf("formatElapsedDuration(%v) = %q, want %q", tt.in, got, tt.want)
			}
		})
	}
}

func jobTime() (t time.Time) {
	return time.Unix(1, 0)
}
