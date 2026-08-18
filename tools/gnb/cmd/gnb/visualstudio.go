package main

import (
	"fmt"
	"os"
	"runtime"
	"time"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/cmakerun"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/fetcher"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/platform"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/vcpkg"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/visualstudio"
	"github.com/spf13/cobra"
)

func newVisualStudioCommand(ctx appContext) *cobra.Command {
	skipSetup := false
	cmd := &cobra.Command{
		Use:   "visualstudio",
		Short: "Generate the Visual Studio solution and open it",
		Args:  cobra.NoArgs,
		RunE: func(cmd *cobra.Command, args []string) error {
			if runtime.GOOS != "windows" {
				return fmt.Errorf("gnb visualstudio is supported only on Windows")
			}

			startTime := time.Now()
			if !skipSetup {
				if _, err := os.Stat(vcpkg.Toolchain(ctx.repoRoot, ctx.cfg)); os.IsNotExist(err) {
					if err := platform.EnsureLinuxPreparePackages(); err != nil {
						return err
					}
					if err := vcpkg.Ensure(ctx.repoRoot, ctx.cfg, false); err != nil {
						return err
					}
					if err := fetcher.EnsureExternal(ctx.repoRoot, ctx.cfg); err != nil {
						return err
					}
				}
				if err := vcpkg.EnsureBootstrapped(ctx.repoRoot, ctx.cfg); err != nil {
					return err
				}
			}
			if err := platform.EnsureMSVCEnvironment(); err != nil {
				return err
			}
			cmakePath, err := vcpkg.EnsureBundledCMake(ctx.repoRoot, ctx.cfg)
			if err != nil {
				return err
			}
			if err := cmakerun.ConfigureWithCMake(ctx.repoRoot, cmakePath, "windows-vcproj"); err != nil {
				return err
			}

			solutionPath, err := visualstudio.SolutionPath(ctx.repoRoot)
			if err != nil {
				return err
			}
			devenv, err := visualstudio.Launch(solutionPath)
			if err != nil {
				return err
			}
			fmt.Fprintf(cmd.OutOrStdout(), "[visualstudio] opened %s\n", solutionPath)
			fmt.Fprintf(cmd.OutOrStdout(), "[visualstudio] executable: %s\n", devenv)
			fmt.Fprintf(cmd.OutOrStdout(), "[visualstudio] completed in %s\n", formatDuration(time.Since(startTime)))
			return nil
		},
	}
	cmd.Flags().BoolVar(&skipSetup, "skip-setup", false, "skip vcpkg/bootstrap dependency checks")
	return cmd
}
