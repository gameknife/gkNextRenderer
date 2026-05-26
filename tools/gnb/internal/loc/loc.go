// Package loc implements the `gnb loc` line-of-code summary command.
//
// It walks src/ and aggregates lines per category. The category hierarchy is:
//
//	src/Application/<Editor|Game|Render|Util>/<project>/...   -> category=Application, sub=<Editor|Game|Render|Util>, leaf=<project>
//	src/Engine/<area>/...                                     -> category=Engine,      sub=<area>
//	src/Tests/...                                             -> category=Tests
//	src/ThirdParty/<lib>/...                                  -> category=ThirdParty,  sub=<lib>   (skipped by default)
//	src/<other-top>/...                                       -> category=<other-top>
//
// Default file extensions are C/C++ source and headers. Override with --ext.
package loc

import (
	"bufio"
	"fmt"
	"io/fs"
	"os"
	"path/filepath"
	"sort"
	"strings"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/console"
)

// Options controls the loc walk and rendering.
type Options struct {
	Root             string   // repo root
	Extensions       []string // file suffixes to count (include leading dot)
	IncludeThirdParty bool    // include src/ThirdParty in the report
}

// DefaultExtensions returns the file extensions counted by default.
func DefaultExtensions() []string {
	return []string{".cpp", ".cc", ".c", ".h", ".hpp", ".hxx", ".cxx", ".inl"}
}

type leafKey struct {
	category string
	sub      string
	leaf     string
}

type entry struct {
	files int
	lines int
}

// Report aggregates the raw per-leaf entries plus convenience rollups.
type Report struct {
	leaves      map[leafKey]*entry
	categories  map[string]*entry
	subKeys     map[string][]string             // category -> ordered sub names
	leafKeys    map[string]map[string][]string  // category -> sub -> ordered leaf names
	subTotals   map[string]map[string]*entry    // category -> sub -> totals
	total       entry
}

func newReport() *Report {
	return &Report{
		leaves:    map[leafKey]*entry{},
		categories: map[string]*entry{},
		subKeys:   map[string][]string{},
		leafKeys:  map[string]map[string][]string{},
		subTotals: map[string]map[string]*entry{},
	}
}

// Snapshot is the dashboard-friendly view of a scan result. Categories,
// subcategories and leaves are pre-sorted by line count descending so the
// caller can render directly without resorting.
type Snapshot struct {
	TotalFiles         int
	TotalLines         int
	Categories         []CategorySummary
	IncludedThirdParty bool
}

// CategorySummary aggregates a top-level src/ category.
type CategorySummary struct {
	Name  string
	Files int
	Lines int
	Subs  []SubSummary
}

// SubSummary aggregates a sub-bucket (e.g. Application/Game, Engine/Vulkan).
type SubSummary struct {
	Name   string
	Files  int
	Lines  int
	Leaves []LeafSummary
}

// LeafSummary is the most granular bucket (an Application project name).
type LeafSummary struct {
	Name  string
	Files int
	Lines int
}

// Run scans the repository and prints the report to stdout.
func Run(opts Options) error {
	report, err := scan(opts)
	if err != nil {
		return err
	}
	render(report, opts)
	return nil
}

// Scan walks the source tree and returns a sorted snapshot. It does not
// produce any output.
func Scan(opts Options) (*Snapshot, error) {
	report, err := scan(opts)
	if err != nil {
		return nil, err
	}
	return reportToSnapshot(report, opts), nil
}

func scan(opts Options) (*Report, error) {
	if len(opts.Extensions) == 0 {
		opts.Extensions = DefaultExtensions()
	}
	extSet := map[string]bool{}
	for _, ext := range opts.Extensions {
		extSet[strings.ToLower(ext)] = true
	}

	srcRoot := filepath.Join(opts.Root, "src")
	if info, err := os.Stat(srcRoot); err != nil || !info.IsDir() {
		return nil, fmt.Errorf("src directory not found at %s", srcRoot)
	}

	report := newReport()
	walkErr := filepath.WalkDir(srcRoot, func(path string, d fs.DirEntry, err error) error {
		if err != nil {
			return err
		}
		if d.IsDir() {
			return nil
		}
		if !extSet[strings.ToLower(filepath.Ext(d.Name()))] {
			return nil
		}
		rel, err := filepath.Rel(srcRoot, path)
		if err != nil {
			return err
		}
		key, ok := classify(filepath.ToSlash(rel))
		if !ok {
			return nil
		}
		if key.category == "ThirdParty" && !opts.IncludeThirdParty {
			return nil
		}
		lines, err := countLines(path)
		if err != nil {
			return err
		}
		report.add(key, lines)
		return nil
	})
	if walkErr != nil {
		return nil, walkErr
	}
	return report, nil
}

func reportToSnapshot(report *Report, opts Options) *Snapshot {
	snap := &Snapshot{
		TotalFiles:         report.total.files,
		TotalLines:         report.total.lines,
		IncludedThirdParty: opts.IncludeThirdParty,
	}
	names := make([]string, 0, len(report.categories))
	for name := range report.categories {
		names = append(names, name)
	}
	sort.Slice(names, func(i, j int) bool {
		return report.categories[names[i]].lines > report.categories[names[j]].lines
	})
	for _, name := range names {
		c := report.categories[name]
		cs := CategorySummary{Name: name, Files: c.files, Lines: c.lines}
		subs := append([]string(nil), report.subKeys[name]...)
		sort.Slice(subs, func(i, j int) bool {
			return report.subTotals[name][subs[i]].lines > report.subTotals[name][subs[j]].lines
		})
		for _, sub := range subs {
			s := report.subTotals[name][sub]
			ss := SubSummary{Name: sub, Files: s.files, Lines: s.lines}
			leaves := append([]string(nil), report.leafKeys[name][sub]...)
			sort.Slice(leaves, func(i, j int) bool {
				li := report.leaves[leafKey{category: name, sub: sub, leaf: leaves[i]}]
				lj := report.leaves[leafKey{category: name, sub: sub, leaf: leaves[j]}]
				return li.lines > lj.lines
			})
			for _, leaf := range leaves {
				l := report.leaves[leafKey{category: name, sub: sub, leaf: leaf}]
				ss.Leaves = append(ss.Leaves, LeafSummary{Name: leaf, Files: l.files, Lines: l.lines})
			}
			cs.Subs = append(cs.Subs, ss)
		}
		snap.Categories = append(snap.Categories, cs)
	}
	return snap
}

// classify maps a path (forward-slash, relative to src/) to its category key.
func classify(rel string) (leafKey, bool) {
	parts := strings.Split(rel, "/")
	if len(parts) < 2 {
		return leafKey{}, false
	}
	top := parts[0]
	switch top {
	case "Application":
		// Application/<Editor|Game|Render|Util>/<project>/...
		if len(parts) < 4 {
			// e.g. Application/foo.cpp directly — bucket under "(root)" sub for visibility.
			return leafKey{category: "Application", sub: "(root)", leaf: parts[1]}, true
		}
		sub := parts[1]
		leaf := parts[2]
		return leafKey{category: "Application", sub: sub, leaf: leaf}, true
	case "Engine":
		// Engine/<area>/... — loose files at Engine/ root go under "(top-level)".
		if len(parts) < 3 {
			return leafKey{category: "Engine", sub: "(top-level)"}, true
		}
		return leafKey{category: "Engine", sub: parts[1]}, true
	case "Tests":
		return leafKey{category: "Tests"}, true
	case "ThirdParty":
		if len(parts) < 3 {
			return leafKey{category: "ThirdParty", sub: "(top-level)"}, true
		}
		return leafKey{category: "ThirdParty", sub: parts[1]}, true
	default:
		// Unknown top-level dir under src/ — still bucket so nothing is silently dropped.
		if len(parts) < 3 {
			return leafKey{category: top}, true
		}
		return leafKey{category: top, sub: parts[1]}, true
	}
}

func (r *Report) add(key leafKey, lines int) {
	entryRef, ok := r.leaves[key]
	if !ok {
		entryRef = &entry{}
		r.leaves[key] = entryRef
		if key.sub != "" {
			if _, exists := r.leafKeys[key.category]; !exists {
				r.leafKeys[key.category] = map[string][]string{}
			}
			if key.leaf != "" {
				if !containsString(r.leafKeys[key.category][key.sub], key.leaf) {
					r.leafKeys[key.category][key.sub] = append(r.leafKeys[key.category][key.sub], key.leaf)
				}
			}
			if !containsString(r.subKeys[key.category], key.sub) {
				r.subKeys[key.category] = append(r.subKeys[key.category], key.sub)
			}
		}
	}
	entryRef.files++
	entryRef.lines += lines

	if r.categories[key.category] == nil {
		r.categories[key.category] = &entry{}
	}
	r.categories[key.category].files++
	r.categories[key.category].lines += lines

	if key.sub != "" {
		if r.subTotals[key.category] == nil {
			r.subTotals[key.category] = map[string]*entry{}
		}
		if r.subTotals[key.category][key.sub] == nil {
			r.subTotals[key.category][key.sub] = &entry{}
		}
		r.subTotals[key.category][key.sub].files++
		r.subTotals[key.category][key.sub].lines += lines
	}

	r.total.files++
	r.total.lines += lines
}

func render(report *Report, opts Options) {
	categories := make([]string, 0, len(report.categories))
	for name := range report.categories {
		categories = append(categories, name)
	}
	sort.Slice(categories, func(i, j int) bool {
		return report.categories[categories[i]].lines > report.categories[categories[j]].lines
	})

	console.Header(fmt.Sprintf("LOC summary (%d files, %d lines)", report.total.files, report.total.lines))
	if !opts.IncludeThirdParty {
		console.Muted("(ThirdParty excluded — pass --thirdparty to include)")
	}
	fmt.Println()

	for _, category := range categories {
		c := report.categories[category]
		pct := percent(c.lines, report.total.lines)
		fmt.Printf("%-32s %s files  %s lines  %s\n",
			console.Bold(category),
			formatInt(c.files, 5),
			formatInt(c.lines, 8),
			console.Dim(fmt.Sprintf("%5.1f%%", pct)))

		subs := report.subKeys[category]
		sort.Slice(subs, func(i, j int) bool {
			return report.subTotals[category][subs[i]].lines > report.subTotals[category][subs[j]].lines
		})

		for _, sub := range subs {
			s := report.subTotals[category][sub]
			fmt.Printf("  %-30s %s files  %s lines\n",
				sub,
				formatInt(s.files, 5),
				formatInt(s.lines, 8))

			leaves := report.leafKeys[category][sub]
			if len(leaves) == 0 {
				continue
			}
			sort.Slice(leaves, func(i, j int) bool {
				li := report.leaves[leafKey{category: category, sub: sub, leaf: leaves[i]}]
				lj := report.leaves[leafKey{category: category, sub: sub, leaf: leaves[j]}]
				return li.lines > lj.lines
			})
			for _, leaf := range leaves {
				l := report.leaves[leafKey{category: category, sub: sub, leaf: leaf}]
				fmt.Printf("    %-28s %s files  %s lines\n",
					leaf,
					formatInt(l.files, 5),
					formatInt(l.lines, 8))
			}
		}
		fmt.Println()
	}

	fmt.Printf("%-32s %s files  %s lines\n",
		console.Bold("TOTAL"),
		formatInt(report.total.files, 5),
		formatInt(report.total.lines, 8))
}

func percent(part, whole int) float64 {
	if whole == 0 {
		return 0
	}
	return float64(part) * 100.0 / float64(whole)
}

func formatInt(n int, width int) string {
	return fmt.Sprintf("%*d", width, n)
}

// countLines returns the number of "code lines" in the file: total physical lines
// minus blank lines and minus lines that contain only C/C++ comments. A line that
// mixes code with a trailing // or /* */ comment still counts as code.
//
// The block-comment state is tracked across lines with a simple state machine.
// String/char literals are NOT parsed, so a comment-looking sequence inside a
// string literal (e.g. const char* s = "//";) would be misclassified. This is
// the standard trade-off most lightweight LOC tools make and is fine for stats.
func countLines(path string) (int, error) {
	f, err := os.Open(path)
	if err != nil {
		return 0, err
	}
	defer f.Close()
	scanner := bufio.NewScanner(f)
	scanner.Buffer(make([]byte, 64*1024), 4*1024*1024)
	count := 0
	inBlock := false
	for scanner.Scan() {
		line := scanner.Text()
		hasCode, blockAfter := stripCommentsAndCheckCode(line, inBlock)
		inBlock = blockAfter
		if hasCode {
			count++
		}
	}
	if err := scanner.Err(); err != nil {
		return 0, err
	}
	return count, nil
}

// stripCommentsAndCheckCode scans a single line, honoring the inBlockComment
// state coming in, and reports (hasCode, inBlockAfter). hasCode is true if any
// non-whitespace, non-comment character is present on this line.
func stripCommentsAndCheckCode(line string, inBlock bool) (bool, bool) {
	hasCode := false
	i := 0
	n := len(line)
	for i < n {
		if inBlock {
			// Look for closing */.
			end := strings.Index(line[i:], "*/")
			if end < 0 {
				return hasCode, true
			}
			i += end + 2
			inBlock = false
			continue
		}
		c := line[i]
		// Start of // line comment — ignore the rest of the line.
		if c == '/' && i+1 < n && line[i+1] == '/' {
			break
		}
		// Start of /* block comment.
		if c == '/' && i+1 < n && line[i+1] == '*' {
			i += 2
			inBlock = true
			continue
		}
		if c != ' ' && c != '\t' && c != '\r' {
			hasCode = true
		}
		i++
	}
	return hasCode, inBlock
}

func containsString(slice []string, value string) bool {
	for _, s := range slice {
		if s == value {
			return true
		}
	}
	return false
}
