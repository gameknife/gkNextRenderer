package bridge

import (
	"bufio"
	"bytes"
	"context"
	"encoding/json"
	"os"
	"path/filepath"
	"strings"
	"testing"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/ai"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/ai/config"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/ai/protocol"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/ai/provider"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/ai/router"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/ai/session"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/ai/workflow"
)

type scriptProvider struct{}

func (*scriptProvider) Descriptor() provider.Descriptor {
	return provider.Descriptor{ID: "fake", Kind: "fake", Configured: true, Available: true, DefaultModel: "fake-model", Capabilities: provider.Capabilities{Streaming: true}}
}
func (*scriptProvider) Chat(_ context.Context, _ protocol.ChatRequest, _ protocol.EventSink) (protocol.ChatResponse, error) {
	return protocol.ChatResponse{Content: "chat completed", FinishReason: "stop"}, nil
}
func testRuntime() *ai.Runtime {
	cfg := config.Config{DefaultProfile: "test", Profiles: map[string]config.Profile{"test": {Provider: "fake"}}}
	registry := provider.NewRegistry()
	_ = registry.Register(&scriptProvider{})
	return &ai.Runtime{Config: cfg, Registry: registry, Router: router.New(cfg, registry), Sessions: session.NewStore(20), Workflows: workflow.NewRegistry()}
}

func TestHandshakeCatalogAndProtocolErrors(t *testing.T) {
	input := strings.Join([]string{
		`{"jsonrpc":"2.0","id":"1","method":"initialize","params":{"protocolVersion":2}}`,
		`{"jsonrpc":"2.0","id":"2","method":"providers.list","params":{}}`,
		`{"jsonrpc":"2.0","id":"3","method":"missing","params":{}}`,
	}, "\n") + "\n"
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
	if seen["1"].Error != nil || !bytes.Contains(seen["1"].Result, []byte(`"protocolVersion":2`)) {
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

func TestSharedProtocolFixturesAreValidNDJSON(t *testing.T) {
	dir := filepath.Join("..", "..", "..", "..", "..", "tests", "fixtures", "gnb-ai-protocol", "v2")
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
		for scanner.Scan() {
			var value map[string]any
			if err := json.Unmarshal(scanner.Bytes(), &value); err != nil {
				t.Fatalf("%s: %v", entry.Name(), err)
			}
			if value["jsonrpc"] != "2.0" {
				t.Fatalf("%s missing jsonrpc 2.0", entry.Name())
			}
		}
		_ = raw.Close()
		if err := scanner.Err(); err != nil {
			t.Fatal(err)
		}
	}
}

func TestStatelessChatDoesNotReuseOrAppendSessionHistory(t *testing.T) {
	runtime := testRuntime()
	sess := runtime.Sessions.Create("test", "fake", "fake-model")
	if err := runtime.Sessions.Append(sess.ID, protocol.Message{Role: protocol.RoleUser, Content: "old context"}); err != nil {
		t.Fatal(err)
	}
	server := New(runtime, strings.NewReader(""), &bytes.Buffer{})
	raw, _ := json.Marshal(runParams{
		SessionID: sess.ID,
		RunID:     "stateless-run",
		Messages:  []protocol.Message{{Role: protocol.RoleUser, Content: "snapshot"}},
		Stateless: true,
	})
	if _, err := server.chat(context.Background(), raw); err != nil {
		t.Fatal(err)
	}
	stored, ok := runtime.Sessions.Get(sess.ID)
	if !ok || len(stored.Messages) != 1 || stored.Messages[0].Content != "old context" {
		t.Fatalf("stateless request mutated history: %#v", stored.Messages)
	}
}
