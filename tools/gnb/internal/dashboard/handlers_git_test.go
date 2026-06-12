package dashboard

import (
	"bytes"
	"html/template"
	"strings"
	"testing"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/gitops"
)

func parseDashboardTemplates(t *testing.T) *template.Template {
	t.Helper()
	tpl, err := template.New("dashboard").
		Funcs(templateFuncs()).
		ParseFS(templateFS, "templates/*.html")
	if err != nil {
		t.Fatal(err)
	}
	return tpl
}

func TestGitCommitDetailMarksBodyForMarkdownRendering(t *testing.T) {
	var out bytes.Buffer
	err := parseDashboardTemplates(t).ExecuteTemplate(&out, "git_commit_detail", gitops.Commit{
		Hash:    "abc1234",
		Subject: "Render commit details",
		Author:  "Tester",
		Date:    "2026-06-12",
		Body:    "## Details\n\n- first\n- second",
	})
	if err != nil {
		t.Fatal(err)
	}

	body := out.String()
	if !strings.Contains(body, `class="commit-message" data-md`) {
		t.Fatalf("commit detail missing markdown hook:\n%s", body)
	}
	if strings.Contains(body, "<pre>") {
		t.Fatalf("commit detail should not render the message as plain preformatted text:\n%s", body)
	}
}

func TestGitBodyUsesCompactIconActions(t *testing.T) {
	var out bytes.Buffer
	err := parseDashboardTemplates(t).ExecuteTemplate(&out, "git_body", indexVM{
		GitVM: gitVM{
			Commits: []gitops.Commit{{
				Hash: "abc1234", Full: "abc123456789", Subject: "Subject", Author: "Tester", Date: "2026-06-12",
			}},
			Stashes: []gitops.Stash{{
				Ref: "stash@{0}", Message: "work in progress",
			}},
		},
	})
	if err != nil {
		t.Fatal(err)
	}

	body := out.String()
	for _, want := range []string{
		`class="stash-actions"`,
		`aria-label="应用并移除 stash@{0}"`,
		`aria-label="应用并保留 stash@{0}"`,
		`aria-label="删除 stash@{0}"`,
		`class="commit-reset"`,
		`aria-label="reset --hard 到提交 abc1234"`,
	} {
		if !strings.Contains(body, want) {
			t.Fatalf("git body missing %q:\n%s", want, body)
		}
	}
	if strings.Contains(body, ">pop</button>") || strings.Contains(body, ">apply</button>") || strings.Contains(body, ">drop</button>") {
		t.Fatalf("stash actions should use icons instead of text labels:\n%s", body)
	}
}
