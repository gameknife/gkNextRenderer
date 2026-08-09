package claudecompat

import (
	"context"
	"encoding/json"
	"io"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/ai/protocol"
)

func TestChatContentToolsUsageContract(t *testing.T) {
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.URL.Path != "/v1/messages" || r.Header.Get("x-api-key") != "test-secret" || r.Header.Get("anthropic-version") != anthropicVersion {
			t.Fatalf("unexpected request path or headers")
		}
		var body request
		if err := json.NewDecoder(r.Body).Decode(&body); err != nil {
			t.Fatal(err)
		}
		if body.System != "be helpful" || body.Model != "fixture-model" || len(body.Tools) != 1 || len(body.Messages) != 4 {
			t.Fatalf("unexpected request: %#v", body)
		}
		if body.Messages[1].Content[1].Type != "tool_use" || body.Messages[2].Content[0].Type != "tool_result" {
			t.Fatalf("tool transcript was not converted: %#v", body.Messages)
		}
		_, _ = io.WriteString(w, `{"content":[{"type":"text","text":"working"},{"type":"tool_use","id":"c2","name":"lookup","input":{"q":"y"}}],"stop_reason":"tool_use","usage":{"input_tokens":7,"output_tokens":3}}`)
	}))
	defer server.Close()
	adapter := New(Config{ID: "mock", Endpoint: server.URL, APIKey: "test-secret", DefaultModel: "fixture-model"})
	response, err := adapter.Chat(context.Background(), protocol.ChatRequest{
		Model: "fixture-model", MaxOutputTokens: 2048,
		Messages: []protocol.Message{
			{Role: protocol.RoleSystem, Content: "be helpful"},
			{Role: protocol.RoleUser, Content: "start"},
			{Role: protocol.RoleAssistant, Content: "checking", ToolCalls: []protocol.ToolCall{{ID: "c1", Name: "lookup", Arguments: `{"q":"x"}`}}},
			{Role: protocol.RoleTool, ToolCallID: "c1", Content: "result"},
			{Role: protocol.RoleUser, Content: "continue"},
		},
		Tools: []protocol.ToolDescriptor{{Name: "lookup", Description: "look up", InputSchema: map[string]any{"type": "object"}}},
	}, nil)
	if err != nil {
		t.Fatal(err)
	}
	if response.Content != "working" || response.FinishReason != "tool_use" || response.Usage.PromptTokens != 7 || len(response.ToolCalls) != 1 || response.ToolCalls[0].Arguments != `{"q":"y"}` {
		t.Fatalf("unexpected response: %#v", response)
	}
}

func TestChatStreamAssemblesTextReasoningAndToolCall(t *testing.T) {
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Type", "text/event-stream")
		_, _ = io.WriteString(w, "data: {\"type\":\"message_start\",\"message\":{\"usage\":{\"input_tokens\":2}}}\n\n")
		_, _ = io.WriteString(w, "data: {\"type\":\"content_block_delta\",\"index\":0,\"delta\":{\"type\":\"thinking_delta\",\"thinking\":\"r\"}}\n\n")
		_, _ = io.WriteString(w, "data: {\"type\":\"content_block_delta\",\"index\":1,\"delta\":{\"type\":\"text_delta\",\"text\":\"ok\"}}\n\n")
		_, _ = io.WriteString(w, "data: {\"type\":\"content_block_start\",\"index\":2,\"content_block\":{\"type\":\"tool_use\",\"id\":\"c1\",\"name\":\"lookup\",\"input\":{}}}\n\n")
		_, _ = io.WriteString(w, "data: {\"type\":\"content_block_delta\",\"index\":2,\"delta\":{\"type\":\"input_json_delta\",\"partial_json\":\"{\\\"q\\\":\"}}\n\n")
		_, _ = io.WriteString(w, "data: {\"type\":\"content_block_delta\",\"index\":2,\"delta\":{\"type\":\"input_json_delta\",\"partial_json\":\"\\\"x\\\"}\"}}\n\n")
		_, _ = io.WriteString(w, "data: {\"type\":\"message_delta\",\"delta\":{\"stop_reason\":\"tool_use\"},\"usage\":{\"output_tokens\":4}}\n\n")
		_, _ = io.WriteString(w, "data: {\"type\":\"message_stop\"}\n\n")
	}))
	defer server.Close()
	adapter := New(Config{ID: "mock", Endpoint: server.URL, APIKey: "secret"})
	var events []protocol.Event
	response, err := adapter.Chat(context.Background(), protocol.ChatRequest{}, func(_ context.Context, event protocol.Event) error {
		events = append(events, event)
		return nil
	})
	if err != nil {
		t.Fatal(err)
	}
	if response.Content != "ok" || response.Usage.PromptTokens != 2 || response.Usage.CompletionTokens != 4 || response.FinishReason != "tool_use" || len(response.ToolCalls) != 1 || response.ToolCalls[0].Arguments != `{"q":"x"}` {
		t.Fatalf("response=%#v", response)
	}
	if len(events) != 2 || events[0].Type != protocol.EventReasoningDelta || events[1].Type != protocol.EventContentDelta {
		t.Fatalf("events=%#v", events)
	}
}

func TestHTTPErrorDoesNotExposeAPIKey(t *testing.T) {
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		http.Error(w, `{"error":{"message":"nope"}}`, http.StatusUnauthorized)
	}))
	defer server.Close()
	adapter := New(Config{ID: "mock", Endpoint: server.URL, APIKey: "top-secret"})
	_, err := adapter.Chat(context.Background(), protocol.ChatRequest{}, nil)
	if err == nil || !strings.Contains(err.Error(), "401") || strings.Contains(err.Error(), "top-secret") {
		t.Fatalf("unexpected redacted error: %v", err)
	}
}
