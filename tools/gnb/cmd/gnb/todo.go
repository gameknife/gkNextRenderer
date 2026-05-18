package main

import (
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"os"
	"regexp"
	"strconv"
	"strings"
	"time"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/console"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/spec"
	"github.com/spf13/cobra"
)

func newTodoCommand(ctx appContext) *cobra.Command {
	cmd := &cobra.Command{
		Use:   "todo",
		Short: "Manage .spec/ interactive workflow tasks",
		Long: "Manage .spec/TODO.md, the interactive workflow task list.\n\n" +
			"Format: see .spec/README.md\n" +
			"With no subcommand, prints pending tasks in \"下一步\" and a subcommand hint.",
		RunE: func(cmd *cobra.Command, args []string) error {
			if err := runTodoList(ctx, todoListOptions{section: spec.SectionNext, onlyPending: true}); err != nil {
				return err
			}
			fmt.Println()
			fmt.Println("Subcommands: list | show | add | move | swap | done | block | delete | archive | next")
			fmt.Println("Run `gnb todo <cmd> --help` for usage.")
			return nil
		},
	}
	cmd.AddCommand(newTodoListCommand(ctx))
	cmd.AddCommand(newTodoShowCommand(ctx))
	cmd.AddCommand(newTodoNextCommand(ctx))
	cmd.AddCommand(newTodoAddCommand(ctx))
	cmd.AddCommand(newTodoMoveCommand(ctx))
	cmd.AddCommand(newTodoSwapCommand(ctx))
	cmd.AddCommand(newTodoDoneCommand(ctx))
	cmd.AddCommand(newTodoBlockCommand(ctx))
	cmd.AddCommand(newTodoDeleteCommand(ctx))
	cmd.AddCommand(newTodoArchiveCommand(ctx))
	return cmd
}

// validTypes is the set of recommended type tags. `add` warns (but does not
// reject) tags outside this set so users can experiment.
var validTypes = map[string]bool{
	"BUG":      true,
	"FEAT":     true,
	"IDEA":     true,
	"SPIKE":    true,
	"REFACTOR": true,
	"DOC":      true,
}

// ----- list -------------------------------------------------------------

type todoListOptions struct {
	section     spec.SectionKind // SectionUnknown == all
	onlyPending bool
	onlyDone    bool
	onlyBlocked bool
	typeFilter  string
}

func newTodoListCommand(ctx appContext) *cobra.Command {
	var opts todoListOptions
	var sectionFlag string
	cmd := &cobra.Command{
		Use:   "list",
		Short: "List tasks in .spec/TODO.md",
		RunE: func(cmd *cobra.Command, args []string) error {
			switch strings.ToLower(sectionFlag) {
			case "", "all":
				opts.section = spec.SectionUnknown
			case "next", "下一步":
				opts.section = spec.SectionNext
			case "backlog", "待规划":
				opts.section = spec.SectionBacklog
			case "recent", "done", "最近完成":
				opts.section = spec.SectionRecent
			default:
				return fmt.Errorf("unknown section %q (expected next|backlog|recent|all)", sectionFlag)
			}
			return runTodoList(ctx, opts)
		},
	}
	cmd.Flags().StringVar(&sectionFlag, "section", "all", "filter by section: next | backlog | recent | all")
	cmd.Flags().BoolVar(&opts.onlyPending, "pending", false, "only [ ] tasks")
	cmd.Flags().BoolVar(&opts.onlyDone, "done", false, "only [x] tasks")
	cmd.Flags().BoolVar(&opts.onlyBlocked, "blocked", false, "only [!] tasks")
	cmd.Flags().StringVarP(&opts.typeFilter, "type", "t", "", "filter by type tag (BUG, FEAT, ...)")
	return cmd
}

func runTodoList(ctx appContext, opts todoListOptions) error {
	doc, err := spec.Parse(spec.TODOPath(ctx.repoRoot))
	if err != nil {
		return err
	}
	console.Label("milestone", fmt.Sprintf("%s (%s)", doc.Milestone, doc.MilestoneStatus))
	sections := []spec.SectionKind{spec.SectionNext, spec.SectionBacklog, spec.SectionRecent}
	totalShown := 0
	for _, sec := range sections {
		if opts.section != spec.SectionUnknown && opts.section != sec {
			continue
		}
		tasks := filterTasks(doc.SectionTasks(sec), opts)
		if len(tasks) == 0 {
			continue
		}
		console.Header(sec.Heading())
		for _, t := range tasks {
			fmt.Printf("  %s %s  %s%s  %s%s%s\n",
				statusIcon(t.Status), t.FormattedID(),
				bracketIfSet(t.Priority), bracketIfSet(t.Type),
				t.Title,
				dimIf(t.Arrow != "", "  → "+t.Arrow),
				dimIf(t.Paren != "", "  ("+t.Paren+")"),
			)
		}
		totalShown += len(tasks)
		fmt.Println()
	}
	if totalShown == 0 {
		console.Muted("(no tasks match)")
	}
	// Friendly reminder if recent section is bloated.
	if len(doc.SectionTasks(spec.SectionRecent)) > 10 {
		console.Warn("最近完成 已有 %d 条，建议运行 `gnb todo archive` 归档", len(doc.SectionTasks(spec.SectionRecent)))
	}
	return nil
}

func filterTasks(tasks []spec.Task, opts todoListOptions) []spec.Task {
	out := tasks[:0:0]
	for _, t := range tasks {
		if opts.onlyPending && t.Status != spec.StatusPending {
			continue
		}
		if opts.onlyDone && t.Status != spec.StatusDone {
			continue
		}
		if opts.onlyBlocked && t.Status != spec.StatusBlocked {
			continue
		}
		if opts.typeFilter != "" && !strings.EqualFold(t.Type, opts.typeFilter) {
			continue
		}
		out = append(out, t)
	}
	return out
}

func statusIcon(s spec.Status) string {
	switch s {
	case spec.StatusPending:
		return "[ ]"
	case spec.StatusDoing:
		return "[/]"
	case spec.StatusDone:
		return "[x]"
	case spec.StatusBlocked:
		return "[!]"
	}
	return "[?]"
}

func bracketIfSet(s string) string {
	if s == "" {
		return ""
	}
	return "[" + s + "]"
}

func dimIf(cond bool, s string) string {
	if !cond {
		return ""
	}
	return s
}

// ----- show -------------------------------------------------------------

func newTodoShowCommand(ctx appContext) *cobra.Command {
	return &cobra.Command{
		Use:   "show <id>",
		Short: "Show a task plus its spec/journal/blocker if present",
		Args:  cobra.ExactArgs(1),
		RunE: func(cmd *cobra.Command, args []string) error {
			id, err := parseID(args[0])
			if err != nil {
				return err
			}
			doc, err := spec.Parse(spec.TODOPath(ctx.repoRoot))
			if err != nil {
				return err
			}
			t, _, ok := doc.FindTask(id)
			if !ok {
				return fmt.Errorf("task #%05d not found", id)
			}
			console.Header(fmt.Sprintf("%s %s  %s%s  %s",
				statusIcon(t.Status), t.FormattedID(),
				bracketIfSet(t.Priority), bracketIfSet(t.Type), t.Title))
			console.Label("section", sectionName(t.Section))
			if t.Arrow != "" {
				console.Label("link", t.Arrow)
			}
			if t.Paren != "" {
				console.Label("note", t.Paren)
			}
			if body, ok := spec.ReadIfExists(spec.SpecPath(ctx.repoRoot, id)); ok {
				console.Header("\n── spec ──")
				fmt.Println(body)
			}
			if body, ok := spec.ReadIfExists(spec.JournalPath(ctx.repoRoot, id)); ok {
				console.Header("\n── journal ──")
				fmt.Println(body)
			}
			if body, ok := spec.ReadIfExists(spec.BlockerPath(ctx.repoRoot, id)); ok {
				console.Header("\n── blocker ──")
				fmt.Println(body)
			}
			return nil
		},
	}
}

func sectionName(s spec.SectionKind) string {
	switch s {
	case spec.SectionNext:
		return "下一步"
	case spec.SectionBacklog:
		return "待规划"
	case spec.SectionRecent:
		return "最近完成"
	}
	return "?"
}

// ----- next -------------------------------------------------------------

type nextOutput struct {
	Found       bool   `json:"found"`
	ID          int    `json:"id,omitempty"`
	IDStr       string `json:"id_str,omitempty"`
	Status      string `json:"status,omitempty"`
	Priority    string `json:"priority,omitempty"`
	Type        string `json:"type,omitempty"`
	Title       string `json:"title,omitempty"`
	Section     string `json:"section,omitempty"`
	SpecPath    string `json:"spec_path,omitempty"`
	SpecExists  bool   `json:"spec_exists,omitempty"`
	JournalPath string `json:"journal_path,omitempty"`
	BlockerPath string `json:"blocker_path,omitempty"`
}

func newTodoNextCommand(ctx appContext) *cobra.Command {
	var asJSON bool
	cmd := &cobra.Command{
		Use:   "next",
		Short: "Print the next pending task in 下一步 (intended for AGENT/orchestrator use)",
		RunE: func(cmd *cobra.Command, args []string) error {
			doc, err := spec.Parse(spec.TODOPath(ctx.repoRoot))
			if err != nil {
				return err
			}
			var found *spec.Task
			for i := range doc.Tasks {
				if doc.Tasks[i].Section == spec.SectionNext && doc.Tasks[i].Status == spec.StatusPending {
					found = &doc.Tasks[i]
					break
				}
			}
			if found == nil {
				if asJSON {
					return writeJSON(nextOutput{Found: false})
				}
				console.Muted("(no pending task in 下一步)")
				return nil
			}
			specPath := spec.SpecPath(ctx.repoRoot, found.ID)
			_, specExists := os.Stat(specPath)
			out := nextOutput{
				Found:      true,
				ID:         found.ID,
				IDStr:      found.FormattedID(),
				Status:     string(found.Status),
				Priority:   found.Priority,
				Type:       found.Type,
				Title:      found.Title,
				Section:    sectionName(found.Section),
				SpecPath:   spec.SpecRel(found.ID),
				SpecExists: specExists == nil,
			}
			if asJSON {
				return writeJSON(out)
			}
			fmt.Printf("%s %s  %s%s  %s\n",
				statusIcon(found.Status), found.FormattedID(),
				bracketIfSet(found.Priority), bracketIfSet(found.Type), found.Title)
			if out.SpecExists {
				fmt.Printf("spec: %s\n", out.SpecPath)
			}
			return nil
		},
	}
	cmd.Flags().BoolVar(&asJSON, "json", false, "emit JSON (orchestrator-friendly)")
	return cmd
}

func writeJSON(v any) error {
	enc := json.NewEncoder(os.Stdout)
	enc.SetIndent("", "  ")
	return enc.Encode(v)
}

// ----- add --------------------------------------------------------------

func newTodoAddCommand(ctx appContext) *cobra.Command {
	var (
		typeFlag    string
		priority    string
		sectionFlag string
		idFlag      int
		specFlag    bool
		specText    string
		specFrom    string
	)
	cmd := &cobra.Command{
		Use:   "add <title>",
		Short: "Add a new task to .spec/TODO.md",
		Long: "Add a new task. --type is required.\n\n" +
			"Recommended types: BUG, FEAT, IDEA, SPIKE, REFACTOR, DOC (others allowed with a warning).\n" +
			"Examples:\n" +
			"  gnb todo add -t bug \"修复贴图采样越界\"\n" +
			"  gnb todo add -t feat -p P1 \"体积雾\" --section backlog\n" +
			"  gnb todo add -t feat \"重构材质缓存\" --spec\n" +
			"  gnb todo add -t feat \"重构材质缓存\" --spec-from docs/material-cache.md",
		// Args validation is done inside RunE so that bad input prints help
		// instead of an error message.
		RunE: func(cmd *cobra.Command, args []string) error {
			showHelp := func() error {
				_ = cmd.Help()
				return nil
			}
			if len(args) < 1 {
				return showHelp()
			}
			title := strings.Join(args, " ")
			section := spec.SectionNext
			switch strings.ToLower(sectionFlag) {
			case "", "next", "下一步":
				section = spec.SectionNext
			case "backlog", "待规划":
				section = spec.SectionBacklog
			default:
				return showHelp()
			}
			typeUp := strings.ToUpper(strings.TrimSpace(typeFlag))
			if typeUp == "" {
				return showHelp()
			}
			if !validTypes[typeUp] {
				console.Warn("type %q is not in the recommended set (BUG, FEAT, IDEA, SPIKE, REFACTOR, DOC); using it anyway", typeUp)
			}
			priUp := strings.ToUpper(strings.TrimSpace(priority))
			if priUp != "" && !regexp.MustCompile(`^P[0-9]$`).MatchString(priUp) {
				return showHelp()
			}
			createSpec := specFlag || specText != "" || specFrom != ""
			if specText != "" && specFrom != "" {
				return fmt.Errorf("--spec-text and --spec-from are mutually exclusive")
			}
			specBody := specText
			if specFrom != "" {
				data, err := readSpecBody(specFrom)
				if err != nil {
					return err
				}
				specBody = string(data)
				createSpec = true
			}
			doc, err := spec.Parse(spec.TODOPath(ctx.repoRoot))
			if err != nil {
				return err
			}
			newID := idFlag
			if newID == 0 {
				newID = doc.MaxID() + 1
			}
			if newID > 99999 {
				return fmt.Errorf("task id exceeds 5-digit range")
			}
			if createSpec {
				if _, err := os.Stat(spec.SpecPath(ctx.repoRoot, newID)); err == nil {
					return fmt.Errorf("spec already exists: %s", spec.SpecPath(ctx.repoRoot, newID))
				}
			}
			id, err := doc.AppendTask(section, spec.Task{
				Priority: priUp,
				Type:     typeUp,
				Title:    title,
				ID:       newID,
				Arrow:    specArrowIf(createSpec, newID),
			})
			if err != nil {
				return err
			}
			if err := doc.Save(); err != nil {
				return err
			}
			console.Success("added #%05d  [%s]%s %s  → %s", id, typeUp, priorityBracket(priUp), title, sectionName(section))
			if createSpec {
				path, err := spec.WriteSpecStub(ctx.repoRoot, spec.SpecStub{
					TaskID:   id,
					Title:    title,
					Type:     typeUp,
					Priority: priUp,
					Body:     specBody,
				})
				if err != nil {
					return err
				}
				console.Success("wrote spec: %s", path)
			}
			return nil
		},
	}
	cmd.Flags().StringVarP(&typeFlag, "type", "t", "", "task type (BUG, FEAT, IDEA, SPIKE, REFACTOR, DOC) — required")
	cmd.Flags().StringVarP(&priority, "priority", "p", "", "priority (P0, P1, P2)")
	cmd.Flags().StringVar(&sectionFlag, "section", "next", "target section: next | backlog")
	cmd.Flags().IntVar(&idFlag, "id", 0, "force a specific ID instead of auto-assigning")
	cmd.Flags().BoolVar(&specFlag, "spec", false, "create .spec/specs/<id>.md and link it from TODO.md")
	cmd.Flags().StringVar(&specText, "spec-text", "", "create a spec file with this markdown body")
	cmd.Flags().StringVar(&specFrom, "spec-from", "", "create a spec file from a markdown file; use - for stdin")
	return cmd
}

func specArrowIf(enabled bool, id int) string {
	if !enabled {
		return ""
	}
	return spec.SpecRel(id)
}

func readSpecBody(path string) ([]byte, error) {
	if path == "-" {
		return io.ReadAll(os.Stdin)
	}
	return os.ReadFile(path)
}

func priorityBracket(p string) string {
	if p == "" {
		return ""
	}
	return "[" + p + "]"
}

// ----- move / swap ------------------------------------------------------

func newTodoMoveCommand(ctx appContext) *cobra.Command {
	var (
		toFlag     string
		beforeFlag string
		afterFlag  string
	)
	cmd := &cobra.Command{
		Use:   "move <id>",
		Short: "Move a task within or between 下一步 and 待规划",
		Long: "Move a task line as if dragging it in the TODO list.\n\n" +
			"Examples:\n" +
			"  gnb todo move 00021 --to next\n" +
			"  gnb todo move 00021 --before 00018\n" +
			"  gnb todo move 00018 --after 00021",
		Args: cobra.ExactArgs(1),
		RunE: func(cmd *cobra.Command, args []string) error {
			id, err := parseID(args[0])
			if err != nil {
				return err
			}
			toSection := spec.SectionUnknown
			if toFlag != "" {
				toSection, err = parseMaintenanceSection(toFlag)
				if err != nil {
					return err
				}
			}
			beforeID, err := optionalID(beforeFlag)
			if err != nil {
				return err
			}
			afterID, err := optionalID(afterFlag)
			if err != nil {
				return err
			}
			doc, err := spec.Parse(spec.TODOPath(ctx.repoRoot))
			if err != nil {
				return err
			}
			if err := doc.MoveTask(id, spec.MovePlacement{
				ToSection: toSection,
				BeforeID:  beforeID,
				AfterID:   afterID,
			}); err != nil {
				return err
			}
			if err := doc.Save(); err != nil {
				return err
			}
			console.Success("moved #%05d", id)
			return nil
		},
	}
	cmd.Flags().StringVar(&toFlag, "to", "", "target section: next | backlog (optional when --before/--after is used)")
	cmd.Flags().StringVar(&beforeFlag, "before", "", "insert before task ID")
	cmd.Flags().StringVar(&afterFlag, "after", "", "insert after task ID")
	return cmd
}

func newTodoSwapCommand(ctx appContext) *cobra.Command {
	return &cobra.Command{
		Use:   "swap <id-a> <id-b>",
		Short: "Swap two tasks in 下一步/待规划",
		Args:  cobra.ExactArgs(2),
		RunE: func(cmd *cobra.Command, args []string) error {
			aID, err := parseID(args[0])
			if err != nil {
				return err
			}
			bID, err := parseID(args[1])
			if err != nil {
				return err
			}
			doc, err := spec.Parse(spec.TODOPath(ctx.repoRoot))
			if err != nil {
				return err
			}
			if err := doc.SwapTasks(aID, bID); err != nil {
				return err
			}
			if err := doc.Save(); err != nil {
				return err
			}
			console.Success("swapped #%05d and #%05d", aID, bID)
			return nil
		},
	}
}

func parseMaintenanceSection(s string) (spec.SectionKind, error) {
	switch strings.ToLower(strings.TrimSpace(s)) {
	case "next", "下一步":
		return spec.SectionNext, nil
	case "backlog", "待规划":
		return spec.SectionBacklog, nil
	default:
		return spec.SectionUnknown, fmt.Errorf("unknown section %q (expected next|backlog)", s)
	}
}

func optionalID(s string) (int, error) {
	if strings.TrimSpace(s) == "" {
		return 0, nil
	}
	return parseID(s)
}

// ----- done -------------------------------------------------------------

func newTodoDoneCommand(ctx appContext) *cobra.Command {
	var (
		summary   string
		buildOK   bool
		noJournal bool
	)
	cmd := &cobra.Command{
		Use:   "done <id>",
		Short: "Mark a task [x] and create a journal stub",
		Args:  cobra.ExactArgs(1),
		RunE: func(cmd *cobra.Command, args []string) error {
			id, err := parseID(args[0])
			if err != nil {
				return err
			}
			doc, err := spec.Parse(spec.TODOPath(ctx.repoRoot))
			if err != nil {
				return err
			}
			if _, _, ok := doc.FindTask(id); !ok {
				return fmt.Errorf("task #%05d not found", id)
			}
			date := time.Now().Format("2006-01-02")
			if err := doc.MarkStatus(id, spec.StatusDone,
				spec.WithArrow(spec.JournalRel(id)),
				spec.WithParen(date),
			); err != nil {
				return err
			}
			if err := doc.Save(); err != nil {
				return err
			}
			console.Success("#%05d marked [x] (→ %s)", id, spec.JournalRel(id))
			if !noJournal {
				path, err := spec.WriteJournalStub(ctx.repoRoot, spec.JournalStub{
					TaskID:  id,
					BuildOK: buildOK,
					Summary: summary,
				})
				if errors.Is(err, os.ErrExist) {
					console.Info("journal already exists: %s", path)
				} else if err != nil {
					return err
				} else {
					console.Success("wrote journal stub: %s", path)
				}
			}
			return nil
		},
	}
	cmd.Flags().StringVar(&summary, "summary", "", "initial 做了什么 summary in the journal stub")
	cmd.Flags().BoolVar(&buildOK, "build-ok", false, "set build_ok: true in the journal frontmatter")
	cmd.Flags().BoolVar(&noJournal, "no-journal", false, "do not create journal/<id>.md")
	return cmd
}

// ----- delete -----------------------------------------------------------

func newTodoDeleteCommand(ctx appContext) *cobra.Command {
	var (
		yes        bool
		keepSpec   bool
		alsoFiles  bool
	)
	cmd := &cobra.Command{
		Use:   "delete <id>",
		Short: "Permanently remove a task (and its specs/<id>.md by default)",
		Long: "Delete a task line from TODO.md and remove its specs/<id>.md.\n\n" +
			"Pass --keep-spec to leave specs/<id>.md alone.\n" +
			"Pass --also-files to also drop journal/<id>.md and blockers/<id>.md.\n" +
			"Without -y, prints what would be deleted and aborts.",
		Args: cobra.ExactArgs(1),
		RunE: func(cmd *cobra.Command, args []string) error {
			id, err := parseID(args[0])
			if err != nil {
				return err
			}
			doc, err := spec.Parse(spec.TODOPath(ctx.repoRoot))
			if err != nil {
				return err
			}
			t, _, ok := doc.FindTask(id)
			if !ok {
				return fmt.Errorf("task #%05d not found", id)
			}
			specPath := spec.SpecPath(ctx.repoRoot, id)
			_, specExists := os.Stat(specPath)
			journalPath := spec.JournalPath(ctx.repoRoot, id)
			_, journalExists := os.Stat(journalPath)
			blockerPath := spec.BlockerPath(ctx.repoRoot, id)
			_, blockerExists := os.Stat(blockerPath)
			if !yes {
				console.Info("将删除任务 #%05d [%s]%s %s", id, t.Type, priorityBracket(t.Priority), t.Title)
				if !keepSpec && specExists == nil {
					console.Info("  + %s", specPath)
				}
				if alsoFiles && journalExists == nil {
					console.Info("  + %s", journalPath)
				}
				if alsoFiles && blockerExists == nil {
					console.Info("  + %s", blockerPath)
				}
				console.Warn("dry-run；加 -y 真正执行")
				return nil
			}
			if _, err := doc.DeleteTask(id); err != nil {
				return err
			}
			if err := doc.Save(); err != nil {
				return err
			}
			console.Success("removed #%05d from TODO.md", id)
			if !keepSpec {
				if removed, err := spec.RemoveIfExists(specPath); err != nil {
					return err
				} else if removed {
					console.Success("removed %s", specPath)
				}
			}
			if alsoFiles {
				if removed, err := spec.RemoveIfExists(journalPath); err != nil {
					return err
				} else if removed {
					console.Success("removed %s", journalPath)
				}
				if removed, err := spec.RemoveIfExists(blockerPath); err != nil {
					return err
				} else if removed {
					console.Success("removed %s", blockerPath)
				}
			}
			return nil
		},
	}
	cmd.Flags().BoolVarP(&yes, "yes", "y", false, "execute the deletion (otherwise dry-run)")
	cmd.Flags().BoolVar(&keepSpec, "keep-spec", false, "keep specs/<id>.md")
	cmd.Flags().BoolVar(&alsoFiles, "also-files", false, "also delete journal/<id>.md and blockers/<id>.md")
	return cmd
}

// ----- block ------------------------------------------------------------

func newTodoBlockCommand(ctx appContext) *cobra.Command {
	var reason string
	cmd := &cobra.Command{
		Use:   "block <id>",
		Short: "Mark a task [!] and create a blocker stub",
		Args:  cobra.ExactArgs(1),
		RunE: func(cmd *cobra.Command, args []string) error {
			id, err := parseID(args[0])
			if err != nil {
				return err
			}
			doc, err := spec.Parse(spec.TODOPath(ctx.repoRoot))
			if err != nil {
				return err
			}
			if _, _, ok := doc.FindTask(id); !ok {
				return fmt.Errorf("task #%05d not found", id)
			}
			if err := doc.MarkStatus(id, spec.StatusBlocked,
				spec.WithClearArrow(),
				spec.WithParen(spec.BlockerRel(id)),
			); err != nil {
				return err
			}
			if err := doc.Save(); err != nil {
				return err
			}
			console.Success("#%05d marked [!]", id)
			path, err := spec.WriteBlockerStub(ctx.repoRoot, spec.BlockerStub{
				TaskID: id,
				Reason: reason,
			})
			if errors.Is(err, os.ErrExist) {
				console.Info("blocker already exists: %s", path)
			} else if err != nil {
				return err
			} else {
				console.Success("wrote blocker stub: %s", path)
			}
			return nil
		},
	}
	cmd.Flags().StringVarP(&reason, "reason", "r", "", "short description of the blocker")
	return cmd
}

// ----- archive ----------------------------------------------------------

func newTodoArchiveCommand(ctx appContext) *cobra.Command {
	var (
		older int
		keep  int
	)
	cmd := &cobra.Command{
		Use:   "archive",
		Short: "Move tasks from 最近完成 into .spec/ARCHIVE.md",
		Long:  "By default archives every task in 最近完成. Use --older N to archive entries older than N days, or --keep N to retain only the most recent N.",
		RunE: func(cmd *cobra.Command, args []string) error {
			if older > 0 && keep > 0 {
				return fmt.Errorf("--older and --keep are mutually exclusive")
			}
			res, err := spec.Archive(ctx.repoRoot, spec.ArchiveOptions{
				OlderThanDays: older,
				Keep:          keep,
			})
			if err != nil {
				return err
			}
			if len(res.Moved) == 0 {
				console.Muted("(nothing to archive)")
				return nil
			}
			console.Success("archived %d task(s) → %s [%s]", len(res.Moved), res.Archive, res.Bucket)
			for _, t := range res.Moved {
				fmt.Printf("  %s  %s\n", t.FormattedID(), t.Title)
			}
			return nil
		},
	}
	cmd.Flags().IntVar(&older, "older", 0, "archive entries older than N days (per their completion date)")
	cmd.Flags().IntVar(&keep, "keep", 0, "archive everything except the most recent N entries")
	return cmd
}

// ----- helpers ----------------------------------------------------------

func parseID(s string) (int, error) {
	s = strings.TrimSpace(s)
	s = strings.TrimPrefix(s, "#")
	id, err := strconv.Atoi(s)
	if err != nil || id <= 0 {
		return 0, fmt.Errorf("invalid id %q (expected number or #NNNNN)", s)
	}
	return id, nil
}
