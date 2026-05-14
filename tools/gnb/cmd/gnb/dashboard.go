package main

import (
	"context"
	"os/signal"
	"syscall"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/dashboard"
	"github.com/spf13/cobra"
)

func newDashboardCommand(ctx appContext) *cobra.Command {
	var (
		port   int
		noOpen bool
	)
	cmd := &cobra.Command{
		Use:   "dashboard",
		Short: "Launch the local web dashboard for .spec/ workflow",
		Long: "Serve a local web UI at http://127.0.0.1:<port> that visualizes .spec/TODO.md,\n" +
			"task journals, and blocker reports. Supports adding tasks and marking done/blocked.\n" +
			"The dashboard reads and writes the same files as `gnb todo`.",
		RunE: func(cmd *cobra.Command, args []string) error {
			srv, err := dashboard.New(dashboard.Options{
				RepoRoot: ctx.repoRoot,
				Port:     port,
				NoOpen:   noOpen,
				Version:  resolvedVersion(),
				Preset:   ctx.preset,
			})
			if err != nil {
				return err
			}
			signalCtx, stop := signal.NotifyContext(context.Background(), syscall.SIGINT, syscall.SIGTERM)
			defer stop()
			return srv.Run(signalCtx)
		},
	}
	cmd.Flags().IntVar(&port, "port", 7777, "TCP port to listen on (127.0.0.1)")
	cmd.Flags().BoolVar(&noOpen, "no-open", false, "do not auto-launch the browser")
	return cmd
}
