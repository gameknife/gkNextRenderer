package dashboard

import (
	"os"
	"path/filepath"
	"strings"
	"testing"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/llm"
)

func TestChatStorePersistsSessions(t *testing.T) {
	path := filepath.Join(t.TempDir(), "dashboard_chats.json")
	store := NewChatStore(path)
	sess := store.Create("gemma-test")
	store.AppendExchange(sess.ID, "gemma-test", "请总结 NextEngine", "好的")

	reloaded := NewChatStore(path)
	items := reloaded.List()
	if len(items) != 1 {
		t.Fatalf("sessions = %d, want 1", len(items))
	}
	if items[0].Title == "请总结 NextEngine" || !strings.Contains(items[0].Title, "NextEngine") {
		t.Fatalf("title = %q", items[0].Title)
	}
	if len(items[0].Messages) != 2 {
		t.Fatalf("messages = %d, want 2", len(items[0].Messages))
	}
}

func TestChatStoreArchiveHidesSession(t *testing.T) {
	store := NewChatStore()
	sess := store.Create("gemma-test")
	store.AppendExchange(sess.ID, "gemma-test", "hello", "world")
	store.Archive(sess.ID, "gemma-test")
	if items := store.List(); len(items) != 1 {
		t.Fatalf("archive should create a replacement visible session, got %d", len(items))
	}
	if items := store.List(); items[0].ID == sess.ID {
		t.Fatalf("archived session is still visible")
	}
}

func TestEstimateChatTokens(t *testing.T) {
	sess := NewChatStore().Create("gemma-test")
	out := NewChatStore().AppendExchange(sess.ID, "gemma-test", "hello world", "你好，NextEngine")
	if got := EstimateChatTokens(out.Messages); got <= 0 {
		t.Fatalf("estimated tokens = %d, want positive", got)
	}
}

func TestChatMessagesWithStandardContextReadsAgents(t *testing.T) {
	root := t.TempDir()
	if err := os.WriteFile(filepath.Join(root, "AGENTS.md"), []byte("Always answer in Chinese."), 0o644); err != nil {
		t.Fatal(err)
	}
	messages := ChatMessagesWithStandardContext(root, []llm.ChatMessage{{Role: "user", Content: "hello"}})
	if len(messages) != 2 {
		t.Fatalf("messages = %d, want 2", len(messages))
	}
	if messages[0].Role != "system" || !containsText(messages[0].Content, "AGENTS.md") {
		t.Fatalf("standard context missing: %#v", messages[0])
	}
}

func TestChatTitleFromTextSummarizesPrompt(t *testing.T) {
	title := chatTitleFromText("请搜索本地仓库，查看是否有关于 ImGui HDR 绘制问题的历史记录或相关代码。")
	if title == "" || strings.Contains(title, "请搜索") || strings.Contains(title, "相关代码") {
		t.Fatalf("bad title = %q", title)
	}
	if !strings.Contains(title, "ImGui HDR") {
		t.Fatalf("title lost subject = %q", title)
	}
}

func TestChatTitleFromTextCleansQuestionStyle(t *testing.T) {
	title := chatTitleFromText("之前做过CSM阴影相关的实现，可否帮我回忆起当时的实现细节？")
	if strings.Contains(title, "可否") || strings.Contains(title, "帮我") || strings.Contains(title, "之前做过") {
		t.Fatalf("bad title = %q", title)
	}
	if strings.Contains(title, "实现，实现") {
		t.Fatalf("duplicated title segment = %q", title)
	}
	if !strings.Contains(title, "CSM") || !strings.Contains(title, "阴影") {
		t.Fatalf("title lost subject = %q", title)
	}
}

func containsText(s string, sub string) bool {
	return strings.Contains(s, sub)
}
