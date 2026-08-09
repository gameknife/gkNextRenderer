package bridge

import (
	"bufio"
	"context"
	"encoding/json"
	"fmt"
	"io"
	"sync"
	"sync/atomic"
	"time"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/ai"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/ai/protocol"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/ai/router"
	aisession "github.com/gameknife/gknextrenderer/tools/gnb/internal/ai/session"
)

const ProtocolVersion = 2
const maxFrameBytes = 4 << 20

type frame struct {
	JSONRPC string          `json:"jsonrpc"`
	ID      json.RawMessage `json:"id,omitempty"`
	Method  string          `json:"method,omitempty"`
	Params  json.RawMessage `json:"params,omitempty"`
	Result  json.RawMessage `json:"result,omitempty"`
	Error   *rpcError       `json:"error,omitempty"`
}
type rpcError struct {
	Code    int            `json:"code"`
	Message string         `json:"message"`
	Data    map[string]any `json:"data,omitempty"`
}
type Server struct {
	runtime     *ai.Runtime
	reader      io.Reader
	writer      io.Writer
	writeMu     sync.Mutex
	stateMu     sync.Mutex
	initialized bool
	closed      chan struct{}
	nextID      atomic.Uint64
	handlers    sync.WaitGroup
}

func New(runtime *ai.Runtime, reader io.Reader, writer io.Writer) *Server {
	return &Server{runtime: runtime, reader: reader, writer: writer, closed: make(chan struct{})}
}
func (s *Server) Serve(ctx context.Context) error {
	scanner := bufio.NewScanner(s.reader)
	scanner.Buffer(make([]byte, 0, 64<<10), maxFrameBytes)
	for scanner.Scan() {
		var message frame
		if err := json.Unmarshal(scanner.Bytes(), &message); err != nil {
			s.writeError(nil, -32700, "parse error", map[string]any{"category": "protocol", "retryable": false})
			continue
		}
		if message.Method == "initialize" {
			s.handle(ctx, message)
		} else {
			s.handlers.Add(1)
			go func() { defer s.handlers.Done(); s.handle(ctx, message) }()
		}
	}
	close(s.closed)
	s.handlers.Wait()
	if err := scanner.Err(); err != nil {
		return fmt.Errorf("bridge read: %w", err)
	}
	return nil
}
func (s *Server) handle(parent context.Context, message frame) {
	if message.JSONRPC != "2.0" || message.Method == "" {
		s.writeError(message.ID, -32600, "invalid request", map[string]any{"category": "protocol", "retryable": false})
		return
	}
	s.stateMu.Lock()
	initialized := s.initialized
	s.stateMu.Unlock()
	if !initialized && message.Method != "initialize" {
		s.writeError(message.ID, -32001, "initialize required", map[string]any{"category": "protocol_version", "retryable": false})
		return
	}
	var result any
	var err error
	switch message.Method {
	case "initialize":
		result, err = s.initialize(message.Params)
	case "providers.list":
		result = s.runtime.Registry.Descriptors()
	case "profiles.list":
		result = s.runtime.Config.Profiles
	case "session.create":
		var p struct{ Profile, Provider, Model string }
		err = json.Unmarshal(message.Params, &p)
		if err == nil {
			result = s.runtime.Sessions.Create(p.Profile, p.Provider, p.Model)
		}
	case "session.reset":
		var p struct{ SessionID string }
		err = json.Unmarshal(message.Params, &p)
		if err == nil {
			result, err = s.runtime.Sessions.Reset(p.SessionID)
		}
	case "session.close":
		var p struct{ SessionID string }
		err = json.Unmarshal(message.Params, &p)
		if err == nil {
			result = map[string]any{"closed": s.runtime.Sessions.Close(p.SessionID)}
		}
	case "llm.chat":
		result, err = s.chat(parent, message.Params)
	case "workflow.run":
		result, err = s.runWorkflow(parent, message.Params)
	case "run.cancel":
		var p struct{ RunID string }
		err = json.Unmarshal(message.Params, &p)
		if err == nil {
			result = map[string]any{"cancelled": s.runtime.Sessions.CancelRun(p.RunID)}
		}
	case "shutdown":
		result = map[string]any{"ok": true}
	default:
		s.writeError(message.ID, -32601, "method not found", map[string]any{"category": "protocol", "retryable": false})
		return
	}
	if err != nil {
		s.writeMappedError(message.ID, err)
		return
	}
	s.writeResult(message.ID, result)
}
func (s *Server) initialize(raw json.RawMessage) (any, error) {
	var p struct {
		ProtocolVersion int `json:"protocolVersion"`
	}
	if err := json.Unmarshal(raw, &p); err != nil {
		return nil, err
	}
	if p.ProtocolVersion != ProtocolVersion {
		return nil, &protocol.Error{Category: "protocol_version", Message: fmt.Sprintf("unsupported protocol version %d", p.ProtocolVersion)}
	}
	s.stateMu.Lock()
	s.initialized = true
	s.stateMu.Unlock()
	return map[string]any{"protocolVersion": ProtocolVersion, "server": map[string]any{"name": "gnb-ai-bridge", "version": "2"}, "capabilities": map[string]bool{"streaming": true, "structuredOutput": true, "cancellation": true}}, nil
}

type runParams struct {
	SessionID       string                   `json:"sessionId"`
	RunID           string                   `json:"runId"`
	Profile         string                   `json:"profile"`
	Provider        string                   `json:"provider"`
	Model           string                   `json:"model"`
	Messages        []protocol.Message       `json:"messages"`
	Prompt          string                   `json:"prompt"`
	MaxOutputTokens int                      `json:"maxOutputTokens"`
	EnableThinking  bool                     `json:"enableThinking"`
	ResponseFormat  *protocol.ResponseFormat `json:"responseFormat"`
	DeadlineMs      int                      `json:"deadlineMs"`
	Stateless       bool                     `json:"stateless"`
}

func (s *Server) prepareRun(parent context.Context, p *runParams) (context.Context, error) {
	if p.RunID == "" {
		p.RunID = fmt.Sprintf("run-%d", s.nextID.Add(1))
	}
	if p.SessionID == "" {
		session := s.runtime.Sessions.Create(p.Profile, p.Provider, p.Model)
		p.SessionID = session.ID
	}
	ctx, err := s.runtime.Sessions.StartRun(p.SessionID, p.RunID, parent)
	if err != nil {
		return nil, err
	}
	if p.DeadlineMs > 0 {
		deadline, _ := context.WithTimeout(ctx, time.Duration(p.DeadlineMs)*time.Millisecond)
		return deadline, nil
	}
	return ctx, nil
}
func (s *Server) chat(parent context.Context, raw json.RawMessage) (any, error) {
	var p runParams
	if err := json.Unmarshal(raw, &p); err != nil {
		return nil, err
	}
	ctx, err := s.prepareRun(parent, &p)
	if err != nil {
		return nil, err
	}
	defer func() {}()
	session, _ := s.runtime.Sessions.Get(p.SessionID)
	var messages []protocol.Message
	if !p.Stateless {
		messages = append(messages, session.Messages...)
	}
	messages = append(messages, p.Messages...)
	if p.Prompt != "" {
		messages = append(messages, protocol.Message{Role: protocol.RoleUser, Content: p.Prompt})
	}
	response, route, err := s.runtime.Router.Chat(ctx, router.Overrides{Profile: first(p.Profile, session.ProfileID), Provider: first(p.Provider, session.ProviderID), Model: first(p.Model, session.ModelID)}, protocol.ChatRequest{Messages: messages, MaxOutputTokens: p.MaxOutputTokens, EnableThinking: p.EnableThinking, ResponseFormat: p.ResponseFormat}, s.eventSink(p.RunID))
	trace := aisession.Trace{RunID: p.RunID, Status: "completed", Usage: response.Usage}
	if route.Provider != nil {
		trace.Provider = route.Provider.Descriptor().ID
		trace.Model = route.Model
	}
	if err != nil {
		trace.Status = "failed"
		trace.Error = err.Error()
	}
	s.runtime.Sessions.FinishRun(p.RunID, trace)
	if err != nil {
		return nil, err
	}
	appended := append([]protocol.Message{}, p.Messages...)
	if p.Prompt != "" {
		appended = append(appended, protocol.Message{Role: protocol.RoleUser, Content: p.Prompt})
	}
	appended = append(appended, protocol.Message{Role: protocol.RoleAssistant, Content: response.Content})
	if !p.Stateless {
		_ = s.runtime.Sessions.Append(p.SessionID, appended...)
	}
	return map[string]any{"runId": p.RunID, "sessionId": p.SessionID, "content": response.Content, "finishReason": response.FinishReason, "usage": response.Usage, "provider": trace.Provider, "model": trace.Model, "structuredOutputMode": response.StructuredOutputMode}, nil
}
func (s *Server) runWorkflow(parent context.Context, raw json.RawMessage) (any, error) {
	var p struct {
		Name  string          `json:"name"`
		Input json.RawMessage `json:"input"`
		RunID string          `json:"runId"`
	}
	if err := json.Unmarshal(raw, &p); err != nil {
		return nil, err
	}
	if p.RunID == "" {
		p.RunID = fmt.Sprintf("run-%d", s.nextID.Add(1))
	}
	result, err := s.runtime.Workflows.Run(parent, p.Name, p.Input, s.eventSink(p.RunID))
	if err != nil {
		return nil, err
	}
	return map[string]any{"runId": p.RunID, "output": json.RawMessage(result)}, nil
}
func (s *Server) eventSink(runID string) protocol.EventSink {
	return func(_ context.Context, event protocol.Event) error {
		event.RunID = runID
		return s.write(frame{JSONRPC: "2.0", Method: "run.event", Params: mustJSON(event)})
	}
}
func (s *Server) writeResult(id json.RawMessage, result any) {
	s.write(frame{JSONRPC: "2.0", ID: id, Result: mustJSON(result)})
}
func (s *Server) writeMappedError(id json.RawMessage, err error) {
	code := -32603
	category := "internal"
	retryable := false
	if structured, ok := err.(*protocol.Error); ok {
		category = string(structured.Category)
		retryable = structured.Retryable
		switch structured.Category {
		case "protocol_version":
			code = -32001
		case protocol.ErrorNotConfigured:
			code = -32002
		case protocol.ErrorUnavailable:
			code = -32003
		case protocol.ErrorTimeout:
			code = -32004
		case protocol.ErrorCancelled:
			code = -32005
		case protocol.ErrorModelBusy:
			code = -32009
		}
	}
	s.writeError(id, code, err.Error(), map[string]any{"category": category, "retryable": retryable})
}
func (s *Server) writeError(id json.RawMessage, code int, message string, data map[string]any) {
	s.write(frame{JSONRPC: "2.0", ID: id, Error: &rpcError{Code: code, Message: message, Data: data}})
}
func (s *Server) write(message frame) error {
	s.writeMu.Lock()
	defer s.writeMu.Unlock()
	raw, err := json.Marshal(message)
	if err != nil {
		return err
	}
	if len(raw) > maxFrameBytes {
		return fmt.Errorf("frame too large")
	}
	_, err = s.writer.Write(append(raw, '\n'))
	return err
}
func mustJSON(value any) json.RawMessage { raw, _ := json.Marshal(value); return raw }
func first(values ...string) string {
	for _, value := range values {
		if value != "" {
			return value
		}
	}
	return ""
}
