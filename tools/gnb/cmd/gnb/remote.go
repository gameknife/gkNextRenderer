package main

import (
	"fmt"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/console"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/remoteplay"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/runner"
	"github.com/spf13/cobra"
)

type remoteCmdOptions struct {
	Target        string
	Scene         string
	Bind          string
	Resolution    string
	Encoder       string
	HttpPort      uint32
	SignalingPort uint32
	BitrateKbps   uint32
	Fps           uint32
	ShowWindow    bool
	DryRun        bool
}

func newRemoteCommand(ctx appContext) *cobra.Command {
	opts := remoteCmdOptions{
		Target:        "gkNextRenderer",
		Bind:          "0.0.0.0",
		Encoder:       "auto",
		HttpPort:      8088,
		SignalingPort: 8089,
		Fps:           30,
	}
	cmd := &cobra.Command{
		Use:   "remote [--scene <path>] [--target <name>] [app-args]",
		Short: "Run a target in WebRTC remote host mode",
		Long: "Start a target with --remote, print the browser URLs, and keep the process attached.\n\n" +
			"Examples:\n" +
			"  gnb remote\n" +
			"  gnb remote --scene assets/models/playground.glb --res 1280x720\n" +
			"  gnb remote --target gkNextEditor --show-window",
		RunE: func(cmd *cobra.Command, args []string) error {
			printRemoteAccessUrls(opts.Bind, opts.HttpPort)
			runArgs := remoteRunArgs(opts, args)
			runOpts := runner.Options{Target: opts.Target, Preset: ctx.preset, Args: runArgs, DryRun: opts.DryRun}
			if opts.Scene != "" {
				runOpts.Scenes = append(runOpts.Scenes, opts.Scene)
			}
			return runner.Run(ctx.repoRoot, runOpts)
		},
	}
	cmd.Flags().StringVar(&opts.Scene, "scene", "", "scene to load (file path or built-in .proc name)")
	cmd.Flags().StringVar(&opts.Target, "target", opts.Target, "target executable to run")
	cmd.Flags().StringVar(&opts.Bind, "bind", opts.Bind, "remote bind address passed as --remote-bind")
	cmd.Flags().Uint32Var(&opts.HttpPort, "http-port", opts.HttpPort, "HTTP client port passed as --remote-http-port")
	cmd.Flags().Uint32Var(&opts.SignalingPort, "port", opts.SignalingPort, "signaling WebSocket port passed as --remote-port")
	cmd.Flags().Uint32Var(&opts.BitrateKbps, "bitrate", opts.BitrateKbps, "starting bitrate in kbps passed as --remote-bitrate")
	cmd.Flags().Uint32Var(&opts.Fps, "fps", opts.Fps, "target stream fps passed as --remote-fps")
	cmd.Flags().StringVar(&opts.Resolution, "res", "", "encode resolution passed as --remote-res, e.g. 1280x720")
	cmd.Flags().StringVar(&opts.Encoder, "encoder", opts.Encoder, "video encoder passed as --remote-encoder (auto, vulkan)")
	cmd.Flags().BoolVar(&opts.ShowWindow, "show-window", false, "keep the local desktop window visible")
	cmd.Flags().BoolVar(&opts.DryRun, "dry-run", false, "print the command without running")
	return cmd
}

func remoteRunArgs(opts remoteCmdOptions, trailingArgs []string) []string {
	return remoteplay.RunArgs(remoteplay.Options{
		Bind:          opts.Bind,
		Resolution:    opts.Resolution,
		Encoder:       opts.Encoder,
		HttpPort:      opts.HttpPort,
		SignalingPort: opts.SignalingPort,
		BitrateKbps:   opts.BitrateKbps,
		Fps:           opts.Fps,
		ShowWindow:    opts.ShowWindow,
	}, trailingArgs)
}

func printRemoteAccessUrls(bind string, httpPort uint32) {
	console.Header("Remote Play")
	for _, url := range buildRemoteAccessURLs(bind, httpPort, remoteplay.LocalIPv4Hosts()) {
		fmt.Printf("  %s\n", url)
	}
	fmt.Println()
	console.Warn("LAN only / unauthenticated; do not expose this endpoint to the public internet")
}

func buildRemoteAccessURLs(bind string, httpPort uint32, discoveredHosts []string) []string {
	return remoteplay.BuildAccessURLs(bind, httpPort, discoveredHosts)
}

func formatURLHost(host string) string {
	return remoteplay.FormatURLHost(host)
}
