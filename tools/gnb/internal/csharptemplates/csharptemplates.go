// Package csharptemplates compiles the shipped C# game templates.
//
// The templates are the first code a user of the managed layer ever reads, and nothing else in the
// tree builds them: a change to GkNext.Engine that breaks one is invisible until somebody creates a
// project from the launcher and gets a wall of compiler errors instead of a game. The C++ unit test
// covers substitution and the manifest; this covers the half that matters most, which is whether
// the result compiles at all.
//
// A template is instantiated the way ManagedGameTemplate.cpp instantiates it — the same {{Token}}
// and __Token__ replacement, applied to file contents and file names alike — into a scratch project
// under assets/csharp. It has to live there because the generated csproj reaches its two references
// by relative path; anywhere else would be checking a csproj no user will ever have.
package csharptemplates

import (
	"fmt"
	"os"
	"path/filepath"
	"sort"
	"strings"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/dotnetsdk"
)

// TemplateRoot is where the shipped templates live, relative to the repository root.
const TemplateRoot = "assets/templates/games"

// ScratchPrefix marks the throwaway project directories this package writes into assets/csharp.
// csharpsln.Discover skips anything carrying it, so a check interrupted half way through cannot
// leave a phantom project in the IDE solution.
const ScratchPrefix = "_templatecheck_"

// Result is the outcome for one template.
type Result struct {
	ID     string
	Output string
	Err    error
}

// Discover lists the templates that ship with the engine, in a stable order.
func Discover(repoRoot string) ([]string, error) {
	root := filepath.Join(repoRoot, filepath.FromSlash(TemplateRoot))
	entries, err := os.ReadDir(root)
	if err != nil {
		return nil, fmt.Errorf("reading %s: %w", TemplateRoot, err)
	}

	var ids []string
	for _, entry := range entries {
		if !entry.IsDir() {
			continue
		}
		// Both are required for a template to be usable, and the C++ loader agrees: a directory
		// with only a manifest produces "template has no files to copy" at creation time.
		if !fileExists(filepath.Join(root, entry.Name(), "template.json")) {
			continue
		}
		if !dirExists(filepath.Join(root, entry.Name(), "files")) {
			continue
		}
		ids = append(ids, entry.Name())
	}
	sort.Strings(ids)
	if len(ids) == 0 {
		return nil, fmt.Errorf("no game templates found under %s", TemplateRoot)
	}
	return ids, nil
}

// CleanScratch removes scratch projects left behind by an interrupted run. Called before a check
// as well as after one, because the directory a crash leaves behind would otherwise fail the next
// instantiation with "already exists".
func CleanScratch(repoRoot string) error {
	managed := dotnetsdk.SourceDir(repoRoot)
	entries, err := os.ReadDir(managed)
	if err != nil {
		if os.IsNotExist(err) {
			return nil
		}
		return err
	}
	for _, entry := range entries {
		if entry.IsDir() && strings.HasPrefix(entry.Name(), ScratchPrefix) {
			if err := os.RemoveAll(filepath.Join(managed, entry.Name())); err != nil {
				return err
			}
		}
	}
	return nil
}

// Check instantiates each template and builds it, returning one result per template. Templates are
// built even after an earlier one fails, so a single run reports every broken template rather than
// only the first.
func Check(repoRoot string, toolchain dotnetsdk.Toolchain, ids []string, configuration string) ([]Result, error) {
	if len(ids) == 0 {
		discovered, err := Discover(repoRoot)
		if err != nil {
			return nil, err
		}
		ids = discovered
	}
	if configuration == "" {
		configuration = "Debug"
	}
	if err := CleanScratch(repoRoot); err != nil {
		return nil, err
	}
	defer func() { _ = CleanScratch(repoRoot) }()

	results := make([]Result, 0, len(ids))
	for _, id := range ids {
		output, err := checkOne(repoRoot, toolchain, id, configuration)
		results = append(results, Result{ID: id, Output: output, Err: err})
	}
	return results, nil
}

func checkOne(repoRoot string, toolchain dotnetsdk.Toolchain, id string, configuration string) (string, error) {
	// A C# identifier: the project name becomes a namespace and a type name, so it cannot carry the
	// hyphens or digits-first shapes a directory name is allowed to have.
	project := ScratchPrefix + sanitizeIdentifier(id)
	projectDir := filepath.Join(dotnetsdk.SourceDir(repoRoot), project)
	sourceDir := filepath.Join(repoRoot, filepath.FromSlash(TemplateRoot), id, "files")

	if err := os.RemoveAll(projectDir); err != nil {
		return "", err
	}
	defer func() { _ = os.RemoveAll(projectDir) }()

	tokens := [][2]string{
		{"ProjectName", project},
		{"Namespace", project},
		{"DisplayName", id + " template check"},
		{"GameId", strings.ToLower(project)},
		{"TemplateId", id},
	}
	if err := instantiate(sourceDir, projectDir, tokens); err != nil {
		return "", err
	}

	csproj := filepath.Join(projectDir, project+".csproj")
	if !fileExists(csproj) {
		return "", fmt.Errorf("template %q produced no %s.csproj", id, project)
	}

	command := toolchain.Command(projectDir, "build", csproj, "-c", configuration, "--nologo", "-v", "q")
	output, err := command.CombinedOutput()
	if err != nil {
		return string(output), fmt.Errorf("template %q does not compile: %w", id, err)
	}
	return string(output), nil
}

// instantiate mirrors ManagedGameTemplate.cpp: every {{Token}} and __Token__ is replaced, in file
// contents and in path components alike.
func instantiate(sourceDir string, targetDir string, tokens [][2]string) error {
	return filepath.WalkDir(sourceDir, func(path string, entry os.DirEntry, err error) error {
		if err != nil {
			return err
		}
		relative, relErr := filepath.Rel(sourceDir, path)
		if relErr != nil {
			return relErr
		}
		destination := filepath.Join(targetDir, substitute(relative, tokens))

		if entry.IsDir() {
			return os.MkdirAll(destination, 0o755)
		}
		content, readErr := os.ReadFile(path)
		if readErr != nil {
			return readErr
		}
		if mkErr := os.MkdirAll(filepath.Dir(destination), 0o755); mkErr != nil {
			return mkErr
		}
		return os.WriteFile(destination, []byte(substitute(string(content), tokens)), 0o644)
	})
}

func substitute(text string, tokens [][2]string) string {
	for _, token := range tokens {
		text = strings.ReplaceAll(text, "{{"+token[0]+"}}", token[1])
		text = strings.ReplaceAll(text, "__"+token[0]+"__", token[1])
	}
	return text
}

func sanitizeIdentifier(value string) string {
	var builder strings.Builder
	for _, r := range value {
		switch {
		case r >= 'a' && r <= 'z', r >= 'A' && r <= 'Z', r >= '0' && r <= '9':
			builder.WriteRune(r)
		default:
			builder.WriteRune('_')
		}
	}
	return builder.String()
}

func fileExists(path string) bool {
	info, err := os.Stat(path)
	return err == nil && !info.IsDir()
}

func dirExists(path string) bool {
	info, err := os.Stat(path)
	return err == nil && info.IsDir()
}
