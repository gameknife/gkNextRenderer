package protocol

import "context"

type Role string

const (
	RoleSystem    Role = "system"
	RoleUser      Role = "user"
	RoleAssistant Role = "assistant"
	RoleTool      Role = "tool"
)

type Message struct {
	Role       Role       `json:"role"`
	Content    string     `json:"content,omitempty"`
	Name       string     `json:"name,omitempty"`
	ToolCallID string     `json:"toolCallId,omitempty"`
	ToolCalls  []ToolCall `json:"toolCalls,omitempty"`
}

type ToolCall struct {
	ID        string `json:"id"`
	Name      string `json:"name"`
	Arguments string `json:"arguments"`
}

type ToolDescriptor struct {
	Name        string         `json:"name"`
	Description string         `json:"description"`
	InputSchema map[string]any `json:"inputSchema"`
	Mutating    bool           `json:"mutating,omitempty"`
}

type ChatRequest struct {
	Model           string           `json:"model,omitempty"`
	Messages        []Message        `json:"messages"`
	Tools           []ToolDescriptor `json:"tools,omitempty"`
	Temperature     float64          `json:"temperature,omitempty"`
	TopP            float64          `json:"topP,omitempty"`
	MaxOutputTokens int              `json:"maxOutputTokens,omitempty"`
}

type Usage struct {
	PromptTokens     int `json:"promptTokens,omitempty"`
	CompletionTokens int `json:"completionTokens,omitempty"`
}

type ChatResponse struct {
	Content      string     `json:"content,omitempty"`
	ToolCalls    []ToolCall `json:"toolCalls,omitempty"`
	FinishReason string     `json:"finishReason,omitempty"`
	Usage        Usage      `json:"usage,omitempty"`
}

type EventType string

const (
	EventContentDelta   EventType = "content.delta"
	EventReasoningDelta EventType = "reasoning.delta"
	EventStepStarted    EventType = "step.started"
	EventToolCall       EventType = "tool.call"
	EventToolResult     EventType = "tool.result"
	EventRunCompleted   EventType = "run.completed"
	EventRunFailed      EventType = "run.failed"
	EventRunCancelled   EventType = "run.cancelled"
)

type Event struct {
	Type         EventType `json:"type"`
	Content      string    `json:"content,omitempty"`
	FinishReason string    `json:"finishReason,omitempty"`
	RunID        string    `json:"runId,omitempty"`
	Sequence     int       `json:"sequence,omitempty"`
	Step         int       `json:"step,omitempty"`
	CallID       string    `json:"callId,omitempty"`
	Name         string    `json:"name,omitempty"`
	Arguments    string    `json:"arguments,omitempty"`
	Usage        Usage     `json:"usage,omitempty"`
	Error        string    `json:"error,omitempty"`
}

type EventSink func(context.Context, Event) error

type ErrorCategory string

const (
	ErrorInvalidArgument ErrorCategory = "invalid_argument"
	ErrorNotConfigured   ErrorCategory = "not_configured"
	ErrorUnavailable     ErrorCategory = "unavailable"
	ErrorAuthentication  ErrorCategory = "authentication"
	ErrorQuota           ErrorCategory = "quota"
	ErrorRateLimit       ErrorCategory = "rate_limit"
	ErrorTimeout         ErrorCategory = "deadline_exceeded"
	ErrorCancelled       ErrorCategory = "cancelled"
	ErrorProvider        ErrorCategory = "provider"
	ErrorModelBusy       ErrorCategory = "model_busy"
)

type Error struct {
	Category  ErrorCategory
	Message   string
	Retryable bool
	Status    int
}

func (e *Error) Error() string { return e.Message }
