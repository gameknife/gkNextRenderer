package main

import (
	"context"
	"fmt"
	"os"
	"strings"
	"time"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/ai"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/ai/agent"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/ai/bridge"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/ai/protocol"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/ai/router"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/console"
	"github.com/spf13/cobra"
)

func newAgentCommand(ctx appContext) *cobra.Command {
	root := &cobra.Command{Use: "agent", Short: "AI Agent runtime, diagnostics, and bridge"}
	root.AddCommand(newAgentDoctorCommand(ctx))
	root.AddCommand(newAgentRunCommand(ctx))
	root.AddCommand(newAgentBridgeCommand(ctx))
	return root
}

func newAgentBridgeCommand(ctx appContext) *cobra.Command {
	var stdio bool
	cmd := &cobra.Command{Use: "bridge", Short: "Run the Engine JSON-RPC bridge", RunE: func(cmd *cobra.Command, args []string) error {
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

func newAgentDoctorCommand(ctx appContext) *cobra.Command {
	return &cobra.Command{Use: "doctor", Short: "Validate AI profiles, providers, and credentials without a paid inference", RunE: func(cmd *cobra.Command, args []string) error {
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

func newAgentRunCommand(ctx appContext) *cobra.Command {
	var profileID, providerID, modelID, system string
	cmd := &cobra.Command{Use: "run <prompt>", Short: "Run the unified bounded AI Agent with configured tools", Args: cobra.MinimumNArgs(1), RunE: func(cmd *cobra.Command, args []string) error {
		runtime, err := ai.NewRuntime(ctx.repoRoot, ctx.cfg)
		if err != nil {
			return err
		}
		messages := []protocol.Message{}
		if system != "" {
			messages = append(messages, protocol.Message{Role: protocol.RoleSystem, Content: system})
		}
		messages = append(messages, protocol.Message{Role: protocol.RoleUser, Content: strings.Join(args, " ")})
		runCtx, cancel := context.WithTimeout(cmd.Context(), 5*time.Minute)
		defer cancel()
		route, err := runtime.Router.Resolve(router.Overrides{Profile: profileID, Provider: providerID, Model: modelID})
		if err != nil {
			return err
		}
		result, err := runtime.RunAgent(runCtx, router.Overrides{Profile: profileID, Provider: providerID, Model: modelID}, protocol.ChatRequest{Messages: messages}, agent.Options{MaxSteps: route.Settings.MaxSteps, MaxToolCalls: route.Settings.MaxToolCalls}, nil)
		if err != nil {
			return err
		}
		fmt.Println(strings.TrimSpace(result.Content))
		console.Info("provider=%s model=%s steps=%d tools=%d elapsed=%s", result.Trace.Provider, result.Trace.Model, result.Trace.Steps, result.Trace.ToolCalls, result.Trace.Elapsed.Round(time.Millisecond))
		return nil
	}}
	cmd.Flags().StringVar(&profileID, "profile", "general", "AI profile")
	cmd.Flags().StringVar(&providerID, "provider", "", "override provider id")
	cmd.Flags().StringVar(&modelID, "model", "", "override model id")
	cmd.Flags().StringVar(&system, "system", "", "optional system prompt")
	return cmd
}
