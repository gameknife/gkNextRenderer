package dashboard

import (
	"context"
	"testing"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/ai/protocol"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/ai/router"
)

type probeChatStub struct {
	requests  []protocol.ChatRequest
	responses []protocol.ChatResponse
}

func (s *probeChatStub) Chat(_ context.Context, _ router.Overrides, request protocol.ChatRequest, _ protocol.EventSink) (protocol.ChatResponse, router.Route, error) {
	s.requests = append(s.requests, request)
	response := s.responses[len(s.requests)-1]
	return response, router.Route{}, nil
}

func TestToolCallSmokeUsesOneInMemoryLookupAndTwoRequests(t *testing.T) {
	stub := &probeChatStub{responses: []protocol.ChatResponse{
		{ToolCalls: []protocol.ToolCall{{ID: "call-1", Name: diagnosticToolName, Arguments: `{"key":"beta"}`}}, Usage: protocol.Usage{PromptTokens: 3, CompletionTokens: 2}},
		{Content: "B-42", FinishReason: "stop", Usage: protocol.Usage{PromptTokens: 5, CompletionTokens: 1}},
	}}
	var events []chatToolEvent
	result, err := runToolCallSmoke(context.Background(), stub, router.Overrides{}, protocol.ChatRequest{Messages: []protocol.Message{{Role: protocol.RoleUser, Content: "beta"}}}, func(event chatToolEvent) { events = append(events, event) })
	if err != nil {
		t.Fatal(err)
	}
	if len(stub.requests) != 2 {
		t.Fatalf("requests = %d, want 2", len(stub.requests))
	}
	if len(stub.requests[0].Tools) != 1 || stub.requests[0].Tools[0].Name != diagnosticToolName {
		t.Fatalf("first request tools = %#v", stub.requests[0].Tools)
	}
	if len(stub.requests[1].Tools) != 0 {
		t.Fatalf("second request unexpectedly contains tools")
	}
	if result.Content != "B-42" || result.Usage.PromptTokens != 8 || result.Usage.CompletionTokens != 3 {
		t.Fatalf("result = %#v", result)
	}
	if len(events) != 2 || events[1].Detail != "B-42" {
		t.Fatalf("events = %#v", events)
	}
}

func TestToolCallSmokeRejectsSecondToolCall(t *testing.T) {
	stub := &probeChatStub{responses: []protocol.ChatResponse{
		{ToolCalls: []protocol.ToolCall{{ID: "call-1", Name: diagnosticToolName, Arguments: `{"key":"alpha"}`}}},
		{ToolCalls: []protocol.ToolCall{{ID: "call-2", Name: diagnosticToolName, Arguments: `{"key":"beta"}`}}},
	}}
	if _, err := runToolCallSmoke(context.Background(), stub, router.Overrides{}, protocol.ChatRequest{}, nil); err == nil {
		t.Fatal("expected additional tool call to fail")
	}
}
