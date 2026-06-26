package main

import (
	"fmt"
	"net"
	"sort"
	"strings"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/console"
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
	runArgs := []string{
		"--remote",
		fmt.Sprintf("--remote-bind=%s", opts.Bind),
		fmt.Sprintf("--remote-http-port=%d", opts.HttpPort),
		fmt.Sprintf("--remote-port=%d", opts.SignalingPort),
		fmt.Sprintf("--remote-bitrate=%d", opts.BitrateKbps),
		fmt.Sprintf("--remote-fps=%d", opts.Fps),
		fmt.Sprintf("--remote-encoder=%s", opts.Encoder),
	}
	if opts.Resolution != "" {
		runArgs = append(runArgs, "--remote-res="+opts.Resolution)
	}
	if opts.ShowWindow {
		runArgs = append(runArgs, "--remote-show-window")
	}
	return append(runArgs, trailingArgs...)
}

func printRemoteAccessUrls(bind string, httpPort uint32) {
	console.Header("Remote Play")
	for _, url := range buildRemoteAccessURLs(bind, httpPort, localIPv4Hosts()) {
		fmt.Printf("  %s\n", url)
	}
	fmt.Println()
	console.Warn("LAN only / unauthenticated; do not expose this endpoint to the public internet")
}

func buildRemoteAccessURLs(bind string, httpPort uint32, discoveredHosts []string) []string {
	if bind != "" && bind != "0.0.0.0" && bind != "::" {
		return []string{fmt.Sprintf("http://%s:%d", formatURLHost(bind), httpPort)}
	}
	hosts := make([]string, 0, len(discoveredHosts)+2)
	hosts = append(hosts, "127.0.0.1", "localhost")
	sortedHosts := append([]string(nil), discoveredHosts...)
	sort.Strings(sortedHosts)
	hosts = append(hosts, sortedHosts...)
	urls := make([]string, 0, len(hosts))
	seen := make(map[string]struct{}, len(hosts))
	for _, host := range hosts {
		host = strings.TrimSpace(host)
		if host == "" {
			continue
		}
		if _, ok := seen[host]; ok {
			continue
		}
		seen[host] = struct{}{}
		urls = append(urls, fmt.Sprintf("http://%s:%d", formatURLHost(host), httpPort))
	}
	return urls
}

func localIPv4Hosts() []string {
	interfaces, err := net.Interfaces()
	if err != nil {
		return nil
	}
	privateHosts := make([]string, 0, 4)
	otherHosts := make([]string, 0, 2)
	seen := make(map[string]struct{})
	for _, iface := range interfaces {
		if iface.Flags&net.FlagUp == 0 || iface.Flags&net.FlagLoopback != 0 {
			continue
		}
		addrs, err := iface.Addrs()
		if err != nil {
			continue
		}
		for _, addr := range addrs {
			var ip net.IP
			switch value := addr.(type) {
			case *net.IPNet:
				ip = value.IP
			case *net.IPAddr:
				ip = value.IP
			}
			ip = ip.To4()
			if ip == nil || ip.IsLoopback() || ip.IsLinkLocalUnicast() {
				continue
			}
			host := ip.String()
			if _, ok := seen[host]; ok {
				continue
			}
			seen[host] = struct{}{}
			if ip.IsPrivate() {
				privateHosts = append(privateHosts, host)
			} else {
				otherHosts = append(otherHosts, host)
			}
		}
	}
	if len(privateHosts) > 0 {
		return privateHosts
	}
	return otherHosts
}

func formatURLHost(host string) string {
	if ip := net.ParseIP(host); ip != nil && ip.To4() == nil {
		return "[" + host + "]"
	}
	return host
}
