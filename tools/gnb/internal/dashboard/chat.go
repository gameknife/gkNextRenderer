package dashboard

import (
	"crypto/rand"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"sync"
	"time"
	"unicode"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/llm"
)

const chatStoreVersion = 1
const chatStandardContextMaxChars = 64000

type ChatStore struct {
	mu       sync.Mutex
	path     string
	sessions map[string]*ChatSession
}

type ChatSession struct {
	ID        string            `json:"id"`
	Title     string            `json:"title,omitempty"`
	ModelID   string            `json:"model_id"`
	Messages  []llm.ChatMessage `json:"messages,omitempty"`
	CreatedAt time.Time         `json:"created_at"`
	UpdatedAt time.Time         `json:"updated_at"`
	Archived  bool              `json:"archived,omitempty"`
}

type chatStoreFile struct {
	Version  int            `json:"version"`
	Sessions []*ChatSession `json:"sessions"`
}

func NewChatStore(paths ...string) *ChatStore {
	path := ""
	if len(paths) > 0 {
		path = paths[0]
	}
	store := &ChatStore{
		path:     path,
		sessions: map[string]*ChatSession{},
	}
	if path != "" {
		_ = store.load()
	}
	return store
}

func (s *ChatStore) Get(id string, defaultModel string) *ChatSession {
	s.mu.Lock()
	defer s.mu.Unlock()
	if id != "" {
		if sess, ok := s.sessions[id]; ok {
			return cloneChatSession(sess)
		}
	}
	sess := s.createLocked(defaultModel)
	_ = s.saveLocked()
	return cloneChatSession(sess)
}

func (s *ChatStore) Latest(defaultModel string) *ChatSession {
	s.mu.Lock()
	defer s.mu.Unlock()
	if sess := s.latestLocked(); sess != nil {
		return cloneChatSession(sess)
	}
	sess := s.createLocked(defaultModel)
	_ = s.saveLocked()
	return cloneChatSession(sess)
}

func (s *ChatStore) Create(modelID string) *ChatSession {
	s.mu.Lock()
	defer s.mu.Unlock()
	sess := s.createLocked(modelID)
	_ = s.saveLocked()
	return cloneChatSession(sess)
}

func (s *ChatStore) Reset(id string, modelID string) *ChatSession {
	s.mu.Lock()
	defer s.mu.Unlock()
	now := time.Now()
	if id == "" {
		id = newChatSessionID()
	}
	sess := &ChatSession{
		ID:        id,
		ModelID:   modelID,
		CreatedAt: now,
		UpdatedAt: now,
	}
	s.sessions[id] = sess
	_ = s.saveLocked()
	return cloneChatSession(sess)
}

func (s *ChatStore) Archive(id string, defaultModel string) *ChatSession {
	s.mu.Lock()
	defer s.mu.Unlock()
	if sess, ok := s.sessions[id]; ok {
		sess.Archived = true
		sess.UpdatedAt = time.Now()
	}
	_ = s.saveLocked()
	if sess := s.latestLocked(); sess != nil {
		return cloneChatSession(sess)
	}
	sess := s.createLocked(defaultModel)
	_ = s.saveLocked()
	return cloneChatSession(sess)
}

func (s *ChatStore) AppendExchange(id string, modelID string, userText string, assistantText string) *ChatSession {
	s.mu.Lock()
	defer s.mu.Unlock()
	sess, ok := s.sessions[id]
	if !ok {
		sess = s.createLocked(modelID)
		if id != "" {
			delete(s.sessions, sess.ID)
			sess.ID = id
			s.sessions[id] = sess
		}
	}
	sess.ModelID = modelID
	sess.Archived = false
	if sess.CreatedAt.IsZero() {
		sess.CreatedAt = time.Now()
	}
	if sess.Title == "" {
		sess.Title = chatTitleFromText(userText)
	}
	sess.Messages = append(sess.Messages,
		llm.ChatMessage{Role: "user", Content: userText},
		llm.ChatMessage{Role: "assistant", Content: assistantText},
	)
	sess.UpdatedAt = time.Now()
	_ = s.saveLocked()
	return cloneChatSession(sess)
}

func (s *ChatStore) List() []ChatSession {
	s.mu.Lock()
	defer s.mu.Unlock()
	sessions := make([]ChatSession, 0, len(s.sessions))
	for _, sess := range s.sessions {
		if sess.Archived {
			continue
		}
		sessions = append(sessions, *cloneChatSession(sess))
	}
	sort.Slice(sessions, func(i, j int) bool {
		return sessions[i].UpdatedAt.After(sessions[j].UpdatedAt)
	})
	return sessions
}

func (s *ChatStore) createLocked(modelID string) *ChatSession {
	now := time.Now()
	sess := &ChatSession{
		ID:        newChatSessionID(),
		ModelID:   modelID,
		CreatedAt: now,
		UpdatedAt: now,
	}
	s.sessions[sess.ID] = sess
	return sess
}

func (s *ChatStore) latestLocked() *ChatSession {
	var latest *ChatSession
	for _, sess := range s.sessions {
		if sess.Archived {
			continue
		}
		if latest == nil || sess.UpdatedAt.After(latest.UpdatedAt) {
			latest = sess
		}
	}
	return latest
}

func (s *ChatStore) load() error {
	s.mu.Lock()
	defer s.mu.Unlock()
	data, err := os.ReadFile(s.path)
	if err != nil {
		if os.IsNotExist(err) {
			return nil
		}
		return err
	}
	var file chatStoreFile
	if err := json.Unmarshal(data, &file); err != nil {
		return err
	}
	for _, sess := range file.Sessions {
		if sess == nil || sess.ID == "" {
			continue
		}
		if sess.CreatedAt.IsZero() {
			sess.CreatedAt = sess.UpdatedAt
		}
		if sess.UpdatedAt.IsZero() {
			sess.UpdatedAt = sess.CreatedAt
		}
		if first := firstUserMessage(sess.Messages); first != "" && shouldRegenerateChatTitle(sess.Title, first) {
			sess.Title = chatTitleFromText(first)
		}
		s.sessions[sess.ID] = cloneChatSession(sess)
	}
	return nil
}

func (s *ChatStore) saveLocked() error {
	if s.path == "" {
		return nil
	}
	if err := os.MkdirAll(filepath.Dir(s.path), 0o755); err != nil {
		return err
	}
	sessions := make([]*ChatSession, 0, len(s.sessions))
	for _, sess := range s.sessions {
		sessions = append(sessions, cloneChatSession(sess))
	}
	sort.Slice(sessions, func(i, j int) bool {
		return sessions[i].UpdatedAt.After(sessions[j].UpdatedAt)
	})
	data, err := json.MarshalIndent(chatStoreFile{Version: chatStoreVersion, Sessions: sessions}, "", "  ")
	if err != nil {
		return err
	}
	tmp := s.path + ".tmp"
	if err := os.WriteFile(tmp, data, 0o644); err != nil {
		return err
	}
	_ = os.Remove(s.path)
	return os.Rename(tmp, s.path)
}

func cloneChatSession(sess *ChatSession) *ChatSession {
	if sess == nil {
		return nil
	}
	out := *sess
	out.Messages = append([]llm.ChatMessage(nil), sess.Messages...)
	return &out
}

func chatTitleFromText(text string) string {
	text = strings.Join(strings.Fields(text), " ")
	if text == "" {
		return "新对话"
	}
	text = strings.Trim(text, " \t\r\n。.!?？；;：:")
	prefixes := []string{
		"请搜索本地仓库，查看是否有关于", "搜索本地仓库，查看是否有关于",
		"请查源代码，", "请查源代码", "查源代码，", "查源代码",
		"请根据", "根据", "请帮我", "帮我", "请你", "请", "麻烦", "能否", "能不能",
		"可否", "可以帮我", "我希望", "我想要", "我想", "帮忙",
	}
	for changed := true; changed; {
		changed = false
		for _, prefix := range prefixes {
			next := strings.TrimSpace(strings.TrimPrefix(text, prefix))
			if next != text {
				text = strings.Trim(next, " \t\r\n，,。.!?？；;：:")
				changed = true
			}
		}
	}
	replacements := []struct {
		old string
		new string
	}{
		{"列出", ""},
		{"之前做过", ""},
		{"可否帮我回忆起", ""},
		{"帮我回忆起", ""},
		{"回忆起", "回顾"},
		{"当时的", ""},
		{"总结一下", "总结"},
		{"总结", "总结"},
		{"给我", ""},
		{"告诉我", ""},
		{"并说明哪些方法有实现文件需要继续查看", ""},
		{"的历史记录或相关代码", ""},
		{"相关的实现", "实现"},
		{"实现细节", "实现"},
		{"相关代码", ""},
		{"历史记录", ""},
	}
	for _, repl := range replacements {
		text = strings.ReplaceAll(text, repl.old, repl.new)
	}
	text = strings.Trim(strings.Join(strings.Fields(text), " "), " \t\r\n，,。.!?？；;：:")
	text = strings.ReplaceAll(text, "实现，实现", "实现")
	text = strings.ReplaceAll(text, "总结，总结", "总结")
	text = strings.TrimSuffix(text, "的")
	text = strings.TrimSpace(text)
	if text == "" {
		return "新对话"
	}
	runes := []rune(text)
	if len(runes) > 28 {
		return string(runes[:28]) + "..."
	}
	return text
}

func shouldRegenerateChatTitle(title string, firstUserText string) bool {
	title = strings.Join(strings.Fields(title), " ")
	first := strings.Join(strings.Fields(firstUserText), " ")
	if title == "" || title == first {
		return true
	}
	if strings.HasSuffix(title, "...") && strings.HasPrefix(first, strings.TrimSuffix(title, "...")) {
		return true
	}
	promptMarkers := []string{"请", "帮我", "可否", "能否", "能不能", "我想", "我希望", "告诉我", "给我", "搜索本地仓库"}
	for _, marker := range promptMarkers {
		if strings.Contains(title, marker) {
			return true
		}
	}
	return false
}

func firstUserMessage(messages []llm.ChatMessage) string {
	for _, msg := range messages {
		if msg.Role == "user" {
			return msg.Content
		}
	}
	return ""
}

func EstimateChatTokens(messages []llm.ChatMessage) int {
	total := 3
	for _, msg := range messages {
		total += 8 + estimateTextTokens(msg.Role) + estimateTextTokens(msg.Content)
	}
	if total < 0 {
		return 0
	}
	return total
}

func ChatMessagesWithStandardContext(repoRoot string, messages []llm.ChatMessage) []llm.ChatMessage {
	out := make([]llm.ChatMessage, 0, len(messages)+1)
	if context := loadChatStandardContext(repoRoot); context != "" {
		out = append(out, llm.ChatMessage{Role: "system", Content: context})
	}
	out = append(out, messages...)
	return out
}

func EstimateChatContextTokens(repoRoot string, messages []llm.ChatMessage) int {
	return EstimateChatTokens(ChatMessagesWithStandardContext(repoRoot, messages))
}

func loadChatStandardContext(repoRoot string) string {
	data, err := os.ReadFile(filepath.Join(repoRoot, "AGENTS.md"))
	if err != nil {
		return ""
	}
	content := strings.TrimSpace(string(data))
	if content == "" {
		return ""
	}
	if len(content) > chatStandardContextMaxChars {
		content = content[:chatStandardContextMaxChars] + "\n... [AGENTS.md truncated]\n"
	}
	return "标准仓库上下文来自 AGENTS.md。请在本会话中遵守这些仓库规则、沟通偏好和工程约束。\n\n" + content
}

func estimateTextTokens(text string) int {
	tokens := 0
	asciiRunes := 0
	flushASCII := func() {
		if asciiRunes > 0 {
			tokens += (asciiRunes + 3) / 4
			asciiRunes = 0
		}
	}
	for _, r := range text {
		if unicode.IsSpace(r) {
			flushASCII()
			continue
		}
		if r <= unicode.MaxASCII {
			asciiRunes++
			continue
		}
		flushASCII()
		tokens++
	}
	flushASCII()
	return tokens
}

func newChatSessionID() string {
	var buf [8]byte
	if _, err := rand.Read(buf[:]); err == nil {
		return hex.EncodeToString(buf[:])
	}
	return fmt.Sprintf("%d", time.Now().UnixNano())
}
