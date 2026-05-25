package dashboard

import (
	"bytes"
	"context"
	"encoding/json"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"regexp"
	"sort"
	"strings"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/llm"
)

const (
	chatToolMaxSteps    = 8
	chatToolResultChars = 24000
)

const chatToolControllerPrompt = `你是本地工程助手的工具控制器。你可以自主调用工具读取仓库信息，但必须只输出一段 JSON，不要输出解释。

可用工具：
- list_dir: 列目录。参数 {"path":"仓库相对路径","max_entries":50}
- find_files: 按路径关键字查找仓库文件/目录。参数 {"query":"NextEngine","max_results":80}
- search_text: 在仓库文本文件中搜索内容。参数 {"query":"class RenderContext","path":"src","max_results":80}
- find_symbol: 查找 C/C++ 符号定义、实现和引用候选。参数 {"symbol":"NextEngine","max_results":80}
- read_file: 读文本文件。参数 {"path":"仓库相对路径","max_chars":12000}
- git_log: 读最近提交。参数 {"limit":8}
- git_show: 读某个提交。参数 {"ref":"HEAD","max_chars":16000}
- run_cmd: 执行只读白名单命令。参数 {"cmd":"git","args":["status","--short"]}

决策规则：
- 如果用户问 .spec、TODO、journal、blocker、最近工作、最近修改、提交历史、本地文件内容，必须调用工具。
- 如果用户问源码、源代码、模块、目录、类、函数、结构体、实现细节、调用关系、NextEngine/gkNextEngine/Runtime/Application/Renderer 等项目内容，必须先调用工具，不要凭记忆回答。
- 不知道目录在哪里时，先用 find_files 搜索路径关键字；找类/函数/配置/字符串时，先用 search_text；找到候选文件后再 read_file。
- 如果用户问某个 C/C++ 类、结构体、模块的 API、public 方法、成员函数、接口，先用 find_symbol 定位符号定义；如果找到定义文件，下一步必须 read_file 读取定义文件后再回答。
- .spec 总结通常先 list_dir(".spec")，再 read_file(".spec/TODO.md")，必要时 read_file(".spec/journal/<id>.md") 或 read_file(".spec/specs/<id>.md")。
- 最近修改通常先 git_log，再按需 read_file 或 git_show。
- 如果工具没有结果，换一个关键词继续查一次，例如 NextEngine 可改查 gkNextEngine、NextGameplay、Runtime、Application、Renderer。
- 每次只调用一个工具。拿到工具结果后再决定下一步。
- 信息足够后输出 {"tool":"final","args":{}}。

JSON 格式只能是：
{"tool":"list_dir","args":{"path":".spec","max_entries":50}}
或
{"tool":"find_files","args":{"query":"NextEngine","max_results":80}}
或
{"tool":"search_text","args":{"query":"class RenderContext","path":"src","max_results":80}}
或
{"tool":"find_symbol","args":{"symbol":"NextEngine","max_results":80}}
或
{"tool":"read_file","args":{"path":".spec/TODO.md","max_chars":12000}}
或
{"tool":"git_log","args":{"limit":8}}
或
{"tool":"git_show","args":{"ref":"HEAD","max_chars":16000}}
或
{"tool":"run_cmd","args":{"cmd":"git","args":["status","--short"]}}
或
{"tool":"final","args":{}}
`

type chatToolCall struct {
	Tool string         `json:"tool"`
	Args map[string]any `json:"args"`
}

type chatToolResult struct {
	Name   string
	Args   map[string]any
	Output string
}

type chatToolEvent struct {
	Step    int    `json:"step"`
	Phase   string `json:"phase"`
	Name    string `json:"name"`
	Summary string `json:"summary,omitempty"`
	Detail  string `json:"detail,omitempty"`
}

func (s *Server) runChatToolLoop(ctx context.Context, client *llm.Client, modelID string, messages []llm.ChatMessage, emit func(chatToolEvent)) ([]llm.ChatMessage, []string, error) {
	controllerMessages := []llm.ChatMessage{{Role: "system", Content: chatToolControllerPrompt}}
	controllerMessages = append(controllerMessages, messages...)

	var results []chatToolResult
	parseFailures := 0
	for step := 0; step < chatToolMaxSteps; step++ {
		reply, err := client.Chat(ctx, llm.ChatRequest{
			Model:       modelID,
			Messages:    controllerMessages,
			Temperature: 0.0,
			MaxTokens:   512,
		})
		if err != nil {
			return nil, nil, err
		}
		call, err := parseChatToolCall(reply)
		if err != nil {
			parseFailures++
			if parseFailures <= 2 {
				controllerMessages = append(controllerMessages,
					llm.ChatMessage{Role: "assistant", Content: reply},
					llm.ChatMessage{Role: "user", Content: "你的上一条回复不是合法工具 JSON。请只输出一个 JSON 对象，选择一个工具或 final，不要解释。"},
				)
				continue
			}
			if len(results) == 0 {
				return messages, nil, nil
			}
			break
		}
		parseFailures = 0
		if call.Tool == "" || call.Tool == "final" {
			break
		}
		if emit != nil {
			emit(chatToolEvent{
				Step:    step + 1,
				Phase:   "start",
				Name:    call.Tool,
				Summary: chatToolCallSummary(call),
			})
		}
		out, err := s.executeChatTool(ctx, call)
		if err != nil {
			out = "ERROR: " + err.Error()
		}
		out = truncateText(out, chatToolResultChars)
		if emit != nil {
			emit(chatToolEvent{
				Step:    step + 1,
				Phase:   "done",
				Name:    call.Tool,
				Summary: chatToolCallSummary(call),
				Detail:  chatToolResultSummary(out),
			})
		}
		results = append(results, chatToolResult{Name: call.Tool, Args: call.Args, Output: out})
		controllerMessages = append(controllerMessages,
			llm.ChatMessage{Role: "assistant", Content: mustJSON(call)},
			llm.ChatMessage{Role: "user", Content: formatToolResult(call.Tool, out)},
		)
	}

	if len(results) == 0 {
		return messages, nil, nil
	}
	finalMessages := []llm.ChatMessage{{
		Role:    "system",
		Content: "你是 gnb dashboard 的本地工程助手。你已经通过工具读取了本地仓库信息。请只基于工具结果和对话内容回答；引用具体文件路径、任务 ID、提交 hash。不要输出 JSON，不要声称无法访问本地文件。",
	}}
	finalMessages = append(finalMessages, llm.ChatMessage{Role: "system", Content: buildToolTranscript(results)})
	finalMessages = append(finalMessages, messages...)
	return finalMessages, toolNames(results), nil
}

func (s *Server) executeChatTool(ctx context.Context, call chatToolCall) (string, error) {
	switch call.Tool {
	case "list_dir":
		return s.toolListDir(call.Args)
	case "find_files":
		return s.toolFindFiles(ctx, call.Args)
	case "search_text":
		return s.toolSearchText(ctx, call.Args)
	case "find_symbol":
		return s.toolFindSymbol(ctx, call.Args)
	case "read_file":
		return s.toolReadFile(call.Args)
	case "git_log":
		limit := clampInt(argInt(call.Args, "limit", 8), 1, 30)
		return gitOutput(ctx, s.opts.RepoRoot, "log", "-n", fmt.Sprintf("%d", limit), "--date=short", "--name-status", "--format=---COMMIT---%n%h %ad %an%n%s")
	case "git_show":
		ref := strings.TrimSpace(argString(call.Args, "ref", "HEAD"))
		if ref == "" {
			ref = "HEAD"
		}
		out, err := gitOutput(ctx, s.opts.RepoRoot, "show", "--stat", "--name-status", "--format=fuller", ref)
		return truncateText(out, clampInt(argInt(call.Args, "max_chars", 16000), 1000, chatToolResultChars)), err
	case "run_cmd":
		return s.toolRunCmd(ctx, call.Args)
	default:
		return "", fmt.Errorf("unknown tool %q", call.Tool)
	}
}

func (s *Server) toolListDir(args map[string]any) (string, error) {
	rel := argString(args, "path", ".")
	maxEntries := clampInt(argInt(args, "max_entries", 50), 1, 200)
	full, ok := safeRepoPath(s.opts.RepoRoot, rel)
	if !ok {
		return "", fmt.Errorf("path escapes repo: %s", rel)
	}
	entries, err := os.ReadDir(full)
	if err != nil {
		return "", err
	}
	sort.Slice(entries, func(i, j int) bool {
		if entries[i].IsDir() != entries[j].IsDir() {
			return entries[i].IsDir()
		}
		return entries[i].Name() < entries[j].Name()
	})
	var b strings.Builder
	for i, entry := range entries {
		if i >= maxEntries {
			fmt.Fprintf(&b, "... [%d more entries]\n", len(entries)-i)
			break
		}
		suffix := ""
		if entry.IsDir() {
			suffix = "/"
		}
		fmt.Fprintf(&b, "%s%s\n", entry.Name(), suffix)
	}
	return b.String(), nil
}

func (s *Server) toolFindFiles(ctx context.Context, args map[string]any) (string, error) {
	query := strings.ToLower(strings.TrimSpace(argString(args, "query", "")))
	maxResults := clampInt(argInt(args, "max_results", 80), 1, 300)
	if query == "" {
		return "", fmt.Errorf("query is required")
	}
	files, err := repoTrackedFiles(ctx, s.opts.RepoRoot)
	if err != nil {
		return "", err
	}
	seen := map[string]bool{}
	var matches []string
	for _, file := range files {
		pathLower := strings.ToLower(file)
		if strings.Contains(pathLower, query) {
			if !seen[file] {
				seen[file] = true
				matches = append(matches, file)
			}
		}
		dir := filepath.ToSlash(filepath.Dir(file))
		for dir != "." && dir != "/" && dir != "" {
			if strings.Contains(strings.ToLower(dir), query) && !seen[dir+"/"] {
				seen[dir+"/"] = true
				matches = append(matches, dir+"/")
			}
			next := filepath.ToSlash(filepath.Dir(dir))
			if next == dir {
				break
			}
			dir = next
		}
	}
	sort.Strings(matches)
	if len(matches) == 0 {
		return "(no matching files or directories)", nil
	}
	return strings.Join(limitStrings(matches, maxResults), "\n"), nil
}

func (s *Server) toolSearchText(ctx context.Context, args map[string]any) (string, error) {
	query := strings.TrimSpace(argString(args, "query", ""))
	relPath := strings.TrimSpace(argString(args, "path", "."))
	maxResults := clampInt(argInt(args, "max_results", 80), 1, 300)
	if query == "" {
		return "", fmt.Errorf("query is required")
	}
	base, ok := safeRepoPath(s.opts.RepoRoot, relPath)
	if !ok {
		return "", fmt.Errorf("path escapes repo: %s", relPath)
	}
	queryLower := strings.ToLower(query)
	files, err := repoTrackedFiles(ctx, s.opts.RepoRoot)
	if err != nil {
		return "", err
	}
	var matches []string
	for _, rel := range files {
		full, ok := safeRepoPath(s.opts.RepoRoot, rel)
		if !ok || (full != base && !strings.HasPrefix(full, base+string(os.PathSeparator))) {
			continue
		}
		info, err := os.Stat(full)
		if err != nil || info.IsDir() || info.Size() > 512*1024 {
			continue
		}
		data, err := os.ReadFile(full)
		if err != nil || isProbablyBinary(data) {
			continue
		}
		for lineNo, line := range strings.Split(string(data), "\n") {
			if strings.Contains(strings.ToLower(line), queryLower) {
				matches = append(matches, fmt.Sprintf("%s:%d:%s", filepath.ToSlash(rel), lineNo+1, strings.TrimSpace(line)))
				if len(matches) >= maxResults {
					return strings.Join(matches, "\n"), nil
				}
			}
		}
	}
	if len(matches) == 0 {
		return "(no text matches)", nil
	}
	return strings.Join(matches, "\n"), nil
}

type symbolMatch struct {
	kind string
	path string
	line int
	text string
}

func (s *Server) toolFindSymbol(ctx context.Context, args map[string]any) (string, error) {
	symbol := strings.TrimSpace(argString(args, "symbol", ""))
	maxResults := clampInt(argInt(args, "max_results", 80), 1, 300)
	if symbol == "" {
		return "", fmt.Errorf("symbol is required")
	}
	files, err := repoTrackedFiles(ctx, s.opts.RepoRoot)
	if err != nil {
		return "", err
	}
	escaped := regexp.QuoteMeta(symbol)
	defPattern := regexp.MustCompile(`\b(class|struct|enum\s+class)\s+` + escaped + `\b`)
	implPattern := regexp.MustCompile(`\b` + escaped + `::[~A-Za-z_]`)
	refPattern := regexp.MustCompile(`\b` + escaped + `\b`)

	var definitions []symbolMatch
	var declarations []symbolMatch
	var implementations []symbolMatch
	var references []symbolMatch
	for _, rel := range files {
		if !isSourceLikePath(rel) {
			continue
		}
		full, ok := safeRepoPath(s.opts.RepoRoot, rel)
		if !ok {
			continue
		}
		info, err := os.Stat(full)
		if err != nil || info.IsDir() || info.Size() > 512*1024 {
			continue
		}
		data, err := os.ReadFile(full)
		if err != nil || isProbablyBinary(data) {
			continue
		}
		for lineNo, line := range strings.Split(string(data), "\n") {
			trimmed := strings.TrimSpace(line)
			if trimmed == "" {
				continue
			}
			match := symbolMatch{
				path: filepath.ToSlash(rel),
				line: lineNo + 1,
				text: trimmed,
			}
			switch {
			case matchesOutsideString(defPattern, line):
				if isForwardDeclarationLine(trimmed) {
					match.kind = "declaration"
					declarations = append(declarations, match)
				} else {
					match.kind = "definition"
					definitions = append(definitions, match)
				}
			case matchesOutsideString(implPattern, line):
				match.kind = "implementation"
				implementations = append(implementations, match)
			case matchesOutsideString(refPattern, line):
				match.kind = "reference"
				references = append(references, match)
			}
		}
	}
	sortSymbolMatches(definitions)
	sortSymbolMatches(declarations)
	sortSymbolMatches(implementations)
	sortSymbolMatches(references)
	results := append([]symbolMatch{}, definitions...)
	results = append(results, declarations...)
	results = append(results, implementations...)
	results = append(results, references...)
	if len(results) == 0 {
		return "(no symbol matches)", nil
	}
	var b strings.Builder
	fmt.Fprintf(&b, "symbol: %s\n", symbol)
	writeSymbolSection(&b, "definitions", definitions, maxResults)
	remaining := maxResults - countLimited(definitions, maxResults)
	if remaining > 0 {
		writeSymbolSection(&b, "declarations", declarations, remaining)
		remaining -= countLimited(declarations, remaining)
	}
	if remaining > 0 {
		writeSymbolSection(&b, "implementations", implementations, remaining)
		remaining -= countLimited(implementations, remaining)
	}
	if remaining > 0 {
		writeSymbolSection(&b, "references", references, remaining)
	}
	return strings.TrimRight(b.String(), "\n"), nil
}

func (s *Server) toolReadFile(args map[string]any) (string, error) {
	rel := argString(args, "path", "")
	maxChars := clampInt(argInt(args, "max_chars", 12000), 256, chatToolResultChars)
	if rel == "" {
		return "", fmt.Errorf("path is required")
	}
	full, ok := safeRepoPath(s.opts.RepoRoot, rel)
	if !ok {
		return "", fmt.Errorf("path escapes repo: %s", rel)
	}
	info, err := os.Stat(full)
	if err != nil {
		return "", err
	}
	if info.IsDir() {
		return "", fmt.Errorf("path is a directory: %s", rel)
	}
	if info.Size() > 2*1024*1024 {
		return "", fmt.Errorf("file too large: %s (%d bytes)", rel, info.Size())
	}
	data, err := os.ReadFile(full)
	if err != nil {
		return "", err
	}
	if isProbablyBinary(data) {
		return "", fmt.Errorf("binary file skipped: %s", rel)
	}
	return truncateText(string(data), maxChars), nil
}

func (s *Server) toolRunCmd(ctx context.Context, args map[string]any) (string, error) {
	cmdName := strings.TrimSpace(argString(args, "cmd", ""))
	cmdArgs := argStringSlice(args, "args")
	if cmdName == "" {
		return "", fmt.Errorf("cmd is required")
	}
	if err := validateReadOnlyCommand(cmdName, cmdArgs); err != nil {
		return "", err
	}
	return commandOutput(ctx, s.opts.RepoRoot, cmdName, cmdArgs...)
}

func validateReadOnlyCommand(cmdName string, args []string) error {
	base := strings.ToLower(filepath.Base(cmdName))
	switch base {
	case "git", "git.exe":
		if len(args) == 0 {
			return fmt.Errorf("git subcommand required")
		}
		allowed := map[string]bool{
			"branch": true, "diff": true, "log": true, "show": true,
			"status": true, "rev-parse": true, "ls-files": true,
		}
		if !allowed[args[0]] {
			return fmt.Errorf("git subcommand %q is not allowed", args[0])
		}
	case "gnb.bat", "gnb.exe", "gnb":
		if len(args) == 0 || args[0] != "todo" {
			return fmt.Errorf("only gnb todo read commands are allowed")
		}
		if len(args) >= 2 && args[1] != "list" && args[1] != "show" && args[1] != "next" {
			return fmt.Errorf("gnb todo %q is not allowed", args[1])
		}
	default:
		return fmt.Errorf("command %q is not allowed", cmdName)
	}
	for _, arg := range args {
		if strings.ContainsAny(arg, "\r\n;&|<>") {
			return fmt.Errorf("unsafe shell metacharacter in arg %q", arg)
		}
	}
	return nil
}

func parseChatToolCall(reply string) (chatToolCall, error) {
	raw := extractJSONObject(reply)
	if raw == "" {
		return chatToolCall{}, fmt.Errorf("no json object in tool reply")
	}
	var call chatToolCall
	if err := json.Unmarshal([]byte(raw), &call); err != nil {
		return chatToolCall{}, err
	}
	call.Tool = strings.TrimSpace(call.Tool)
	if call.Args == nil {
		call.Args = map[string]any{}
	}
	return call, nil
}

func extractJSONObject(s string) string {
	s = strings.TrimSpace(s)
	start := strings.IndexByte(s, '{')
	if start < 0 {
		return ""
	}
	depth := 0
	inString := false
	escape := false
	for i := start; i < len(s); i++ {
		ch := s[i]
		if inString {
			if escape {
				escape = false
			} else if ch == '\\' {
				escape = true
			} else if ch == '"' {
				inString = false
			}
			continue
		}
		switch ch {
		case '"':
			inString = true
		case '{':
			depth++
		case '}':
			depth--
			if depth == 0 {
				return s[start : i+1]
			}
		}
	}
	return ""
}

func buildToolTranscript(results []chatToolResult) string {
	var b strings.Builder
	b.WriteString("本轮工具调用结果如下：\n")
	for i, res := range results {
		fmt.Fprintf(&b, "\n## Tool %d: %s\nargs: %s\n```text\n%s\n```\n", i+1, res.Name, mustJSON(res.Args), res.Output)
	}
	return b.String()
}

func formatToolResult(name string, output string) string {
	return fmt.Sprintf("TOOL_RESULT %s\n```text\n%s\n```", name, output)
}

func chatToolCallSummary(call chatToolCall) string {
	switch call.Tool {
	case "list_dir":
		return "列出 " + argString(call.Args, "path", ".")
	case "find_files":
		return "查找路径 " + quoteSummary(argString(call.Args, "query", ""))
	case "search_text":
		path := argString(call.Args, "path", ".")
		return "在 " + path + " 搜索 " + quoteSummary(argString(call.Args, "query", ""))
	case "find_symbol":
		return "定位符号 " + quoteSummary(argString(call.Args, "symbol", ""))
	case "read_file":
		return "读取 " + argString(call.Args, "path", "")
	case "git_log":
		return fmt.Sprintf("读取最近 %d 个提交", clampInt(argInt(call.Args, "limit", 8), 1, 30))
	case "git_show":
		return "查看提交 " + argString(call.Args, "ref", "HEAD")
	case "run_cmd":
		cmd := argString(call.Args, "cmd", "")
		args := argStringSlice(call.Args, "args")
		return strings.TrimSpace(cmd + " " + strings.Join(args, " "))
	default:
		return call.Tool
	}
}

func chatToolResultSummary(output string) string {
	output = strings.TrimSpace(output)
	if output == "" {
		return "无输出"
	}
	if strings.HasPrefix(output, "ERROR:") {
		return firstNonEmptyLine(output)
	}
	lines := 0
	for _, line := range strings.Split(output, "\n") {
		if strings.TrimSpace(line) != "" {
			lines++
		}
	}
	first := firstNonEmptyLine(output)
	if lines <= 1 {
		return first
	}
	return fmt.Sprintf("%d 行结果；%s", lines, first)
}

func firstNonEmptyLine(text string) string {
	for _, line := range strings.Split(text, "\n") {
		line = strings.TrimSpace(line)
		if line != "" {
			return truncateSummary(line, 180)
		}
	}
	return ""
}

func quoteSummary(text string) string {
	text = strings.TrimSpace(text)
	if text == "" {
		return "\"\""
	}
	return `"` + truncateSummary(text, 120) + `"`
}

func truncateSummary(text string, max int) string {
	text = strings.Join(strings.Fields(text), " ")
	if max <= 0 || len(text) <= max {
		return text
	}
	if max <= 1 {
		return text[:max]
	}
	return text[:max-1] + "…"
}

func toolNames(results []chatToolResult) []string {
	names := make([]string, 0, len(results))
	for _, res := range results {
		names = append(names, res.Name)
	}
	return names
}

func mustJSON(v any) string {
	data, err := json.Marshal(v)
	if err != nil {
		return "{}"
	}
	return string(data)
}

func gitOutput(ctx context.Context, repoRoot string, args ...string) (string, error) {
	return commandOutput(ctx, repoRoot, "git", args...)
}

func repoTrackedFiles(ctx context.Context, repoRoot string) ([]string, error) {
	out, err := gitOutput(ctx, repoRoot, "ls-files")
	if err != nil {
		return nil, err
	}
	var files []string
	for _, line := range strings.Split(out, "\n") {
		line = strings.TrimSpace(line)
		if line != "" {
			files = append(files, filepath.ToSlash(line))
		}
	}
	return files, nil
}

func isSourceLikePath(path string) bool {
	switch strings.ToLower(filepath.Ext(path)) {
	case ".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx", ".inl", ".ipp", ".mm", ".m":
		return true
	default:
		return false
	}
}

func sortSymbolMatches(matches []symbolMatch) {
	sort.Slice(matches, func(i, j int) bool {
		if matches[i].path != matches[j].path {
			return matches[i].path < matches[j].path
		}
		return matches[i].line < matches[j].line
	})
}

func matchesOutsideString(pattern *regexp.Regexp, line string) bool {
	loc := pattern.FindStringIndex(line)
	if loc == nil {
		return false
	}
	inString := false
	escape := false
	for _, ch := range line[:loc[0]] {
		if escape {
			escape = false
			continue
		}
		if ch == '\\' {
			escape = true
			continue
		}
		if ch == '"' {
			inString = !inString
		}
	}
	return !inString
}

func isForwardDeclarationLine(line string) bool {
	return strings.HasSuffix(line, ";") && !strings.Contains(line, "{")
}

func writeSymbolSection(b *strings.Builder, name string, matches []symbolMatch, max int) {
	if len(matches) == 0 || max <= 0 {
		return
	}
	fmt.Fprintf(b, "\n[%s]\n", name)
	limit := len(matches)
	if limit > max {
		limit = max
	}
	for _, match := range matches[:limit] {
		fmt.Fprintf(b, "%s:%d:%s\n", match.path, match.line, match.text)
	}
	if len(matches) > limit {
		fmt.Fprintf(b, "... [%d more %s]\n", len(matches)-limit, name)
	}
}

func countLimited(items []symbolMatch, max int) int {
	if len(items) < max {
		return len(items)
	}
	return max
}

func commandOutput(ctx context.Context, repoRoot string, name string, args ...string) (string, error) {
	cmd := exec.CommandContext(ctx, name, args...)
	cmd.Dir = repoRoot
	var stdout, stderr bytes.Buffer
	cmd.Stdout = &stdout
	cmd.Stderr = &stderr
	if err := cmd.Run(); err != nil {
		msg := strings.TrimSpace(stderr.String())
		if msg == "" {
			msg = err.Error()
		}
		return "", fmt.Errorf("%s %s failed: %s", name, strings.Join(args, " "), msg)
	}
	return stdout.String(), nil
}

func safeRepoPath(repoRoot string, rel string) (string, bool) {
	cleanRel := filepath.Clean(filepath.FromSlash(rel))
	if cleanRel == "." {
		cleanRel = ""
	}
	if filepath.IsAbs(cleanRel) || strings.HasPrefix(cleanRel, "..") {
		return "", false
	}
	rootAbs, err := filepath.Abs(repoRoot)
	if err != nil {
		return "", false
	}
	full, err := filepath.Abs(filepath.Join(rootAbs, cleanRel))
	if err != nil {
		return "", false
	}
	if full != rootAbs && !strings.HasPrefix(full, rootAbs+string(os.PathSeparator)) {
		return "", false
	}
	return full, true
}

func isProbablyBinary(data []byte) bool {
	if len(data) == 0 {
		return false
	}
	limit := len(data)
	if limit > 4096 {
		limit = 4096
	}
	for _, b := range data[:limit] {
		if b == 0 {
			return true
		}
	}
	return false
}

func argString(args map[string]any, key string, fallback string) string {
	if v, ok := args[key]; ok {
		if s, ok := v.(string); ok {
			return s
		}
	}
	return fallback
}

func argInt(args map[string]any, key string, fallback int) int {
	if v, ok := args[key]; ok {
		switch n := v.(type) {
		case float64:
			return int(n)
		case int:
			return n
		}
	}
	return fallback
}

func argStringSlice(args map[string]any, key string) []string {
	raw, ok := args[key]
	if !ok {
		return nil
	}
	vals, ok := raw.([]any)
	if !ok {
		return nil
	}
	out := make([]string, 0, len(vals))
	for _, v := range vals {
		if s, ok := v.(string); ok {
			out = append(out, s)
		}
	}
	return out
}

func clampInt(n int, min int, max int) int {
	if n < min {
		return min
	}
	if n > max {
		return max
	}
	return n
}

func limitStrings(items []string, max int) []string {
	if len(items) <= max {
		return items
	}
	out := append([]string(nil), items[:max]...)
	out = append(out, fmt.Sprintf("... [%d more matches]", len(items)-max))
	return out
}

func truncateText(s string, max int) string {
	if max <= 0 || len(s) <= max {
		return s
	}
	if max < 32 {
		return s[:max]
	}
	return s[:max] + "\n... [truncated]\n"
}
