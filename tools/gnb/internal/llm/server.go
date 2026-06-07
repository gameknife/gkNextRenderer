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
	PID       int
	Host      string
	Port      int
	Model     string // model id loaded by the running server (read from PID file)
	ContextN  int
	Parallel  int
	Reasoning string
	Running   bool
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
		if len(lines) >= 5 {
			line := strings.TrimSpace(lines[4])
			if strings.HasPrefix(line, "ctx:") {
				if ctxN, err := strconv.Atoi(strings.TrimPrefix(line, "ctx:")); err == nil {
					info.ContextN = ctxN
				}
			} else {
				info.Reasoning = strings.TrimPrefix(line, "reasoning:")
			}
		}
		if len(lines) >= 6 {
			line := strings.TrimSpace(lines[5])
			if strings.HasPrefix(line, "parallel:") {
				if parallel, err := strconv.Atoi(strings.TrimPrefix(line, "parallel:")); err == nil {
					info.Parallel = parallel
				}
			} else {
				info.Reasoning = strings.TrimPrefix(line, "reasoning:")
			}
		}
		if len(lines) >= 7 {
			info.Reasoning = strings.TrimPrefix(strings.TrimSpace(lines[6]), "reasoning:")
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
		if (info.Model == "" || info.Model == active.ID) && info.ContextN == active.ContextN && info.Parallel == 1 && info.Reasoning == "auto" {
			return info, nil
		}
		console.Info("llama-server mode changed (model %s -> %s, ctx %d -> %d, parallel %d -> 1, reasoning %s -> auto); restarting", info.Model, active.ID, info.ContextN, active.ContextN, info.Parallel, info.Reasoning)
		if err := s.Stop(); err != nil {
			return info, fmt.Errorf("stop stale llama-server: %w", err)
		}
	} else if info.PID != 0 {
		_ = os.Remove(s.layout.PIDFile)
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
		"--parallel", "1",
		"--n-gpu-layers", strconv.Itoa(s.cfg.Server.GPULayers),
		"--jinja", // Gemma chat template lives in the GGUF
		"--reasoning", "auto",
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

	pidContent := fmt.Sprintf("%d\n%s\n%d\n%s\nctx:%d\nparallel:1\nreasoning:auto\n", pid, s.cfg.Server.Host, s.cfg.Server.Port, active.ID, active.ContextN)
	if err := os.WriteFile(s.layout.PIDFile, []byte(pidContent), 0o644); err != nil {
		return info, err
	}

	if err := waitHealthy(ctx, s.cfg.Server.Host, s.cfg.Server.Port, 90*time.Second, pid); err != nil {
		_ = os.Remove(s.layout.PIDFile)
		return info, fmt.Errorf("llama-server did not become ready: %w (see %s)", err, logPath)
	}
	return ServerInfo{PID: pid, Host: s.cfg.Server.Host, Port: s.cfg.Server.Port, Model: active.ID, ContextN: active.ContextN, Parallel: 1, Reasoning: "auto", Running: true}, nil
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

func waitHealthy(ctx context.Context, host string, port int, max time.Duration, pid int) error {
	deadline := time.Now().Add(max)
	for {
		if ctx.Err() != nil {
			return ctx.Err()
		}
		if time.Now().After(deadline) {
			return fmt.Errorf("timeout after %s", max)
		}
		if pid != 0 && !processAlive(pid) {
			return fmt.Errorf("process %d exited before /health became ready", pid)
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
