package dashboard

import (
	"os"
	"path/filepath"
	"regexp"
	"sort"
	"strings"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/platform"
)

var addExecutablePattern = regexp.MustCompile(`(?im)\badd_executable\s*\(\s*([A-Za-z0-9_.+-]+)`)

type targetVM struct {
	Name     string
	Runnable bool
	Built    bool
}

func discoverTargets(repoRoot, preset string, fallback []string) []targetVM {
	names := discoverCMakeExecutables(filepath.Join(repoRoot, "src"))
	seen := make(map[string]bool, len(names)+len(fallback))
	merged := make([]string, 0, len(names)+len(fallback))
	appendName := func(name string) {
		name = strings.TrimSpace(name)
		if name == "" || seen[name] {
			return
		}
		seen[name] = true
		merged = append(merged, name)
	}
	for _, name := range names {
		appendName(name)
	}
	for _, name := range fallback {
		appendName(name)
	}

	binDir := platform.BinDir(repoRoot, preset)
	targets := make([]targetVM, 0, len(merged))
	for _, name := range merged {
		_, err := os.Stat(platform.ExecutablePath(binDir, name))
		targets = append(targets, targetVM{
			Name:     name,
			Runnable: name != "gkNextUnitTests",
			Built:    err == nil,
		})
	}
	return targets
}

func discoverCMakeExecutables(root string) []string {
	var files []string
	_ = filepath.WalkDir(root, func(path string, entry os.DirEntry, err error) error {
		if err != nil || entry.IsDir() {
			return nil
		}
		name := entry.Name()
		if name == "CMakeLists.txt" || strings.EqualFold(filepath.Ext(name), ".cmake") {
			files = append(files, path)
		}
		return nil
	})
	sort.Strings(files)

	seen := map[string]bool{}
	var targets []string
	for _, path := range files {
		content, err := os.ReadFile(path)
		if err != nil {
			continue
		}
		for _, match := range addExecutablePattern.FindAllSubmatch(content, -1) {
			name := string(match[1])
			if !seen[name] {
				seen[name] = true
				targets = append(targets, name)
			}
		}
	}
	return targets
}
