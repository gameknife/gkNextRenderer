// Chat tab: the local-LLM conversation UI — building the chat view model,
// session lifecycle (new/clear/archive/switch), and the send / streaming
// endpoints. Tool-calling logic lives in chat_tools.go.
package dashboard

import (
	"context"
	"encoding/json"
	"fmt"
	"net/http"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"time"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/config"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/llm"
)

func (s *Server) buildChatVM(sessionID string, errText string) chatVM {
	if s.chats == nil {
		s.chats = NewChatStore(chatStorePath(s.opts))
	}
	cfg := s.opts.Config.External.LLM
	active := cfg.ActiveModel().ID
	var sess *ChatSession
	if sessionID == "" {
		sess = s.chats.Latest(active)
	} else {
		sess = s.chats.Get(sessionID, active)
	}
	selected := sess.ModelID
	if selected == "" {
		selected = active
	}
	contextLimit := chatContextLimit(cfg.Models, selected)
	contextUsed := EstimateChatContextTokens(s.opts.RepoRoot, sess.Messages)
	status := llm.NewServer(s.opts.RepoRoot, cfg).Status()
	layout := llm.ResolveLayout(s.opts.RepoRoot, cfg)
	vm := chatVM{
		SessionID:     sess.ID,
		SelectedModel: selected,
		Messages:      sess.Messages,
		Context: chatContextVM{
			Used:    contextUsed,
			Limit:   contextLimit,
			Percent: percentOf(contextUsed, contextLimit),
		},
		Error:         errText,
		ServerRunning: status.Running,
		RunningModel:  status.Model,
		Endpoint:      fmt.Sprintf("%s:%d", status.Host, status.Port),
	}
	for _, model := range cfg.Models {
		_, statErr := os.Stat(layout.ModelPath(model))
		vm.Models = append(vm.Models, chatModelVM{
			ID:         model.ID,
			ContextN:   model.ContextN,
			Downloaded: statErr == nil,
			Active:     model.ID == selected,
			Running:    status.Running && status.Model == model.ID,
		})
	}
	for _, item := range s.chats.List() {
		title := item.Title
		if title == "" {
			title = "新对话"
		}
		limit := chatContextLimit(cfg.Models, item.ModelID)
		used := EstimateChatContextTokens(s.opts.RepoRoot, item.Messages)
		vm.Sessions = append(vm.Sessions, chatSessionVM{
			ID:             item.ID,
			Title:          title,
			ModelID:        item.ModelID,
			UpdatedAt:      item.UpdatedAt,
			RelativeTime:   relativeTime(item.UpdatedAt),
			MessageCount:   len(item.Messages),
			Active:         item.ID == sess.ID,
			ContextUsed:    used,
			ContextLimit:   limit,
			ContextPercent: percentOf(used, limit),
		})
	}
	return vm
}

func (s *Server) renderChatPanel(w http.ResponseWriter, sessionID string, errText string) {
	s.render(w, "chat_panel", s.buildChatVM(sessionID, errText))
}

func (s *Server) handleChatClear(w http.ResponseWriter, r *http.Request) {
	if s.chats == nil {
		s.chats = NewChatStore(chatStorePath(s.opts))
	}
	if err := r.ParseForm(); err != nil {
		httpError(w, err)
		return
	}
	modelID := strings.TrimSpace(r.FormValue("model"))
	cfg, err := llm.SelectModel(s.opts.Config.External.LLM, modelID)
	if err != nil {
		s.renderChatPanel(w, r.FormValue("session_id"), err.Error())
		return
	}
	sess := s.chats.Reset(strings.TrimSpace(r.FormValue("session_id")), cfg.ActiveModel().ID)
	s.renderChatPanel(w, sess.ID, "")
}

func (s *Server) handleChatSession(w http.ResponseWriter, r *http.Request) {
	sessionID := strings.TrimSpace(r.URL.Query().Get("id"))
	s.renderChatPanel(w, sessionID, "")
}

func (s *Server) handleChatNew(w http.ResponseWriter, r *http.Request) {
	if s.chats == nil {
		s.chats = NewChatStore(chatStorePath(s.opts))
	}
	if err := r.ParseForm(); err != nil {
		httpError(w, err)
		return
	}
	modelID := strings.TrimSpace(r.FormValue("model"))
	cfg, err := llm.SelectModel(s.opts.Config.External.LLM, modelID)
	if err != nil {
		s.renderChatPanel(w, r.FormValue("session_id"), err.Error())
		return
	}
	sess := s.chats.Create(cfg.ActiveModel().ID)
	s.renderChatPanel(w, sess.ID, "")
}

func (s *Server) handleChatArchive(w http.ResponseWriter, r *http.Request) {
	if s.chats == nil {
		s.chats = NewChatStore(chatStorePath(s.opts))
	}
	if err := r.ParseForm(); err != nil {
		httpError(w, err)
		return
	}
	modelID := strings.TrimSpace(r.FormValue("model"))
	cfg, err := llm.SelectModel(s.opts.Config.External.LLM, modelID)
	if err != nil {
		s.renderChatPanel(w, r.FormValue("session_id"), err.Error())
		return
	}
	sess := s.chats.Archive(strings.TrimSpace(r.FormValue("session_id")), cfg.ActiveModel().ID)
	s.renderChatPanel(w, sess.ID, "")
}

func (s *Server) handleChatSend(w http.ResponseWriter, r *http.Request) {
	if s.chats == nil {
		s.chats = NewChatStore(chatStorePath(s.opts))
	}
	if err := r.ParseForm(); err != nil {
		httpError(w, err)
		return
	}
	sessionID := strings.TrimSpace(r.FormValue("session_id"))
	modelID := strings.TrimSpace(r.FormValue("model"))
	userText := strings.TrimSpace(r.FormValue("message"))
	thinking := r.FormValue("thinking") == "1"
	maxTokens := parseChatMaxTokens(r.FormValue("max_tokens"))
	if userText == "" {
		s.renderChatPanel(w, sessionID, "请输入要发送的内容")
		return
	}
	cfg, err := llm.SelectModel(s.opts.Config.External.LLM, modelID)
	if err != nil {
		s.renderChatPanel(w, sessionID, err.Error())
		return
	}
	modelID = cfg.ActiveModel().ID
	sess := s.chats.Get(sessionID, modelID)
	visibleMessages := append([]llm.ChatMessage(nil), sess.Messages...)
	visibleMessages = append(visibleMessages, llm.ChatMessage{Role: "user", Content: userText})
	messages := ChatMessagesWithStandardContext(s.opts.RepoRoot, visibleMessages)

	ctx, cancel := context.WithTimeout(r.Context(), 5*time.Minute)
	defer cancel()
	srv := llm.NewServer(s.opts.RepoRoot, cfg)
	if _, err := srv.EnsureRunning(ctx); err != nil {
		s.renderChatPanel(w, sess.ID, "启动 LLM 失败: "+err.Error())
		return
	}
	client := llm.NewClient(srv.BaseURL())
	messages, _, err = s.runChatToolLoop(ctx, client, modelID, messages, nil)
	if err != nil {
		s.renderChatPanel(w, sess.ID, "工具调用失败: "+err.Error())
		return
	}
	reply, err := client.Chat(ctx, llm.ChatRequest{
		Model:       modelID,
		Messages:    messages,
		Temperature: 0.7,
		MaxTokens:   maxTokens,
		ChatTemplateKwargs: map[string]any{
			"enable_thinking": thinking,
		},
	})
	if err != nil {
		s.renderChatPanel(w, sess.ID, "LLM 请求失败: "+err.Error())
		return
	}
	sess = s.chats.AppendExchange(sess.ID, modelID, userText, strings.TrimSpace(reply))
	s.renderChatPanel(w, sess.ID, "")
}

func (s *Server) handleChatSendStream(w http.ResponseWriter, r *http.Request) {
	if s.chats == nil {
		s.chats = NewChatStore(chatStorePath(s.opts))
	}
	if err := r.ParseForm(); err != nil {
		httpError(w, err)
		return
	}
	flusher, ok := w.(http.Flusher)
	if !ok {
		http.Error(w, "streaming unsupported", http.StatusInternalServerError)
		return
	}
	w.Header().Set("Content-Type", "text/event-stream; charset=utf-8")
	w.Header().Set("Cache-Control", "no-cache, no-transform")
	w.Header().Set("Connection", "keep-alive")
	w.Header().Set("X-Accel-Buffering", "no")

	emit := func(event string, payload any) bool {
		data, err := json.Marshal(payload)
		if err != nil {
			data = []byte(`{"error":"encode stream payload failed"}`)
		}
		if _, err := fmt.Fprintf(w, "event: %s\ndata: %s\n\n", event, data); err != nil {
			return false
		}
		flusher.Flush()
		return true
	}

	sessionID := strings.TrimSpace(r.FormValue("session_id"))
	modelID := strings.TrimSpace(r.FormValue("model"))
	userText := strings.TrimSpace(r.FormValue("message"))
	thinking := r.FormValue("thinking") == "1"
	maxTokens := parseChatMaxTokens(r.FormValue("max_tokens"))
	if userText == "" {
		emit("error", map[string]string{"message": "请输入要发送的内容"})
		return
	}
	cfg, err := llm.SelectModel(s.opts.Config.External.LLM, modelID)
	if err != nil {
		emit("error", map[string]string{"message": err.Error()})
		return
	}
	modelID = cfg.ActiveModel().ID
	sess := s.chats.Get(sessionID, modelID)
	if !emit("start", map[string]string{"session_id": sess.ID, "model": modelID}) {
		return
	}

	visibleMessages := append([]llm.ChatMessage(nil), sess.Messages...)
	visibleMessages = append(visibleMessages, llm.ChatMessage{Role: "user", Content: userText})
	messages := ChatMessagesWithStandardContext(s.opts.RepoRoot, visibleMessages)

	ctx, cancel := context.WithTimeout(r.Context(), 5*time.Minute)
	defer cancel()
	srv := llm.NewServer(s.opts.RepoRoot, cfg)
	emit("status", map[string]string{"message": "正在准备本地模型..."})
	if _, err := srv.EnsureRunning(ctx); err != nil {
		emit("error", map[string]string{"message": "启动 LLM 失败: " + err.Error()})
		return
	}
	client := llm.NewClient(srv.BaseURL())
	messages, _, err = s.runChatToolLoop(ctx, client, modelID, messages, func(event chatToolEvent) {
		emit("tool", event)
	})
	if err != nil {
		emit("error", map[string]string{"message": "工具调用失败: " + err.Error()})
		return
	}
	emit("status", map[string]string{"message": "模型已就绪，正在生成..."})
	reasoningEmitted := false
	if thinking {
		emit("thinking", map[string]string{"message": "正在思考..."})
		reasoningEmitted = true
	}

	var reply strings.Builder
	finishReason := ""
	err = client.ChatStream(ctx, llm.ChatRequest{
		Model:       modelID,
		Messages:    messages,
		Temperature: 0.7,
		MaxTokens:   maxTokens,
		ChatTemplateKwargs: map[string]any{
			"enable_thinking": thinking,
		},
	}, func(delta llm.StreamDelta) error {
		if delta.FinishReason != "" {
			finishReason = delta.FinishReason
			return nil
		}
		if delta.Reasoning != "" {
			if !reasoningEmitted {
				if !emit("thinking", map[string]string{"message": "正在思考..."}) {
					return fmt.Errorf("client disconnected")
				}
				reasoningEmitted = true
			}
			return nil
		}
		reply.WriteString(delta.Text)
		if !emit("delta", map[string]string{"text": delta.Text}) {
			return fmt.Errorf("client disconnected")
		}
		return nil
	})
	if err != nil {
		emit("error", map[string]string{"message": "LLM 请求失败: " + err.Error()})
		return
	}
	sess = s.chats.AppendExchange(sess.ID, modelID, userText, strings.TrimSpace(reply.String()))
	contextLimit := chatContextLimit(s.opts.Config.External.LLM.Models, modelID)
	contextUsed := EstimateChatContextTokens(s.opts.RepoRoot, sess.Messages)
	emit("done", map[string]any{
		"session_id":    sess.ID,
		"messages":      len(sess.Messages),
		"finish_reason": finishReason,
		"truncated":     finishReason == "length",
		"max_tokens":    maxTokens,
		"context_used":  contextUsed,
		"context_limit": contextLimit,
		"context_pct":   percentOf(contextUsed, contextLimit),
		"session_title": sess.Title,
		"session_age":   relativeTime(sess.UpdatedAt),
	})
}

func parseChatMaxTokens(raw string) int {
	n, err := strconv.Atoi(strings.TrimSpace(raw))
	if err != nil || n == 0 {
		return defaultChatMaxTokens
	}
	if n < minChatMaxTokens {
		return minChatMaxTokens
	}
	if n > maxChatMaxTokens {
		return maxChatMaxTokens
	}
	return n
}

func chatStorePath(opts Options) string {
	layout := llm.ResolveLayout(opts.RepoRoot, opts.Config.External.LLM)
	return filepath.Join(layout.RunDir, "dashboard_chats.json")
}

func chatContextLimit(models []config.ModelConfig, modelID string) int {
	for _, model := range models {
		if model.ID == modelID {
			return model.ContextN
		}
	}
	if len(models) > 0 {
		return models[0].ContextN
	}
	return 0
}

func percentOf(used int, limit int) int {
	if used <= 0 || limit <= 0 {
		return 0
	}
	pct := int(float64(used) * 100 / float64(limit))
	if pct < 1 {
		return 1
	}
	if pct > 100 {
		return 100
	}
	return pct
}
