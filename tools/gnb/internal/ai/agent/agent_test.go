package agent

import (
	"context"
	"encoding/json"
	"errors"
	"strings"
	"testing"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/ai/protocol"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/ai/tool"
)

type scriptedChat struct {
	responses []protocol.ChatResponse
	index     int
	seen      []protocol.ChatRequest
}

func (s *scriptedChat) call(_ context.Context, req protocol.ChatRequest, _ protocol.EventSink) (protocol.ChatResponse, error) {
	s.seen = append(s.seen, req)
	if s.index >= len(s.responses) {
		return protocol.ChatResponse{}, errors.New("script exhausted")
	}
	response := s.responses[s.index]
	s.index++
	return response, nil
}
func registryWith(name, result string) *tool.Registry {
	registry := tool.NewRegistry()
	_ = registry.Register(tool.Entry{Descriptor: protocol.ToolDescriptor{Name: name, InputSchema: map[string]any{"type": "object"}}, Handler: func(_ context.Context, args json.RawMessage, _ tool.Context) (string, error) { return result, nil }})
	return registry
}

func TestRunCompletesToolChainAndTranscript(t *testing.T) {
	script := &scriptedChat{responses: []protocol.ChatResponse{{ToolCalls: []protocol.ToolCall{{ID: "c1", Name: "list", Arguments: `{}`}}}, {Content: "two nodes"}}}
	result, err := Run(context.Background(), protocol.ChatRequest{Messages: []protocol.Message{{Role: protocol.RoleUser, Content: "list"}}}, registryWith("list", "a,b"), script.call, Options{}, nil)
	if err != nil {
		t.Fatal(err)
	}
	if result.Content != "two nodes" || result.Trace.ToolCalls != 1 || result.Trace.Steps != 2 || len(result.Transcript) != 4 || len(script.seen[0].Tools) != 1 {
		t.Fatalf("result=%#v seen=%#v", result, script.seen)
	}
}

func TestUnknownToolAndExceptionBecomeObservations(t *testing.T) {
	registry := tool.NewRegistry()
	_ = registry.Register(tool.Entry{Descriptor: protocol.ToolDescriptor{Name: "boom"}, Handler: func(context.Context, json.RawMessage, tool.Context) (string, error) { return "", errors.New("kaboom") }})
	script := &scriptedChat{responses: []protocol.ChatResponse{{ToolCalls: []protocol.ToolCall{{ID: "u", Name: "missing", Arguments: `{}`}, {ID: "b", Name: "boom", Arguments: `{}`}}}, {Content: "done"}}}
	result, err := Run(context.Background(), protocol.ChatRequest{}, registry, script.call, Options{}, nil)
	if err != nil {
		t.Fatal(err)
	}
	joined := ""
	for _, m := range result.Transcript {
		joined += m.Content
	}
	if !strings.Contains(joined, "unknown tool") || !strings.Contains(joined, "kaboom") {
		t.Fatalf("transcript=%#v", result.Transcript)
	}
}

func TestMaxStepsGroundingCancelAndFallbackJSON(t *testing.T) {
	loop := &scriptedChat{responses: []protocol.ChatResponse{{ToolCalls: []protocol.ToolCall{{ID: "1", Name: "t", Arguments: `{}`}}}, {ToolCalls: []protocol.ToolCall{{ID: "2", Name: "t", Arguments: `{}`}}}}}
	result, err := Run(context.Background(), protocol.ChatRequest{}, registryWith("t", "again"), loop.call, Options{MaxSteps: 2}, nil)
	if err == nil || result.Trace.Steps != 2 {
		t.Fatalf("result=%#v err=%v", result, err)
	}
	ground := &scriptedChat{responses: []protocol.ChatResponse{{Content: "guess"}, {ToolCalls: []protocol.ToolCall{{ID: "1", Name: "t", Arguments: `{}`}}}, {Content: "grounded"}}}
	result, err = Run(context.Background(), protocol.ChatRequest{}, registryWith("t", "fact"), ground.call, Options{MaxGroundingRetries: 1}, nil)
	if err != nil || result.Trace.GroundingRetries != 1 || result.Content != "grounded" {
		t.Fatalf("result=%#v err=%v", result, err)
	}
	ctx, cancel := context.WithCancel(context.Background())
	cancel()
	result, err = Run(ctx, protocol.ChatRequest{}, registryWith("t", "x"), ground.call, Options{}, nil)
	if err == nil || result.Trace.Status != "cancelled" {
		t.Fatalf("result=%#v err=%v", result, err)
	}
	calls := ParseFallbackToolCalls("```json\n[{\"name\":\"a\",\"arguments\":{}},{\"tool\":\"b\",\"args\":{}}]\n```")
	if len(calls) != 2 || calls[0].Name != "a" || calls[1].Name != "b" {
		t.Fatalf("calls=%#v", calls)
	}
}
