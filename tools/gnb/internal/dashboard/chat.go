package dashboard

import (
	"crypto/rand"
	"encoding/hex"
	"fmt"
	"sync"
	"time"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/llm"
)

type ChatStore struct {
	mu       sync.Mutex
	sessions map[string]*ChatSession
}

type ChatSession struct {
	ID        string
	ModelID   string
	Messages  []llm.ChatMessage
	UpdatedAt time.Time
}

func NewChatStore() *ChatStore {
	return &ChatStore{sessions: map[string]*ChatSession{}}
}

func (s *ChatStore) Get(id string, defaultModel string) *ChatSession {
	s.mu.Lock()
	defer s.mu.Unlock()
	if id != "" {
		if sess, ok := s.sessions[id]; ok {
			return cloneChatSession(sess)
		}
	}
	sess := &ChatSession{
		ID:        newChatSessionID(),
		ModelID:   defaultModel,
		UpdatedAt: time.Now(),
	}
	s.sessions[sess.ID] = sess
	return cloneChatSession(sess)
}

func (s *ChatStore) Reset(id string, modelID string) *ChatSession {
	s.mu.Lock()
	defer s.mu.Unlock()
	if id == "" {
		id = newChatSessionID()
	}
	sess := &ChatSession{
		ID:        id,
		ModelID:   modelID,
		UpdatedAt: time.Now(),
	}
	s.sessions[id] = sess
	return cloneChatSession(sess)
}

func (s *ChatStore) AppendExchange(id string, modelID string, userText string, assistantText string) *ChatSession {
	s.mu.Lock()
	defer s.mu.Unlock()
	sess, ok := s.sessions[id]
	if !ok {
		sess = &ChatSession{ID: id}
		s.sessions[id] = sess
	}
	sess.ModelID = modelID
	sess.Messages = append(sess.Messages,
		llm.ChatMessage{Role: "user", Content: userText},
		llm.ChatMessage{Role: "assistant", Content: assistantText},
	)
	sess.UpdatedAt = time.Now()
	return cloneChatSession(sess)
}

func cloneChatSession(sess *ChatSession) *ChatSession {
	if sess == nil {
		return nil
	}
	out := *sess
	out.Messages = append([]llm.ChatMessage(nil), sess.Messages...)
	return &out
}

func newChatSessionID() string {
	var buf [8]byte
	if _, err := rand.Read(buf[:]); err == nil {
		return hex.EncodeToString(buf[:])
	}
	return fmt.Sprintf("%d", time.Now().UnixNano())
}
