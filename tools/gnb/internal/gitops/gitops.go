// Package gitops wraps the local `git` CLI with the small set of operations
// gnb exposes from both the CLI and the dashboard (status, branch listing,
// checkout, pull, log). Every function is a thin shell-out so behavior matches
// what the user would see from a terminal.
package gitops

import (
	"bytes"
	"errors"
	"fmt"
	"os/exec"
	"strings"
	"time"
)

// ErrDirty is returned by guarded write operations when the working tree has
// uncommitted changes that would be lost.
var ErrDirty = errors.New("working tree is dirty (uncommitted changes)")

// Status summarizes the working tree.
type Status struct {
	Branch     string // current branch name; empty when in detached HEAD
	Head       string // short HEAD sha
	Dirty      bool
	Ahead      int // commits ahead of upstream
	Behind     int // commits behind upstream
	Upstream   string
	DirtyHint  string      // first dirty path (informational)
	DirtyFiles []DirtyFile // all dirty entries (modified/added/deleted/renamed/untracked)
}

// DirtyFile is one entry from `git status --porcelain=v2`.
// Code is the two-char XY status (e.g. ".M" unstaged-modified, "M." staged-modified,
// "??" untracked). For renames/copies, Path is the destination path.
type DirtyFile struct {
	Code string
	Path string
}

// Branch describes one local branch entry.
type Branch struct {
	Name    string
	Current bool
	Remote  string // upstream tracking branch ("" if none)
	Subject string // last commit subject
}

// Commit is one log entry.
type Commit struct {
	Hash    string // short hash
	Full    string // full hash
	Subject string
	Author  string
	Date    string // ISO-like date
	Body    string // full body (filled on demand by ShowCommit)
}

// Status runs `git status --porcelain=v2 --branch`.
func GetStatus(repoRoot string) (Status, error) {
	out, err := run(repoRoot, "status", "--porcelain=v2", "--branch")
	if err != nil {
		return Status{}, err
	}
	st := Status{}
	for _, line := range strings.Split(out, "\n") {
		switch {
		case strings.HasPrefix(line, "# branch.head "):
			st.Branch = strings.TrimPrefix(line, "# branch.head ")
			if st.Branch == "(detached)" {
				st.Branch = ""
			}
		case strings.HasPrefix(line, "# branch.oid "):
			h := strings.TrimPrefix(line, "# branch.oid ")
			if len(h) > 8 {
				h = h[:8]
			}
			st.Head = h
		case strings.HasPrefix(line, "# branch.upstream "):
			st.Upstream = strings.TrimPrefix(line, "# branch.upstream ")
		case strings.HasPrefix(line, "# branch.ab "):
			rest := strings.TrimPrefix(line, "# branch.ab ")
			fields := strings.Fields(rest)
			for _, f := range fields {
				if strings.HasPrefix(f, "+") {
					fmt.Sscanf(f, "+%d", &st.Ahead)
				} else if strings.HasPrefix(f, "-") {
					fmt.Sscanf(f, "-%d", &st.Behind)
				}
			}
		case line == "":
			// skip
		default:
			// Any non-`#` line means at least one dirty entry.
			if df, ok := parseDirtyEntry(line); ok {
				st.Dirty = true
				st.DirtyFiles = append(st.DirtyFiles, df)
				if st.DirtyHint == "" {
					st.DirtyHint = df.Path
				}
			}
		}
	}
	return st, nil
}

// parseDirtyEntry decodes one porcelain v2 entry line. Returns false for
// ignored (`!`) entries and malformed lines.
func parseDirtyEntry(line string) (DirtyFile, bool) {
	if line == "" {
		return DirtyFile{}, false
	}
	switch line[0] {
	case '1':
		// "1 XY sub mH mI mW hH hI <path>"
		parts := strings.SplitN(line, " ", 9)
		if len(parts) < 9 {
			return DirtyFile{}, false
		}
		return DirtyFile{Code: parts[1], Path: parts[8]}, true
	case '2':
		// "2 XY sub mH mI mW hH hI X<score> <path>\t<origPath>"
		parts := strings.SplitN(line, " ", 10)
		if len(parts) < 10 {
			return DirtyFile{}, false
		}
		path := parts[9]
		if tab := strings.IndexByte(path, '\t'); tab >= 0 {
			path = path[:tab]
		}
		return DirtyFile{Code: parts[1], Path: path}, true
	case 'u':
		// "u XY sub m1 m2 m3 mW h1 h2 h3 <path>"
		parts := strings.SplitN(line, " ", 11)
		if len(parts) < 11 {
			return DirtyFile{}, false
		}
		return DirtyFile{Code: parts[1], Path: parts[10]}, true
	case '?':
		if len(line) < 3 {
			return DirtyFile{}, false
		}
		return DirtyFile{Code: "??", Path: line[2:]}, true
	case '!':
		return DirtyFile{}, false
	}
	return DirtyFile{}, false
}

// CurrentBranch returns the short branch name (empty on detached HEAD).
func CurrentBranch(repoRoot string) (string, error) {
	out, err := run(repoRoot, "rev-parse", "--abbrev-ref", "HEAD")
	if err != nil {
		return "", err
	}
	out = strings.TrimSpace(out)
	if out == "HEAD" {
		return "", nil
	}
	return out, nil
}

// RemoteBranch describes one remote-tracking ref (e.g. origin/feat-a).
type RemoteBranch struct {
	FullName  string // e.g. origin/feat-a
	ShortName string // local name to create when tracking (feat-a)
	Subject   string
	HasLocal  bool // a local branch with the same short name already exists
}

// Stash is one entry from `git stash list`.
type Stash struct {
	Ref     string // e.g. stash@{0}
	Index   int
	Message string
}

// Branches lists local branches with upstream + last subject.
func Branches(repoRoot string) ([]Branch, error) {
	out, err := run(repoRoot, "for-each-ref",
		"--format=%(HEAD)%00%(refname:short)%00%(upstream:short)%00%(contents:subject)",
		"refs/heads")
	if err != nil {
		return nil, err
	}
	var branches []Branch
	for _, line := range strings.Split(out, "\n") {
		line = strings.TrimRight(line, "\r")
		if line == "" {
			continue
		}
		parts := strings.SplitN(line, "\x00", 4)
		if len(parts) < 4 {
			continue
		}
		branches = append(branches, Branch{
			Current: strings.TrimSpace(parts[0]) == "*",
			Name:    parts[1],
			Remote:  parts[2],
			Subject: parts[3],
		})
	}
	return branches, nil
}

// RemoteBranches lists `refs/remotes/<remote>/<name>` entries, skipping the
// special "origin/HEAD" alias. HasLocal flags entries whose short name
// already exists locally, so the UI can suppress duplicate switch buttons.
func RemoteBranches(repoRoot string) ([]RemoteBranch, error) {
	out, err := run(repoRoot, "for-each-ref",
		"--format=%(refname:short)%00%(contents:subject)",
		"refs/remotes",
	)
	if err != nil {
		return nil, err
	}
	localNames := map[string]bool{}
	if locals, err := Branches(repoRoot); err == nil {
		for _, b := range locals {
			localNames[b.Name] = true
		}
	}
	var out2 []RemoteBranch
	for _, line := range strings.Split(out, "\n") {
		line = strings.TrimRight(line, "\r")
		if line == "" {
			continue
		}
		parts := strings.SplitN(line, "\x00", 2)
		if len(parts) < 2 {
			continue
		}
		full := parts[0]
		if strings.HasSuffix(full, "/HEAD") {
			continue
		}
		// short name = drop the leading "<remote>/". When git collapses a
		// symbolic ref (refs/remotes/origin/HEAD → "origin"), there is no
		// slash — skip those, they're not real branches.
		idx := strings.IndexByte(full, '/')
		if idx < 0 {
			continue
		}
		short := full[idx+1:]
		out2 = append(out2, RemoteBranch{
			FullName:  full,
			ShortName: short,
			Subject:   parts[1],
			HasLocal:  localNames[short],
		})
	}
	return out2, nil
}

// CreateBranch creates and switches to a new branch from startPoint (defaults
// to current HEAD when empty). Refuses on dirty unless force is true.
func CreateBranch(repoRoot, name, startPoint string, force bool) error {
	if name == "" {
		return fmt.Errorf("branch name required")
	}
	if !force {
		st, err := GetStatus(repoRoot)
		if err != nil {
			return err
		}
		if st.Dirty {
			return ErrDirty
		}
	}
	args := []string{"checkout", "-b", name}
	if startPoint != "" {
		args = append(args, startPoint)
	}
	_, err := run(repoRoot, args...)
	return err
}

// CheckoutRemote creates a local tracking branch from a remote ref
// (e.g. "origin/feat-a"). The local branch takes the short name ("feat-a").
// Refuses on dirty unless force is true.
func CheckoutRemote(repoRoot, remoteRef string, force bool) error {
	if remoteRef == "" {
		return fmt.Errorf("remote ref required")
	}
	if !force {
		st, err := GetStatus(repoRoot)
		if err != nil {
			return err
		}
		if st.Dirty {
			return ErrDirty
		}
	}
	// `git switch --track <remote>/<branch>` infers the local name; fall back
	// to checkout if the user's git predates `switch` (rare on modern setups).
	if _, err := run(repoRoot, "switch", "--track", remoteRef); err == nil {
		return nil
	}
	idx := strings.IndexByte(remoteRef, '/')
	short := remoteRef
	if idx >= 0 {
		short = remoteRef[idx+1:]
	}
	_, err := run(repoRoot, "checkout", "-b", short, "--track", remoteRef)
	return err
}

// ResetHard runs `git reset --hard <ref>`. This is unconditionally destructive
// — the caller is responsible for user confirmation. Does NOT check dirty
// because the whole point of reset is to discard those changes.
func ResetHard(repoRoot, ref string) error {
	if ref == "" {
		return fmt.Errorf("ref required")
	}
	_, err := run(repoRoot, "reset", "--hard", ref)
	return err
}

// StashPush wraps `git stash push -m <msg>`. When includeUntracked is true,
// adds `-u` so new files are stashed too.
func StashPush(repoRoot, message string, includeUntracked bool) (string, error) {
	args := []string{"stash", "push"}
	if includeUntracked {
		args = append(args, "-u")
	}
	if message != "" {
		args = append(args, "-m", message)
	}
	return run(repoRoot, args...)
}

// StashList returns all stash entries newest first.
func StashList(repoRoot string) ([]Stash, error) {
	out, err := run(repoRoot, "stash", "list", "--format=%gd%x00%gs")
	if err != nil {
		return nil, err
	}
	var stashes []Stash
	for i, line := range strings.Split(out, "\n") {
		line = strings.TrimRight(line, "\r")
		if line == "" {
			continue
		}
		parts := strings.SplitN(line, "\x00", 2)
		if len(parts) < 2 {
			continue
		}
		stashes = append(stashes, Stash{
			Ref:     parts[0],
			Index:   i,
			Message: parts[1],
		})
	}
	return stashes, nil
}

// StashAction executes one of "pop"/"apply"/"drop" on the given ref.
// ref is a stash reference such as "stash@{0}"; when empty, git defaults to the
// most recent stash.
func StashAction(repoRoot, action, ref string) (string, error) {
	switch action {
	case "pop", "apply", "drop":
	default:
		return "", fmt.Errorf("unsupported stash action %q", action)
	}
	args := []string{"stash", action}
	if ref != "" {
		args = append(args, ref)
	}
	return run(repoRoot, args...)
}

// Checkout switches to branch. Errors with ErrDirty if the working tree is
// dirty and force is false.
func Checkout(repoRoot, branch string, force bool) error {
	if !force {
		st, err := GetStatus(repoRoot)
		if err != nil {
			return err
		}
		if st.Dirty {
			return ErrDirty
		}
	}
	_, err := run(repoRoot, "checkout", branch)
	return err
}

// Pull runs `git pull --ff-only` (refuses to merge unrelated work). Errors
// with ErrDirty if there are uncommitted changes.
func Pull(repoRoot string) (string, error) {
	st, err := GetStatus(repoRoot)
	if err != nil {
		return "", err
	}
	if st.Dirty {
		return "", ErrDirty
	}
	return run(repoRoot, "pull", "--ff-only")
}

// Fetch runs `git fetch --all --prune`.
func Fetch(repoRoot string) (string, error) {
	return run(repoRoot, "fetch", "--all", "--prune")
}

// Log returns the most recent n commits as oneline entries.
func Log(repoRoot string, n int) ([]Commit, error) {
	if n <= 0 {
		n = 30
	}
	out, err := run(repoRoot, "log",
		fmt.Sprintf("-n%d", n),
		"--pretty=format:%h%x00%H%x00%an%x00%ad%x00%s",
		"--date=short",
	)
	if err != nil {
		return nil, err
	}
	return parseLog(out), nil
}

// DailyCommitCounts returns repository-wide commit counts grouped by local
// calendar date. All refs are included so activity on non-current branches is
// represented without counting the same commit more than once.
func DailyCommitCounts(repoRoot string, since time.Time) (map[string]int, error) {
	out, err := run(repoRoot, "log",
		"--all",
		"--since="+since.Format("2006-01-02"),
		"--date=format-local:%Y-%m-%d",
		"--pretty=format:%ad",
	)
	if err != nil {
		return nil, err
	}
	counts := make(map[string]int)
	for _, line := range strings.Split(out, "\n") {
		date := strings.TrimSpace(line)
		if date != "" {
			counts[date]++
		}
	}
	return counts, nil
}

// LogRange returns commits in a revision range such as "HEAD..origin/main".
func LogRange(repoRoot, revRange string, n int) ([]Commit, error) {
	if revRange == "" {
		return nil, nil
	}
	if n <= 0 {
		n = 30
	}
	out, err := run(repoRoot, "log",
		fmt.Sprintf("-n%d", n),
		"--pretty=format:%h%x00%H%x00%an%x00%ad%x00%s",
		"--date=short",
		revRange,
	)
	if err != nil {
		return nil, err
	}
	return parseLog(out), nil
}

func parseLog(out string) []Commit {
	var commits []Commit
	for _, line := range strings.Split(out, "\n") {
		line = strings.TrimRight(line, "\r")
		if line == "" {
			continue
		}
		parts := strings.SplitN(line, "\x00", 5)
		if len(parts) < 5 {
			continue
		}
		commits = append(commits, Commit{
			Hash:    parts[0],
			Full:    parts[1],
			Author:  parts[2],
			Date:    parts[3],
			Subject: parts[4],
		})
	}
	return commits
}

// ShowCommit returns the full commit message for a single ref.
func ShowCommit(repoRoot, ref string) (Commit, error) {
	out, err := run(repoRoot, "show", "--no-patch",
		"--pretty=format:%h%x00%H%x00%an%x00%ad%x00%s%x00%B",
		"--date=iso", ref,
	)
	if err != nil {
		return Commit{}, err
	}
	out = strings.TrimRight(out, "\n")
	parts := strings.SplitN(out, "\x00", 6)
	if len(parts) < 6 {
		return Commit{}, fmt.Errorf("unexpected git show output: %q", out)
	}
	return Commit{
		Hash:    parts[0],
		Full:    parts[1],
		Author:  parts[2],
		Date:    parts[3],
		Subject: parts[4],
		Body:    strings.TrimSpace(parts[5]),
	}, nil
}

// DiffStaged returns the unified diff of staged changes (vs HEAD).
func DiffStaged(repoRoot string) (string, error) {
	return run(repoRoot, "diff", "--staged", "--no-color")
}

// DiffWorkingTree returns the unified diff of all unstaged tracked changes.
// Note: this does NOT include untracked files; callers needing full coverage
// must also list untracked files separately.
func DiffWorkingTree(repoRoot string) (string, error) {
	return run(repoRoot, "diff", "--no-color")
}

// DiffStagedStat returns the human-readable `git diff --staged --stat` summary.
func DiffStagedStat(repoRoot string) (string, error) {
	return run(repoRoot, "diff", "--staged", "--stat", "--no-color")
}

// DiffWorkingTreeStat returns the human-readable `git diff --stat` summary
// for unstaged tracked changes.
func DiffWorkingTreeStat(repoRoot string) (string, error) {
	return run(repoRoot, "diff", "--stat", "--no-color")
}

// FileChange is a single entry from `git diff --name-status`.
type FileChange struct {
	Code string // M, A, D, R, C, etc.
	Path string
}

// StagedNameStatus returns `git diff --staged --name-status` entries.
func StagedNameStatus(repoRoot string) ([]FileChange, error) {
	out, err := run(repoRoot, "diff", "--staged", "--name-status")
	if err != nil {
		return nil, err
	}
	return parseNameStatus(out), nil
}

// WorkingTreeNameStatus returns `git diff --name-status` entries (tracked
// modifications only — untracked files come from UntrackedFiles).
func WorkingTreeNameStatus(repoRoot string) ([]FileChange, error) {
	out, err := run(repoRoot, "diff", "--name-status")
	if err != nil {
		return nil, err
	}
	return parseNameStatus(out), nil
}

func parseNameStatus(out string) []FileChange {
	out = strings.TrimSpace(out)
	if out == "" {
		return nil
	}
	var changes []FileChange
	for _, line := range strings.Split(out, "\n") {
		fields := strings.Fields(line)
		if len(fields) < 2 {
			continue
		}
		// Rename / copy lines look like: R100 old new -- take the last path so
		// the LLM sees the destination filename it would naturally describe.
		changes = append(changes, FileChange{Code: fields[0], Path: fields[len(fields)-1]})
	}
	return changes
}

// UntrackedFiles returns the list of untracked file paths (NUL-safe split
// using the porcelain z-format is overkill here; -z would complicate the
// parser and untracked filenames with newlines are vanishingly rare).
func UntrackedFiles(repoRoot string) ([]string, error) {
	out, err := run(repoRoot, "ls-files", "--others", "--exclude-standard")
	if err != nil {
		return nil, err
	}
	out = strings.TrimSpace(out)
	if out == "" {
		return nil, nil
	}
	return strings.Split(out, "\n"), nil
}

// CreateCommit creates a commit with the given message.
func CreateCommit(repoRoot, message string) (string, error) {
	return run(repoRoot, "commit", "-m", message)
}

// AddAll stages all working tree changes (tracked + untracked).
func AddAll(repoRoot string) error {
	_, err := run(repoRoot, "add", "-A")
	return err
}

// AddPath stages one working tree path.
func AddPath(repoRoot, path string) error {
	if path == "" {
		return fmt.Errorf("path required")
	}
	_, err := run(repoRoot, "add", "--", path)
	return err
}

// UnstagePath removes one path from the index while keeping the working tree
// content intact.
func UnstagePath(repoRoot, path string) error {
	if path == "" {
		return fmt.Errorf("path required")
	}
	if _, err := run(repoRoot, "restore", "--staged", "--", path); err == nil {
		return nil
	}
	_, err := run(repoRoot, "reset", "HEAD", "--", path)
	return err
}

// UnstageAll removes all staged entries from the index without touching the
// working tree.
func UnstageAll(repoRoot string) error {
	if _, err := run(repoRoot, "restore", "--staged", ":/"); err == nil {
		return nil
	}
	_, err := run(repoRoot, "reset", "HEAD")
	return err
}

func run(dir string, args ...string) (string, error) {
	cmd := exec.Command("git", args...)
	cmd.Dir = dir
	var stdout, stderr bytes.Buffer
	cmd.Stdout = &stdout
	cmd.Stderr = &stderr
	if err := cmd.Run(); err != nil {
		msg := strings.TrimSpace(stderr.String())
		if msg == "" {
			msg = err.Error()
		}
		return "", fmt.Errorf("git %s: %s", strings.Join(args, " "), msg)
	}
	return strings.TrimRight(stdout.String(), "\n"), nil
}
