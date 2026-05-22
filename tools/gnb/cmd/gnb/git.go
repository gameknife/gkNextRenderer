package main

import (
	"errors"
	"fmt"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/console"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/gitops"
	"github.com/spf13/cobra"
)

func newGitCommand(ctx appContext) *cobra.Command {
	root := &cobra.Command{
		Use:   "git",
		Short: "Common git operations (status / branch / switch / pull / log)",
	}
	root.AddCommand(newGitStatusCommand(ctx))
	root.AddCommand(newGitBranchCommand(ctx))
	root.AddCommand(newGitSwitchCommand(ctx))
	root.AddCommand(newGitPullCommand(ctx))
	root.AddCommand(newGitFetchCommand(ctx))
	root.AddCommand(newGitLogCommand(ctx))
	root.AddCommand(newGitResetCommand(ctx))
	root.AddCommand(newGitStashCommand(ctx))
	return root
}

func newGitStatusCommand(ctx appContext) *cobra.Command {
	return &cobra.Command{
		Use:   "status",
		Short: "Show current branch, head, and dirty/upstream summary",
		RunE: func(cmd *cobra.Command, args []string) error {
			st, err := gitops.GetStatus(ctx.repoRoot)
			if err != nil {
				return err
			}
			console.Label("branch", emptyDash(st.Branch))
			console.Label("head", st.Head)
			if st.Upstream != "" {
				console.Label("upstream", st.Upstream)
				console.Label("ahead/behind", fmt.Sprintf("+%d / -%d", st.Ahead, st.Behind))
			}
			if st.Dirty {
				console.Warn("working tree dirty (e.g. %s)", st.DirtyHint)
			} else {
				console.Success("working tree clean")
			}
			return nil
		},
	}
}

func newGitBranchCommand(ctx appContext) *cobra.Command {
	showRemote := false
	cmd := &cobra.Command{
		Use:   "branch",
		Short: "List local (and optionally remote) branches",
		RunE: func(cmd *cobra.Command, args []string) error {
			branches, err := gitops.Branches(ctx.repoRoot)
			if err != nil {
				return err
			}
			for _, b := range branches {
				marker := "  "
				if b.Current {
					marker = "* "
				}
				remote := ""
				if b.Remote != "" {
					remote = " [" + b.Remote + "]"
				}
				fmt.Printf("%s%s%s  %s\n", marker, b.Name, remote, b.Subject)
			}
			if !showRemote {
				return nil
			}
			rb, err := gitops.RemoteBranches(ctx.repoRoot)
			if err != nil {
				return err
			}
			if len(rb) > 0 {
				fmt.Println()
				console.Header("remote branches")
				for _, r := range rb {
					mark := "  "
					if r.HasLocal {
						mark = "= "
					}
					fmt.Printf("%s%s  %s\n", mark, r.FullName, r.Subject)
				}
			}
			return nil
		},
	}
	cmd.Flags().BoolVarP(&showRemote, "remote", "r", false, "also list remote-tracking branches")
	return cmd
}

func newGitSwitchCommand(ctx appContext) *cobra.Command {
	var (
		force      bool
		create     bool
		track      bool
		startPoint string
	)
	cmd := &cobra.Command{
		Use:   "switch [-c|--create] [--track] <branch>",
		Short: "Switch branches; with -c create a new branch; with --track create a local branch tracking a remote ref",
		Args:  cobra.ExactArgs(1),
		RunE: func(cmd *cobra.Command, args []string) error {
			name := args[0]
			switch {
			case create:
				if err := gitops.CreateBranch(ctx.repoRoot, name, startPoint, force); err != nil {
					if errors.Is(err, gitops.ErrDirty) {
						return fmt.Errorf("%w: commit/stash first or pass --force", err)
					}
					return err
				}
				console.Success("created and switched to %s", name)
			case track:
				if err := gitops.CheckoutRemote(ctx.repoRoot, name, force); err != nil {
					if errors.Is(err, gitops.ErrDirty) {
						return fmt.Errorf("%w: commit/stash first or pass --force", err)
					}
					return err
				}
				console.Success("created local branch tracking %s", name)
			default:
				if err := gitops.Checkout(ctx.repoRoot, name, force); err != nil {
					if errors.Is(err, gitops.ErrDirty) {
						return fmt.Errorf("%w: commit/stash first or pass --force", err)
					}
					return err
				}
				console.Success("switched to %s", name)
			}
			return nil
		},
	}
	cmd.Flags().BoolVar(&force, "force", false, "switch even if the working tree is dirty")
	cmd.Flags().BoolVarP(&create, "create", "c", false, "create a new branch and switch to it")
	cmd.Flags().BoolVar(&track, "track", false, "treat <branch> as a remote ref (e.g. origin/foo) and create a local tracking branch")
	cmd.Flags().StringVar(&startPoint, "start", "", "start point for the new branch (with -c); defaults to HEAD")
	return cmd
}

func newGitPullCommand(ctx appContext) *cobra.Command {
	return &cobra.Command{
		Use:   "pull",
		Short: "Fast-forward pull current branch (refuses if working tree is dirty)",
		RunE: func(cmd *cobra.Command, args []string) error {
			out, err := gitops.Pull(ctx.repoRoot)
			if err != nil {
				if errors.Is(err, gitops.ErrDirty) {
					return fmt.Errorf("%w: commit/stash first", err)
				}
				return err
			}
			if out != "" {
				fmt.Println(out)
			}
			console.Success("pulled")
			return nil
		},
	}
}

func newGitFetchCommand(ctx appContext) *cobra.Command {
	return &cobra.Command{
		Use:   "fetch",
		Short: "Fetch all remotes with prune",
		RunE: func(cmd *cobra.Command, args []string) error {
			out, err := gitops.Fetch(ctx.repoRoot)
			if err != nil {
				return err
			}
			if out != "" {
				fmt.Println(out)
			}
			console.Success("fetched")
			return nil
		},
	}
}

func newGitLogCommand(ctx appContext) *cobra.Command {
	limit := 30
	cmd := &cobra.Command{
		Use:   "log",
		Short: "Show recent commits (oneline)",
		RunE: func(cmd *cobra.Command, args []string) error {
			commits, err := gitops.Log(ctx.repoRoot, limit)
			if err != nil {
				return err
			}
			for _, c := range commits {
				fmt.Printf("%s  %s  %s  %s\n", c.Hash, c.Date, truncate(c.Author, 16), c.Subject)
			}
			return nil
		},
	}
	cmd.Flags().IntVarP(&limit, "limit", "n", 30, "number of commits to show")
	return cmd
}

func newGitResetCommand(ctx appContext) *cobra.Command {
	hard := false
	cmd := &cobra.Command{
		Use:   "reset <ref> --hard",
		Short: "Reset current branch to <ref> (destructive: requires --hard)",
		Long: "Move the current branch pointer to <ref> and discard all local " +
			"changes. This permanently drops uncommitted work and any commits not " +
			"reachable elsewhere. --hard must be passed explicitly so it isn't " +
			"triggered by accident.",
		Args: cobra.ExactArgs(1),
		RunE: func(cmd *cobra.Command, args []string) error {
			if !hard {
				return fmt.Errorf("refusing to reset without --hard (only --hard is supported)")
			}
			ref := args[0]
			if err := gitops.ResetHard(ctx.repoRoot, ref); err != nil {
				return err
			}
			console.Success("reset HEAD --hard %s", ref)
			return nil
		},
	}
	cmd.Flags().BoolVar(&hard, "hard", false, "required; confirms destructive intent")
	return cmd
}

func newGitStashCommand(ctx appContext) *cobra.Command {
	root := &cobra.Command{
		Use:   "stash",
		Short: "Wrap git stash (push / pop / apply / list / drop)",
	}
	var (
		message          string
		includeUntracked bool
	)
	push := &cobra.Command{
		Use:   "push",
		Short: "Stash uncommitted changes",
		RunE: func(cmd *cobra.Command, args []string) error {
			out, err := gitops.StashPush(ctx.repoRoot, message, includeUntracked)
			if err != nil {
				return err
			}
			if out != "" {
				fmt.Println(out)
			}
			console.Success("stashed")
			return nil
		},
	}
	push.Flags().StringVarP(&message, "message", "m", "", "stash message")
	push.Flags().BoolVarP(&includeUntracked, "include-untracked", "u", false, "also stash untracked files")

	list := &cobra.Command{
		Use:   "list",
		Short: "List stash entries",
		RunE: func(cmd *cobra.Command, args []string) error {
			stashes, err := gitops.StashList(ctx.repoRoot)
			if err != nil {
				return err
			}
			for _, s := range stashes {
				fmt.Printf("%-12s %s\n", s.Ref, s.Message)
			}
			return nil
		},
	}

	mkAction := func(action string) *cobra.Command {
		return &cobra.Command{
			Use:   action + " [stash@{N}]",
			Short: fmt.Sprintf("%s a stash entry (default: most recent)", action),
			Args:  cobra.MaximumNArgs(1),
			RunE: func(cmd *cobra.Command, args []string) error {
				ref := ""
				if len(args) == 1 {
					ref = args[0]
				}
				out, err := gitops.StashAction(ctx.repoRoot, action, ref)
				if err != nil {
					return err
				}
				if out != "" {
					fmt.Println(out)
				}
				console.Success("%s %s", action, ref)
				return nil
			},
		}
	}

	root.AddCommand(push, list, mkAction("pop"), mkAction("apply"), mkAction("drop"))
	return root
}

func emptyDash(s string) string {
	if s == "" {
		return "(detached)"
	}
	return s
}

func truncate(s string, n int) string {
	if len(s) <= n {
		return s
	}
	if n <= 1 {
		return s[:n]
	}
	return s[:n-1] + "…"
}
