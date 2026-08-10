package validate

import (
	"bufio"
	"context"
	"crypto/rand"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"net"
	"os"
	"os/exec"
	"path/filepath"
	"strconv"
	"strings"
	"time"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/console"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/platform"
)

type Options struct {
	RepoRoot, Preset, Target, Scene, Script, Report string
	Width, Height                                   int
	Visible, SyncValidation                         bool
	Args                                            []string
	Env                                             []string
}
type Script struct {
	Name, Target, Scene string
	Defaults            struct{ WaitFrames, StepTimeoutMs int }
	Viewport            struct{ Width, Height int }
	Steps               []map[string]any
}
type Report struct {
	Name        string           `json:"name"`
	Passed      bool             `json:"passed"`
	StartedAt   string           `json:"startedAt"`
	FinishedAt  string           `json:"finishedAt"`
	Steps       []map[string]any `json:"steps"`
	Screenshots []string         `json:"screenshots"`
}
type client struct {
	conn  net.Conn
	rw    *bufio.ReadWriter
	token string
	next  int
}

func Run(ctx context.Context, o Options) error {
	data, err := os.ReadFile(o.Script)
	if err != nil {
		return err
	}
	var s Script
	if err = json.Unmarshal(data, &s); err != nil {
		return err
	}
	return run(ctx, o, s)
}

func Shot(ctx context.Context, o Options, frames int, ui bool, out string) error {
	if frames <= 0 {
		frames = 90
	}
	s := Script{Name: "agent_validation"}
	s.Defaults.StepTimeoutMs = 30000
	s.Steps = []map[string]any{{"type": "wait-until", "query": "engine.status", "op": "eq", "value": "Running", "timeoutMs": 30000}, {"type": "wait-frames", "n": frames}, {"type": "screenshot", "out": out, "ui": ui}, {"type": "quit"}}
	return run(ctx, o, s)
}

// Trace runs a target to a stable frame without creating a screenshot. Callers
// pass --asset-trace through Options.Args so the engine records runtime coverage.
func Trace(ctx context.Context, o Options, frames int) error {
	if frames <= 0 {
		frames = 120
	}
	s := Script{Name: "asset_trace"}
	s.Defaults.StepTimeoutMs = 30000
	s.Steps = []map[string]any{{"type": "wait-until", "query": "engine.status", "op": "eq", "value": "Running", "timeoutMs": 30000}, {"type": "wait-frames", "n": frames}, {"type": "quit"}}
	return run(ctx, o, s)
}

func run(ctx context.Context, o Options, s Script) (retErr error) {
	if o.Target == "" {
		o.Target = s.Target
	}
	if o.Target == "" {
		o.Target = "gkNextRenderer"
	}
	if o.Scene == "" {
		o.Scene = s.Scene
	}
	if o.Width == 0 {
		o.Width = s.Viewport.Width
	}
	if o.Height == 0 {
		o.Height = s.Viewport.Height
	}
	bin := platform.BinDir(o.RepoRoot, o.Preset)
	exe := platform.ExecutablePath(bin, o.Target)
	if _, err := os.Stat(exe); err != nil {
		return fmt.Errorf("executable not found: %s\n请先运行 `gnb build %s`", exe, o.Target)
	}
	ln, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		return err
	}
	endpoint := ln.Addr().String()
	_ = ln.Close()
	b := make([]byte, 24)
	_, _ = rand.Read(b)
	token := hex.EncodeToString(b)
	args := []string{"--agent-validation", "--agent-control=" + endpoint, "--agent-control-token=" + token}
	if o.Visible {
		args = append(args, "--agent-visible-window")
	}
	if o.SyncValidation {
		args = append(args, "--sync-validation")
	}
	if o.Width > 0 {
		args = append(args, fmt.Sprintf("--width=%d", o.Width))
	}
	if o.Height > 0 {
		args = append(args, fmt.Sprintf("--height=%d", o.Height))
	}
	if o.Scene != "" {
		args = append(args, "--load-scene="+o.Scene)
	}
	args = append(args, o.Args...)
	console.CommandLine(exe + " " + strings.Join(args, " "))
	cmd := exec.CommandContext(ctx, exe, args...)
	cmd.Dir = filepath.Dir(exe)
	cmd.Env = append(os.Environ(), o.Env...)
	cmd.Stdout, cmd.Stderr = os.Stdout, os.Stderr
	if err = cmd.Start(); err != nil {
		return err
	}
	defer func() {
		if cmd.Process != nil {
			_ = cmd.Process.Kill()
		}
		_ = cmd.Wait()
	}()
	var conn net.Conn
	deadline := time.Now().Add(30 * time.Second)
	for time.Now().Before(deadline) {
		conn, err = net.DialTimeout("tcp", endpoint, 200*time.Millisecond)
		if err == nil {
			break
		}
		if cmd.ProcessState != nil && cmd.ProcessState.Exited() {
			break
		}
		time.Sleep(50 * time.Millisecond)
	}
	if err != nil {
		return fmt.Errorf("connect engine control %s: %w", endpoint, err)
	}
	c := &client{conn: conn, rw: bufio.NewReadWriter(bufio.NewReader(conn), bufio.NewWriter(conn)), token: token}
	defer conn.Close()
	if _, err = c.call("handshake", map[string]any{}); err != nil {
		return err
	}
	rep := Report{Name: s.Name, Passed: true, StartedAt: time.Now().Format(time.RFC3339), Steps: []map[string]any{}, Screenshots: []string{}}
	for i, step := range s.Steps {
		entry, e := execute(c, step, s, o)
		entry["idx"] = i
		if e != nil {
			entry["passed"] = false
			entry["message"] = e.Error()
			rep.Passed = false
		}
		rep.Steps = append(rep.Steps, entry)
		if p, ok := entry["out"].(string); ok && entry["passed"] == true {
			rep.Screenshots = append(rep.Screenshots, p)
		}
		if e != nil && boolv(step, "fatal", false) {
			break
		}
	}
	rep.FinishedAt = time.Now().Format(time.RFC3339)
	if o.Report != "" {
		if err = writeReport(o.Report, rep); err != nil {
			return err
		}
	}
	if rep.Passed {
		return nil
	}
	return fmt.Errorf("agent validation failed")
}

func (c *client) call(method string, params any) (any, error) {
	_ = c.conn.SetDeadline(time.Now().Add(30 * time.Second))
	c.next++
	req := map[string]any{"id": strconv.Itoa(c.next), "token": c.token, "method": method, "params": params}
	b, _ := json.Marshal(req)
	if _, err := c.rw.Write(append(b, '\n')); err != nil {
		return nil, err
	}
	if err := c.rw.Flush(); err != nil {
		return nil, err
	}
	line, err := c.rw.ReadBytes('\n')
	if err != nil {
		return nil, err
	}
	var r struct {
		Result any `json:"result"`
		Error  any `json:"error"`
	}
	if err = json.Unmarshal(line, &r); err != nil {
		return nil, err
	}
	if r.Error != nil {
		return nil, fmt.Errorf("%v", r.Error)
	}
	return r.Result, nil
}
func execute(c *client, st map[string]any, s Script, o Options) (map[string]any, error) {
	typ := str(st, "type", "")
	e := map[string]any{"type": typ, "passed": true}
	timeout := duration(st, s)
	switch typ {
	case "wait-frames":
		start, er := query(c, "engine.totalFrames")
		if er != nil {
			return e, er
		}
		target := num(start) + numv(st, "n", float64(max(1, s.Defaults.WaitFrames)))
		return e, poll(timeout, func() (bool, error) { v, x := query(c, "engine.totalFrames"); return num(v) >= target, x })
	case "wait-ms":
		time.Sleep(time.Duration(numv(st, "ms", 0)) * time.Millisecond)
		return e, nil
	case "wait-until", "assert":
		v, er := query(c, str(st, "query", ""))
		if er == nil && compare(v, str(st, "op", "eq"), st["value"]) {
			e["actual"] = v
			e["expected"] = st["value"]
			return e, nil
		}
		if typ == "assert" {
			return e, fmt.Errorf("assertion failed: actual=%v", v)
		}
		er = poll(timeout, func() (bool, error) {
			v, x := query(c, str(st, "query", ""))
			e["actual"] = v
			return compare(v, str(st, "op", "eq"), st["value"]), x
		})
		return e, er
	case "screenshot":
		p := str(st, "out", fmt.Sprintf("screenshots/%s_step", s.Name))
		r, er := c.call(typ, map[string]any{
			"out":              p,
			"ui":               boolv(st, "ui", false),
			"accumulateFrames": uint32(numv(st, "accumulateFrames", 0)),
		})
		if er != nil {
			return e, er
		}
		m, _ := r.(map[string]any)
		path, _ := m["path"].(string)
		e["out"] = path
		er = poll(timeout, func() (bool, error) { _, x := os.Stat(path); return x == nil, nil })
		return e, er
	case "log":
		e["message"] = str(st, "message", str(st, "text", ""))
		return e, nil
	case "quit":
		_, er := c.call(typ, map[string]any{"exitCode": 0})
		return e, er
	case "cvar":
		p := map[string]any{"name": str(st, "name", "")}
		if v, ok := st["set"]; ok {
			p["set"] = v
		}
		_, er := c.call(typ, p)
		return e, er
	case "exec":
		_, er := c.call(typ, map[string]any{"line": str(st, "line", "")})
		return e, er
	case "key", "text", "mouse-move", "mouse-button", "click", "drag", "scroll":
		p := clone(st)
		delete(p, "type")
		normalizePoints(p, o.Width, o.Height)
		_, er := c.call(typ, p)
		return e, er
	default:
		return e, fmt.Errorf("unknown step type %q", typ)
	}
}
func query(c *client, q string) (any, error) { return c.call("query", map[string]any{"query": q}) }
func poll(d time.Duration, f func() (bool, error)) error {
	end := time.Now().Add(d)
	for {
		ok, e := f()
		if ok {
			return nil
		}
		if e != nil {
			return e
		}
		if time.Now().After(end) {
			return fmt.Errorf("timed out after %s", d)
		}
		time.Sleep(16 * time.Millisecond)
	}
}
func compare(a any, op string, b any) bool {
	switch op {
	case "eq":
		return fmt.Sprint(a) == fmt.Sprint(b)
	case "ne":
		return fmt.Sprint(a) != fmt.Sprint(b)
	case "gt":
		return num(a) > num(b)
	case "ge":
		return num(a) >= num(b)
	case "lt":
		return num(a) < num(b)
	case "le":
		return num(a) <= num(b)
	case "contains":
		return strings.Contains(fmt.Sprint(a), fmt.Sprint(b))
	}
	return false
}
func normalizePoints(p map[string]any, w, h int) {
	for _, k := range []string{"to", "at", "from"} {
		v, ok := p[k].(map[string]any)
		if !ok {
			continue
		}
		if n, ok := v["norm"].([]any); ok && len(n) > 1 {
			p[k] = []any{num(n[0]) * float64(w), num(n[1]) * float64(h)}
		} else if px, ok := v["px"].([]any); ok {
			p[k] = px
		}
	}
}
func writeReport(path string, r Report) error {
	if !filepath.IsAbs(path) {
		path, _ = filepath.Abs(path)
	}
	if err := os.MkdirAll(filepath.Dir(path), 0755); err != nil {
		return err
	}
	b, _ := json.MarshalIndent(r, "", "  ")
	return os.WriteFile(path, b, 0644)
}
func duration(st map[string]any, s Script) time.Duration {
	ms := numv(st, "timeoutMs", float64(s.Defaults.StepTimeoutMs))
	if ms <= 0 {
		ms = 8000
	}
	return time.Duration(ms) * time.Millisecond
}
func clone(m map[string]any) map[string]any {
	r := map[string]any{}
	for k, v := range m {
		r[k] = v
	}
	return r
}
func str(m map[string]any, k, d string) string {
	if v, ok := m[k].(string); ok {
		return v
	}
	return d
}
func boolv(m map[string]any, k string, d bool) bool {
	if v, ok := m[k].(bool); ok {
		return v
	}
	return d
}
func numv(m map[string]any, k string, d float64) float64 {
	if v, ok := m[k]; ok {
		return num(v)
	}
	return d
}
func num(v any) float64 {
	switch x := v.(type) {
	case float64:
		return x
	case int:
		return float64(x)
	case json.Number:
		r, _ := x.Float64()
		return r
	case string:
		r, _ := strconv.ParseFloat(x, 64)
		return r
	}
	return 0
}
func max(a, b int) int {
	if a > b {
		return a
	}
	return b
}
