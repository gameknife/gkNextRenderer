package gemini

import (
	"bufio"
	"bytes"
	"context"
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"net/url"
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
	return provider.Descriptor{ID: a.config.ID, Kind: "gemini", DisplayName: a.config.DisplayName, Models: a.config.Models, DefaultModel: a.config.DefaultModel, Configured: configured, ConfiguredReason: reason, Available: true, Capabilities: provider.Capabilities{Streaming: true, NativeTools: true, StructuredOutput: true, ReasoningControl: true, JSONMode: true}}
}

type part struct {
	Text         string `json:"text,omitempty"`
	FunctionCall *struct {
		Name string         `json:"name"`
		Args map[string]any `json:"args"`
	} `json:"functionCall,omitempty"`
	FunctionResponse map[string]any `json:"functionResponse,omitempty"`
}
type content struct {
	Role  string `json:"role,omitempty"`
	Parts []part `json:"parts"`
}
type request struct {
	SystemInstruction *content         `json:"systemInstruction,omitempty"`
	Contents          []content        `json:"contents"`
	Tools             []map[string]any `json:"tools,omitempty"`
	GenerationConfig  map[string]any   `json:"generationConfig,omitempty"`
}
type response struct {
	Candidates []struct {
		FinishReason string  `json:"finishReason"`
		Content      content `json:"content"`
	} `json:"candidates"`
	Usage struct {
		Prompt     int `json:"promptTokenCount"`
		Completion int `json:"candidatesTokenCount"`
	} `json:"usageMetadata"`
	Error *struct {
		Code            int `json:"code"`
		Message, Status string
	} `json:"error,omitempty"`
}

func (a *Adapter) Chat(ctx context.Context, in protocol.ChatRequest, sink protocol.EventSink) (protocol.ChatResponse, error) {
	body := request{GenerationConfig: map[string]any{}}
	if in.Temperature != 0 {
		body.GenerationConfig["temperature"] = in.Temperature
	}
	if in.TopP != 0 {
		body.GenerationConfig["topP"] = in.TopP
	}
	if in.MaxOutputTokens != 0 {
		body.GenerationConfig["maxOutputTokens"] = in.MaxOutputTokens
	}
	for _, m := range in.Messages {
		if m.Role == protocol.RoleSystem {
			if body.SystemInstruction == nil {
				body.SystemInstruction = &content{Parts: []part{{Text: m.Content}}}
			} else {
				body.SystemInstruction.Parts = append(body.SystemInstruction.Parts, part{Text: m.Content})
			}
			continue
		}
		role := "user"
		if m.Role == protocol.RoleAssistant {
			role = "model"
		}
		c := content{Role: role, Parts: []part{{Text: m.Content}}}
		body.Contents = append(body.Contents, c)
	}
	if len(in.Tools) > 0 {
		decls := make([]map[string]any, 0, len(in.Tools))
		for _, t := range in.Tools {
			decls = append(decls, map[string]any{"name": t.Name, "description": t.Description, "parameters": t.InputSchema})
		}
		body.Tools = []map[string]any{{"functionDeclarations": decls}}
	}
	raw, err := json.Marshal(body)
	if err != nil {
		return protocol.ChatResponse{}, err
	}
	method := "generateContent"
	if sink != nil {
		method = "streamGenerateContent"
	}
	endpoint := fmt.Sprintf("%s/models/%s:%s?key=%s", a.config.Endpoint, url.PathEscape(in.Model), method, url.QueryEscape(a.config.APIKey))
	if sink != nil {
		endpoint += "&alt=sse"
	}
	req, err := http.NewRequestWithContext(ctx, http.MethodPost, endpoint, bytes.NewReader(raw))
	if err != nil {
		return protocol.ChatResponse{}, err
	}
	req.Header.Set("Content-Type", "application/json")
	resp, err := a.config.HTTP.Do(req)
	if err != nil {
		return protocol.ChatResponse{}, fmt.Errorf("Gemini unavailable: %w", err)
	}
	defer resp.Body.Close()
	if resp.StatusCode < 200 || resp.StatusCode >= 300 {
		b, _ := io.ReadAll(io.LimitReader(resp.Body, 64<<10))
		return protocol.ChatResponse{}, fmt.Errorf("Gemini HTTP %d: %s", resp.StatusCode, b)
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
		line := strings.TrimSpace(s.Text())
		if !strings.HasPrefix(line, "data:") {
			continue
		}
		var chunk response
		if err := json.Unmarshal([]byte(strings.TrimSpace(strings.TrimPrefix(line, "data:"))), &chunk); err != nil {
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
		if normalized.FinishReason != "" {
			out.FinishReason = normalized.FinishReason
		}
		out.Usage = normalized.Usage
	}
	return out, s.Err()
}
func normalize(in response) (protocol.ChatResponse, error) {
	if in.Error != nil {
		return protocol.ChatResponse{}, fmt.Errorf("Gemini: %s", in.Error.Message)
	}
	out := protocol.ChatResponse{Usage: protocol.Usage{PromptTokens: in.Usage.Prompt, CompletionTokens: in.Usage.Completion}}
	for _, candidate := range in.Candidates {
		out.FinishReason = candidate.FinishReason
		for i, p := range candidate.Content.Parts {
			out.Content += p.Text
			if p.FunctionCall != nil {
				args, _ := json.Marshal(p.FunctionCall.Args)
				out.ToolCalls = append(out.ToolCalls, protocol.ToolCall{ID: fmt.Sprintf("gemini-%d", i), Name: p.FunctionCall.Name, Arguments: string(args)})
			}
		}
		break
	}
	return out, nil
}
