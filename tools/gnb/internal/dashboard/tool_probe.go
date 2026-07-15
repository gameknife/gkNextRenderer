package dashboard

import (
	"context"
	"encoding/json"
	"fmt"
	"strings"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/ai/protocol"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/ai/router"
)

const diagnosticToolName = "lookup_diagnostic_fixture"

var diagnosticFixtures = map[string]string{
	"alpha": "A-17",
	"beta":  "B-42",
}

type chatRouter interface {
	Chat(context.Context, router.Overrides, protocol.ChatRequest, protocol.EventSink) (protocol.ChatResponse, router.Route, error)
}

// runToolCallSmoke performs exactly two model requests and at most one lookup
// against a fixed in-memory map. It intentionally has no general tool registry.
func runToolCallSmoke(ctx context.Context, model chatRouter, overrides router.Overrides, request protocol.ChatRequest, report func(chatToolEvent)) (protocol.ChatResponse, error) {
	request.Tools = []protocol.ToolDescriptor{{
		Name:        diagnosticToolName,
		Description: "Look up a fixed diagnostic fixture by key.",
		InputSchema: map[string]any{
			"type":                 "object",
			"properties":           map[string]any{"key": map[string]any{"type": "string", "enum": []string{"alpha", "beta"}}},
			"required":             []string{"key"},
			"additionalProperties": false,
		},
	}}
	first, _, err := model.Chat(ctx, overrides, request, nil)
	if err != nil {
		return protocol.ChatResponse{}, err
	}
	if len(first.ToolCalls) != 1 || first.ToolCalls[0].Name != diagnosticToolName {
		return protocol.ChatResponse{}, fmt.Errorf("tool probe expected exactly one %s call, got %d", diagnosticToolName, len(first.ToolCalls))
	}
	call := first.ToolCalls[0]
	var arguments struct {
		Key string `json:"key"`
	}
	if err := json.Unmarshal([]byte(call.Arguments), &arguments); err != nil {
		return protocol.ChatResponse{}, fmt.Errorf("tool probe arguments: %w", err)
	}
	value, ok := diagnosticFixtures[strings.TrimSpace(arguments.Key)]
	if !ok {
		return protocol.ChatResponse{}, fmt.Errorf("unknown diagnostic fixture %q", arguments.Key)
	}
	if report != nil {
		report(chatToolEvent{Step: 1, Phase: "start", Name: diagnosticToolName, Summary: call.Arguments})
		report(chatToolEvent{Step: 1, Phase: "done", Name: diagnosticToolName, Detail: value})
	}
	request.Tools = nil
	request.Messages = append(request.Messages,
		protocol.Message{Role: protocol.RoleAssistant, Content: first.Content, ToolCalls: first.ToolCalls},
		protocol.Message{Role: protocol.RoleTool, Name: diagnosticToolName, ToolCallID: call.ID, Content: value},
	)
	second, _, err := model.Chat(ctx, overrides, request, nil)
	if err != nil {
		return protocol.ChatResponse{}, err
	}
	if len(second.ToolCalls) != 0 {
		return protocol.ChatResponse{}, fmt.Errorf("tool probe rejected an additional tool call")
	}
	second.Usage.PromptTokens += first.Usage.PromptTokens
	second.Usage.CompletionTokens += first.Usage.CompletionTokens
	return second, nil
}
