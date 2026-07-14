package dashboard

import (
	"net/http/httptest"
	"strings"
	"testing"
)

func TestProviderSelectorIsOnlyRenderedInChatTab(t *testing.T) {
	s := setupDocsRepo(t)

	todoReq := httptest.NewRequest("GET", "/tab/todo", nil)
	todoReq.SetPathValue("kind", "todo")
	todoRec := httptest.NewRecorder()
	s.handleTab(todoRec, todoReq)
	if todoRec.Code != 200 {
		t.Fatalf("todo status = %d: %s", todoRec.Code, todoRec.Body.String())
	}
	if strings.Contains(todoRec.Body.String(), `id="chat-provider"`) {
		t.Fatal("TODO tab unexpectedly contains the chat provider selector")
	}

	chatReq := httptest.NewRequest("GET", "/tab/chat", nil)
	chatReq.SetPathValue("kind", "chat")
	chatRec := httptest.NewRecorder()
	s.handleTab(chatRec, chatReq)
	if chatRec.Code != 200 {
		t.Fatalf("chat status = %d: %s", chatRec.Code, chatRec.Body.String())
	}
	for _, id := range []string{`id="chat-provider"`, `id="chat-profile"`, `id="chat-model"`} {
		if !strings.Contains(chatRec.Body.String(), id) {
			t.Fatalf("chat tab is missing %s", id)
		}
	}
}

func TestStreamingChatRequestIncludesProviderSelection(t *testing.T) {
	layout, err := templateFS.ReadFile("templates/layout.html")
	if err != nil {
		t.Fatal(err)
	}
	source := string(layout)
	for _, expected := range []string{
		"const provider = document.getElementById('chat-provider');",
		"const profile = document.getElementById('chat-profile');",
		"body.set('provider', provider.value);",
		"body.set('profile', profile.value);",
	} {
		if !strings.Contains(source, expected) {
			t.Fatalf("streaming chat request is missing %q", expected)
		}
	}
}
