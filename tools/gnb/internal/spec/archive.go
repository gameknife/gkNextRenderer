package spec

import (
	"os"
	"regexp"
	"strings"
	"time"
)

// ArchiveOptions controls which tasks in the "最近完成" section get moved to
// ARCHIVE.md. At most one of OlderThanDays or Keep should be > 0; if both are
// zero, every task in "最近完成" is archived.
type ArchiveOptions struct {
	OlderThanDays int    // archive entries whose Paren parses as a date older than today-N
	Keep          int    // archive everything except the most recent N entries
	Bucket        string // override monthly bucket "YYYY-MM"; defaults to time.Now()
}

// ArchiveResult is what the command prints.
type ArchiveResult struct {
	Moved    []Task
	Bucket   string
	Archive  string // path to ARCHIVE.md
	TODOPath string
}

// Archive runs the archive operation. Returns ArchiveResult with len(Moved)==0
// when nothing matches; that's not an error.
func Archive(repoRoot string, opts ArchiveOptions) (ArchiveResult, error) {
	res := ArchiveResult{
		Archive:  ArchivePath(repoRoot),
		TODOPath: TODOPath(repoRoot),
	}
	doc, err := Parse(res.TODOPath)
	if err != nil {
		return res, err
	}
	recents := doc.SectionTasks(SectionRecent)
	if len(recents) == 0 {
		return res, nil
	}

	var move, keep []Task
	switch {
	case opts.Keep > 0:
		if len(recents) <= opts.Keep {
			return res, nil
		}
		move = recents[:len(recents)-opts.Keep]
		keep = recents[len(recents)-opts.Keep:]
	case opts.OlderThanDays > 0:
		cutoff := time.Now().AddDate(0, 0, -opts.OlderThanDays)
		for _, t := range recents {
			if d, ok := parseDateLoose(t.Paren); ok && d.Before(cutoff) {
				move = append(move, t)
			} else {
				keep = append(keep, t)
			}
		}
	default:
		move = recents
	}
	if len(move) == 0 {
		return res, nil
	}

	bucket := opts.Bucket
	if bucket == "" {
		bucket = time.Now().Format("2006-01")
	}
	res.Moved = move
	res.Bucket = bucket

	if err := appendToArchive(res.Archive, bucket, move); err != nil {
		return res, err
	}

	lineNums := make([]int, 0, len(move))
	for _, t := range move {
		lineNums = append(lineNums, t.LineNum)
	}
	doc.RemoveLines(lineNums)
	// If "最近完成" is empty after removal, restore the "(暂无)" placeholder.
	if len(doc.SectionTasks(SectionRecent)) == 0 {
		insertPlaceholderIfEmpty(doc, SectionRecent)
	}
	if err := doc.Save(); err != nil {
		return res, err
	}
	return res, nil
}

func parseDateLoose(s string) (time.Time, bool) {
	s = strings.TrimSpace(s)
	for _, layout := range []string{"2006-01-02", "2006/01/02", time.RFC3339} {
		if t, err := time.Parse(layout, s); err == nil {
			return t, true
		}
	}
	return time.Time{}, false
}

var bucketHeadingRE = regexp.MustCompile(`^##\s+(\d{4}-\d{2})\s*$`)

func appendToArchive(path string, bucket string, tasks []Task) error {
	data, err := os.ReadFile(path)
	if err != nil && !os.IsNotExist(err) {
		return err
	}
	text := strings.ReplaceAll(string(data), "\r\n", "\n")
	if text == "" {
		text = "# Archive\n"
	}
	lines := strings.Split(strings.TrimRight(text, "\n"), "\n")

	bucketIdx := -1
	for i, line := range lines {
		if m := bucketHeadingRE.FindStringSubmatch(line); m != nil && m[1] == bucket {
			bucketIdx = i
			break
		}
	}

	formatted := make([]string, 0, len(tasks))
	for _, t := range tasks {
		formatted = append(formatted, FormatLine(t))
	}

	if bucketIdx == -1 {
		// Insert a new "## YYYY-MM" section right after the "# Archive" title (or at top if not found).
		titleIdx := -1
		for i, l := range lines {
			if strings.HasPrefix(l, "# ") {
				titleIdx = i
				break
			}
		}
		insertAt := titleIdx + 1
		newBlock := []string{"", "## " + bucket, ""}
		newBlock = append(newBlock, formatted...)
		out := make([]string, 0, len(lines)+len(newBlock))
		out = append(out, lines[:insertAt]...)
		out = append(out, newBlock...)
		out = append(out, lines[insertAt:]...)
		lines = out
	} else {
		// Append after the existing bucket's last task line (or right after heading if empty).
		end := bucketIdx + 1
		for end < len(lines) {
			line := lines[end]
			if strings.HasPrefix(line, "## ") || strings.HasPrefix(line, "# ") {
				break
			}
			end++
		}
		// Trim trailing blank lines within the bucket.
		for end > bucketIdx+1 && strings.TrimSpace(lines[end-1]) == "" {
			end--
		}
		out := make([]string, 0, len(lines)+len(formatted))
		out = append(out, lines[:end]...)
		out = append(out, formatted...)
		out = append(out, lines[end:]...)
		lines = out
	}

	content := strings.Join(lines, "\n") + "\n"
	return os.WriteFile(path, []byte(content), 0644)
}

// insertPlaceholderIfEmpty puts a "(暂无)" line inside an empty section so the
// document reads nicely. The section heading must already exist.
func insertPlaceholderIfEmpty(d *Document, section SectionKind) {
	rng, ok := d.sectionRanges[section]
	if !ok {
		return
	}
	hasContent := false
	for i := rng[0]; i < rng[1]; i++ {
		if strings.TrimSpace(d.Lines[i]) != "" {
			hasContent = true
			break
		}
	}
	if hasContent {
		return
	}
	// Replace the entire range with: blank, "(暂无)", blank
	prefix := append([]string{}, d.Lines[:rng[0]]...)
	suffix := append([]string{}, d.Lines[rng[1]:]...)
	body := []string{"", "(暂无)", ""}
	d.Lines = append(prefix, append(body, suffix...)...)
	d.sectionRanges[section] = [2]int{rng[0], rng[0] + len(body)}
	delta := len(body) - (rng[1] - rng[0])
	for k, r := range d.sectionRanges {
		if k == section {
			continue
		}
		if r[0] >= rng[1] {
			d.sectionRanges[k] = [2]int{r[0] + delta, r[1] + delta}
		}
	}
}
