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
	"runtime"
	"strings"
	"time"
)

//go:embed templates/*.html
var templateFS embed.FS

// Options configures the dashboard server.
type Options struct {
	RepoRoot string
	Port     int    // listen on localhost:<port>
	NoOpen   bool   // skip auto-launching the browser
	Version  string // gnb version string for display
	Preset   string // CMake preset string for display
}

// Server holds runtime state for the dashboard.
type Server struct {
	opts Options
	tpl  *template.Template
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
	return &Server{opts: opts, tpl: tpl}, nil
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
		"date": func(t time.Time) string { return t.Format("2006-01-02 15:04") },
	}
}

// ensureStatusDir is unused but kept to make adding new mkdir-on-demand cases trivial.
var _ = os.MkdirAll
