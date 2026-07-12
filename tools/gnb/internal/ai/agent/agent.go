package agent

import (
	"context"
	"encoding/json"
	"fmt"
	"strings"
	"sync/atomic"
	"time"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/ai/protocol"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/ai/tool"
)

type ChatFunc func(context.Context, protocol.ChatRequest, protocol.EventSink) (protocol.ChatResponse, error)
type Options struct {
	MaxSteps            int
	MaxToolCalls        int
	MaxGroundingRetries int
	ToolTimeout         time.Duration
	RunID               string
}
type Trace struct {
	RunID            string         `json:"runId"`
	Provider         string         `json:"provider,omitempty"`
	Model            string         `json:"model,omitempty"`
	Steps            int            `json:"steps"`
	ToolCalls        int            `json:"toolCalls"`
	GroundingRetries int            `json:"groundingRetries"`
	Usage            protocol.Usage `json:"usage"`
	Elapsed          time.Duration  `json:"elapsed"`
	Status           string         `json:"status"`
	Error            string         `json:"error,omitempty"`
}
type Result struct {
	Content    string
	Transcript []protocol.Message
	Trace      Trace
}

var nextRunID atomic.Uint64

func Run(ctx context.Context, seed protocol.ChatRequest, registry *tool.Registry, chat ChatFunc, opts Options, sink protocol.EventSink) (Result, error) {
	if opts.MaxSteps <= 0 {
		opts.MaxSteps = 12
	}
	if opts.MaxToolCalls <= 0 {
		opts.MaxToolCalls = 32
	}
	if opts.ToolTimeout == 0 {
		opts.ToolTimeout = 30 * time.Second
	}
	if opts.RunID == "" {
		opts.RunID = fmt.Sprintf("run-%d", nextRunID.Add(1))
	}
	start := time.Now()
	result := Result{Transcript: append([]protocol.Message{}, seed.Messages...), Trace: Trace{RunID: opts.RunID, Status: "running"}}
	sequence := 0
	emit := func(event protocol.Event) {
		if sink == nil {
			return
		}
		sequence++
		event.RunID = opts.RunID
		event.Sequence = sequence
		_ = sink(ctx, event)
	}
	fail := func(status string, err error) (Result, error) {
		result.Trace.Status = status
		result.Trace.Error = err.Error()
		result.Trace.Elapsed = time.Since(start)
		eventType := protocol.EventRunFailed
		if status == "cancelled" {
			eventType = protocol.EventRunCancelled
		}
		emit(protocol.Event{Type: eventType, Error: err.Error()})
		return result, err
	}
	tools := registry.Descriptors()
	usedTool := false
	for step := 1; step <= opts.MaxSteps; step++ {
		if err := ctx.Err(); err != nil {
			return fail("cancelled", err)
		}
		result.Trace.Steps = step
		emit(protocol.Event{Type: protocol.EventStepStarted, Step: step})
		request := seed
		request.Messages = append([]protocol.Message{}, result.Transcript...)
		request.Tools = tools
		var modelEvents []protocol.Event
		var modelSink protocol.EventSink
		if sink != nil {
			modelSink = func(_ context.Context, event protocol.Event) error {
				modelEvents = append(modelEvents, event)
				return nil
			}
		}
		response, err := chat(ctx, request, modelSink)
		if err != nil {
			return fail("failed", err)
		}
		result.Trace.Usage.PromptTokens += response.Usage.PromptTokens
		result.Trace.Usage.CompletionTokens += response.Usage.CompletionTokens
		calls := response.ToolCalls
		if len(calls) == 0 {
			calls = ParseFallbackToolCalls(response.Content)
		}
		assistant := protocol.Message{Role: protocol.RoleAssistant, Content: response.Content, ToolCalls: calls}
		result.Transcript = append(result.Transcript, assistant)
		if len(calls) == 0 {
			if !usedTool && result.Trace.GroundingRetries < opts.MaxGroundingRetries && len(tools) > 0 {
				result.Trace.GroundingRetries++
				result.Transcript = append(result.Transcript, protocol.Message{Role: protocol.RoleUser, Content: "Use the available tools to ground the answer before finalizing."})
				continue
			}
			for _, event := range modelEvents {
				emit(event)
			}
			result.Content = response.Content
			result.Trace.Status = "completed"
			result.Trace.Elapsed = time.Since(start)
			emit(protocol.Event{Type: protocol.EventRunCompleted, Content: result.Content, Usage: result.Trace.Usage})
			return result, nil
		}
		usedTool = true
		for _, call := range calls {
			if result.Trace.ToolCalls >= opts.MaxToolCalls {
				return fail("failed", fmt.Errorf("maximum tool calls exceeded (%d)", opts.MaxToolCalls))
			}
			emit(protocol.Event{Type: protocol.EventToolCall, Step: step, CallID: call.ID, Name: call.Name, Arguments: call.Arguments})
			observation, toolErr := registry.Execute(ctx, call, opts.RunID, step, opts.ToolTimeout)
			if ctx.Err() != nil {
				return fail("cancelled", ctx.Err())
			}
			if toolErr != nil {
				observation = "ERROR: " + toolErr.Error()
			} else {
				result.Trace.ToolCalls++
			}
			emit(protocol.Event{Type: protocol.EventToolResult, Step: step, CallID: call.ID, Name: call.Name, Content: observation})
			result.Transcript = append(result.Transcript, protocol.Message{Role: protocol.RoleTool, Name: call.Name, ToolCallID: call.ID, Content: observation})
		}
	}
	return fail("failed", fmt.Errorf("maximum agent steps exceeded (%d)", opts.MaxSteps))
}

func ParseFallbackToolCalls(content string) []protocol.ToolCall {
	candidate := strings.TrimSpace(content)
	if start := strings.Index(candidate, "```json"); start >= 0 {
		candidate = candidate[start+7:]
		if end := strings.Index(candidate, "```"); end >= 0 {
			candidate = candidate[:end]
		}
	} else if start := strings.IndexAny(candidate, "[{"); start >= 0 {
		candidate = candidate[start:]
	}
	candidate = strings.TrimSpace(candidate)
	var raw any
	if json.Unmarshal([]byte(candidate), &raw) != nil {
		return nil
	}
	objects := []any{raw}
	if array, ok := raw.([]any); ok {
		objects = array
	}
	out := make([]protocol.ToolCall, 0, len(objects))
	for i, item := range objects {
		object, ok := item.(map[string]any)
		if !ok {
			continue
		}
		name, _ := object["name"].(string)
		if name == "" {
			name, _ = object["tool"].(string)
		}
		if name == "" {
			continue
		}
		args := object["arguments"]
		if args == nil {
			args = object["args"]
		}
		if args == nil {
			args = map[string]any{}
		}
		encoded, err := json.Marshal(args)
		if err != nil {
			continue
		}
		out = append(out, protocol.ToolCall{ID: fmt.Sprintf("fallback-%d", i+1), Name: name, Arguments: string(encoded)})
	}
	return out
}
