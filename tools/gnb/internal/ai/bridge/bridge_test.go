package bridge

import (
	"bufio"
	"bytes"
	"context"
	"encoding/json"
	"io"
	"os"
	"path/filepath"
	"strings"
	"sync"
	"testing"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/ai"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/ai/config"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/ai/protocol"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/ai/provider"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/ai/router"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/ai/session"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/ai/tool"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/ai/workflow"
)

type scriptProvider struct {
	mu   sync.Mutex
	step int
}

func (p *scriptProvider) Descriptor() provider.Descriptor {
	return provider.Descriptor{ID: "fake", Kind: "fake", Configured: true, Available: true, DefaultModel: "fake-model", Capabilities: provider.Capabilities{NativeTools: true, Streaming: true}}
}
func (p *scriptProvider) Chat(_ context.Context, _ protocol.ChatRequest, _ protocol.EventSink) (protocol.ChatResponse, error) {
	p.mu.Lock()
	defer p.mu.Unlock()
	p.step++
	if p.step == 1 {
		return protocol.ChatResponse{ToolCalls: []protocol.ToolCall{{ID: "call-1", Name: "scene_echo", Arguments: `{"text":"hi"}`}}, FinishReason: "tool_calls"}, nil
	}
	return protocol.ChatResponse{Content: "remote tool completed", FinishReason: "stop"}, nil
}
func testRuntime() *ai.Runtime {
	cfg := config.Config{DefaultProfile: "test", Profiles: map[string]config.Profile{"test": {Provider: "fake", MaxSteps: 4, MaxToolCalls: 4}}}
	registry := provider.NewRegistry()
	_ = registry.Register(&scriptProvider{})
	return &ai.Runtime{Config: cfg, Registry: registry, Router: router.New(cfg, registry), Tools: tool.NewRegistry(), Sessions: session.NewStore(20), Workflows: workflow.NewRegistry()}
}

func TestHandshakeCatalogAndProtocolErrors(t *testing.T) {
	input := strings.Join([]string{`{"jsonrpc":"2.0","id":"1","method":"initialize","params":{"protocolVersion":1}}`, `{"jsonrpc":"2.0","id":"2","method":"providers.list","params":{}}`, `{"jsonrpc":"2.0","id":"3","method":"missing","params":{}}`}, "\n") + "\n"
	var output bytes.Buffer
	if err := New(testRuntime(), strings.NewReader(input), &output).Serve(context.Background()); err != nil {
		t.Fatal(err)
	}
	seen := map[string]frame{}
	scanner := bufio.NewScanner(&output)
	for scanner.Scan() {
		var message frame
		if err := json.Unmarshal(scanner.Bytes(), &message); err != nil {
			t.Fatal(err)
		}
		var id string
		_ = json.Unmarshal(message.ID, &id)
		seen[id] = message
	}
	if seen["1"].Error != nil || !bytes.Contains(seen["1"].Result, []byte(`"protocolVersion":1`)) {
		t.Fatalf("initialize=%s", seen["1"].Result)
	}
	if !bytes.Contains(seen["2"].Result, []byte(`"fake"`)) {
		t.Fatalf("providers=%s", seen["2"].Result)
	}
	if seen["3"].Error == nil || seen["3"].Error.Code != -32601 {
		t.Fatalf("missing=%#v", seen["3"].Error)
	}
}

func TestMalformedAndVersionMismatchFrames(t *testing.T) {
	input := "{bad json}\n" + `{"jsonrpc":"2.0","id":"version","method":"initialize","params":{"protocolVersion":99}}` + "\n"
	var output bytes.Buffer
	if err := New(testRuntime(), strings.NewReader(input), &output).Serve(context.Background()); err != nil {
		t.Fatal(err)
	}
	scanner := bufio.NewScanner(&output)
	var codes []int
	for scanner.Scan() {
		var message frame
		_ = json.Unmarshal(scanner.Bytes(), &message)
		if message.Error != nil {
			codes = append(codes, message.Error.Code)
		}
	}
	if len(codes) != 2 || codes[0] != -32700 || codes[1] != -32001 {
		t.Fatalf("codes=%v output=%s", codes, output.String())
	}
}

func TestRemoteToolRoundTrip(t *testing.T) {
	clientToServerR, clientToServerW := io.Pipe()
	serverToClientR, serverToClientW := io.Pipe()
	server := New(testRuntime(), clientToServerR, serverToClientW)
	done := make(chan error, 1)
	go func() { done <- server.Serve(context.Background()) }()
	write := func(value any) { raw, _ := json.Marshal(value); _, _ = clientToServerW.Write(append(raw, '\n')) }
	read := bufio.NewScanner(serverToClientR)
	write(map[string]any{"jsonrpc": "2.0", "id": "init", "method": "initialize", "params": map[string]any{"protocolVersion": 1}})
	if !read.Scan() {
		t.Fatal("no initialize response")
	}
	write(map[string]any{"jsonrpc": "2.0", "id": "tools", "method": "tools.register", "params": map[string]any{"tools": []any{map[string]any{"name": "scene_echo", "description": "echo", "inputSchema": map[string]any{"type": "object"}}}}})
	if !read.Scan() {
		t.Fatal("no tools response")
	}
	write(map[string]any{"jsonrpc": "2.0", "id": "session", "method": "session.create", "params": map[string]any{"profile": "test", "provider": "fake", "model": "fake-model"}})
	if !read.Scan() {
		t.Fatal("no session response")
	}
	var sessionResponse frame
	_ = json.Unmarshal(read.Bytes(), &sessionResponse)
	var sessionResult struct {
		SessionID string `json:"id"`
	}
	_ = json.Unmarshal(sessionResponse.Result, &sessionResult)
	write(map[string]any{"jsonrpc": "2.0", "id": "run", "method": "agent.run", "params": map[string]any{"sessionId": sessionResult.SessionID, "prompt": "use echo"}})
	gotResult := false
	for read.Scan() {
		var message frame
		if err := json.Unmarshal(read.Bytes(), &message); err != nil {
			t.Fatal(err)
		}
		if message.Method == "tool.execute" {
			var id string
			_ = json.Unmarshal(message.ID, &id)
			write(map[string]any{"jsonrpc": "2.0", "id": id, "result": map[string]any{"content": "echo: hi"}})
			continue
		}
		var id string
		_ = json.Unmarshal(message.ID, &id)
		if id == "run" {
			if message.Error != nil {
				t.Fatalf("run error=%#v", message.Error)
			}
			if !bytes.Contains(message.Result, []byte("remote tool completed")) {
				t.Fatalf("run result=%s", message.Result)
			}
			gotResult = true
			break
		}
	}
	if !gotResult {
		t.Fatal("missing agent result")
	}
	_ = clientToServerW.Close()
	_ = serverToClientR.Close()
	if err := <-done; err != nil {
		t.Fatal(err)
	}
}

func TestSharedProtocolFixturesAreValidNDJSON(t *testing.T) {
	dir := filepath.Join("..", "..", "..", "..", "..", "tests", "fixtures", "gnb-agent-protocol", "v1")
	entries, err := os.ReadDir(dir)
	if err != nil {
		t.Fatal(err)
	}
	if len(entries) < 5 {
		t.Fatalf("expected protocol fixtures, got %d", len(entries))
	}
	for _, entry := range entries {
		raw, err := os.Open(filepath.Join(dir, entry.Name()))
		if err != nil {
			t.Fatal(err)
		}
		scanner := bufio.NewScanner(raw)
		line := 0
		for scanner.Scan() {
			line++
			var value map[string]any
			if err := json.Unmarshal(scanner.Bytes(), &value); err != nil {
				t.Fatalf("%s:%d: %v", entry.Name(), line, err)
			}
			if value["jsonrpc"] != "2.0" {
				t.Fatalf("%s:%d missing jsonrpc 2.0", entry.Name(), line)
			}
		}
		_ = raw.Close()
		if err := scanner.Err(); err != nil {
			t.Fatal(err)
		}
	}
}
