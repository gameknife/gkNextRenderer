package main

import (
	"fmt"
	"os"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/ai"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/ai/bridge"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/console"
	"github.com/spf13/cobra"
)

func newAICommand(ctx appContext) *cobra.Command {
	root := &cobra.Command{Use: "ai", Short: "AI provider diagnostics and Engine bridge"}
	root.AddCommand(newAIDoctorCommand(ctx))
	root.AddCommand(newAIBridgeCommand(ctx))
	return root
}

func newLegacyAgentCommand(ctx appContext) *cobra.Command {
	root := &cobra.Command{Use: "agent", Hidden: true}
	root.AddCommand(newAIDoctorCommand(ctx))
	root.AddCommand(newAIBridgeCommand(ctx))
	return root
}

func newAIBridgeCommand(ctx appContext) *cobra.Command {
	var stdio bool
	cmd := &cobra.Command{Use: "bridge", Short: "Run the Engine JSON-RPC AI bridge", RunE: func(cmd *cobra.Command, args []string) error {
		if !stdio {
			return fmt.Errorf("only --stdio transport is currently supported")
		}
		protocolOut := os.Stdout
		os.Stdout = os.Stderr
		runtime, err := ai.NewRuntime(ctx.repoRoot, ctx.cfg)
		if err != nil {
			return err
		}
		return bridge.New(runtime, os.Stdin, protocolOut).Serve(cmd.Context())
	}}
	cmd.Flags().BoolVar(&stdio, "stdio", false, "use NDJSON over stdin/stdout")
	return cmd
}

func newAIDoctorCommand(ctx appContext) *cobra.Command {
	return &cobra.Command{Use: "doctor", Short: "Validate AI profiles, providers, and credentials without inference", RunE: func(cmd *cobra.Command, args []string) error {
		runtime, err := ai.NewRuntime(ctx.repoRoot, ctx.cfg)
		if err != nil {
			return err
		}
		failed := false
		for _, descriptor := range runtime.Registry.Descriptors() {
			state := "configured"
			if !descriptor.Available {
				state = "unavailable"
				failed = true
			} else if !descriptor.Configured {
				state = "not configured: " + descriptor.ConfiguredReason
				failed = true
			}
			console.Label(descriptor.ID, fmt.Sprintf("%s (%s, %s)", state, descriptor.Kind, descriptor.DefaultModel))
		}
		for id, profile := range runtime.Config.Profiles {
			if _, ok := runtime.Registry.Get(profile.Provider); !ok {
				console.Error("profile %s references missing provider %s", id, profile.Provider)
				failed = true
			} else {
				console.Label("profile "+id, profile.Provider+"/"+profile.Model)
			}
		}
		if failed {
			return fmt.Errorf("AI configuration has unavailable entries")
		}
		console.Success("AI configuration is valid")
		return nil
	}}
}
