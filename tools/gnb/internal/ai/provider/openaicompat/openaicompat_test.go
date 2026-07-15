package openaicompat

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
		if r.Header.Get("Authorization") != "Bearer test-secret" {
			t.Fatalf("missing bearer token")
		}
		var requestBody map[string]any
		if err := json.NewDecoder(r.Body).Decode(&requestBody); err != nil {
			t.Fatal(err)
		}
		if requestBody["model"] != "fixture-model" {
			t.Fatalf("model = %#v", requestBody["model"])
		}
		_, _ = io.WriteString(w, `{"choices":[{"finish_reason":"tool_calls","message":{"content":"working","tool_calls":[{"id":"c1","type":"function","function":{"name":"lookup","arguments":"{\"q\":\"x\"}"}}]}}],"usage":{"prompt_tokens":7,"completion_tokens":3}}`)
	}))
	defer server.Close()
	adapter := New(Config{ID: "mock", Endpoint: server.URL, APIKey: "test-secret", DefaultModel: "fixture-model"})
	response, err := adapter.Chat(context.Background(), protocol.ChatRequest{Model: "fixture-model", Messages: []protocol.Message{{Role: protocol.RoleUser, Content: "go"}}}, nil)
	if err != nil {
		t.Fatal(err)
	}
	if response.Content != "working" || response.FinishReason != "tool_calls" || response.Usage.PromptTokens != 7 || len(response.ToolCalls) != 1 || response.ToolCalls[0].Name != "lookup" {
		t.Fatalf("unexpected response: %#v", response)
	}
}

func TestChatStreamContract(t *testing.T) {
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Type", "text/event-stream")
		_, _ = io.WriteString(w, "data: {\"choices\":[{\"delta\":{\"reasoning_content\":\"r\"}}]}\n\n")
		_, _ = io.WriteString(w, "data: {\"choices\":[{\"delta\":{\"content\":\"ok\"},\"finish_reason\":\"stop\"}],\"usage\":{\"prompt_tokens\":2,\"completion_tokens\":1}}\n\n")
		_, _ = io.WriteString(w, "data: [DONE]\n\n")
	}))
	defer server.Close()
	adapter := New(Config{ID: "mock", Endpoint: server.URL, APIKey: "secret"})
	var events []protocol.Event
	response, err := adapter.Chat(context.Background(), protocol.ChatRequest{}, func(ctx context.Context, event protocol.Event) error { events = append(events, event); return nil })
	if err != nil {
		t.Fatal(err)
	}
	if response.Content != "ok" || response.Usage.CompletionTokens != 1 || len(events) != 2 || events[0].Type != protocol.EventReasoningDelta {
		t.Fatalf("response=%#v events=%#v", response, events)
	}
}

func TestChatStreamAssemblesToolCallArgumentChunks(t *testing.T) {
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Type", "text/event-stream")
		_, _ = io.WriteString(w, "data: {\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":0,\"id\":\"c1\",\"function\":{\"name\":\"lookup\",\"arguments\":\"{\\\"q\\\":\"}}]}}]}\n\n")
		_, _ = io.WriteString(w, "data: {\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":0,\"function\":{\"arguments\":\"\\\"x\\\"}\"}}]},\"finish_reason\":\"tool_calls\"}]}\n\n")
		_, _ = io.WriteString(w, "data: [DONE]\n\n")
	}))
	defer server.Close()
	adapter := New(Config{ID: "mock", Endpoint: server.URL, APIKey: "secret"})
	response, err := adapter.Chat(context.Background(), protocol.ChatRequest{}, func(context.Context, protocol.Event) error { return nil })
	if err != nil {
		t.Fatal(err)
	}
	if len(response.ToolCalls) != 1 || response.ToolCalls[0].ID != "c1" || response.ToolCalls[0].Name != "lookup" || response.ToolCalls[0].Arguments != `{"q":"x"}` {
		t.Fatalf("response=%#v", response)
	}
}

func TestHTTPErrorDoesNotExposeAuthorization(t *testing.T) {
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		http.Error(w, `{"error":"nope"}`, http.StatusUnauthorized)
	}))
	defer server.Close()
	adapter := New(Config{ID: "mock", Endpoint: server.URL, APIKey: "top-secret"})
	_, err := adapter.Chat(context.Background(), protocol.ChatRequest{}, nil)
	if err == nil || !strings.Contains(err.Error(), "401") || strings.Contains(err.Error(), "top-secret") {
		t.Fatalf("unexpected redacted error: %v", err)
	}
}

func TestChatTranslatesJSONSchemaResponseFormat(t *testing.T) {
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		var body map[string]any
		if err := json.NewDecoder(r.Body).Decode(&body); err != nil {
			t.Fatal(err)
		}
		format, ok := body["response_format"].(map[string]any)
		if !ok || format["type"] != "json_schema" {
			t.Fatalf("response_format = %#v", body["response_format"])
		}
		_, _ = io.WriteString(w, `{"choices":[{"finish_reason":"stop","message":{"content":"{}"}}]}`)
	}))
	defer server.Close()
	adapter := New(Config{ID: "mock", Endpoint: server.URL, APIKey: "secret"})
	_, err := adapter.Chat(context.Background(), protocol.ChatRequest{ResponseFormat: &protocol.ResponseFormat{Mode: "schema", Name: "fixture", Schema: map[string]any{"type": "object"}, Strict: true}}, nil)
	if err != nil {
		t.Fatal(err)
	}
}

func TestLlamaCppRequestControlsThinkingExplicitly(t *testing.T) {
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		var body map[string]any
		if err := json.NewDecoder(r.Body).Decode(&body); err != nil {
			t.Fatal(err)
		}
		kwargs, ok := body["chat_template_kwargs"].(map[string]any)
		if !ok || kwargs["enable_thinking"] != false {
			t.Fatalf("chat_template_kwargs = %#v", body["chat_template_kwargs"])
		}
		_, _ = io.WriteString(w, `{"choices":[{"finish_reason":"stop","message":{"content":"{}"}}]}`)
	}))
	defer server.Close()
	adapter := New(Config{ID: "local", Endpoint: server.URL, APIKey: "local", ChatTemplateKwargs: true})
	if _, err := adapter.Chat(context.Background(), protocol.ChatRequest{EnableThinking: false}, nil); err != nil {
		t.Fatal(err)
	}
}
