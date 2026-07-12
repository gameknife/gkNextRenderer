package openaicompat

import (
	"bufio"
	"bytes"
	"context"
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"strings"
	"time"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/ai/protocol"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/ai/provider"
)

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
	return provider.Descriptor{ID: a.config.ID, Kind: "openai-compatible", DisplayName: a.config.DisplayName,
		Models: a.config.Models, DefaultModel: a.config.DefaultModel, Configured: configured, ConfiguredReason: reason,
		Available: true, Capabilities: provider.Capabilities{Streaming: true, NativeTools: true, StructuredOutput: true, ReasoningControl: true, JSONMode: true}}
}

type request struct {
	Model       string           `json:"model"`
	Messages    []map[string]any `json:"messages"`
	Tools       []map[string]any `json:"tools,omitempty"`
	Temperature float64          `json:"temperature,omitempty"`
	TopP        float64          `json:"top_p,omitempty"`
	MaxTokens   int              `json:"max_tokens,omitempty"`
	Stream      bool             `json:"stream"`
}

type response struct {
	Choices []struct {
		FinishReason string `json:"finish_reason"`
		Message      struct {
			Content   string `json:"content"`
			ToolCalls []struct {
				ID       string `json:"id"`
				Function struct {
					Name      string          `json:"name"`
					Arguments json.RawMessage `json:"arguments"`
				} `json:"function"`
			} `json:"tool_calls"`
		} `json:"message"`
		Delta struct {
			Content          string `json:"content"`
			ReasoningContent string `json:"reasoning_content"`
			ToolCalls        []struct {
				Index    int    `json:"index"`
				ID       string `json:"id"`
				Function struct {
					Name      string `json:"name"`
					Arguments string `json:"arguments"`
				} `json:"function"`
			} `json:"tool_calls"`
		} `json:"delta"`
	} `json:"choices"`
	Usage struct {
		Prompt     int `json:"prompt_tokens"`
		Completion int `json:"completion_tokens"`
	} `json:"usage"`
	Error *struct {
		Message string `json:"message"`
		Type    string `json:"type"`
	} `json:"error,omitempty"`
}

func (a *Adapter) Chat(ctx context.Context, in protocol.ChatRequest, sink protocol.EventSink) (protocol.ChatResponse, error) {
	body := request{Model: in.Model, Temperature: in.Temperature, TopP: in.TopP, MaxTokens: in.MaxOutputTokens, Stream: sink != nil}
	for _, m := range in.Messages {
		item := map[string]any{"role": m.Role, "content": m.Content}
		if m.Name != "" {
			item["name"] = m.Name
		}
		if m.ToolCallID != "" {
			item["tool_call_id"] = m.ToolCallID
		}
		if len(m.ToolCalls) > 0 {
			calls := make([]map[string]any, 0, len(m.ToolCalls))
			for _, call := range m.ToolCalls {
				calls = append(calls, map[string]any{"id": call.ID, "type": "function", "function": map[string]any{"name": call.Name, "arguments": call.Arguments}})
			}
			item["tool_calls"] = calls
		}
		body.Messages = append(body.Messages, item)
	}
	for _, tool := range in.Tools {
		body.Tools = append(body.Tools, map[string]any{"type": "function", "function": map[string]any{"name": tool.Name, "description": tool.Description, "parameters": tool.InputSchema}})
	}
	raw, err := json.Marshal(body)
	if err != nil {
		return protocol.ChatResponse{}, err
	}
	req, err := http.NewRequestWithContext(ctx, http.MethodPost, a.config.Endpoint+"/chat/completions", bytes.NewReader(raw))
	if err != nil {
		return protocol.ChatResponse{}, err
	}
	req.Header.Set("Content-Type", "application/json")
	req.Header.Set("Authorization", "Bearer "+a.config.APIKey)
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
	return a.readStream(ctx, resp.Body, sink)
}

func (a *Adapter) readStream(ctx context.Context, body io.Reader, sink protocol.EventSink) (protocol.ChatResponse, error) {
	var out protocol.ChatResponse
	type partialCall struct{ id, name, args string }
	partials := map[int]*partialCall{}
	s := bufio.NewScanner(body)
	s.Buffer(make([]byte, 0, 64<<10), 4<<20)
	for s.Scan() {
		line := strings.TrimSpace(s.Text())
		if !strings.HasPrefix(line, "data:") {
			continue
		}
		data := strings.TrimSpace(strings.TrimPrefix(line, "data:"))
		if data == "[DONE]" {
			break
		}
		var chunk response
		if err := json.Unmarshal([]byte(data), &chunk); err != nil {
			return out, fmt.Errorf("decode provider stream: %w", err)
		}
		for _, choice := range chunk.Choices {
			if choice.Delta.Content != "" {
				out.Content += choice.Delta.Content
				if err := sink(ctx, protocol.Event{Type: protocol.EventContentDelta, Content: choice.Delta.Content}); err != nil {
					return out, err
				}
			}
			if choice.Delta.ReasoningContent != "" {
				if err := sink(ctx, protocol.Event{Type: protocol.EventReasoningDelta, Content: choice.Delta.ReasoningContent}); err != nil {
					return out, err
				}
			}
			if choice.FinishReason != "" {
				out.FinishReason = choice.FinishReason
			}
			for _, call := range choice.Delta.ToolCalls {
				partial := partials[call.Index]
				if partial == nil {
					partial = &partialCall{}
					partials[call.Index] = partial
				}
				if call.ID != "" {
					partial.id = call.ID
				}
				if call.Function.Name != "" {
					partial.name = call.Function.Name
				}
				partial.args += call.Function.Arguments
			}
		}
		out.Usage = protocol.Usage{PromptTokens: chunk.Usage.Prompt, CompletionTokens: chunk.Usage.Completion}
	}
	if err := s.Err(); err != nil {
		return out, err
	}
	for index := 0; index < len(partials); index++ {
		if call := partials[index]; call != nil {
			out.ToolCalls = append(out.ToolCalls, protocol.ToolCall{ID: call.id, Name: call.name, Arguments: call.args})
		}
	}
	return out, nil
}

func normalize(in response) (protocol.ChatResponse, error) {
	if in.Error != nil {
		return protocol.ChatResponse{}, &protocol.Error{Category: protocol.ErrorProvider, Message: in.Error.Message}
	}
	if len(in.Choices) == 0 {
		return protocol.ChatResponse{}, fmt.Errorf("provider returned no choices")
	}
	c := in.Choices[0]
	out := protocol.ChatResponse{Content: c.Message.Content, FinishReason: c.FinishReason, Usage: protocol.Usage{PromptTokens: in.Usage.Prompt, CompletionTokens: in.Usage.Completion}}
	for _, call := range c.Message.ToolCalls {
		args := string(call.Function.Arguments)
		if len(args) > 0 && args[0] == '"' {
			var decoded string
			if json.Unmarshal(call.Function.Arguments, &decoded) == nil {
				args = decoded
			}
		}
		out.ToolCalls = append(out.ToolCalls, protocol.ToolCall{ID: call.ID, Name: call.Function.Name, Arguments: args})
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
	retry := status >= 500
	if status == 401 || status == 403 {
		category = protocol.ErrorAuthentication
	}
	if status == 429 {
		category = protocol.ErrorRateLimit
		retry = true
	}
	return &protocol.Error{Category: category, Message: fmt.Sprintf("provider HTTP %d: %s", status, body), Retryable: retry, Status: status}
}
