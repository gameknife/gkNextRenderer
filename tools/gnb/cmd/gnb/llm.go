package main

import (
	"context"
	"fmt"
	"os"
	"strings"
	"time"

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
	cmd := &cobra.Command{
		Use:   "chat <prompt>",
		Short: "Send a one-shot prompt to the local LLM",
		Args:  cobra.MinimumNArgs(1),
		RunE: func(cmd *cobra.Command, args []string) error {
			cfg, err := selectLLMModel(ctx.cfg.External.LLM, modelID)
			if err != nil {
				return err
			}
			srv := llm.NewServer(ctx.repoRoot, cfg)
			c, cancel := context.WithTimeout(cmd.Context(), 5*time.Minute)
			defer cancel()
			if _, err := srv.EnsureRunning(c); err != nil {
				return err
			}
			client := llm.NewClient(srv.BaseURL())
			msgs := []llm.ChatMessage{}
			if system != "" {
				msgs = append(msgs, llm.ChatMessage{Role: "system", Content: system})
			}
			msgs = append(msgs, llm.ChatMessage{Role: "user", Content: strings.Join(args, " ")})
			reply, err := client.Chat(c, llm.ChatRequest{
				Messages:    msgs,
				Temperature: 0.7,
				MaxTokens:   512,
			})
			if err != nil {
				return err
			}
			fmt.Println(strings.TrimSpace(reply))
			return nil
		},
	}
	cmd.Flags().StringVar(&system, "system", "", "optional system prompt")
	cmd.Flags().StringVar(&modelID, "model", "", "override the active LLM model (id from [[external.llm.models]])")
	return cmd
}

func newLLMModelsCommand(ctx appContext) *cobra.Command {
	return &cobra.Command{
		Use:   "models",
		Short: "List configured LLM models and mark the active one",
		RunE: func(cmd *cobra.Command, args []string) error {
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
}
