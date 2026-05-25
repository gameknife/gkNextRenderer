package dashboard

import (
	"context"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"testing"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/llm"
)

func TestParseChatToolCall(t *testing.T) {
	call, err := parseChatToolCall("```json\n{\"tool\":\"read_file\",\"args\":{\"path\":\".spec/TODO.md\"}}\n```")
	if err != nil {
		t.Fatal(err)
	}
	if call.Tool != "read_file" {
		t.Fatalf("tool = %q, want read_file", call.Tool)
	}
	if call.Args["path"] != ".spec/TODO.md" {
		t.Fatalf("path = %#v", call.Args["path"])
	}
}

func TestExtractJSONObjectSkipsText(t *testing.T) {
	raw := extractJSONObject("plan:\n{\"tool\":\"final\",\"args\":{}}\nthanks")
	if raw != `{"tool":"final","args":{}}` {
		t.Fatalf("json = %q", raw)
	}
}

func TestValidateReadOnlyCommand(t *testing.T) {
	if err := validateReadOnlyCommand("git", []string{"status", "--short"}); err != nil {
		t.Fatalf("git status rejected: %v", err)
	}
	if err := validateReadOnlyCommand("git", []string{"reset", "--hard"}); err == nil {
		t.Fatalf("git reset should be rejected")
	}
	if err := validateReadOnlyCommand("cmd", []string{"/c", "dir"}); err == nil {
		t.Fatalf("cmd should be rejected")
	}
}

func TestParseSearchTextToolCall(t *testing.T) {
	call, err := parseChatToolCall(`{"tool":"search_text","args":{"query":"NextEngine","path":"src","max_results":20}}`)
	if err != nil {
		t.Fatal(err)
	}
	if call.Tool != "search_text" {
		t.Fatalf("tool = %q", call.Tool)
	}
	if call.Args["query"] != "NextEngine" {
		t.Fatalf("query = %#v", call.Args["query"])
	}
}

func TestParseFindSymbolToolCall(t *testing.T) {
	call, err := parseChatToolCall(`{"tool":"find_symbol","args":{"symbol":"NextEngine","max_results":20}}`)
	if err != nil {
		t.Fatal(err)
	}
	if call.Tool != "find_symbol" {
		t.Fatalf("tool = %q", call.Tool)
	}
	if call.Args["symbol"] != "NextEngine" {
		t.Fatalf("symbol = %#v", call.Args["symbol"])
	}
}

func TestParseNativeToolCallFormat(t *testing.T) {
	call, err := parseChatToolCall(`我将搜索本地仓库。

<|tool_call>call: Tool 1: find_files args: {"max_results":80,"query":"ImGui hdr"}`)
	if err != nil {
		t.Fatal(err)
	}
	if call.Tool != "find_files" {
		t.Fatalf("tool = %q", call.Tool)
	}
	if call.Args["query"] != "ImGui hdr" {
		t.Fatalf("query = %#v", call.Args["query"])
	}
}

func TestParseArgsOnlyJSONIsRejected(t *testing.T) {
	if _, err := parseChatToolCall(`args: {"max_results":80,"query":"ImGui hdr"}`); err == nil {
		t.Fatalf("args-only JSON should be rejected")
	}
}

func TestToolFindSymbolPrioritizesDefinition(t *testing.T) {
	root := t.TempDir()
	runGit(t, root, "init")
	srcDir := filepath.Join(root, "src")
	if err := os.MkdirAll(srcDir, 0o755); err != nil {
		t.Fatal(err)
	}
	header := filepath.Join(srcDir, "NextEngine.hpp")
	impl := filepath.Join(srcDir, "NextEngine.cpp")
	fwd := filepath.Join(srcDir, "RuntimeFwd.hpp")
	js := filepath.Join(srcDir, "QuickJSEngine.cpp")
	if err := os.WriteFile(header, []byte("class NextEngine\n{\npublic:\n    void Run();\n};\n"), 0o644); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(impl, []byte("#include \"NextEngine.hpp\"\nvoid NextEngine::Run() {}\n"), 0o644); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(fwd, []byte("class NextEngine;\n"), 0o644); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(js, []byte("result += \"export class NextEngine {\\n\";\n"), 0o644); err != nil {
		t.Fatal(err)
	}
	runGit(t, root, "add", ".")

	srv := &Server{opts: Options{RepoRoot: root}}
	out, err := srv.toolFindSymbol(context.Background(), map[string]any{"symbol": "NextEngine", "max_results": float64(10)})
	if err != nil {
		t.Fatal(err)
	}
	defIndex := strings.Index(out, "[definitions]")
	declIndex := strings.Index(out, "[declarations]")
	implIndex := strings.Index(out, "[implementations]")
	if defIndex < 0 || declIndex < 0 || implIndex < 0 || defIndex > declIndex || declIndex > implIndex {
		t.Fatalf("definition should appear before declaration and implementation:\n%s", out)
	}
	if !strings.Contains(out, "src/NextEngine.hpp:1:class NextEngine") {
		t.Fatalf("definition missing:\n%s", out)
	}
	if strings.Contains(out, "export class NextEngine") {
		t.Fatalf("string literal should not be treated as a symbol match:\n%s", out)
	}
}

func TestForcedToolContinuationAfterDiscoveryOnly(t *testing.T) {
	reason := forcedToolContinuationReason(
		[]llm.ChatMessage{{Role: "user", Content: "请列出 NextEngine 的 public 方法"}},
		[]chatToolResult{{Name: "find_symbol", Output: "src/Engine/Runtime/Engine.hpp:43:class NextEngine final"}},
	)
	if reason == "" {
		t.Fatalf("expected continuation after discovery-only source lookup")
	}
	if !strings.Contains(reason, "read_file") {
		t.Fatalf("reason should steer toward read_file, got %q", reason)
	}
}

func TestForcedToolContinuationAllowsEvidence(t *testing.T) {
	reason := forcedToolContinuationReason(
		[]llm.ChatMessage{{Role: "user", Content: "请列出 NextEngine 的 public 方法"}},
		[]chatToolResult{
			{Name: "find_symbol", Output: "src/Engine/Runtime/Engine.hpp:43:class NextEngine final"},
			{Name: "read_file", Output: "class NextEngine final { public: void Start(); };"},
		},
	)
	if reason != "" {
		t.Fatalf("did not expect continuation after evidence tool, got %q", reason)
	}
}

func TestForcedToolContinuationIgnoresCasualChat(t *testing.T) {
	reason := forcedToolContinuationReason(
		[]llm.ChatMessage{{Role: "user", Content: "你好"}},
		[]chatToolResult{{Name: "list_dir", Output: "src/\nassets/\n"}},
	)
	if reason != "" {
		t.Fatalf("casual chat should not be forced to continue, got %q", reason)
	}
}

func runGit(t *testing.T, dir string, args ...string) {
	t.Helper()
	cmd := exec.Command("git", args...)
	cmd.Dir = dir
	out, err := cmd.CombinedOutput()
	if err != nil {
		t.Fatalf("git %s failed: %v\n%s", strings.Join(args, " "), err, out)
	}
}
