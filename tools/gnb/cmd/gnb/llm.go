package main

import (
	"context"
	"fmt"
	"os"
	"strings"
	"time"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/ai"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/ai/protocol"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/ai/router"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/config"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/console"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/llm"
	"github.com/spf13/cobra"
)

func newLLMCommand(ctx appContext) *cobra.Command {
	root := &cobra.Command{
		Use:   "llm",
		Short: "Local LLM (llama.cpp + Gemma) lifecycle and chat",
	}
	root.AddCommand(newLLMSetupCommand(ctx))
	root.AddCommand(newLLMServeCommand(ctx))
	root.AddCommand(newLLMStopCommand(ctx))
	root.AddCommand(newLLMStatusCommand(ctx))
	root.AddCommand(newLLMChatCommand(ctx))
	root.AddCommand(newLLMModelsCommand(ctx))
	root.AddCommand(newLLMProvidersCommand(ctx))
	return root
}

// selectLLMModel returns a copy of cfg whose Active field is set to modelID
// (when non-empty) and validates that the model exists. Used by every command
// that takes an optional --model override so behavior stays consistent.
func selectLLMModel(cfg config.LLMConfig, modelID string) (config.LLMConfig, error) {
	return llm.SelectModel(cfg, modelID)
}

func newLLMSetupCommand(ctx appContext) *cobra.Command {
	var modelID string
	var allModels bool
	cmd := &cobra.Command{
		Use:   "setup",
		Short: "Download llama.cpp binaries and the GGUF model(s)",
		RunE: func(cmd *cobra.Command, args []string) error {
			cfg, err := selectLLMModel(ctx.cfg.External.LLM, modelID)
			if err != nil {
				return err
			}
			if err := llm.EnsureBinaries(ctx.repoRoot, cfg); err != nil {
				return err
			}
			if allModels {
				for _, m := range cfg.Models {
					if err := llm.EnsureModelEntry(ctx.repoRoot, cfg, m); err != nil {
						return fmt.Errorf("ensure model %s: %w", m.ID, err)
					}
				}
				console.Success("LLM ready (%d models)", len(cfg.Models))
				return nil
			}
			if err := llm.EnsureModel(ctx.repoRoot, cfg); err != nil {
				return err
			}
			console.Success("LLM ready (model: %s)", cfg.ActiveModel().ID)
			return nil
		},
	}
	cmd.Flags().StringVar(&modelID, "model", "", "model id to download (default: active model)")
	cmd.Flags().BoolVar(&allModels, "all", false, "download every configured model's GGUF")
	return cmd
}

func newLLMServeCommand(ctx appContext) *cobra.Command {
	var modelID string
	cmd := &cobra.Command{
		Use:   "serve",
		Short: "Start llama-server (detached) and wait for /health",
		RunE: func(cmd *cobra.Command, args []string) error {
			cfg, err := selectLLMModel(ctx.cfg.External.LLM, modelID)
			if err != nil {
				return err
			}
			srv := llm.NewServer(ctx.repoRoot, cfg)
			c, cancel := context.WithTimeout(cmd.Context(), 3*time.Minute)
			defer cancel()
			info, err := srv.EnsureRunning(c)
			if err != nil {
				return err
			}
			console.Success("llama-server running pid=%d %s:%d model=%s", info.PID, info.Host, info.Port, info.Model)
			return nil
		},
	}
	cmd.Flags().StringVar(&modelID, "model", "", "override the active LLM model (id from [[external.llm.models]])")
	return cmd
}

func newLLMStopCommand(ctx appContext) *cobra.Command {
	return &cobra.Command{
		Use:   "stop",
		Short: "Stop the background llama-server",
		RunE: func(cmd *cobra.Command, args []string) error {
			srv := llm.NewServer(ctx.repoRoot, ctx.cfg.External.LLM)
			if err := srv.Stop(); err != nil {
				return err
			}
			console.Success("stopped")
			return nil
		},
	}
}

func newLLMStatusCommand(ctx appContext) *cobra.Command {
	return &cobra.Command{
		Use:   "status",
		Short: "Show llama-server status",
		RunE: func(cmd *cobra.Command, args []string) error {
			srv := llm.NewServer(ctx.repoRoot, ctx.cfg.External.LLM)
			info := srv.Status()
			console.Label("endpoint", fmt.Sprintf("%s:%d", info.Host, info.Port))
			console.Label("pid", fmt.Sprintf("%d", info.PID))
			console.Label("active", ctx.cfg.External.LLM.ActiveModel().ID)
			if info.Model != "" {
				console.Label("running model", info.Model)
			}
			if info.ContextN != 0 {
				console.Label("running ctx", fmt.Sprintf("%d", info.ContextN))
			}
			if info.Parallel != 0 {
				console.Label("parallel", fmt.Sprintf("%d", info.Parallel))
			}
			if info.Running {
				console.Success("running")
			} else {
				console.Warn("not running")
			}
			return nil
		},
	}
}

func newLLMChatCommand(ctx appContext) *cobra.Command {
	system := ""
	modelID := ""
	providerID := ""
	profileID := ""
	cmd := &cobra.Command{
		Use:   "chat <prompt>",
		Short: "Send a one-shot prompt through the configured AI provider",
		Args:  cobra.MinimumNArgs(1),
		RunE: func(cmd *cobra.Command, args []string) error {
			c, cancel := context.WithTimeout(cmd.Context(), 5*time.Minute)
			defer cancel()
			runtime, err := ai.NewRuntime(ctx.repoRoot, ctx.cfg)
			if err != nil {
				return err
			}
			msgs := []protocol.Message{}
			if system != "" {
				msgs = append(msgs, protocol.Message{Role: protocol.RoleSystem, Content: system})
			}
			msgs = append(msgs, protocol.Message{Role: protocol.RoleUser, Content: strings.Join(args, " ")})
			reply, _, err := runtime.Router.Chat(c, router.Overrides{Profile: profileID, Provider: providerID, Model: modelID}, protocol.ChatRequest{Messages: msgs}, nil)
			if err != nil {
				return err
			}
			fmt.Println(strings.TrimSpace(reply.Content))
			return nil
		},
	}
	cmd.Flags().StringVar(&system, "system", "", "optional system prompt")
	cmd.Flags().StringVar(&modelID, "model", "", "override the active LLM model (id from [[external.llm.models]])")
	cmd.Flags().StringVar(&providerID, "provider", "", "override the AI provider id")
	cmd.Flags().StringVar(&profileID, "profile", "", "AI profile (default: ai.default_profile)")
	return cmd
}

func newLLMProvidersCommand(ctx appContext) *cobra.Command {
	return &cobra.Command{Use: "providers", Short: "List configured AI providers and capabilities", RunE: func(cmd *cobra.Command, args []string) error {
		runtime, err := ai.NewRuntime(ctx.repoRoot, ctx.cfg)
		if err != nil {
			return err
		}
		for _, d := range runtime.Registry.Descriptors() {
			status := "configured"
			if !d.Configured {
				status = "not configured: " + d.ConfiguredReason
			}
			fmt.Printf("%-16s %-20s %-18s %s\n", d.ID, d.DisplayName, d.Kind, status)
		}
		return nil
	}}
}

func newLLMModelsCommand(ctx appContext) *cobra.Command {
	var providerID string
	cmd := &cobra.Command{
		Use:   "models",
		Short: "List configured LLM models and mark the active one",
		RunE: func(cmd *cobra.Command, args []string) error {
			if providerID != "" && providerID != "localllm" {
				runtime, err := ai.NewRuntime(ctx.repoRoot, ctx.cfg)
				if err != nil {
					return err
				}
				p, ok := runtime.Registry.Get(providerID)
				if !ok {
					return fmt.Errorf("unknown AI provider %q", providerID)
				}
				d := p.Descriptor()
				for _, model := range d.Models {
					marker := "  "
					if model == d.DefaultModel {
						marker = "* "
					}
					fmt.Printf("%s%s\n", marker, model)
				}
				return nil
			}
			cfg := ctx.cfg.External.LLM
			if len(cfg.Models) == 0 {
				console.Warn("no models configured in [[external.llm.models]]")
				return nil
			}
			active := cfg.ActiveModel().ID
			layout := llm.ResolveLayout(ctx.repoRoot, cfg)
			for _, m := range cfg.Models {
				marker := "  "
				if m.ID == active {
					marker = "* "
				}
				status := "missing"
				if _, err := os.Stat(layout.ModelPath(m)); err == nil {
					status = "downloaded"
				}
				fmt.Printf("%s%-32s ctx=%d  %s\n", marker, m.ID, m.ContextN, status)
			}
			return nil
		},
	}
	cmd.Flags().StringVar(&providerID, "provider", "localllm", "AI provider id")
	return cmd
}
