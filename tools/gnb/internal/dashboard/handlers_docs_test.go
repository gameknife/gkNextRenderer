package dashboard

import (
	"html/template"
	"net/http/httptest"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func setupDocsRepo(t *testing.T) *Server {
	t.Helper()
	dir := t.TempDir()
	for path, body := range map[string]string{
		"docs/zeta.md":                  "# Zeta\n\n根目录末尾文档。\n",
		"docs/alpha.md":                 "# Alpha\n\n第一篇文档。\n",
		"docs/architecture/overview.md": "# Overview\n\n架构文档。\n",
		"docs/projects/zeta.md":         "# Project Zeta\n\n项目末尾文档。\n",
		"docs/projects/guide.md":        "# Guide\n\n项目文档。\n",
		"docs/gallery/ignore.avif":      "not-markdown",
		"docs/projects/ignore.txt":      "ignore me",
		"src/example.hpp":               "#pragma once\n\nstruct Example\n{\n    int value;\n};\n",
		"assets/binary.dat":             "binary\x00data",
		".spec/TODO.md":                 "# TODO\n\n## Milestone: 测试  <!-- status: active -->\n\n### 下一步\n\n(暂无)\n\n### 待规划\n\n(暂无)\n\n### 最近完成\n\n(暂无)\n",
	} {
		full := filepath.Join(dir, filepath.FromSlash(path))
		if err := os.MkdirAll(filepath.Dir(full), 0o755); err != nil {
			t.Fatal(err)
		}
		if err := os.WriteFile(full, []byte(body), 0o644); err != nil {
			t.Fatal(err)
		}
	}
	tpl, err := template.New("dashboard").
		Funcs(templateFuncs()).
		ParseFS(templateFS, "templates/*.html")
	if err != nil {
		t.Fatal(err)
	}
	return &Server{
		opts:  Options{RepoRoot: dir},
		tpl:   tpl,
		jobs:  NewJobManager(),
		chats: NewChatStore(),
	}
}

func TestBuildDocsVMListsMarkdownFilesOnly(t *testing.T) {
	s := setupDocsRepo(t)

	vm := s.buildDocsVM("", false, "", "")

	if len(vm.Files) != 5 {
		t.Fatalf("len(files) = %d, want 5", len(vm.Files))
	}
	wantFiles := []string{
		"docs/alpha.md",
		"docs/zeta.md",
		"docs/architecture/overview.md",
		"docs/projects/guide.md",
		"docs/projects/zeta.md",
	}
	for i, want := range wantFiles {
		if vm.Files[i].RelPath != want {
			t.Fatalf("files[%d] = %q, want %q; files = %+v", i, vm.Files[i].RelPath, want, vm.Files)
		}
	}
	if len(vm.Folders) != 3 {
		t.Fatalf("len(folders) = %d, want 3", len(vm.Folders))
	}
	wantFolders := []string{"docs", "docs/architecture", "docs/projects"}
	for i, want := range wantFolders {
		if vm.Folders[i].Dir != want {
			t.Fatalf("folders[%d] = %q, want %q; folders = %+v", i, vm.Folders[i].Dir, want, vm.Folders)
		}
	}
	if !vm.HasDoc || vm.Selected.RelPath != "docs/alpha.md" {
		t.Fatalf("selected = %+v, want docs/alpha.md", vm.Selected)
	}
	if !vm.Folders[0].Active || vm.Folders[1].Active || vm.Folders[2].Active {
		t.Fatalf("folder active states = %+v, want only docs active", vm.Folders)
	}
	if !strings.Contains(vm.Content, "第一篇文档") {
		t.Fatalf("content = %q, want alpha markdown", vm.Content)
	}
}

func TestResolveDocMarkdownPathRejectsTraversalAndNonMarkdown(t *testing.T) {
	s := setupDocsRepo(t)

	if _, _, err := resolveDocMarkdownPath(s.opts.RepoRoot, "../AGENTS.md"); err == nil {
		t.Fatal("expected traversal path to fail")
	}
	if _, _, err := resolveDocMarkdownPath(s.opts.RepoRoot, "docs/gallery/ignore.avif"); err == nil {
		t.Fatal("expected non-markdown path to fail")
	}
	if rel, _, err := resolveDocMarkdownPath(s.opts.RepoRoot, "docs/projects/guide.md"); err != nil || rel != "docs/projects/guide.md" {
		t.Fatalf("resolve nested markdown = (%q, %v), want docs/projects/guide.md", rel, err)
	}
}

func TestHandleTabDocsRendersEditView(t *testing.T) {
	s := setupDocsRepo(t)

	req := httptest.NewRequest("GET", "/tab/docs?file=docs/projects/guide.md&edit=1", nil)
	req.SetPathValue("kind", "docs")
	rec := httptest.NewRecorder()

	s.handleTab(rec, req)

	if rec.Code != 200 {
		t.Fatalf("status = %d (%s), want 200", rec.Code, rec.Body.String())
	}
	body := rec.Body.String()
	if !strings.Contains(body, `name="path" value="docs/projects/guide.md"`) {
		t.Fatalf("rendered body missing selected path:\n%s", body)
	}
	if !strings.Contains(body, "<textarea") {
		t.Fatalf("rendered body missing textarea:\n%s", body)
	}
	if !strings.Contains(body, "项目文档") {
		t.Fatalf("rendered body missing markdown text:\n%s", body)
	}
	if !strings.Contains(body, `data-doc-folder="docs/projects"`) {
		t.Fatalf("rendered body missing projects folder group:\n%s", body)
	}
	if !strings.Contains(body, `class="docs-folder active"`) {
		t.Fatalf("rendered body missing active folder:\n%s", body)
	}
	if !strings.Contains(body, `data-docs-search`) || !strings.Contains(body, `class="docs-main"`) {
		t.Fatalf("rendered body missing docs workspace controls:\n%s", body)
	}
}

func TestHandleDocsSaveWritesFileAndReturnsPreview(t *testing.T) {
	s := setupDocsRepo(t)

	form := strings.NewReader("path=docs%2Falpha.md&body=%23%23+Updated%0A%0A%E4%BF%AE%E6%94%B9%E5%90%8E")
	req := httptest.NewRequest("POST", "/docs/save", form)
	req.Header.Set("Content-Type", "application/x-www-form-urlencoded")
	rec := httptest.NewRecorder()

	s.handleDocsSave(rec, req)

	if rec.Code != 200 {
		t.Fatalf("status = %d (%s), want 200", rec.Code, rec.Body.String())
	}
	data, err := os.ReadFile(filepath.Join(s.opts.RepoRoot, "docs", "alpha.md"))
	if err != nil {
		t.Fatal(err)
	}
	if string(data) != "## Updated\n\n修改后\n" {
		t.Fatalf("saved doc = %q, want normalized markdown", string(data))
	}
	body := rec.Body.String()
	if strings.Contains(body, "<textarea") {
		t.Fatalf("save should return preview mode, got edit form:\n%s", body)
	}
	if !strings.Contains(body, "修改后") {
		t.Fatalf("preview missing saved body:\n%s", body)
	}
}

func TestBuildDocsSourceVMReadsRepoFileAndFocusesLine(t *testing.T) {
	s := setupDocsRepo(t)

	vm := buildDocsSourceVM(s.opts.RepoRoot, "src/example.hpp", "5")

	if vm.Error != "" {
		t.Fatalf("unexpected error: %s", vm.Error)
	}
	if vm.RelPath != "src/example.hpp" || vm.Line != 5 || vm.LineCount != 6 {
		t.Fatalf("source metadata = %+v", vm)
	}
	if vm.Language != "cpp" || !strings.Contains(vm.Content, "struct Example") {
		t.Fatalf("source highlighting metadata = %+v", vm)
	}
	if len(vm.Lines) != 6 || !vm.Lines[4].Focus || vm.Lines[4].Text != "    int value;" {
		t.Fatalf("focused source line = %+v", vm.Lines)
	}
}

func TestBuildDocsSourceVMRejectsTraversalAndBinary(t *testing.T) {
	s := setupDocsRepo(t)

	if vm := buildDocsSourceVM(s.opts.RepoRoot, "../outside.txt", ""); vm.Error == "" {
		t.Fatal("expected traversal path to fail")
	}
	if vm := buildDocsSourceVM(s.opts.RepoRoot, "assets/binary.dat", ""); vm.Error == "" {
		t.Fatal("expected binary file to fail")
	}
}

func TestHandleDocsSourceRendersHighlightedLine(t *testing.T) {
	s := setupDocsRepo(t)
	req := httptest.NewRequest("GET", "/docs/source?path=src/example.hpp&line=5", nil)
	rec := httptest.NewRecorder()

	s.handleDocsSource(rec, req)

	if rec.Code != 200 {
		t.Fatalf("status = %d (%s), want 200", rec.Code, rec.Body.String())
	}
	body := rec.Body.String()
	if !strings.Contains(body, `class="docs-source-line-number focus" data-line="5"`) {
		t.Fatalf("rendered body missing focused line:\n%s", body)
	}
	if !strings.Contains(body, `<code class="language-cpp">`) {
		t.Fatalf("rendered body missing source language:\n%s", body)
	}
	if !strings.Contains(body, "int value;") {
		t.Fatalf("rendered body missing source text:\n%s", body)
	}
}

func TestDocsSourceLanguageUsesProjectRelevantHighlighters(t *testing.T) {
	cases := map[string]string{
		"src/main.cpp":                   "cpp",
		"src/Common/CoreMinimal.hpp":     "cpp",
		"assets/shaders/main.frag.slang": "cpp",
		"assets/shaders/shared.glsl":     "glsl",
		"assets/scad/example.scad":       "openscad",
		"tools/gnb/internal/main.go":     "go",
		"tools/build.ps1":                "powershell",
		"assets/scripts/game.ts":         "typescript",
		"cmake/toolchain.cmake":          "cmake",
		"CMakeLists.txt":                 "cmake",
		"docs/unknown.custom-extension":  "",
	}
	for path, want := range cases {
		if got := docsSourceLanguage(path); got != want {
			t.Errorf("docsSourceLanguage(%q) = %q, want %q", path, got, want)
		}
	}
}

func TestHandleTabSettingsRendersDisplayControls(t *testing.T) {
	s := setupDocsRepo(t)
	req := httptest.NewRequest("GET", "/tab/settings", nil)
	req.SetPathValue("kind", "settings")
	rec := httptest.NewRecorder()

	s.handleTab(rec, req)

	if rec.Code != 200 {
		t.Fatalf("status = %d (%s), want 200", rec.Code, rec.Body.String())
	}
	body := rec.Body.String()
	for _, want := range []string{
		`data-display-settings`,
		`data-display-setting="uiFontSize"`,
		`data-display-setting="codeFontSize"`,
		`data-display-setting="density"`,
		`data-display-reset`,
	} {
		if !strings.Contains(body, want) {
			t.Fatalf("settings response missing %q:\n%s", want, body)
		}
	}
}
