// Build/Run/Test jobs: start, cancel, and stream (SSE) long-running gnb
// subprocesses, plus the JobSpec builders that turn a target into a command.
// Job execution and buffering lives in jobs.go.
package dashboard

import (
	"fmt"
	"net/http"
	"path/filepath"
	"runtime"
	"strconv"
	"strings"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/platform"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/remoteplay"
)

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
	case JobRemote:
		var err error
		extraArgs := strings.Fields(r.FormValue("extraArgs"))
		spec, err = s.remoteJobSpec(target, remoteplay.Options{
			Bind:          strings.TrimSpace(r.FormValue("bind")),
			Resolution:    strings.TrimSpace(r.FormValue("resolution")),
			Encoder:       strings.TrimSpace(r.FormValue("encoder")),
			HttpPort:      parseUint32OrDefault(r.FormValue("httpPort"), 8088),
			SignalingPort: parseUint32OrDefault(r.FormValue("signalingPort"), 8089),
			BitrateKbps:   parseUint32OrDefault(r.FormValue("bitrateKbps"), 0),
			Fps:           parseUint32OrDefault(r.FormValue("fps"), 30),
			ShowWindow:    r.FormValue("showWindow") == "1",
			Scene:         strings.TrimSpace(r.FormValue("scene")),
		}, extraArgs)
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
	s.render(w, "log_panel", s.jobSnapshot(job))
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
	s.render(w, "log_panel", s.jobSnapshot(job))
}

func (s *Server) jobSnapshot(job *Job) JobSnapshot {
	snap := job.snapshot()
	snap.StreamBase = s.streamBaseURL
	return snap
}

func (s *Server) handleJobStreamOptions(w http.ResponseWriter, r *http.Request) {
	if !setStreamCORSHeaders(w, r) {
		http.Error(w, "origin not allowed", http.StatusForbidden)
		return
	}
	w.Header().Set("Access-Control-Allow-Methods", http.MethodGet)
	w.WriteHeader(http.StatusNoContent)
}

func setStreamCORSHeaders(w http.ResponseWriter, r *http.Request) bool {
	origin := strings.TrimSpace(r.Header.Get("Origin"))
	if origin == "" {
		return true
	}
	requestOrigin := "http://" + r.Host
	if origin != requestOrigin &&
		origin != "wails://wails" &&
		!strings.HasPrefix(origin, "http://wails.localhost") {
		return false
	}
	w.Header().Set("Access-Control-Allow-Origin", origin)
	w.Header().Set("Vary", "Origin")
	w.Header().Set("Access-Control-Allow-Private-Network", "true")
	return true
}

// handleJobStream serves Server-Sent Events for one job. The initial buffered
// output is replayed before the live channel takes over. The connection ends
// when the job terminates or the client disconnects.
func (s *Server) handleJobStream(w http.ResponseWriter, r *http.Request) {
	if !setStreamCORSHeaders(w, r) {
		http.Error(w, "origin not allowed", http.StatusForbidden)
		return
	}
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

func (s *Server) remoteJobSpec(target string, opts remoteplay.Options, extraArgs []string) (JobSpec, error) {
	if target == "" {
		return JobSpec{}, fmt.Errorf("请选择要远程启动的 target")
	}
	if strings.TrimSpace(opts.Bind) == "" {
		opts.Bind = "0.0.0.0"
	}
	if strings.TrimSpace(opts.Encoder) == "" {
		opts.Encoder = "auto"
	}
	if opts.HttpPort == 0 {
		opts.HttpPort = 8088
	}
	if opts.SignalingPort == 0 {
		opts.SignalingPort = 8089
	}
	if opts.Fps == 0 {
		opts.Fps = 30
	}
	binDir := platform.BinDir(s.opts.RepoRoot, s.opts.Preset)
	exe := platform.ExecutablePath(binDir, target)
	return JobSpec{
		Kind:       JobRemote,
		Target:     target,
		Name:       exe,
		Args:       remoteplay.RunArgs(opts, extraArgs),
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

func parseUint32OrDefault(raw string, fallback uint32) uint32 {
	raw = strings.TrimSpace(raw)
	if raw == "" {
		return fallback
	}
	value, err := strconv.ParseUint(raw, 10, 32)
	if err != nil {
		return fallback
	}
	return uint32(value)
}
