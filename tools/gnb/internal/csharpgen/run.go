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
	EntryCount int
	Changed    bool
	OutputPath string
}

// Run regenerates Engine.g.cs from the def file. With check set it only reports whether the file
// on disk is up to date, which is what CI and the pre-commit path want.
func Run(repoRoot string, check bool) (Result, error) {
	defPath := filepath.Join(repoRoot, filepath.FromSlash(DefPath))
	outPath := filepath.Join(repoRoot, filepath.FromSlash(OutputPath))

	entries, err := ParseFile(defPath)
	if err != nil {
		return Result{}, err
	}

	generated, err := Generate(entries)
	if err != nil {
		return Result{}, err
	}

	result := Result{EntryCount: len(entries), OutputPath: outPath}

	existing, readErr := os.ReadFile(outPath)
	// Compare with line endings normalised: git may check the file out with CRLF on Windows, and a
	// spurious "out of date" would make the check useless.
	result.Changed = readErr != nil || normalize(string(existing)) != normalize(generated)

	if check {
		if result.Changed {
			return result, fmt.Errorf("%s is out of date; run 'gnb csharpgen'", OutputPath)
		}
		return result, nil
	}

	if !result.Changed {
		return result, nil
	}

	if err := os.MkdirAll(filepath.Dir(outPath), 0o755); err != nil {
		return result, err
	}
	return result, os.WriteFile(outPath, []byte(generated), 0o644)
}

func normalize(text string) string {
	return strings.ReplaceAll(text, "\r\n", "\n")
}
