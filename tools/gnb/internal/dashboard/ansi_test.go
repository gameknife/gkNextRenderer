package dashboard

import (
	"strings"
	"testing"
)

func TestAnsiToHTMLFormatsStructuredSpdlogLine(t *testing.T) {
	line := "[2026-04-09 00:31:54.985] [warning] [AIService.cpp:881] Failed to initialize provider: Gemini"

	got := ansiToHTML(line)

	if !strings.Contains(got, `<span style="color:#6b7384">[2026-04-09 00:31:54.985]</span>`) {
		t.Fatalf("missing timestamp styling: %q", got)
	}
	if !strings.Contains(got, `<span style="color:#eab308;font-weight:600">[warning]</span>`) {
		t.Fatalf("missing warning level styling: %q", got)
	}
	if !strings.Contains(got, `<span style="color:#9aa3b2">[AIService.cpp:881]</span>`) {
		t.Fatalf("missing source styling: %q", got)
	}
	if !strings.Contains(got, `Failed to initialize provider: Gemini`) {
		t.Fatalf("missing message text: %q", got)
	}
}

func TestAnsiToHTMLKeepsAnsiColorsWhenPresent(t *testing.T) {
	line := "\x1b[31m[error]\x1b[0m fatal"

	got := ansiToHTML(line)

	if !strings.Contains(got, `<span style="color:#ef4444;">[error]</span> fatal`) {
		t.Fatalf("expected ANSI-derived color output, got %q", got)
	}
	if strings.Contains(got, `font-weight:600">[error]`) {
		t.Fatalf("unexpected structured log fallback for ANSI line: %q", got)
	}
}

func TestAnsiToHTMLFallsBackToEscapedPlainText(t *testing.T) {
	line := "plain <text>"

	got := ansiToHTML(line)

	if got != "plain &lt;text&gt;" {
		t.Fatalf("unexpected plain fallback output: %q", got)
	}
}
