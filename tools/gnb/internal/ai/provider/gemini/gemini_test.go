package gemini

import (
	"context"
	"io"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/ai/protocol"
)

func TestGeminiContentToolUsageContract(t *testing.T) {
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if !strings.Contains(r.URL.Path, "fixture:generateContent") || r.URL.Query().Get("key") != "secret" {
			t.Fatalf("unexpected URL %s", r.URL.String())
		}
		_, _ = io.WriteString(w, `{"candidates":[{"finishReason":"STOP","content":{"parts":[{"text":"ok"},{"functionCall":{"name":"lookup","args":{"q":"x"}}}]}}],"usageMetadata":{"promptTokenCount":4,"candidatesTokenCount":2}}`)
	}))
	defer server.Close()
	adapter := New(Config{ID: "gemini", Endpoint: server.URL, APIKey: "secret", DefaultModel: "fixture"})
	response, err := adapter.Chat(context.Background(), protocol.ChatRequest{Model: "fixture", Messages: []protocol.Message{{Role: protocol.RoleSystem, Content: "system"}, {Role: protocol.RoleUser, Content: "go"}}}, nil)
	if err != nil {
		t.Fatal(err)
	}
	if response.Content != "ok" || response.Usage.PromptTokens != 4 || len(response.ToolCalls) != 1 || response.ToolCalls[0].Name != "lookup" {
		t.Fatalf("response=%#v", response)
	}
}

func TestGeminiStreamContract(t *testing.T) {
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		_, _ = io.WriteString(w, "data: {\"candidates\":[{\"finishReason\":\"STOP\",\"content\":{\"parts\":[{\"text\":\"hi\"}]}}]}\n\n")
	}))
	defer server.Close()
	adapter := New(Config{ID: "gemini", Endpoint: server.URL, APIKey: "secret"})
	var events []protocol.Event
	response, err := adapter.Chat(context.Background(), protocol.ChatRequest{Model: "fixture"}, func(_ context.Context, e protocol.Event) error { events = append(events, e); return nil })
	if err != nil || response.Content != "hi" || len(events) != 1 {
		t.Fatalf("response=%#v events=%#v err=%v", response, events, err)
	}
}
