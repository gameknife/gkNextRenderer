package llm

import (
	"context"
	"fmt"
	"io"
	"net"
	"net/http"
	"os"
	"os/exec"
	"path/filepath"
	"strconv"
	"strings"
	"time"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/config"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/console"
)

// Server tracks a running llama-server instance via a PID file. The file
// stores three lines: pid, host, port. This lets gnb commands across
// invocations discover and reuse a running server.
type Server struct {
	repoRoot string
	cfg      config.LLMConfig
	layout   Layout
}

func NewServer(repoRoot string, cfg config.LLMConfig) *Server {
	return &Server{
		repoRoot: repoRoot,
		cfg:      cfg,
		layout:   ResolveLayout(repoRoot, cfg),
	}
}

type ServerInfo struct {
	PID     int
	Host    string
	Port    int
	Model   string // model id loaded by the running server (read from PID file)
	Running bool
}

func (s *Server) BaseURL() string {
	return fmt.Sprintf("http://%s:%d", s.cfg.Server.Host, s.cfg.Server.Port)
}

func (s *Server) Status() ServerInfo {
	info := ServerInfo{Host: s.cfg.Server.Host, Port: s.cfg.Server.Port}
	data, err := os.ReadFile(s.layout.PIDFile)
	if err == nil {
		lines := strings.Split(strings.TrimSpace(string(data)), "\n")
		if len(lines) > 0 {
			if pid, err := strconv.Atoi(strings.TrimSpace(lines[0])); err == nil {
				info.PID = pid
			}
		}
		if len(lines) >= 3 {
			info.Host = strings.TrimSpace(lines[1])
			if port, err := strconv.Atoi(strings.TrimSpace(lines[2])); err == nil {
				info.Port = port
			}
		}
		if len(lines) >= 4 {
			info.Model = strings.TrimSpace(lines[3])
		}
	}
	info.Running = s.healthyAt(info.Host, info.Port)
	return info
}

// EnsureRunning starts llama-server if it isn't already responding. Blocks
// until the /health endpoint returns OK or the timeout elapses. If a server
// is already running with a different model than the configured active one,
// it is stopped and restarted so callers always observe the requested model.
func (s *Server) EnsureRunning(ctx context.Context) (ServerInfo, error) {
	active := s.cfg.ActiveModel()
	if active.ID == "" {
		return ServerInfo{}, fmt.Errorf("no LLM model configured (check [external.llm].active in gnb.toml)")
	}

	info := s.Status()
	if info.Running {
		if info.Model == "" || info.Model == active.ID {
			return info, nil
		}
		console.Info("active model changed (%s -> %s); restarting llama-server", info.Model, active.ID)
		if err := s.Stop(); err != nil {
			return info, fmt.Errorf("stop stale llama-server: %w", err)
		}
	}

	if err := EnsureBinaries(s.repoRoot, s.cfg); err != nil {
		return info, err
	}
	if err := EnsureModelEntry(s.repoRoot, s.cfg, active); err != nil {
		return info, err
	}
	if err := os.MkdirAll(s.layout.RunDir, 0o755); err != nil {
		return info, err
	}

	bin := s.layout.ServerBinary()
	modelPath := s.layout.ModelPath(active)
	args := []string{
		"--model", modelPath,
		"--host", s.cfg.Server.Host,
		"--port", strconv.Itoa(s.cfg.Server.Port),
		"--ctx-size", strconv.Itoa(active.ContextN),
		"--n-gpu-layers", strconv.Itoa(s.cfg.Server.GPULayers),
		"--jinja", // Gemma chat template lives in the GGUF
	}
	console.Info("starting llama-server (%s) model=%s on %s:%d", filepath.Base(bin), active.ID, s.cfg.Server.Host, s.cfg.Server.Port)
	cmd := exec.Command(bin, args...)
	cmd.Dir = s.layout.BinDir
	logPath := filepath.Join(s.layout.RunDir, "server.log")
	logFile, err := os.OpenFile(logPath, os.O_CREATE|os.O_WRONLY|os.O_TRUNC, 0o644)
	if err != nil {
		return info, err
	}
	cmd.Stdout = logFile
	cmd.Stderr = logFile
	detach(cmd)
	if err := cmd.Start(); err != nil {
		logFile.Close()
		return info, fmt.Errorf("start llama-server: %w", err)
	}
	pid := cmd.Process.Pid
	_ = cmd.Process.Release()

	pidContent := fmt.Sprintf("%d\n%s\n%d\n%s\n", pid, s.cfg.Server.Host, s.cfg.Server.Port, active.ID)
	if err := os.WriteFile(s.layout.PIDFile, []byte(pidContent), 0o644); err != nil {
		return info, err
	}

	if err := waitHealthy(ctx, s.cfg.Server.Host, s.cfg.Server.Port, 90*time.Second); err != nil {
		return info, fmt.Errorf("llama-server did not become ready: %w (see %s)", err, logPath)
	}
	return ServerInfo{PID: pid, Host: s.cfg.Server.Host, Port: s.cfg.Server.Port, Model: active.ID, Running: true}, nil
}

func (s *Server) Stop() error {
	info := s.Status()
	if info.PID == 0 {
		_ = os.Remove(s.layout.PIDFile)
		return nil
	}
	if err := killPID(info.PID); err != nil {
		return err
	}
	_ = os.Remove(s.layout.PIDFile)
	return nil
}

func (s *Server) healthyAt(host string, port int) bool {
	if port == 0 {
		return false
	}
	if !portReachable(host, port, 200*time.Millisecond) {
		return false
	}
	ctx, cancel := context.WithTimeout(context.Background(), 1*time.Second)
	defer cancel()
	req, err := http.NewRequestWithContext(ctx, "GET", fmt.Sprintf("http://%s:%d/health", host, port), nil)
	if err != nil {
		return false
	}
	resp, err := http.DefaultClient.Do(req)
	if err != nil {
		return false
	}
	defer resp.Body.Close()
	_, _ = io.Copy(io.Discard, resp.Body)
	return resp.StatusCode == 200
}

func portReachable(host string, port int, timeout time.Duration) bool {
	conn, err := net.DialTimeout("tcp", net.JoinHostPort(host, strconv.Itoa(port)), timeout)
	if err != nil {
		return false
	}
	_ = conn.Close()
	return true
}

func waitHealthy(ctx context.Context, host string, port int, max time.Duration) error {
	deadline := time.Now().Add(max)
	for {
		if ctx.Err() != nil {
			return ctx.Err()
		}
		if time.Now().After(deadline) {
			return fmt.Errorf("timeout after %s", max)
		}
		c, err := net.DialTimeout("tcp", net.JoinHostPort(host, strconv.Itoa(port)), 500*time.Millisecond)
		if err == nil {
			c.Close()
			req, _ := http.NewRequestWithContext(ctx, "GET", fmt.Sprintf("http://%s:%d/health", host, port), nil)
			resp, err := http.DefaultClient.Do(req)
			if err == nil {
				_, _ = io.Copy(io.Discard, resp.Body)
				resp.Body.Close()
				if resp.StatusCode == 200 {
					return nil
				}
			}
		}
		time.Sleep(500 * time.Millisecond)
	}
}
