package repotools

import (
	"context"
	"encoding/json"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"sort"
	"strconv"
	"strings"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/ai/protocol"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/ai/tool"
)

const maxResultChars = 24000

func Register(registry *tool.Registry, repoRoot string) error {
	entries := []tool.Entry{
		entry("list_dir", "List a repository directory", schema(map[string]any{"path": stringProp("Repository-relative path"), "max_entries": numberProp("Maximum entries")}), func(ctx context.Context, args map[string]any) (string, error) { return listDir(repoRoot, args) }),
		entry("find_files", "Find repository files by path substring", schema(map[string]any{"query": stringProp("Path substring"), "max_results": numberProp("Maximum results")}), func(ctx context.Context, args map[string]any) (string, error) { return findFiles(ctx, repoRoot, args) }),
		entry("search_text", "Search text in repository files", schema(map[string]any{"query": stringProp("Text or regex"), "path": stringProp("Repository-relative search root"), "max_results": numberProp("Maximum matches")}), func(ctx context.Context, args map[string]any) (string, error) { return searchText(ctx, repoRoot, args) }),
		entry("find_symbol", "Find a source symbol definition and references", schema(map[string]any{"symbol": stringProp("C/C++ symbol"), "max_results": numberProp("Maximum matches")}), func(ctx context.Context, args map[string]any) (string, error) {
			args["query"] = args["symbol"]
			args["path"] = "src"
			return searchText(ctx, repoRoot, args)
		}),
		entry("read_file", "Read a repository text file", schema(map[string]any{"path": stringProp("Repository-relative file path"), "max_chars": numberProp("Maximum characters")}), func(ctx context.Context, args map[string]any) (string, error) { return readFile(repoRoot, args) }),
		entry("git_log", "Read recent git history", schema(map[string]any{"limit": numberProp("Commit count")}), func(ctx context.Context, args map[string]any) (string, error) {
			return command(ctx, repoRoot, "git", "log", "-n", strconv.Itoa(clamp(integer(args, "limit", 8), 1, 30)), "--date=short", "--name-status", "--format=---COMMIT---%n%h %ad %an%n%s")
		}),
		entry("git_show", "Read one git commit", schema(map[string]any{"ref": stringProp("Commit ref")}), func(ctx context.Context, args map[string]any) (string, error) {
			return command(ctx, repoRoot, "git", "show", "--stat", "--name-status", "--format=fuller", stringArg(args, "ref", "HEAD"))
		}),
		entry("run_cmd", "Run a read-only repository command", schema(map[string]any{"cmd": stringProp("git or rg"), "args": map[string]any{"type": "array", "items": map[string]any{"type": "string"}}}), func(ctx context.Context, args map[string]any) (string, error) {
			return runReadOnly(ctx, repoRoot, args)
		}),
	}
	for _, item := range entries {
		if err := registry.Register(item); err != nil {
			return err
		}
	}
	return nil
}
func entry(name, description string, input map[string]any, handler func(context.Context, map[string]any) (string, error)) tool.Entry {
	return tool.Entry{Descriptor: protocol.ToolDescriptor{Name: name, Description: description, InputSchema: input}, Handler: func(ctx context.Context, raw json.RawMessage, _ tool.Context) (string, error) {
		var args map[string]any
		if err := json.Unmarshal(raw, &args); err != nil {
			return "", err
		}
		return handler(ctx, args)
	}}
}
func schema(properties map[string]any) map[string]any {
	return map[string]any{"type": "object", "properties": properties}
}
func stringProp(description string) map[string]any {
	return map[string]any{"type": "string", "description": description}
}
func numberProp(description string) map[string]any {
	return map[string]any{"type": "integer", "description": description}
}
func listDir(root string, args map[string]any) (string, error) {
	full, ok := safePath(root, stringArg(args, "path", "."))
	if !ok {
		return "", fmt.Errorf("path escapes repository")
	}
	entries, err := os.ReadDir(full)
	if err != nil {
		return "", err
	}
	sort.Slice(entries, func(i, j int) bool { return entries[i].Name() < entries[j].Name() })
	limit := clamp(integer(args, "max_entries", 50), 1, 200)
	var b strings.Builder
	for i, item := range entries {
		if i >= limit {
			fmt.Fprintf(&b, "... %d more\n", len(entries)-i)
			break
		}
		suffix := ""
		if item.IsDir() {
			suffix = "/"
		}
		b.WriteString(item.Name() + suffix + "\n")
	}
	return b.String(), nil
}
func findFiles(ctx context.Context, root string, args map[string]any) (string, error) {
	query := strings.ToLower(stringArg(args, "query", ""))
	if query == "" {
		return "", fmt.Errorf("query is required")
	}
	out, err := command(ctx, root, "rg", "--files")
	if err != nil {
		return "", err
	}
	limit := clamp(integer(args, "max_results", 80), 1, 200)
	var matches []string
	for _, line := range strings.Split(out, "\n") {
		if strings.Contains(strings.ToLower(filepath.ToSlash(line)), query) {
			matches = append(matches, line)
			if len(matches) >= limit {
				break
			}
		}
	}
	return strings.Join(matches, "\n"), nil
}
func searchText(ctx context.Context, root string, args map[string]any) (string, error) {
	query := stringArg(args, "query", "")
	if query == "" {
		return "", fmt.Errorf("query is required")
	}
	rel := stringArg(args, "path", ".")
	full, ok := safePath(root, rel)
	if !ok {
		return "", fmt.Errorf("path escapes repository")
	}
	limit := clamp(integer(args, "max_results", 80), 1, 200)
	out, err := command(ctx, root, "rg", "-n", "--color", "never", "--max-count", strconv.Itoa(limit), query, full)
	if err != nil && strings.TrimSpace(out) == "" {
		return "", nil
	}
	return truncate(out, maxResultChars), nil
}
func readFile(root string, args map[string]any) (string, error) {
	full, ok := safePath(root, stringArg(args, "path", ""))
	if !ok {
		return "", fmt.Errorf("path escapes repository")
	}
	raw, err := os.ReadFile(full)
	if err != nil {
		return "", err
	}
	if strings.IndexByte(string(raw), 0) >= 0 {
		return "", fmt.Errorf("binary file")
	}
	return truncate(string(raw), clamp(integer(args, "max_chars", 12000), 256, maxResultChars)), nil
}
func runReadOnly(ctx context.Context, root string, args map[string]any) (string, error) {
	name := stringArg(args, "cmd", "")
	var values []string
	if raw, ok := args["args"].([]any); ok {
		for _, value := range raw {
			if text, ok := value.(string); ok {
				values = append(values, text)
			}
		}
	}
	if name == "git" {
		if len(values) == 0 {
			return "", fmt.Errorf("git subcommand required")
		}
		allowed := map[string]bool{"status": true, "log": true, "show": true, "diff": true, "ls-files": true, "rev-parse": true}
		if !allowed[values[0]] {
			return "", fmt.Errorf("git subcommand %q is not read-only", values[0])
		}
	} else if name != "rg" {
		return "", fmt.Errorf("command %q is not allowed", name)
	}
	return command(ctx, root, name, values...)
}
func command(ctx context.Context, root, name string, args ...string) (string, error) {
	cmd := exec.CommandContext(ctx, name, args...)
	cmd.Dir = root
	raw, err := cmd.CombinedOutput()
	text := truncate(string(raw), maxResultChars)
	if err != nil {
		return text, fmt.Errorf("%s: %w", text, err)
	}
	return text, nil
}
func safePath(root, rel string) (string, bool) {
	if rel == "" {
		return "", false
	}
	full, err := filepath.Abs(filepath.Join(root, rel))
	if err != nil {
		return "", false
	}
	base, _ := filepath.Abs(root)
	relative, err := filepath.Rel(base, full)
	if err != nil || relative == ".." || strings.HasPrefix(relative, ".."+string(filepath.Separator)) {
		return "", false
	}
	return full, true
}
func stringArg(args map[string]any, key, fallback string) string {
	if value, ok := args[key].(string); ok && strings.TrimSpace(value) != "" {
		return strings.TrimSpace(value)
	}
	return fallback
}
func integer(args map[string]any, key string, fallback int) int {
	switch value := args[key].(type) {
	case float64:
		return int(value)
	case int:
		return value
	}
	return fallback
}
func clamp(value, min, max int) int {
	if value < min {
		return min
	}
	if value > max {
		return max
	}
	return value
}
func truncate(value string, max int) string {
	if len(value) <= max {
		return value
	}
	return value[:max] + "\n... [truncated]"
}
