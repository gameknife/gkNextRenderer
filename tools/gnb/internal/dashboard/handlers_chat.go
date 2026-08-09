// Chat tab: the local-LLM conversation UI — building the chat view model,
// session lifecycle (new/clear/archive/switch), and the send / streaming
// endpoints. The optional tool-call smoke probe is deliberately local to the
// dashboard and never exposes repository or engine capabilities.
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

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/ai"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/ai/protocol"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/ai/router"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/config"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/llm"
)

type chatToolEvent struct {
	Step    int    `json:"step"`
	Phase   string `json:"phase"`
	Name    string `json:"name"`
	Summary string `json:"summary,omitempty"`
	Detail  string `json:"detail,omitempty"`
}

func (s *Server) buildChatVM(sessionID string, selectedOverride string, errText string, flashText string) chatVM {
	return s.buildChatVMSelection(sessionID, selectedOverride, "", errText, flashText)
}
func (s *Server) buildChatVMSelection(sessionID, selectedOverride, providerOverride, errText, flashText string) chatVM {
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
	selectedProvider := sess.ProviderID
	if selectedProvider == "" {
		selectedProvider = "localllm"
	}
	selectedProfile := sess.ProfileID
	if selectedProfile == "" {
		selectedProfile = "general"
	}
	if providerOverride != "" {
		selectedProvider = providerOverride
	}
	if selected == "" {
		selected = active
	}
	if selectedOverride != "" {
		selected = selectedOverride
	}
	contextLimit := chatContextLimit(cfg.Models, selected)
	contextUsed := EstimateChatContextTokens(s.opts.RepoRoot, sess.Messages)
	status := llm.NewServer(s.opts.RepoRoot, cfg).Status()
	layout := llm.ResolveLayout(s.opts.RepoRoot, cfg)
	vm := chatVM{
		SessionID:        sess.ID,
		SelectedModel:    selected,
		SelectedProvider: selectedProvider,
		SelectedProfile:  selectedProfile,
		Messages:         sess.Messages,
		Context: chatContextVM{
			Used:    contextUsed,
			Limit:   contextLimit,
			Percent: percentOf(contextUsed, contextLimit),
		},
		Error:         errText,
		Flash:         flashText,
		ServerRunning: status.Running,
		RunningModel:  status.Model,
		Endpoint:      fmt.Sprintf("%s:%d", status.Host, status.Port),
	}
	runtime, runtimeErr := ai.NewRuntime(s.opts.RepoRoot, s.opts.Config)
	if runtimeErr == nil {
		for _, descriptor := range runtime.Registry.Descriptors() {
			vm.Providers = append(vm.Providers, chatProviderVM{ID: descriptor.ID, DisplayName: descriptor.DisplayName, Kind: descriptor.Kind, Configured: descriptor.Configured, Active: descriptor.ID == selectedProvider})
			if descriptor.ID == selectedProvider && descriptor.ID != "localllm" {
				for _, model := range descriptor.Models {
					vm.Models = append(vm.Models, chatModelVM{ID: model, Downloaded: true, Active: model == selected})
				}
			}
		}
	}
	if selectedProvider == "localllm" {
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
			ProviderID:     item.ProviderID,
			ProfileID:      item.ProfileID,
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
	s.render(w, "chat_panel", s.buildChatVM(sessionID, "", errText, ""))
}

func (s *Server) renderChatPanelFlash(w http.ResponseWriter, sessionID string, selectedModel string, flashText string) {
	s.render(w, "chat_panel", s.buildChatVM(sessionID, selectedModel, "", flashText))
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
	profileID, providerID, modelID, err := s.resolveChatSelection(r.FormValue("profile"), r.FormValue("provider"), modelID)
	if err != nil {
		s.renderChatPanel(w, r.FormValue("session_id"), err.Error())
		return
	}
	sess := s.chats.ResetSelection(strings.TrimSpace(r.FormValue("session_id")), profileID, providerID, modelID)
	s.renderChatPanel(w, sess.ID, "")
}

func (s *Server) handleChatSession(w http.ResponseWriter, r *http.Request) {
	sessionID := strings.TrimSpace(r.URL.Query().Get("id"))
	s.render(w, "chat_panel", s.buildChatVMSelection(sessionID, "", strings.TrimSpace(r.URL.Query().Get("provider")), "", ""))
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
	profileID, providerID, modelID, err := s.resolveChatSelection(r.FormValue("profile"), r.FormValue("provider"), modelID)
	if err != nil {
		s.renderChatPanel(w, r.FormValue("session_id"), err.Error())
		return
	}
	sess := s.chats.CreateSelection(profileID, providerID, modelID)
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
	profileID, providerID, modelID, err := s.resolveChatSelection(r.FormValue("profile"), r.FormValue("provider"), modelID)
	if err != nil {
		s.renderChatPanel(w, r.FormValue("session_id"), err.Error())
		return
	}
	sess := s.chats.ArchiveSelection(strings.TrimSpace(r.FormValue("session_id")), profileID, providerID, modelID)
	s.renderChatPanel(w, sess.ID, "")
}

func (s *Server) handleChatServe(w http.ResponseWriter, r *http.Request) {
	if err := r.ParseForm(); err != nil {
		httpError(w, err)
		return
	}
	sessionID := strings.TrimSpace(r.FormValue("session_id"))
	modelID := strings.TrimSpace(r.FormValue("model"))
	providerID := strings.TrimSpace(r.FormValue("provider"))
	profileID := strings.TrimSpace(r.FormValue("profile"))
	profileID, providerID, modelID, err := s.resolveChatSelection(profileID, providerID, modelID)
	if err != nil {
		s.renderChatPanel(w, sessionID, err.Error())
		return
	}
	if providerID != "localllm" {
		s.renderChatPanel(w, sessionID, "Serve 仅适用于 LocalLlama provider")
		return
	}
	cfg, err := llm.SelectModel(s.opts.Config.External.LLM, modelID)
	if err != nil {
		s.renderChatPanel(w, sessionID, err.Error())
		return
	}
	ctx, cancel := context.WithTimeout(r.Context(), 3*time.Minute)
	defer cancel()
	srv := llm.NewServer(s.opts.RepoRoot, cfg)
	info, err := srv.EnsureRunning(ctx)
	if err != nil {
		s.renderChatPanel(w, sessionID, "启动 LLM 失败: "+err.Error())
		return
	}
	s.renderChatPanelFlash(w, sessionID, cfg.ActiveModel().ID, fmt.Sprintf("llama-server running pid=%d model=%s", info.PID, info.Model))
}

func (s *Server) handleChatStop(w http.ResponseWriter, r *http.Request) {
	if err := r.ParseForm(); err != nil {
		httpError(w, err)
		return
	}
	sessionID := strings.TrimSpace(r.FormValue("session_id"))
	modelID := strings.TrimSpace(r.FormValue("model"))
	srv := llm.NewServer(s.opts.RepoRoot, s.opts.Config.External.LLM)
	if err := srv.Stop(); err != nil {
		s.renderChatPanel(w, sessionID, "停止 LLM 失败: "+err.Error())
		return
	}
	s.renderChatPanelFlash(w, sessionID, modelID, "llama-server stopped")
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
	providerID := strings.TrimSpace(r.FormValue("provider"))
	profileID := strings.TrimSpace(r.FormValue("profile"))
	userText := strings.TrimSpace(r.FormValue("message"))
	thinking := r.FormValue("thinking") == "1"
	toolProbe := r.FormValue("tool_probe") == "1"
	maxTokens := parseChatMaxTokens(r.FormValue("max_tokens"))
	if userText == "" {
		s.renderChatPanel(w, sessionID, "请输入要发送的内容")
		return
	}
	profileID, providerID, modelID, err := s.resolveChatSelection(profileID, providerID, modelID)
	if err != nil {
		s.renderChatPanel(w, sessionID, err.Error())
		return
	}
	sess := s.chats.Get(sessionID, modelID)
	visibleMessages := append([]llm.ChatMessage(nil), sess.Messages...)
	visibleMessages = append(visibleMessages, llm.ChatMessage{Role: "user", Content: userText})
	messages := ChatMessagesWithStandardContext(s.opts.RepoRoot, visibleMessages)

	ctx, cancel := context.WithTimeout(r.Context(), 5*time.Minute)
	defer cancel()
	runtime, err := ai.NewRuntime(s.opts.RepoRoot, s.opts.Config)
	if err != nil {
		s.renderChatPanel(w, sess.ID, "AI runtime 失败: "+err.Error())
		return
	}
	converted := make([]protocol.Message, len(messages))
	for i, message := range messages {
		converted[i] = protocol.Message{Role: protocol.Role(message.Role), Content: message.Content}
	}
	request := protocol.ChatRequest{Messages: converted, Temperature: .7, MaxOutputTokens: maxTokens, EnableThinking: thinking}
	var result protocol.ChatResponse
	if toolProbe {
		result, err = runToolCallSmoke(ctx, runtime.Router, router.Overrides{Profile: profileID, Provider: providerID, Model: modelID}, request, nil)
	} else {
		result, _, err = runtime.Router.Chat(ctx, router.Overrides{Profile: profileID, Provider: providerID, Model: modelID}, request, nil)
	}
	if err != nil {
		s.renderChatPanel(w, sess.ID, "LLM 请求失败: "+err.Error())
		return
	}
	sess = s.chats.AppendExchangeSelection(sess.ID, profileID, providerID, modelID, userText, strings.TrimSpace(result.Content))
	s.renderChatPanel(w, sess.ID, "")
}

func (s *Server) handleChatSendStream(w http.ResponseWriter, r *http.Request) {
	if !setStreamCORSHeaders(w, r) {
		http.Error(w, "origin not allowed", http.StatusForbidden)
		return
	}
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
	providerID := strings.TrimSpace(r.FormValue("provider"))
	profileID := strings.TrimSpace(r.FormValue("profile"))
	userText := strings.TrimSpace(r.FormValue("message"))
	thinking := r.FormValue("thinking") == "1"
	toolProbe := r.FormValue("tool_probe") == "1"
	maxTokens := parseChatMaxTokens(r.FormValue("max_tokens"))
	if userText == "" {
		emit("error", map[string]string{"message": "请输入要发送的内容"})
		return
	}
	profileID, providerID, modelID, err := s.resolveChatSelection(profileID, providerID, modelID)
	if err != nil {
		emit("error", map[string]string{"message": err.Error()})
		return
	}
	sess := s.chats.Get(sessionID, modelID)
	if !emit("start", map[string]string{"session_id": sess.ID, "model": modelID}) {
		return
	}

	visibleMessages := append([]llm.ChatMessage(nil), sess.Messages...)
	visibleMessages = append(visibleMessages, llm.ChatMessage{Role: "user", Content: userText})
	messages := ChatMessagesWithStandardContext(s.opts.RepoRoot, visibleMessages)

	ctx, cancel := context.WithTimeout(r.Context(), 5*time.Minute)
	defer cancel()
	emit("status", map[string]string{"message": "正在请求模型..."})
	reasoningEmitted := false
	if thinking {
		emit("thinking", map[string]string{"message": "正在思考..."})
		reasoningEmitted = true
	}

	runtime, err := ai.NewRuntime(s.opts.RepoRoot, s.opts.Config)
	if err != nil {
		emit("error", map[string]string{"message": "AI runtime 失败: " + err.Error()})
		return
	}
	converted := make([]protocol.Message, len(messages))
	for i, message := range messages {
		converted[i] = protocol.Message{Role: protocol.Role(message.Role), Content: message.Content}
	}
	request := protocol.ChatRequest{Messages: converted, Temperature: .7, MaxOutputTokens: maxTokens, EnableThinking: thinking}
	sink := func(_ context.Context, event protocol.Event) error {
		switch event.Type {
		case protocol.EventReasoningDelta:
			if !reasoningEmitted {
				if !emit("thinking", map[string]string{"message": "正在思考..."}) {
					return fmt.Errorf("client disconnected")
				}
				reasoningEmitted = true
			}
		case protocol.EventContentDelta:
			if !emit("delta", map[string]string{"text": event.Content}) {
				return fmt.Errorf("client disconnected")
			}
		}
		return nil
	}
	var result protocol.ChatResponse
	var route router.Route
	if toolProbe {
		route, err = runtime.Router.Resolve(router.Overrides{Profile: profileID, Provider: providerID, Model: modelID})
		if err == nil {
			result, err = runToolCallSmoke(ctx, runtime.Router, router.Overrides{Profile: profileID, Provider: providerID, Model: modelID}, request, func(event chatToolEvent) {
				emit("tool", event)
			})
		}
		if err == nil && result.Content != "" {
			emit("delta", map[string]string{"text": result.Content})
		}
	} else {
		result, route, err = runtime.Router.Chat(ctx, router.Overrides{Profile: profileID, Provider: providerID, Model: modelID}, request, sink)
	}
	if err != nil {
		emit("error", map[string]string{"message": "LLM 请求失败: " + err.Error()})
		return
	}
	sess = s.chats.AppendExchangeSelection(sess.ID, profileID, providerID, modelID, userText, strings.TrimSpace(result.Content))
	contextLimit := chatContextLimit(s.opts.Config.External.LLM.Models, modelID)
	contextUsed := EstimateChatContextTokens(s.opts.RepoRoot, sess.Messages)
	emit("done", map[string]any{
		"session_id":    sess.ID,
		"messages":      len(sess.Messages),
		"provider":      route.Provider.Descriptor().ID,
		"model":         route.Model,
		"finish_reason": result.FinishReason,
		"prompt_tokens": result.Usage.PromptTokens,
		"output_tokens": result.Usage.CompletionTokens,
		"truncated":     result.FinishReason == "length",
		"max_tokens":    maxTokens,
		"context_used":  contextUsed,
		"context_limit": contextLimit,
		"context_pct":   percentOf(contextUsed, contextLimit),
		"session_title": sess.Title,
		"session_age":   relativeTime(sess.UpdatedAt),
	})
}

func (s *Server) handleChatSendStreamOptions(w http.ResponseWriter, r *http.Request) {
	if !setStreamCORSHeaders(w, r) {
		http.Error(w, "origin not allowed", http.StatusForbidden)
		return
	}
	w.Header().Set("Access-Control-Allow-Methods", http.MethodPost)
	w.Header().Set("Access-Control-Allow-Headers", "Content-Type")
	w.WriteHeader(http.StatusNoContent)
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

func (s *Server) resolveChatSelection(profileID, providerID, modelID string) (string, string, string, error) {
	if profileID == "" {
		profileID = "general"
	}
	runtime, err := ai.NewRuntime(s.opts.RepoRoot, s.opts.Config)
	if err != nil {
		return "", "", "", err
	}
	route, err := runtime.Router.Resolve(router.Overrides{Profile: profileID, Provider: providerID, Model: modelID})
	if err != nil {
		return "", "", "", err
	}
	descriptor := route.Provider.Descriptor()
	if route.Model != "" && len(descriptor.Models) > 0 {
		found := false
		for _, candidate := range descriptor.Models {
			if candidate == route.Model {
				found = true
				break
			}
		}
		if !found {
			return "", "", "", fmt.Errorf("model %q is not configured for provider %q", route.Model, descriptor.ID)
		}
	}
	return profileID, descriptor.ID, route.Model, nil
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
