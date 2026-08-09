package llm

import (
	"bytes"
	"context"
	"fmt"
	"os"
	"path/filepath"
	"strings"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/config"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/gitops"
)

const CommitMsgSystemPrompt = `You write git commit messages. The user gives you a unified diff plus a file-list summary; treat that diff as the SOLE source of truth — do not invent files, components, or motivations that aren't visible in it.

Output format (and ONLY this):
- Line 1: <type>(<optional scope>): <imperative subject, no trailing period, <= 72 chars>
- Allowed types: feat, fix, refactor, perf, docs, test, chore, build, ci, style
- Optionally a blank line then a body (wrap at 72 columns) explaining WHY when the change is non-trivial. Reference concrete files/functions from the diff.

Do NOT include: markdown fences, "Commit message:" labels, generic filler like "various improvements", or commentary about the diff format itself.`

// CommitMessageResult bundles the generated message with diagnostics so the
// caller (CLI / dashboard) can show provenance to the user.
type CommitMessageResult struct {
	Message   string
	Source    string // "staged" or "working tree"
	Truncated bool
}

type CommitChatFunc func(context.Context, []ChatMessage, float64, int) (string, error)

func GenerateCommitMessageWithChat(ctx context.Context, repoRoot string, maxDiffChars int, temperature float64, chat CommitChatFunc) (CommitMessageResult, error) {
	res := CommitMessageResult{}
	diff, source, truncated, err := CollectDiff(repoRoot, DiffBudget{TotalChars: maxDiffChars})
	if err != nil {
		return res, err
	}
	res.Source, res.Truncated = source, truncated
	if strings.TrimSpace(diff) == "" {
		return res, fmt.Errorf("no changes to describe")
	}
	reply, err := chat(ctx, []ChatMessage{{Role: "system", Content: CommitMsgSystemPrompt}, {Role: "user", Content: buildCommitUserPrompt(diff)}}, temperature, 256)
	if err != nil {
		return res, err
	}
	res.Message = SanitizeCommitMessage(reply)
	if res.Message == "" {
		return res, fmt.Errorf("LLM returned an empty commit message")
	}
	return res, nil
}

// DiffBudget bounds how much we serialise for the model. Sized so the typical
// commit stays small enough for fast local commit-message generation after the
// system prompt + chat template overhead.
type DiffBudget struct {
	TotalChars   int // overall cap for the body content (excluding the always-kept header). 0 = use default
	PerFileChars int // cap for any single file's content. 0 = use default
	MaxFileBytes int // skip untracked files larger than this. 0 = use default
}

func (b DiffBudget) withDefaults() DiffBudget {
	if b.TotalChars <= 0 {
		b.TotalChars = 16000
	}
	if b.PerFileChars <= 0 {
		b.PerFileChars = 4000
	}
	if b.MaxFileBytes <= 0 {
		b.MaxFileBytes = 64 * 1024
	}
	return b
}

// GenerateCommitMessage collects the diff, ensures the configured active model
// is running, calls it, and post-processes the output.
//
// maxDiffChars <= 0 keeps the default budget.
func GenerateCommitMessage(ctx context.Context, repoRoot string, cfg config.LLMConfig, maxDiffChars int, temperature float64) (CommitMessageResult, error) {
	return generateCommitMessage(ctx, repoRoot, cfg, maxDiffChars, temperature, false)
}

// GenerateCommitMessageReuseRunning reuses any healthy llama-server without
// switching its model. It starts the configured active model only when no
// server is currently available.
func GenerateCommitMessageReuseRunning(ctx context.Context, repoRoot string, cfg config.LLMConfig, maxDiffChars int, temperature float64) (CommitMessageResult, error) {
	return generateCommitMessage(ctx, repoRoot, cfg, maxDiffChars, temperature, true)
}

func generateCommitMessage(ctx context.Context, repoRoot string, cfg config.LLMConfig, maxDiffChars int, temperature float64, reuseRunning bool) (CommitMessageResult, error) {
	srv := NewServer(repoRoot, cfg)
	var err error
	var info ServerInfo
	if reuseRunning {
		info, err = srv.EnsureRunningOrReuse(ctx)
	} else {
		info, err = srv.EnsureRunning(ctx)
	}
	if err != nil {
		return CommitMessageResult{}, err
	}
	client := NewClient(info.BaseURL())
	return GenerateCommitMessageWithChat(ctx, repoRoot, maxDiffChars, temperature, func(chatCtx context.Context, messages []ChatMessage, temp float64, maxTokens int) (string, error) {
		return client.Chat(chatCtx, ChatRequest{Messages: messages, Temperature: temp, MaxTokens: maxTokens})
	})
}

func buildCommitUserPrompt(diff string) string {
	return "Write the commit message for the change below. The summary header lists every changed path; the diff that follows is the authoritative content (untracked/new files are rendered as synthetic +++ b/<path> blocks).\n\n" +
		diff +
		"\n\nReturn ONLY the commit message — no fences, no labels."
}

// CollectDiff assembles a prompt-ready description of the current change.
//
// Layout of the returned text:
//  1. A "summary header" listing every changed path with its status code
//     (M/A/D/R/??). This is ALWAYS preserved even if budgets force the body
//     to be truncated, so the model retains a global view.
//  2. The body: tracked-file diff (from `git diff [--staged]`) plus
//     synthesised new-file diffs for untracked files (read from disk).
//
// Returns the assembled text, a mode label ("staged" / "working tree"), and
// a "truncated" flag set when any per-file or total budget was hit.
func CollectDiff(repoRoot string, budget DiffBudget) (string, string, bool, error) {
	budget = budget.withDefaults()

	staged, err := gitops.DiffStaged(repoRoot)
	if err != nil {
		return "", "", false, err
	}
	if strings.TrimSpace(staged) != "" {
		stat, _ := gitops.DiffStagedStat(repoRoot)
		files, _ := gitops.StagedNameStatus(repoRoot)
		body, truncated := truncateDiffBody(staged, budget)
		return buildPrompt("staged", files, nil, stat, body), "staged", truncated, nil
	}

	working, err := gitops.DiffWorkingTree(repoRoot)
	if err != nil {
		return "", "", false, err
	}
	workingStat, _ := gitops.DiffWorkingTreeStat(repoRoot)
	trackedFiles, _ := gitops.WorkingTreeNameStatus(repoRoot)
	untracked, _ := gitops.UntrackedFiles(repoRoot)

	var bodyBuf strings.Builder
	overallTruncated := false
	totalUsed := 0

	if t := strings.TrimSpace(working); t != "" {
		piece, used, truncated := budgetAccept(t+"\n", budget.TotalChars-totalUsed)
		bodyBuf.WriteString(piece)
		totalUsed += used
		if truncated {
			overallTruncated = true
		}
	}

	// Synthesise diffs for untracked files. git diff alone DOES NOT include
	// their content — for "new feature" commits this used to leave the model
	// guessing from filenames only.
	for _, rel := range untracked {
		if totalUsed >= budget.TotalChars {
			overallTruncated = true
			bodyBuf.WriteString(fmt.Sprintf("\n... [%d more untracked files omitted, budget exhausted]\n", len(untracked)-indexOf(untracked, rel)))
			break
		}
		entry, truncated := renderUntracked(repoRoot, rel, budget)
		if truncated {
			overallTruncated = true
		}
		remaining := budget.TotalChars - totalUsed
		piece, used, cut := budgetAccept(entry, remaining)
		bodyBuf.WriteString(piece)
		totalUsed += used
		if cut {
			overallTruncated = true
		}
	}

	return buildPrompt("working tree", trackedFiles, untracked, workingStat, bodyBuf.String()), "working tree", overallTruncated, nil
}

// buildPrompt formats the always-on summary header followed by the diff body.
// The header is what keeps the model anchored when the body has to be
// truncated to fit the context window.
func buildPrompt(mode string, tracked []gitops.FileChange, untracked []string, stat string, body string) string {
	var b strings.Builder
	fmt.Fprintf(&b, "# Mode: %s\n", mode)
	fmt.Fprintf(&b, "# Files changed (%d):\n", len(tracked)+len(untracked))
	for _, fc := range tracked {
		fmt.Fprintf(&b, "#   %-3s %s\n", fc.Code, fc.Path)
	}
	for _, u := range untracked {
		fmt.Fprintf(&b, "#   %-3s %s\n", "??", u)
	}
	if s := strings.TrimSpace(stat); s != "" {
		b.WriteString("#\n# git diff --stat:\n")
		for _, ln := range strings.Split(s, "\n") {
			b.WriteString("#   ")
			b.WriteString(ln)
			b.WriteString("\n")
		}
	}
	b.WriteString("\n")
	b.WriteString(body)
	return b.String()
}

// renderUntracked materialises one untracked file as a synthetic unified diff
// entry (`--- /dev/null` / `+++ b/<path>` / `+<content line>`). Skips binaries
// and files larger than the configured byte budget; oversize text files are
// truncated at PerFileChars.
func renderUntracked(repoRoot, rel string, budget DiffBudget) (string, bool) {
	full := filepath.Join(repoRoot, rel)
	info, err := os.Stat(full)
	if err != nil || info.IsDir() {
		return "", false
	}
	if info.Size() > int64(budget.MaxFileBytes) {
		return fmt.Sprintf("# Untracked %s skipped: %d bytes > %d byte cap\n", rel, info.Size(), budget.MaxFileBytes), true
	}
	data, err := os.ReadFile(full)
	if err != nil {
		return fmt.Sprintf("# Untracked %s skipped: %v\n", rel, err), false
	}
	if isBinary(data) {
		return fmt.Sprintf("# Untracked binary %s skipped (%d bytes)\n", rel, len(data)), false
	}

	content := string(data)
	truncated := false
	if len(content) > budget.PerFileChars {
		content = content[:budget.PerFileChars]
		truncated = true
		// snap back to the last newline to avoid a chopped line
		if nl := strings.LastIndexByte(content, '\n'); nl > budget.PerFileChars/2 {
			content = content[:nl+1]
		}
	}

	var b strings.Builder
	fmt.Fprintf(&b, "diff --git a/%s b/%s\n", rel, rel)
	b.WriteString("new file mode 100644\n")
	fmt.Fprintf(&b, "--- /dev/null\n+++ b/%s\n", rel)
	lines := strings.Split(content, "\n")
	// Drop the empty trailing element strings.Split produces when content ends with \n
	if len(lines) > 0 && lines[len(lines)-1] == "" {
		lines = lines[:len(lines)-1]
	}
	fmt.Fprintf(&b, "@@ -0,0 +1,%d @@\n", len(lines))
	for _, ln := range lines {
		b.WriteString("+")
		b.WriteString(ln)
		b.WriteString("\n")
	}
	if truncated {
		fmt.Fprintf(&b, "# ... [%s truncated at %d chars]\n", rel, budget.PerFileChars)
	}
	return b.String(), truncated
}

// budgetAccept takes as much of s as fits in `remaining` chars, snapping back
// to the last newline when it has to cut. Returns the accepted slice, bytes
// consumed, and whether truncation happened.
func budgetAccept(s string, remaining int) (string, int, bool) {
	if remaining <= 0 {
		return "", 0, true
	}
	if len(s) <= remaining {
		return s, len(s), false
	}
	cut := s[:remaining]
	if nl := strings.LastIndexByte(cut, '\n'); nl > remaining/2 {
		cut = cut[:nl+1]
	}
	cut += "\n... [truncated]\n"
	return cut, len(cut), true
}

// truncateDiffBody trims a complete git-diff blob to fit the budget while
// preserving file boundaries: it cuts between "diff --git" headers so the
// model never sees half a hunk.
func truncateDiffBody(diff string, budget DiffBudget) (string, bool) {
	budget = budget.withDefaults()
	if len(diff) <= budget.TotalChars {
		return diff, false
	}
	// Walk file boundaries and keep whole files until we run out of budget.
	parts := strings.Split(diff, "\ndiff --git ")
	var b strings.Builder
	used := 0
	truncated := false
	for i, p := range parts {
		piece := p
		if i > 0 {
			piece = "diff --git " + p
		}
		if used+len(piece) > budget.TotalChars {
			fmt.Fprintf(&b, "\n... [%d more file diff(s) omitted, %d chars over budget]\n", len(parts)-i, used+len(piece)-budget.TotalChars)
			truncated = true
			break
		}
		// Per-file cap on a single overlong file
		if len(piece) > budget.PerFileChars {
			piece = piece[:budget.PerFileChars] + "\n... [file diff truncated]\n"
			truncated = true
		}
		b.WriteString(piece)
		used += len(piece)
	}
	return b.String(), truncated
}

// isBinary uses git's heuristic: a file is binary if its first 8KB contains
// a NUL byte. Cheap and correct for the common cases (PNG, GGUF, exe).
func isBinary(data []byte) bool {
	n := len(data)
	if n > 8192 {
		n = 8192
	}
	return bytes.IndexByte(data[:n], 0) >= 0
}

func indexOf(ss []string, s string) int {
	for i, v := range ss {
		if v == s {
			return i
		}
	}
	return -1
}

// SanitizeCommitMessage strips common LLM artefacts: markdown code fences,
// leading labels, and surrounding whitespace.
func SanitizeCommitMessage(s string) string {
	s = strings.TrimSpace(s)
	if strings.HasPrefix(s, "```") {
		s = strings.TrimPrefix(s, "```")
		// strip a language tag like ```text or ```gitcommit on the first line
		if idx := strings.Index(s, "\n"); idx > 0 && !strings.Contains(s[:idx], " ") {
			s = strings.TrimSpace(s[idx+1:])
		}
		s = strings.TrimSuffix(s, "```")
	}
	for _, prefix := range []string{"Commit message:", "commit message:", "Message:"} {
		s = strings.TrimSpace(strings.TrimPrefix(s, prefix))
	}
	return strings.TrimSpace(s)
}
