package ollama

import (
	"context"
	"io"
	"net/http"
	"net/http/httptest"
	"testing"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/ai/protocol"
)

func TestOllamaContentToolUsageContract(t *testing.T) {
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.URL.Path != "/api/chat" {
			t.Fatalf("path=%s", r.URL.Path)
		}
		_, _ = io.WriteString(w, `{"message":{"role":"assistant","content":"ok","tool_calls":[{"function":{"name":"lookup","arguments":{"q":"x"}}}]},"done":true,"done_reason":"stop","prompt_eval_count":5,"eval_count":2}`)
	}))
	defer server.Close()
	adapter := New(Config{ID: "ollama", Endpoint: server.URL})
	response, err := adapter.Chat(context.Background(), protocol.ChatRequest{Model: "fixture"}, nil)
	if err != nil {
		t.Fatal(err)
	}
	if response.Content != "ok" || response.Usage.PromptTokens != 5 || len(response.ToolCalls) != 1 || response.ToolCalls[0].Name != "lookup" {
		t.Fatalf("response=%#v", response)
	}
}

func TestOllamaStreamContract(t *testing.T) {
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		_, _ = io.WriteString(w, "{\"message\":{\"content\":\"o\"},\"done\":false}\n{\"message\":{\"content\":\"k\"},\"done\":true,\"done_reason\":\"stop\",\"eval_count\":2}\n")
	}))
	defer server.Close()
	adapter := New(Config{ID: "ollama", Endpoint: server.URL})
	var events []protocol.Event
	response, err := adapter.Chat(context.Background(), protocol.ChatRequest{}, func(_ context.Context, e protocol.Event) error { events = append(events, e); return nil })
	if err != nil || response.Content != "ok" || response.Usage.CompletionTokens != 2 || len(events) != 2 {
		t.Fatalf("response=%#v events=%#v err=%v", response, events, err)
	}
}
