package claudecompat

import (
	"bufio"
	"bytes"
	"context"
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"sort"
	"strings"
	"time"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/ai/protocol"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/ai/provider"
)

const anthropicVersion = "2023-06-01"

type Config struct {
	ID, DisplayName, Endpoint, APIKey, DefaultModel string
	Models                                          []string
	HTTP                                            *http.Client
}

type Adapter struct{ config Config }

func New(c Config) *Adapter {
	if c.HTTP == nil {
		c.HTTP = &http.Client{Timeout: 5 * time.Minute}
	}
	c.Endpoint = strings.TrimRight(c.Endpoint, "/")
	return &Adapter{config: c}
}

func (a *Adapter) Descriptor() provider.Descriptor {
	configured := a.config.Endpoint != "" && a.config.APIKey != ""
	reason := ""
	if a.config.Endpoint == "" {
		reason = "endpoint is empty"
	} else if a.config.APIKey == "" {
		reason = "API key is missing"
	}
	return provider.Descriptor{
		ID: a.config.ID, Kind: "claude-compatible", DisplayName: a.config.DisplayName,
		Models: a.config.Models, DefaultModel: a.config.DefaultModel, Configured: configured,
		ConfiguredReason: reason, Available: true,
		Capabilities: provider.Capabilities{Streaming: true, NativeTools: true, ReasoningControl: true},
	}
}

type contentBlock struct {
	Type      string          `json:"type"`
	Text      string          `json:"text,omitempty"`
	ID        string          `json:"id,omitempty"`
	Name      string          `json:"name,omitempty"`
	Input     json.RawMessage `json:"input,omitempty"`
	ToolUseID string          `json:"tool_use_id,omitempty"`
	Content   string          `json:"content,omitempty"`
}

type message struct {
	Role    string         `json:"role"`
	Content []contentBlock `json:"content"`
}

type request struct {
	Model       string           `json:"model"`
	System      string           `json:"system,omitempty"`
	Messages    []message        `json:"messages"`
	Tools       []map[string]any `json:"tools,omitempty"`
	Temperature float64          `json:"temperature,omitempty"`
	TopP        float64          `json:"top_p,omitempty"`
	MaxTokens   int              `json:"max_tokens"`
	Stream      bool             `json:"stream,omitempty"`
}

type response struct {
	Content    []contentBlock `json:"content"`
	StopReason string         `json:"stop_reason"`
	Usage      struct {
		InputTokens  int `json:"input_tokens"`
		OutputTokens int `json:"output_tokens"`
	} `json:"usage"`
	Error *struct {
		Type    string `json:"type"`
		Message string `json:"message"`
	} `json:"error,omitempty"`
}

func (a *Adapter) Chat(ctx context.Context, in protocol.ChatRequest, sink protocol.EventSink) (protocol.ChatResponse, error) {
	body, err := makeRequest(in, sink != nil)
	if err != nil {
		return protocol.ChatResponse{}, err
	}
	raw, err := json.Marshal(body)
	if err != nil {
		return protocol.ChatResponse{}, err
	}
	req, err := http.NewRequestWithContext(ctx, http.MethodPost, a.config.Endpoint+"/v1/messages", bytes.NewReader(raw))
	if err != nil {
		return protocol.ChatResponse{}, err
	}
	req.Header.Set("Content-Type", "application/json")
	req.Header.Set("Accept", "application/json")
	req.Header.Set("x-api-key", a.config.APIKey)
	req.Header.Set("anthropic-version", anthropicVersion)
	if sink != nil {
		req.Header.Set("Accept", "text/event-stream")
	}
	resp, err := a.config.HTTP.Do(req)
	if err != nil {
		return protocol.ChatResponse{}, classifyTransport(err)
	}
	defer resp.Body.Close()
	if resp.StatusCode < 200 || resp.StatusCode >= 300 {
		raw, _ := io.ReadAll(io.LimitReader(resp.Body, 64<<10))
		return protocol.ChatResponse{}, classifyHTTP(resp.StatusCode, string(raw))
	}
	if sink == nil {
		var decoded response
		if err := json.NewDecoder(resp.Body).Decode(&decoded); err != nil {
			return protocol.ChatResponse{}, fmt.Errorf("decode provider response: %w", err)
		}
		return normalize(decoded)
	}
	return readStream(ctx, resp.Body, sink)
}

func makeRequest(in protocol.ChatRequest, stream bool) (request, error) {
	out := request{Model: in.Model, Temperature: in.Temperature, TopP: in.TopP, MaxTokens: in.MaxOutputTokens, Stream: stream}
	if out.MaxTokens <= 0 {
		out.MaxTokens = 1024
	}
	var systems []string
	for _, item := range in.Messages {
		switch item.Role {
		case protocol.RoleSystem:
			systems = append(systems, item.Content)
		case protocol.RoleUser:
			out.Messages = append(out.Messages, message{Role: "user", Content: []contentBlock{{Type: "text", Text: item.Content}}})
		case protocol.RoleAssistant:
			blocks := make([]contentBlock, 0, 1+len(item.ToolCalls))
			if item.Content != "" {
				blocks = append(blocks, contentBlock{Type: "text", Text: item.Content})
			}
			for _, call := range item.ToolCalls {
				input := json.RawMessage(call.Arguments)
				if !json.Valid(input) {
					return request{}, fmt.Errorf("tool call %q has invalid JSON arguments", call.Name)
				}
				blocks = append(blocks, contentBlock{Type: "tool_use", ID: call.ID, Name: call.Name, Input: input})
			}
			out.Messages = append(out.Messages, message{Role: "assistant", Content: blocks})
		case protocol.RoleTool:
			out.Messages = append(out.Messages, message{Role: "user", Content: []contentBlock{{Type: "tool_result", ToolUseID: item.ToolCallID, Content: item.Content}}})
		}
	}
	out.System = strings.Join(systems, "\n\n")
	for _, tool := range in.Tools {
		out.Tools = append(out.Tools, map[string]any{"name": tool.Name, "description": tool.Description, "input_schema": tool.InputSchema})
	}
	return out, nil
}

func normalize(in response) (protocol.ChatResponse, error) {
	if in.Error != nil {
		return protocol.ChatResponse{}, &protocol.Error{Category: protocol.ErrorProvider, Message: in.Error.Message}
	}
	out := protocol.ChatResponse{
		FinishReason: in.StopReason,
		Usage:        protocol.Usage{PromptTokens: in.Usage.InputTokens, CompletionTokens: in.Usage.OutputTokens},
	}
	for _, block := range in.Content {
		switch block.Type {
		case "text":
			out.Content += block.Text
		case "tool_use":
			arguments := string(block.Input)
			if arguments == "" {
				arguments = "{}"
			}
			out.ToolCalls = append(out.ToolCalls, protocol.ToolCall{ID: block.ID, Name: block.Name, Arguments: arguments})
		}
	}
	return out, nil
}

type streamEvent struct {
	Type         string       `json:"type"`
	Index        int          `json:"index"`
	Message      response     `json:"message"`
	ContentBlock contentBlock `json:"content_block"`
	Delta        struct {
		Type        string `json:"type"`
		Text        string `json:"text"`
		Thinking    string `json:"thinking"`
		PartialJSON string `json:"partial_json"`
		StopReason  string `json:"stop_reason"`
	} `json:"delta"`
	Usage struct {
		InputTokens  int `json:"input_tokens"`
		OutputTokens int `json:"output_tokens"`
	} `json:"usage"`
	Error *struct {
		Type    string `json:"type"`
		Message string `json:"message"`
	} `json:"error"`
}

type partialToolCall struct{ id, name, arguments string }

func readStream(ctx context.Context, body io.Reader, sink protocol.EventSink) (protocol.ChatResponse, error) {
	var out protocol.ChatResponse
	partials := map[int]*partialToolCall{}
	scanner := bufio.NewScanner(body)
	scanner.Buffer(make([]byte, 0, 64<<10), 4<<20)
	for scanner.Scan() {
		line := strings.TrimSpace(scanner.Text())
		if !strings.HasPrefix(line, "data:") {
			continue
		}
		data := strings.TrimSpace(strings.TrimPrefix(line, "data:"))
		if data == "" || data == "[DONE]" {
			continue
		}
		var event streamEvent
		if err := json.Unmarshal([]byte(data), &event); err != nil {
			return out, fmt.Errorf("decode provider stream: %w", err)
		}
		switch event.Type {
		case "message_start":
			out.Usage.PromptTokens = event.Message.Usage.InputTokens
			out.Usage.CompletionTokens = event.Message.Usage.OutputTokens
		case "content_block_start":
			if event.ContentBlock.Type == "tool_use" {
				partials[event.Index] = &partialToolCall{id: event.ContentBlock.ID, name: event.ContentBlock.Name}
			}
			if event.ContentBlock.Type == "text" && event.ContentBlock.Text != "" {
				out.Content += event.ContentBlock.Text
				if err := sink(ctx, protocol.Event{Type: protocol.EventContentDelta, Content: event.ContentBlock.Text}); err != nil {
					return out, err
				}
			}
		case "content_block_delta":
			switch event.Delta.Type {
			case "text_delta":
				out.Content += event.Delta.Text
				if err := sink(ctx, protocol.Event{Type: protocol.EventContentDelta, Content: event.Delta.Text}); err != nil {
					return out, err
				}
			case "thinking_delta":
				if err := sink(ctx, protocol.Event{Type: protocol.EventReasoningDelta, Content: event.Delta.Thinking}); err != nil {
					return out, err
				}
			case "input_json_delta":
				if partial := partials[event.Index]; partial != nil {
					partial.arguments += event.Delta.PartialJSON
				}
			}
		case "message_delta":
			out.FinishReason = event.Delta.StopReason
			if event.Usage.InputTokens != 0 {
				out.Usage.PromptTokens = event.Usage.InputTokens
			}
			out.Usage.CompletionTokens = event.Usage.OutputTokens
		case "error":
			message := "Claude-compatible provider stream error"
			if event.Error != nil && event.Error.Message != "" {
				message = event.Error.Message
			}
			return out, &protocol.Error{Category: protocol.ErrorProvider, Message: message}
		}
	}
	if err := scanner.Err(); err != nil {
		return out, err
	}
	indices := make([]int, 0, len(partials))
	for index := range partials {
		indices = append(indices, index)
	}
	sort.Ints(indices)
	for _, index := range indices {
		if call := partials[index]; call != nil {
			arguments := call.arguments
			if arguments == "" {
				arguments = "{}"
			}
			out.ToolCalls = append(out.ToolCalls, protocol.ToolCall{ID: call.id, Name: call.name, Arguments: arguments})
		}
	}
	return out, nil
}

func classifyTransport(err error) error {
	if err == context.Canceled {
		return &protocol.Error{Category: protocol.ErrorCancelled, Message: "provider request cancelled"}
	}
	if err == context.DeadlineExceeded {
		return &protocol.Error{Category: protocol.ErrorTimeout, Message: "provider request timed out", Retryable: true}
	}
	return &protocol.Error{Category: protocol.ErrorUnavailable, Message: fmt.Sprintf("provider unavailable: %v", err), Retryable: true}
}

func classifyHTTP(status int, body string) error {
	category := protocol.ErrorProvider
	retryable := status >= 500
	if status == http.StatusUnauthorized || status == http.StatusForbidden {
		category = protocol.ErrorAuthentication
	}
	if status == http.StatusTooManyRequests {
		category = protocol.ErrorRateLimit
		retryable = true
	}
	return &protocol.Error{Category: category, Message: fmt.Sprintf("provider HTTP %d: %s", status, body), Retryable: retryable, Status: status}
}
