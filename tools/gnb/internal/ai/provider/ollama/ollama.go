package ollama

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
	ID, DisplayName, Endpoint, DefaultModel string
	Models                                  []string
	HTTP                                    *http.Client
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
	configured := a.config.Endpoint != ""
	reason := ""
	if !configured {
		reason = "endpoint is empty"
	}
	return provider.Descriptor{ID: a.config.ID, Kind: "ollama", DisplayName: a.config.DisplayName, Models: a.config.Models, DefaultModel: a.config.DefaultModel, Configured: configured, ConfiguredReason: reason, Available: true, Capabilities: provider.Capabilities{Streaming: true, NativeTools: true, StructuredOutput: true, JSONMode: true}}
}

type message struct {
	Role, Content string
	ToolCalls     []struct {
		Function struct {
			Name      string         `json:"name"`
			Arguments map[string]any `json:"arguments"`
		} `json:"function"`
	} `json:"tool_calls,omitempty"`
}
type request struct {
	Model    string           `json:"model"`
	Messages []message        `json:"messages"`
	Tools    []map[string]any `json:"tools,omitempty"`
	Stream   bool             `json:"stream"`
	Options  map[string]any   `json:"options,omitempty"`
	Format   any              `json:"format,omitempty"`
}
type response struct {
	Message         message `json:"message"`
	Done            bool    `json:"done"`
	DoneReason      string  `json:"done_reason"`
	PromptEvalCount int     `json:"prompt_eval_count"`
	EvalCount       int     `json:"eval_count"`
	Error           string  `json:"error,omitempty"`
}

func (a *Adapter) Chat(ctx context.Context, in protocol.ChatRequest, sink protocol.EventSink) (protocol.ChatResponse, error) {
	body := request{Model: in.Model, Stream: sink != nil, Options: map[string]any{}}
	if in.ResponseFormat != nil {
		if in.ResponseFormat.Mode == "schema" {
			body.Format = in.ResponseFormat.Schema
		} else {
			body.Format = "json"
		}
	}
	for _, m := range in.Messages {
		body.Messages = append(body.Messages, message{Role: string(m.Role), Content: m.Content})
	}
	if in.Temperature != 0 {
		body.Options["temperature"] = in.Temperature
	}
	if in.TopP != 0 {
		body.Options["top_p"] = in.TopP
	}
	if in.MaxOutputTokens != 0 {
		body.Options["num_predict"] = in.MaxOutputTokens
	}
	for _, t := range in.Tools {
		body.Tools = append(body.Tools, map[string]any{"type": "function", "function": map[string]any{"name": t.Name, "description": t.Description, "parameters": t.InputSchema}})
	}
	raw, err := json.Marshal(body)
	if err != nil {
		return protocol.ChatResponse{}, err
	}
	req, err := http.NewRequestWithContext(ctx, http.MethodPost, a.config.Endpoint+"/api/chat", bytes.NewReader(raw))
	if err != nil {
		return protocol.ChatResponse{}, err
	}
	req.Header.Set("Content-Type", "application/json")
	resp, err := a.config.HTTP.Do(req)
	if err != nil {
		return protocol.ChatResponse{}, fmt.Errorf("Ollama unavailable: %w", err)
	}
	defer resp.Body.Close()
	if resp.StatusCode < 200 || resp.StatusCode >= 300 {
		b, _ := io.ReadAll(io.LimitReader(resp.Body, 64<<10))
		return protocol.ChatResponse{}, fmt.Errorf("Ollama HTTP %d: %s", resp.StatusCode, b)
	}
	if sink == nil {
		var decoded response
		if err := json.NewDecoder(resp.Body).Decode(&decoded); err != nil {
			return protocol.ChatResponse{}, err
		}
		return normalize(decoded)
	}
	var out protocol.ChatResponse
	s := bufio.NewScanner(resp.Body)
	s.Buffer(make([]byte, 0, 64<<10), 4<<20)
	for s.Scan() {
		var chunk response
		if err := json.Unmarshal(s.Bytes(), &chunk); err != nil {
			return out, err
		}
		normalized, err := normalize(chunk)
		if err != nil {
			return out, err
		}
		if normalized.Content != "" {
			out.Content += normalized.Content
			if err := sink(ctx, protocol.Event{Type: protocol.EventContentDelta, Content: normalized.Content}); err != nil {
				return out, err
			}
		}
		out.ToolCalls = append(out.ToolCalls, normalized.ToolCalls...)
		if chunk.Done {
			out.FinishReason = chunk.DoneReason
			out.Usage = normalized.Usage
		}
	}
	return out, s.Err()
}
func normalize(in response) (protocol.ChatResponse, error) {
	if in.Error != "" {
		return protocol.ChatResponse{}, fmt.Errorf("Ollama: %s", in.Error)
	}
	out := protocol.ChatResponse{Content: in.Message.Content, FinishReason: in.DoneReason, Usage: protocol.Usage{PromptTokens: in.PromptEvalCount, CompletionTokens: in.EvalCount}}
	for i, c := range in.Message.ToolCalls {
		args, _ := json.Marshal(c.Function.Arguments)
		out.ToolCalls = append(out.ToolCalls, protocol.ToolCall{ID: fmt.Sprintf("ollama-%d", i), Name: c.Function.Name, Arguments: string(args)})
	}
	return out, nil
}
