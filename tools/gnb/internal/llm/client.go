package llm

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
)

// ChatMessage matches the OpenAI chat-completions schema that llama-server
// implements at /v1/chat/completions.
type ChatMessage struct {
	Role    string `json:"role"`
	Content string `json:"content"`
}

type ChatRequest struct {
	Model              string         `json:"model,omitempty"`
	Messages           []ChatMessage  `json:"messages"`
	Temperature        float64        `json:"temperature,omitempty"`
	TopP               float64        `json:"top_p,omitempty"`
	MaxTokens          int            `json:"max_tokens,omitempty"`
	Stream             bool           `json:"stream"`
	ChatTemplateKwargs map[string]any `json:"chat_template_kwargs,omitempty"`
}

type ChatResponse struct {
	Choices []struct {
		Message      ChatMessage `json:"message"`
		FinishReason string      `json:"finish_reason"`
	} `json:"choices"`
}

// Client talks to a running llama-server.
type Client struct {
	BaseURL string
	HTTP    *http.Client
}

func NewClient(baseURL string) *Client {
	return &Client{
		BaseURL: baseURL,
		HTTP:    &http.Client{Timeout: 5 * time.Minute},
	}
}

func (c *Client) Chat(ctx context.Context, req ChatRequest) (string, error) {
	setThinkingDefault(&req)
	body, err := json.Marshal(req)
	if err != nil {
		return "", err
	}
	httpReq, err := http.NewRequestWithContext(ctx, "POST", c.BaseURL+"/v1/chat/completions", bytes.NewReader(body))
	if err != nil {
		return "", err
	}
	httpReq.Header.Set("Content-Type", "application/json")
	resp, err := c.HTTP.Do(httpReq)
	if err != nil {
		return "", err
	}
	defer resp.Body.Close()
	raw, err := io.ReadAll(resp.Body)
	if err != nil {
		return "", err
	}
	if resp.StatusCode < 200 || resp.StatusCode >= 300 {
		return "", fmt.Errorf("llama-server %d: %s", resp.StatusCode, string(raw))
	}
	var out ChatResponse
	if err := json.Unmarshal(raw, &out); err != nil {
		return "", fmt.Errorf("decode chat response: %w (body: %s)", err, string(raw))
	}
	if len(out.Choices) == 0 {
		return "", fmt.Errorf("llama-server returned no choices")
	}
	return out.Choices[0].Message.Content, nil
}

type StreamDelta struct {
	Text         string
	Reasoning    string
	FinishReason string
}

// ChatStream sends a streaming chat-completions request and calls onDelta for
// every content or reasoning delta emitted by llama-server.
func (c *Client) ChatStream(ctx context.Context, req ChatRequest, onDelta func(StreamDelta) error) error {
	req.Stream = true
	setThinkingDefault(&req)
	body, err := json.Marshal(req)
	if err != nil {
		return err
	}
	httpReq, err := http.NewRequestWithContext(ctx, "POST", c.BaseURL+"/v1/chat/completions", bytes.NewReader(body))
	if err != nil {
		return err
	}
	httpReq.Header.Set("Content-Type", "application/json")
	httpReq.Header.Set("Accept", "text/event-stream")
	resp, err := c.HTTP.Do(httpReq)
	if err != nil {
		return err
	}
	defer resp.Body.Close()
	if resp.StatusCode < 200 || resp.StatusCode >= 300 {
		raw, _ := io.ReadAll(resp.Body)
		return fmt.Errorf("llama-server %d: %s", resp.StatusCode, string(raw))
	}

	scanner := bufio.NewScanner(resp.Body)
	scanner.Buffer(make([]byte, 0, 64*1024), 1024*1024)
	for scanner.Scan() {
		line := strings.TrimSpace(scanner.Text())
		if line == "" || strings.HasPrefix(line, ":") || !strings.HasPrefix(line, "data:") {
			continue
		}
		data := strings.TrimSpace(strings.TrimPrefix(line, "data:"))
		if data == "[DONE]" {
			return nil
		}
		delta, err := parseStreamDelta(data)
		if err != nil {
			return err
		}
		if delta.Text != "" || delta.Reasoning != "" || delta.FinishReason != "" {
			if err := onDelta(delta); err != nil {
				return err
			}
		}
	}
	if err := scanner.Err(); err != nil {
		return err
	}
	return nil
}

func setThinkingDefault(req *ChatRequest) {
	if req.ChatTemplateKwargs == nil {
		req.ChatTemplateKwargs = map[string]any{"enable_thinking": false}
		return
	}
	if _, ok := req.ChatTemplateKwargs["enable_thinking"]; !ok {
		req.ChatTemplateKwargs["enable_thinking"] = false
	}
}

func parseStreamDelta(data string) (StreamDelta, error) {
	var chunk struct {
		Content          string `json:"content"`
		ReasoningContent string `json:"reasoning_content"`
		Choices          []struct {
			FinishReason string `json:"finish_reason"`
			Delta        struct {
				Content          string `json:"content"`
				ReasoningContent string `json:"reasoning_content"`
			} `json:"delta"`
			Message ChatMessage `json:"message"`
		} `json:"choices"`
	}
	if err := json.Unmarshal([]byte(data), &chunk); err != nil {
		return StreamDelta{}, fmt.Errorf("decode chat stream chunk: %w (data: %s)", err, data)
	}
	if chunk.Content != "" {
		return StreamDelta{Text: chunk.Content}, nil
	}
	if chunk.ReasoningContent != "" {
		return StreamDelta{Reasoning: chunk.ReasoningContent}, nil
	}
	for _, choice := range chunk.Choices {
		if choice.FinishReason != "" {
			return StreamDelta{FinishReason: choice.FinishReason}, nil
		}
		if choice.Delta.Content != "" {
			return StreamDelta{Text: choice.Delta.Content}, nil
		}
		if choice.Delta.ReasoningContent != "" {
			return StreamDelta{Reasoning: choice.Delta.ReasoningContent}, nil
		}
		if choice.Message.Content != "" {
			return StreamDelta{Text: choice.Message.Content}, nil
		}
	}
	return StreamDelta{}, nil
}
