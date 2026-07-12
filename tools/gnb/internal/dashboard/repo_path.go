package dashboard

import (
	"bytes"
	"path/filepath"
	"strings"
)

func isProbablyBinary(data []byte) bool {
	sample := data
	if len(sample) > 8192 {
		sample = sample[:8192]
	}
	return bytes.IndexByte(sample, 0) >= 0
}

func safeRepoPath(repoRoot string, rel string) (string, bool) {
	if strings.TrimSpace(rel) == "" {
		return "", false
	}
	base, err := filepath.Abs(repoRoot)
	if err != nil {
		return "", false
	}
	full, err := filepath.Abs(filepath.Join(base, filepath.Clean(rel)))
	if err != nil {
		return "", false
	}
	inside, err := filepath.Rel(base, full)
	if err != nil || inside == ".." || strings.HasPrefix(inside, ".."+string(filepath.Separator)) {
		return "", false
	}
	return full, true
}
