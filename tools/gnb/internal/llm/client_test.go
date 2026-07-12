package llm

import (
	"context"
	"io"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
)

func TestClientChatHTTPFixture(t *testing.T) {
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.URL.Path != "/v1/chat/completions" || r.Method != http.MethodPost {
			t.Fatalf("unexpected request: %s %s", r.Method, r.URL.Path)
		}
		body, err := io.ReadAll(r.Body)
		if err != nil {
			t.Fatal(err)
		}
		if !strings.Contains(string(body), `"enable_thinking":false`) {
			t.Fatalf("request should disable thinking by default: %s", body)
		}
		w.Header().Set("Content-Type", "application/json")
		_, _ = io.WriteString(w, `{"choices":[{"message":{"role":"assistant","content":"fixture-ok"},"finish_reason":"stop"}]}`)
	}))
	defer server.Close()

	got, err := NewClient(server.URL).Chat(context.Background(), ChatRequest{
		Messages: []ChatMessage{{Role: "user", Content: "ping"}},
	})
	if err != nil {
		t.Fatal(err)
	}
	if got != "fixture-ok" {
		t.Fatalf("Chat() = %q, want fixture-ok", got)
	}
}

func TestClientChatStreamHTTPFixture(t *testing.T) {
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.Header.Get("Accept") != "text/event-stream" {
			t.Fatalf("Accept = %q", r.Header.Get("Accept"))
		}
		w.Header().Set("Content-Type", "text/event-stream")
		_, _ = io.WriteString(w, ": keepalive\n\n")
		_, _ = io.WriteString(w, "data: {\"choices\":[{\"delta\":{\"reasoning_content\":\"think\"}}]}\n\n")
		_, _ = io.WriteString(w, "data: {\"choices\":[{\"delta\":{\"content\":\"hello\"}}]}\n\n")
		_, _ = io.WriteString(w, "data: {\"choices\":[{\"finish_reason\":\"stop\",\"delta\":{}}]}\n\n")
		_, _ = io.WriteString(w, "data: [DONE]\n\n")
	}))
	defer server.Close()

	var deltas []StreamDelta
	err := NewClient(server.URL).ChatStream(context.Background(), ChatRequest{
		Messages: []ChatMessage{{Role: "user", Content: "ping"}},
	}, func(delta StreamDelta) error {
		deltas = append(deltas, delta)
		return nil
	})
	if err != nil {
		t.Fatal(err)
	}
	if len(deltas) != 3 || deltas[0].Reasoning != "think" || deltas[1].Text != "hello" || deltas[2].FinishReason != "stop" {
		t.Fatalf("unexpected stream deltas: %#v", deltas)
	}
}

func TestClientHTTPErrorFixture(t *testing.T) {
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		http.Error(w, `{"error":{"message":"fixture unavailable"}}`, http.StatusServiceUnavailable)
	}))
	defer server.Close()

	_, err := NewClient(server.URL).Chat(context.Background(), ChatRequest{})
	if err == nil || !strings.Contains(err.Error(), "503") || !strings.Contains(err.Error(), "fixture unavailable") {
		t.Fatalf("unexpected Chat error: %v", err)
	}
	err = NewClient(server.URL).ChatStream(context.Background(), ChatRequest{}, func(StreamDelta) error { return nil })
	if err == nil || !strings.Contains(err.Error(), "503") || !strings.Contains(err.Error(), "fixture unavailable") {
		t.Fatalf("unexpected ChatStream error: %v", err)
	}
}

func TestParseStreamDeltaOpenAIChunk(t *testing.T) {
	delta, err := parseStreamDelta(`{"choices":[{"delta":{"content":"hello"}}]}`)
	if err != nil {
		t.Fatal(err)
	}
	if delta.Text != "hello" {
		t.Fatalf("text = %q, want hello", delta.Text)
	}
}

func TestParseStreamDeltaLlamaContentChunk(t *testing.T) {
	delta, err := parseStreamDelta(`{"content":"world"}`)
	if err != nil {
		t.Fatal(err)
	}
	if delta.Text != "world" {
		t.Fatalf("text = %q, want world", delta.Text)
	}
}

func TestParseStreamDeltaReasoningChunk(t *testing.T) {
	delta, err := parseStreamDelta(`{"choices":[{"delta":{"reasoning_content":"thinking"}}]}`)
	if err != nil {
		t.Fatal(err)
	}
	if delta.Reasoning != "thinking" {
		t.Fatalf("reasoning = %q, want thinking", delta.Reasoning)
	}
}

func TestParseStreamDeltaFinishReason(t *testing.T) {
	delta, err := parseStreamDelta(`{"choices":[{"finish_reason":"length","delta":{}}]}`)
	if err != nil {
		t.Fatal(err)
	}
	if delta.FinishReason != "length" {
		t.Fatalf("finish reason = %q, want length", delta.FinishReason)
	}
}
