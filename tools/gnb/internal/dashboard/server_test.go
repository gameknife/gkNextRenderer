package dashboard

import (
	"bufio"
	"context"
	"io"
	"net/http"
	"net/http/httptest"
	"net/url"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"
)

func TestHTTPServerStreamsChatWithoutWailsFlusher(t *testing.T) {
	srv := &Server{
		jobs:  NewJobManager(),
		chats: NewChatStore(filepath.Join(t.TempDir(), "chats.json")),
	}
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	running, err := srv.start(ctx, 0)
	if err != nil {
		t.Fatal(err)
	}

	form := url.Values{
		"message": {""},
	}
	req, err := http.NewRequest(http.MethodPost, running.URL+"/chat/send-stream",
		strings.NewReader(form.Encode()))
	if err != nil {
		t.Fatal(err)
	}
	req.Header.Set("Content-Type", "application/x-www-form-urlencoded")
	req.Header.Set("Origin", "http://wails.localhost")
	resp, err := http.DefaultClient.Do(req)
	if err != nil {
		t.Fatal(err)
	}
	defer resp.Body.Close()
	body, err := io.ReadAll(resp.Body)
	if err != nil {
		t.Fatal(err)
	}
	if resp.StatusCode != http.StatusOK {
		t.Fatalf("status = %d, want 200; body=%s", resp.StatusCode, body)
	}
	if !strings.Contains(string(body), "event: error") {
		t.Fatalf("missing streamed error event: %q", body)
	}
	if got := resp.Header.Get("Access-Control-Allow-Origin"); got != "http://wails.localhost" {
		t.Fatalf("Access-Control-Allow-Origin = %q, want Wails origin", got)
	}
}

func TestStreamEndpointRejectsUnknownOrigin(t *testing.T) {
	srv := &Server{
		jobs:  NewJobManager(),
		chats: NewChatStore(filepath.Join(t.TempDir(), "chats.json")),
	}
	req := httptest.NewRequest(http.MethodPost, "/chat/send-stream",
		strings.NewReader("message="))
	req.Header.Set("Content-Type", "application/x-www-form-urlencoded")
	req.Header.Set("Origin", "https://example.com")
	rec := httptest.NewRecorder()

	srv.handleChatSendStream(rec, req)

	if rec.Code != http.StatusForbidden {
		t.Fatalf("status = %d, want 403", rec.Code)
	}
}

func TestHTTPServerFlushesJobStreamBeforeCompletion(t *testing.T) {
	srv := &Server{jobs: NewJobManager()}
	job := &Job{
		ID:     "live-job",
		Kind:   JobBuild,
		Target: "all",
		status: StatusRunning,
		lines:  []string{"first line"},
		subs:   map[chan JobEvent]struct{}{},
	}
	srv.jobs.jobs[job.ID] = job

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	running, err := srv.start(ctx, 0)
	if err != nil {
		t.Fatal(err)
	}

	reqCtx, stopRequest := context.WithCancel(context.Background())
	defer stopRequest()
	req, err := http.NewRequestWithContext(reqCtx, http.MethodGet,
		running.URL+"/jobs/"+job.ID+"/stream", nil)
	if err != nil {
		t.Fatal(err)
	}
	req.Header.Set("Origin", "http://wails.localhost")

	responseReady := make(chan *http.Response, 1)
	responseErr := make(chan error, 1)
	go func() {
		resp, requestErr := http.DefaultClient.Do(req)
		if requestErr != nil {
			responseErr <- requestErr
			return
		}
		responseReady <- resp
	}()

	var resp *http.Response
	select {
	case resp = <-responseReady:
	case err := <-responseErr:
		t.Fatal(err)
	case <-time.After(2 * time.Second):
		t.Fatal("stream response headers were buffered until completion")
	}
	defer resp.Body.Close()

	line, err := bufio.NewReader(resp.Body).ReadString('\n')
	if err != nil {
		t.Fatal(err)
	}
	if line != "event: line\n" {
		t.Fatalf("first stream line = %q, want event line", line)
	}
	if got := resp.Header.Get("Access-Control-Allow-Origin"); got != "http://wails.localhost" {
		t.Fatalf("Access-Control-Allow-Origin = %q, want Wails origin", got)
	}
}

func TestServerStartServesDashboardAndStopsWithContext(t *testing.T) {
	repoRoot := t.TempDir()
	specDir := filepath.Join(repoRoot, ".spec")
	if err := os.MkdirAll(specDir, 0o755); err != nil {
		t.Fatal(err)
	}
	todo := "# TODO\n\n## Milestone: Test  <!-- status: active -->\n\n" +
		"### 下一步\n\n(暂无)\n\n### 待规划\n\n(暂无)\n\n### 最近完成\n\n(暂无)\n"
	if err := os.WriteFile(filepath.Join(specDir, "TODO.md"), []byte(todo), 0o644); err != nil {
		t.Fatal(err)
	}

	srv, err := New(Options{RepoRoot: repoRoot, Port: 0, Version: "test", Preset: "windows"})
	if err != nil {
		t.Fatal(err)
	}
	ctx, cancel := context.WithCancel(context.Background())
	running, err := srv.Start(ctx)
	if err != nil {
		t.Fatal(err)
	}

	resp, err := http.Get(running.URL)
	if err != nil {
		cancel()
		t.Fatal(err)
	}
	body, readErr := io.ReadAll(resp.Body)
	resp.Body.Close()
	if readErr != nil {
		cancel()
		t.Fatal(readErr)
	}
	if resp.StatusCode != http.StatusOK {
		cancel()
		t.Fatalf("status = %d, want %d", resp.StatusCode, http.StatusOK)
	}
	if !strings.Contains(string(body), "Test") {
		cancel()
		t.Fatalf("dashboard body does not contain milestone:\n%s", body)
	}

	cancel()
	waitDone := make(chan error, 1)
	go func() { waitDone <- running.Wait() }()
	select {
	case err := <-waitDone:
		if err != nil {
			t.Fatalf("Wait: %v", err)
		}
	case <-time.After(2 * time.Second):
		t.Fatal("dashboard server did not stop after context cancellation")
	}
}
