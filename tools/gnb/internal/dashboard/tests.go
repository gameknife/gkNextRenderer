package dashboard

import (
	"context"
	"os"
	"os/exec"
	"strings"
	"time"
)

// TestCase is a single Catch2 test discovered via `--list-tests`.
type TestCase struct {
	Name string
	Tags string
}

// ListCatch2Tests runs the given test binary with `--list-tests` and parses the
// output into a slice. We deliberately use the verbose form (not
// `--list-test-names-only`) because the latter is renamed/removed across
// Catch2 versions. The parser is permissive: lines indented with exactly 2
// spaces are test names; the very next line indented 4+ spaces is tags.
func ListCatch2Tests(binPath string) ([]TestCase, error) {
	if _, err := os.Stat(binPath); err != nil {
		return nil, err
	}
	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()
	cmd := exec.CommandContext(ctx, binPath, "--list-tests")
	cmd.Dir = filepathDir(binPath)
	out, err := cmd.Output()
	if err != nil {
		return nil, err
	}
	return parseCatch2List(string(out)), nil
}

func parseCatch2List(text string) []TestCase {
	text = strings.ReplaceAll(text, "\r\n", "\n")
	lines := strings.Split(text, "\n")
	var out []TestCase
	var pending *TestCase
	for _, line := range lines {
		if line == "" {
			pending = nil
			continue
		}
		// Footer "N test cases" / "N matching test cases".
		trimmed := strings.TrimSpace(line)
		if strings.HasSuffix(trimmed, "test cases") || strings.HasSuffix(trimmed, "test case") {
			pending = nil
			continue
		}
		indent := countLeadingSpaces(line)
		switch {
		case indent == 2:
			out = append(out, TestCase{Name: strings.TrimSpace(line)})
			pending = &out[len(out)-1]
		case indent >= 4 && pending != nil:
			tags := strings.TrimSpace(line)
			if pending.Tags == "" {
				pending.Tags = tags
			} else {
				pending.Tags += " " + tags
			}
		default:
			pending = nil
		}
	}
	return out
}

func countLeadingSpaces(s string) int {
	n := 0
	for n < len(s) && s[n] == ' ' {
		n++
	}
	return n
}

// filepathDir is a tiny helper to avoid pulling in path/filepath at package top
// just for one call site (the binary's directory is used as the test cwd so
// any --reporter file paths stay near the binary).
func filepathDir(p string) string {
	for i := len(p) - 1; i >= 0; i-- {
		if p[i] == '/' || p[i] == '\\' {
			return p[:i]
		}
	}
	return "."
}
