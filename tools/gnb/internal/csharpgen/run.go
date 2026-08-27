package csharpgen

import (
	"fmt"
	"os"
	"path/filepath"
	"strings"
)

// DefPath is the binding surface's single source of truth.
const DefPath = "src/Modules/NextDotNet/EngineApi.def.h"

// OutputPath is the generated managed file. Nothing else may write it.
const OutputPath = "assets/csharp/GkNext.Engine/Engine.g.cs"

// Result reports what a Run call did, so callers can print something useful.
type Result struct {
	EntryCount    int
	PropertyCount int
	Changed       bool
	Outputs       []string // files that were written, or that are stale under check
}

// Run regenerates the managed layer from its two sources: the hand-written binding surface
// (EngineApi.def.h) and the reflection snapshot (ReflectionManifest.json). With check set it only
// reports whether the files on disk are up to date, which is what CI and the pre-commit path want.
func Run(repoRoot string, check bool) (Result, error) {
	entries, err := ParseFile(filepath.Join(repoRoot, filepath.FromSlash(DefPath)))
	if err != nil {
		return Result{}, err
	}
	engineSource, err := Generate(entries)
	if err != nil {
		return Result{}, err
	}

	manifest, err := ParseManifest(filepath.Join(repoRoot, filepath.FromSlash(ManifestPath)))
	if err != nil {
		return Result{}, err
	}
	componentsSource, err := GenerateComponents(manifest)
	if err != nil {
		return Result{}, err
	}

	result := Result{EntryCount: len(entries)}
	for _, reflected := range manifest.Types {
		result.PropertyCount += len(reflected.Properties)
	}

	files := []struct {
		relative string
		content  string
	}{
		{OutputPath, engineSource},
		{ComponentsOutputPath, componentsSource},
	}

	var stale []string
	for _, file := range files {
		path := filepath.Join(repoRoot, filepath.FromSlash(file.relative))
		existing, readErr := os.ReadFile(path)
		// Compare with line endings normalised: git may check the file out with CRLF on Windows,
		// and a spurious "out of date" would make the check useless.
		if readErr != nil || normalize(string(existing)) != normalize(file.content) {
			stale = append(stale, file.relative)
		}
	}

	result.Changed = len(stale) > 0
	result.Outputs = stale

	if check {
		if result.Changed {
			return result, fmt.Errorf("%s is out of date; run 'gnb csharpgen'", strings.Join(stale, ", "))
		}
		return result, nil
	}
	if !result.Changed {
		return result, nil
	}

	for _, file := range files {
		path := filepath.Join(repoRoot, filepath.FromSlash(file.relative))
		if err := os.MkdirAll(filepath.Dir(path), 0o755); err != nil {
			return result, err
		}
		if err := os.WriteFile(path, []byte(file.content), 0o644); err != nil {
			return result, err
		}
	}
	return result, nil
}

func normalize(text string) string {
	return strings.ReplaceAll(text, "\r\n", "\n")
}
