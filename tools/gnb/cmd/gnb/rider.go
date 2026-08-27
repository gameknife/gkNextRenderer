package main

import (
	"fmt"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/rider"
	"github.com/spf13/cobra"
)

func newRiderCommand(ctx appContext) *cobra.Command {
	return &cobra.Command{
		Use:   "rider",
		Short: "Launch Rider with the root CMake project",
		Args:  cobra.NoArgs,
		RunE: func(cmd *cobra.Command, args []string) error {
			executable, err := rider.Launch(ctx.repoRoot)
			if err != nil {
				return err
			}
			fmt.Fprintf(cmd.OutOrStdout(), "[rider] opened %s\n", ctx.repoRoot+"/CMakeLists.txt")
			fmt.Fprintf(cmd.OutOrStdout(), "[rider] executable: %s\n", executable)
			return nil
		},
	}
}
