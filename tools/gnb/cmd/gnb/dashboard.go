package main

import (
	"context"
	"fmt"
	"os/signal"
	"syscall"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/dashboard"
	"github.com/spf13/cobra"
)

type dashboardCmdOpts struct {
	Port    int
	NoOpen  bool
	Browser bool
}

func runDashboard(ctx appContext, opts dashboardCmdOpts) error {
	if opts.Port == 0 {
		opts.Port = 7777
	}
	if opts.NoOpen && opts.Browser {
		return fmt.Errorf("--browser and --no-open cannot be used together")
	}
	srv, err := dashboard.New(dashboard.Options{
		RepoRoot: ctx.repoRoot,
		Port:     opts.Port,
		NoOpen:   opts.NoOpen,
		Version:  resolvedVersion(),
		Preset:   ctx.preset,
		Config:   ctx.cfg,
	})
	if err != nil {
		return err
	}
	signalCtx, stop := signal.NotifyContext(context.Background(), syscall.SIGINT, syscall.SIGTERM)
	defer stop()
	if opts.NoOpen || opts.Browser {
		return srv.Run(signalCtx)
	}
	return srv.RunDesktop(signalCtx)
}

func newDashboardCommand(ctx appContext) *cobra.Command {
	opts := dashboardCmdOpts{}
	cmd := &cobra.Command{
		Use:   "dashboard",
		Short: "Launch the native desktop dashboard for .spec/ workflow",
		Long: "Launch a Wails desktop window backed by the local dashboard server.\n" +
			"It visualizes .spec/TODO.md,\n" +
			"task journals, and blocker reports. Supports adding tasks and marking done/blocked.\n" +
			"The dashboard reads and writes the same files as `gnb todo`.\n" +
			"Use --browser for the legacy external-browser UI or --no-open for server-only mode.",
		RunE: func(cmd *cobra.Command, args []string) error {
			return runDashboard(ctx, opts)
		},
	}
	cmd.Flags().IntVar(&opts.Port, "port", 7777, "TCP port to listen on (127.0.0.1)")
	cmd.Flags().BoolVar(&opts.Browser, "browser", false, "open the dashboard in the external browser")
	cmd.Flags().BoolVar(&opts.NoOpen, "no-open", false, "run the dashboard server without opening a window")
	return cmd
}
