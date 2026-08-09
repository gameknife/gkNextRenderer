package session

import (
	"context"
	"fmt"
	"sync"
	"sync/atomic"
	"time"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/ai/protocol"
)

type Trace struct {
	RunID    string         `json:"runId"`
	Provider string         `json:"provider,omitempty"`
	Model    string         `json:"model,omitempty"`
	Status   string         `json:"status"`
	Usage    protocol.Usage `json:"usage,omitempty"`
	Error    string         `json:"error,omitempty"`
}

type Session struct {
	ID         string             `json:"id"`
	ProfileID  string             `json:"profileId"`
	ProviderID string             `json:"providerId,omitempty"`
	ModelID    string             `json:"modelId,omitempty"`
	Messages   []protocol.Message `json:"messages"`
	CreatedAt  time.Time          `json:"createdAt"`
	UpdatedAt  time.Time          `json:"updatedAt"`
}
type Run struct {
	ID        string
	SessionID string
	Cancel    context.CancelFunc
	StartedAt time.Time
	Trace     Trace
}
type Store struct {
	mu          sync.RWMutex
	sessions    map[string]*Session
	runs        map[string]*Run
	traces      map[string]Trace
	maxMessages int
}

var nextID atomic.Uint64

func NewStore(maxMessages int) *Store {
	if maxMessages <= 0 {
		maxMessages = 100
	}
	return &Store{sessions: map[string]*Session{}, runs: map[string]*Run{}, traces: map[string]Trace{}, maxMessages: maxMessages}
}
func (s *Store) Create(profile, providerID, modelID string) Session {
	s.mu.Lock()
	defer s.mu.Unlock()
	now := time.Now()
	session := &Session{ID: fmt.Sprintf("session-%d", nextID.Add(1)), ProfileID: profile, ProviderID: providerID, ModelID: modelID, CreatedAt: now, UpdatedAt: now}
	s.sessions[session.ID] = session
	return clone(session)
}
func (s *Store) Get(id string) (Session, bool) {
	s.mu.RLock()
	defer s.mu.RUnlock()
	session, ok := s.sessions[id]
	if !ok {
		return Session{}, false
	}
	return clone(session), true
}
func (s *Store) Append(id string, messages ...protocol.Message) error {
	s.mu.Lock()
	defer s.mu.Unlock()
	session, ok := s.sessions[id]
	if !ok {
		return fmt.Errorf("unknown session %q", id)
	}
	session.Messages = append(session.Messages, messages...)
	if len(session.Messages) > s.maxMessages {
		session.Messages = append([]protocol.Message{}, session.Messages[len(session.Messages)-s.maxMessages:]...)
	}
	session.UpdatedAt = time.Now()
	return nil
}
func (s *Store) Reset(id string) (Session, error) {
	s.mu.Lock()
	defer s.mu.Unlock()
	session, ok := s.sessions[id]
	if !ok {
		return Session{}, fmt.Errorf("unknown session %q", id)
	}
	session.Messages = nil
	session.UpdatedAt = time.Now()
	return clone(session), nil
}
func (s *Store) Close(id string) bool {
	s.mu.Lock()
	defer s.mu.Unlock()
	if _, ok := s.sessions[id]; !ok {
		return false
	}
	delete(s.sessions, id)
	for runID, run := range s.runs {
		if run.SessionID == id {
			run.Cancel()
			delete(s.runs, runID)
		}
	}
	return true
}
func (s *Store) StartRun(sessionID, runID string, parent context.Context) (context.Context, error) {
	s.mu.Lock()
	defer s.mu.Unlock()
	if _, ok := s.sessions[sessionID]; !ok {
		return nil, fmt.Errorf("unknown session %q", sessionID)
	}
	if _, exists := s.runs[runID]; exists {
		return nil, fmt.Errorf("run %q already exists", runID)
	}
	ctx, cancel := context.WithCancel(parent)
	s.runs[runID] = &Run{ID: runID, SessionID: sessionID, Cancel: cancel, StartedAt: time.Now()}
	return ctx, nil
}
func (s *Store) CancelRun(runID string) bool {
	s.mu.RLock()
	run, ok := s.runs[runID]
	s.mu.RUnlock()
	if ok {
		run.Cancel()
	}
	return ok
}
func (s *Store) FinishRun(runID string, trace Trace) {
	s.mu.Lock()
	defer s.mu.Unlock()
	if run, ok := s.runs[runID]; ok {
		run.Cancel()
		delete(s.runs, runID)
	}
	s.traces[runID] = trace
}
func (s *Store) Trace(runID string) (Trace, bool) {
	s.mu.RLock()
	defer s.mu.RUnlock()
	trace, ok := s.traces[runID]
	return trace, ok
}
func clone(in *Session) Session {
	out := *in
	out.Messages = append([]protocol.Message{}, in.Messages...)
	return out
}
