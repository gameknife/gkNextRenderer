package main

import (
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/console"
	"github.com/spf13/cobra"
)

const (
	defaultRepoURL    = "https://github.com/gameknife/gkNextEngine.git"
	defaultRepoFolder = "gkNextEngine"
)

func newInitCommand() *cobra.Command {
	var (
		branch  string
		repo    string
		shallow bool
	)
	cmd := &cobra.Command{
		Use:   "init [dir]",
		Short: "Clone gkNextEngine into a fresh directory and print next steps",
		Long: "Clone https://github.com/gameknife/gkNextEngine into <dir> (default: ./gkNextEngine).\n" +
			"Designed to be invoked by the bootstrap script users download from the paks-latest release.",
		Args: cobra.MaximumNArgs(1),
		RunE: func(cmd *cobra.Command, args []string) error {
			target := defaultRepoFolder
			if len(args) == 1 {
				target = args[0]
			}
			abs, err := filepath.Abs(target)
			if err != nil {
				return err
			}
			if info, err := os.Stat(abs); err == nil {
				if !info.IsDir() {
					return fmt.Errorf("%s exists and is not a directory", abs)
				}
				entries, err := os.ReadDir(abs)
				if err != nil {
					return err
				}
				if len(entries) > 0 {
					return fmt.Errorf("target directory %s is not empty", abs)
				}
			}

			console.Info("cloning %s → %s", repo, abs)
			cloneArgs := []string{"clone"}
			if branch != "" {
				cloneArgs = append(cloneArgs, "--branch", branch)
			}
			if shallow {
				cloneArgs = append(cloneArgs, "--depth", "1")
			}
			cloneArgs = append(cloneArgs, repo, abs)
			c := exec.Command("git", cloneArgs...)
			c.Stdout = os.Stdout
			c.Stderr = os.Stderr
			if err := c.Run(); err != nil {
				return fmt.Errorf("git clone failed: %w", err)
			}
			console.Success("cloned to %s", abs)
			printNextSteps(abs)
			return nil
		},
	}
	cmd.Flags().StringVar(&repo, "repo", defaultRepoURL, "git URL to clone")
	cmd.Flags().StringVar(&branch, "branch", "", "branch to check out (default: repo default)")
	cmd.Flags().BoolVar(&shallow, "shallow", false, "clone with --depth 1")
	return cmd
}

func printNextSteps(dir string) {
	gnbCmd := "./gnb"
	if runtime.GOOS == "windows" {
		gnbCmd = "gnb.bat"
	}
	fmt.Println()
	console.Header("下一步")
	fmt.Printf("  cd %s\n", dir)
	fmt.Printf("  %s setup      # 拉取 vcpkg / SDK / paks\n", gnbCmd)
	fmt.Printf("  %s build      # 构建默认目标\n", gnbCmd)
	fmt.Printf("  %s            # 启动 dashboard（默认行为）\n", gnbCmd)
}
