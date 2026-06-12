package llm

import (
	"context"
	"fmt"
	"net"
	"net/http"
	"os"
	"path/filepath"
	"testing"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/config"
)

func TestEnsureRunningOrReuseKeepsHealthyServer(t *testing.T) {
	listener, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	mux := http.NewServeMux()
	mux.HandleFunc("/health", func(w http.ResponseWriter, _ *http.Request) {
		w.WriteHeader(http.StatusOK)
	})
	httpServer := &http.Server{Handler: mux}
	go func() {
		_ = httpServer.Serve(listener)
	}()
	t.Cleanup(func() {
		_ = httpServer.Close()
	})

	repoRoot := t.TempDir()
	cfg := config.LLMConfig{
		Active: "configured-model",
		Models: []config.ModelConfig{{
			ID:       "configured-model",
			ContextN: 32768,
		}},
		Server: config.ServerConfig{
			Host: "127.0.0.1",
			Port: 1,
		},
	}
	layout := ResolveLayout(repoRoot, cfg)
	if err := os.MkdirAll(layout.RunDir, 0o755); err != nil {
		t.Fatal(err)
	}
	port := listener.Addr().(*net.TCPAddr).Port
	pidFile := fmt.Sprintf("12345\n127.0.0.1\n%d\nrunning-model\nctx:131072\nparallel:4\nreasoning:on\n", port)
	if err := os.WriteFile(layout.PIDFile, []byte(pidFile), 0o644); err != nil {
		t.Fatal(err)
	}

	info, err := NewServer(repoRoot, cfg).EnsureRunningOrReuse(context.Background())
	if err != nil {
		t.Fatal(err)
	}
	if !info.Running || info.Model != "running-model" || info.ContextN != 131072 || info.Parallel != 4 || info.Reasoning != "on" {
		t.Fatalf("unexpected reused server info: %+v", info)
	}
	if info.BaseURL() != fmt.Sprintf("http://127.0.0.1:%d", port) {
		t.Fatalf("base URL = %q, want running server endpoint", info.BaseURL())
	}
	if _, err := os.Stat(filepath.Join(layout.BinDir, "llama-server")); !os.IsNotExist(err) {
		t.Fatalf("reuse should not install or start the configured server: %v", err)
	}
}
