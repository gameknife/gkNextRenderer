// Package dashboard serves a local web UI that visualizes and operates on the
// .spec/ interactive workflow files (TODO.md, journal/, blockers/, ARCHIVE.md).
//
// The server is read-mostly: navigation and rendering happens server-side with
// htmx-driven partial swaps. Mutations call into the spec package directly.
package dashboard

import (
	"context"
	"embed"
	"errors"
	"fmt"
	"html/template"
	"net"
	"net/http"
	"os"
	"os/exec"
	"os/user"
	"runtime"
	"strings"
	"time"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/config"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/spec"
)

//go:embed templates/*.html
var templateFS embed.FS

// Options configures the dashboard server.
type Options struct {
	RepoRoot string
	Port     int           // listen on localhost:<port>
	NoOpen   bool          // skip auto-launching the browser
	Version  string        // gnb version string for display
	Preset   string        // CMake preset string for display
	Config   config.Config // loaded gnb.toml — provides targets list
}

// Server holds runtime state for the dashboard.
type Server struct {
	opts Options
	tpl  *template.Template
	jobs *JobManager
}

// New constructs a Server. Template parsing happens eagerly so problems surface
// before binding the port.
func New(opts Options) (*Server, error) {
	tpl, err := template.New("dashboard").
		Funcs(templateFuncs()).
		ParseFS(templateFS, "templates/*.html")
	if err != nil {
		return nil, fmt.Errorf("parse templates: %w", err)
	}
	return &Server{opts: opts, tpl: tpl, jobs: NewJobManager()}, nil
}

// Run binds the configured port and serves until ctx is canceled.
func (s *Server) Run(ctx context.Context) error {
	addr := fmt.Sprintf("127.0.0.1:%d", s.opts.Port)
	ln, err := net.Listen("tcp", addr)
	if err != nil {
		return fmt.Errorf("bind %s: %w", addr, err)
	}
	mux := s.routes()
	httpSrv := &http.Server{
		Handler:           mux,
		ReadHeaderTimeout: 5 * time.Second,
	}

	fmt.Printf("dashboard listening on http://%s\n", addr)
	if !s.opts.NoOpen {
		go openBrowser("http://" + addr)
	}

	errCh := make(chan error, 1)
	go func() {
		if err := httpSrv.Serve(ln); err != nil && !errors.Is(err, http.ErrServerClosed) {
			errCh <- err
		}
	}()

	select {
	case <-ctx.Done():
		shutdownCtx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
		defer cancel()
		_ = httpSrv.Shutdown(shutdownCtx)
		return nil
	case err := <-errCh:
		return err
	}
}

// openBrowser is best-effort; failure is silent (URL is already printed).
func openBrowser(url string) {
	var cmd *exec.Cmd
	switch runtime.GOOS {
	case "windows":
		cmd = exec.Command("rundll32", "url.dll,FileProtocolHandler", url)
	case "darwin":
		cmd = exec.Command("open", url)
	default:
		cmd = exec.Command("xdg-open", url)
	}
	cmd.Stdout = nil
	cmd.Stderr = nil
	_ = cmd.Start()
}

// templateFuncs exposes the small set of helpers the templates need.
func templateFuncs() template.FuncMap {
	return template.FuncMap{
		"statusIcon": func(s string) string {
			switch s {
			case " ":
				return "○"
			case "/":
				return "◐"
			case "x":
				return "●"
			case "!":
				return "⛔"
			}
			return "?"
		},
		"statusClass": func(s string) string {
			switch s {
			case " ":
				return "pending"
			case "/":
				return "doing"
			case "x":
				return "done"
			case "!":
				return "blocked"
			}
			return ""
		},
		"trim": strings.TrimSpace,
		"shortHash": func(s string) string {
			if len(s) > 8 {
				return s[:8]
			}
			return s
		},
		"date":     func(t time.Time) string { return t.Format("2006-01-02 15:04") },
		"safeHTML": func(s string) template.HTML { return template.HTML(s) },
		"userInitial": func() string {
			if u, err := user.Current(); err == nil && u.Username != "" {
				name := u.Username
				if idx := strings.LastIndexAny(name, "\\/"); idx >= 0 {
					name = name[idx+1:]
				}
				if name != "" {
					return strings.ToUpper(name[:1])
				}
			}
			return "U"
		},
		"sectionTint": func(k spec.SectionKind) template.CSS {
			switch k {
			case spec.SectionNext:
				return template.CSS("rgba(34, 197, 94, 0.14)")
			case spec.SectionBacklog:
				return template.CSS("rgba(59, 130, 246, 0.14)")
			case spec.SectionRecent:
				return template.CSS("rgba(107, 115, 132, 0.18)")
			}
			return template.CSS("rgba(59, 130, 246, 0.12)")
		},
		"sectionColor": func(k spec.SectionKind) template.CSS {
			switch k {
			case spec.SectionNext:
				return template.CSS("#22c55e")
			case spec.SectionBacklog:
				return template.CSS("#3b82f6")
			case spec.SectionRecent:
				return template.CSS("#9aa3b2")
			}
			return template.CSS("#3b82f6")
		},
		"sectionIcon": func(k spec.SectionKind) string {
			switch k {
			case spec.SectionNext:
				return `<svg width="13" height="13" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2"><polyline points="13 17 18 12 13 7"/><polyline points="6 17 11 12 6 7"/></svg>`
			case spec.SectionBacklog:
				return `<svg width="13" height="13" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><rect x="3" y="4" width="18" height="18" rx="2"/><line x1="16" y1="2" x2="16" y2="6"/><line x1="8" y1="2" x2="8" y2="6"/><line x1="3" y1="10" x2="21" y2="10"/></svg>`
			case spec.SectionRecent:
				return `<svg width="13" height="13" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2"><polyline points="20 6 9 17 4 12"/></svg>`
			}
			return `<svg width="13" height="13" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><circle cx="12" cy="12" r="9"/></svg>`
		},
		"jobLines": func(s JobSnapshot) template.HTML {
			// Pre-render the existing buffer for the initial page load; SSE
			// then appends new lines on top of this.
			var sb strings.Builder
			for _, l := range s.Lines {
				sb.WriteString(`<div class="log-line">`)
				sb.WriteString(l)
				sb.WriteString("</div>")
			}
			return template.HTML(sb.String())
		},
		"jobStatusBadge": func(s JobSnapshot) template.HTML {
			return template.HTML(statusBadgeHTML(s.Status, s.ExitNote))
		},
		"jobIsRunning": func(s JobSnapshot) bool { return s.Status == StatusRunning },
		"jobIsTerminal": func(s JobSnapshot) bool {
			return s.Status == StatusSuccess || s.Status == StatusFailed || s.Status == StatusCanceled
		},
		"jobElapsed": func(s JobSnapshot) string {
			end := s.FinishedAt
			if end.IsZero() {
				end = time.Now()
			}
			return formatElapsedDuration(end.Sub(s.StartedAt))
		},
		"emptyHint": func(k spec.SectionKind) string {
			switch k {
			case spec.SectionNext:
				return "暂无任务（在左侧表单添加 →）"
			case spec.SectionBacklog:
				return "暂无最近完成的任务"
			case spec.SectionRecent:
				return "暂无最近完成的任务"
			}
			return "(暂无)"
		},
	}
}

func formatElapsedDuration(d time.Duration) string {
	if d < 0 {
		d = 0
	}
	totalSeconds := int(d / time.Second)
	minutes := totalSeconds / 60
	seconds := totalSeconds % 60
	return fmt.Sprintf("%d min %d sec", minutes, seconds)
}

// ensureStatusDir is unused but kept to make adding new mkdir-on-demand cases trivial.
var _ = os.MkdirAll
