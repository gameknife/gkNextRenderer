package main

import (
	"fmt"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/android"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/tracy"
	"github.com/spf13/cobra"
)

func newTracyCommand(ctx appContext) *cobra.Command {
	androidMode := false
	serial := ""
	avd := ""
	port := 8086
	root := &cobra.Command{
		Use:   "tracy",
		Short: "Fetch and launch the matching Tracy Profiler GUI",
		Args:  cobra.NoArgs,
		RunE: func(cmd *cobra.Command, args []string) error {
			if androidMode {
				result, err := android.Run(ctx.repoRoot, "relwithdebinfo", serial, avd)
				if err != nil {
					return fmt.Errorf("launch Android relwithdebinfo app: %w", err)
				}
				if _, err := android.ForwardPort(ctx.repoRoot, "relwithdebinfo", result.Serial, port); err != nil {
					return err
				}
				fmt.Fprintf(cmd.OutOrStdout(), "[tracy] adb forward tcp:%d -> tcp:8086 on %s\n", port, result.Serial)
				fmt.Fprintf(cmd.OutOrStdout(), "[tracy] connect the GUI to 127.0.0.1:%d\n", port)
			}
			if err := tracy.Launch(ctx.repoRoot, ctx.cfg); err != nil {
				return err
			}
			return nil
		},
	}
	fetch := &cobra.Command{
		Use:   "fetch",
		Short: "Download the official Tracy GUI matching the vcpkg client",
		Args:  cobra.NoArgs,
		RunE: func(cmd *cobra.Command, args []string) error {
			profiler, err := tracy.Ensure(ctx.repoRoot, ctx.cfg)
			if err != nil {
				return err
			}
			fmt.Fprintf(cmd.OutOrStdout(), "[tracy] ready: %s\n", profiler)
			return nil
		},
	}
	root.AddCommand(fetch)
	root.Flags().BoolVar(&androidMode, "android", false, "launch the relwithdebinfo Android APK and forward Tracy over adb")
	root.Flags().StringVar(&serial, "serial", "", "use this online adb device serial")
	root.Flags().StringVar(&avd, "avd", "", "start this local AVD when no adb device is online")
	root.Flags().IntVar(&port, "port", 8086, "Tracy TCP port to forward")
	return root
}
