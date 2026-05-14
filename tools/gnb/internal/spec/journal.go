package spec

import (
	"fmt"
	"os"
	"path/filepath"
	"strings"
	"time"
)

// JournalStub is the minimal completion report layout written by `gnb todo done`.
// The actual report content is expected to be filled in by the AGENT during
// workflow execution; this stub gives it a consistent skeleton.
type JournalStub struct {
	TaskID    int
	BuildOK   bool
	Completed time.Time
	Summary   string   // optional initial summary
	Files     []string // optional file list
	Notes     string   // optional risks/leftover
}

// WriteJournalStub creates journal/<id>.md if it doesn't exist, returning the
// path written. If it already exists, no write happens and the existing path
// is returned with os.ErrExist wrapped.
func WriteJournalStub(repoRoot string, j JournalStub) (string, error) {
	if err := os.MkdirAll(JournalDir(repoRoot), 0755); err != nil {
		return "", err
	}
	path := JournalPath(repoRoot, j.TaskID)
	if _, err := os.Stat(path); err == nil {
		return path, fmt.Errorf("%w: %s", os.ErrExist, path)
	}
	body := buildJournalBody(j)
	if err := os.WriteFile(path, []byte(body), 0644); err != nil {
		return "", err
	}
	return path, nil
}

func buildJournalBody(j JournalStub) string {
	t := j.Completed
	if t.IsZero() {
		t = time.Now()
	}
	var b strings.Builder
	fmt.Fprintf(&b, "---\n")
	fmt.Fprintf(&b, "task: %05d\n", j.TaskID)
	fmt.Fprintf(&b, "completed: %s\n", t.Format(time.RFC3339))
	fmt.Fprintf(&b, "build_ok: %t\n", j.BuildOK)
	fmt.Fprintf(&b, "---\n\n")
	b.WriteString("## 做了什么\n\n")
	if j.Summary != "" {
		b.WriteString(j.Summary)
		b.WriteString("\n\n")
	} else {
		b.WriteString("…\n\n")
	}
	b.WriteString("## 改动文件\n\n")
	if len(j.Files) > 0 {
		for _, f := range j.Files {
			fmt.Fprintf(&b, "- `%s`\n", f)
		}
		b.WriteByte('\n')
	} else {
		b.WriteString("- …\n\n")
	}
	b.WriteString("## 风险/遗留\n\n")
	if j.Notes != "" {
		b.WriteString(j.Notes)
		b.WriteString("\n")
	} else {
		b.WriteString("- 无\n")
	}
	return b.String()
}

// BlockerStub is the minimal blocker file layout written by `gnb todo block`.
type BlockerStub struct {
	TaskID    int
	BlockedAt time.Time
	Reason    string
}

func WriteBlockerStub(repoRoot string, b BlockerStub) (string, error) {
	if err := os.MkdirAll(BlockerDir(repoRoot), 0755); err != nil {
		return "", err
	}
	path := BlockerPath(repoRoot, b.TaskID)
	if _, err := os.Stat(path); err == nil {
		return path, fmt.Errorf("%w: %s", os.ErrExist, path)
	}
	body := buildBlockerBody(b)
	if err := os.WriteFile(path, []byte(body), 0644); err != nil {
		return "", err
	}
	return path, nil
}

func buildBlockerBody(b BlockerStub) string {
	t := b.BlockedAt
	if t.IsZero() {
		t = time.Now()
	}
	var sb strings.Builder
	fmt.Fprintf(&sb, "---\n")
	fmt.Fprintf(&sb, "task: %05d\n", b.TaskID)
	fmt.Fprintf(&sb, "blocked_at: %s\n", t.Format(time.RFC3339))
	fmt.Fprintf(&sb, "---\n\n")
	sb.WriteString("## 歧义点\n\n")
	if b.Reason != "" {
		sb.WriteString(b.Reason)
		sb.WriteString("\n\n")
	} else {
		sb.WriteString("…\n\n")
	}
	sb.WriteString("## 候选方案\n\n- …\n")
	return sb.String()
}

// ReadIfExists is a small helper for `gnb todo show` that returns the file
// contents and true if the file exists, or "" and false otherwise.
func ReadIfExists(path string) (string, bool) {
	data, err := os.ReadFile(path)
	if err != nil {
		return "", false
	}
	return string(data), true
}

// Sanity: ensure JournalDir/BlockerDir/SpecsDir helpers are usable by callers.
var _ = filepath.Join
