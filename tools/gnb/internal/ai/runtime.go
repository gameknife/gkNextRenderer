package ai

import (
	"fmt"

	aiconfig "github.com/gameknife/gknextrenderer/tools/gnb/internal/ai/config"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/ai/provider"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/ai/provider/claudecompat"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/ai/provider/gemini"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/ai/provider/localllama"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/ai/provider/ollama"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/ai/provider/openaicompat"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/ai/router"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/ai/session"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/ai/workflow"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/ai/workflow/commitmessage"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/ai/workflow/scadscene"
	baseconfig "github.com/gameknife/gknextrenderer/tools/gnb/internal/config"
)

type Runtime struct {
	Config    aiconfig.Config
	Registry  *provider.Registry
	Router    *router.Router
	Sessions  *session.Store
	Workflows *workflow.Registry
}

func NewRuntime(repoRoot string, cfg baseconfig.Config) (*Runtime, error) {
	registry := provider.NewRegistry()
	for id, providerConfig := range cfg.AI.Providers {
		var adapter provider.Provider
		switch providerConfig.Kind {
		case "llama-cpp":
			adapter = localllama.New(id, repoRoot, cfg.External.LLM)
		case "openai-compatible":
			adapter = openaicompat.New(openaicompat.Config{
				ID: id, DisplayName: providerConfig.DisplayName, Endpoint: providerConfig.Endpoint,
				APIKey: cfg.AI.ProviderAPIKey(id), DefaultModel: providerConfig.DefaultModel,
				Models: providerConfig.Models,
			})
		case "claude-compatible":
			adapter = claudecompat.New(claudecompat.Config{
				ID: id, DisplayName: providerConfig.DisplayName, Endpoint: providerConfig.Endpoint,
				APIKey: cfg.AI.ProviderAPIKey(id), DefaultModel: providerConfig.DefaultModel,
				Models: providerConfig.Models,
			})
		case "gemini":
			adapter = gemini.New(gemini.Config{ID: id, DisplayName: providerConfig.DisplayName, Endpoint: providerConfig.Endpoint, APIKey: cfg.AI.ProviderAPIKey(id), DefaultModel: providerConfig.DefaultModel, Models: providerConfig.Models})
		case "ollama":
			adapter = ollama.New(ollama.Config{ID: id, DisplayName: providerConfig.DisplayName, Endpoint: providerConfig.Endpoint, DefaultModel: providerConfig.DefaultModel, Models: providerConfig.Models})
		default:
			return nil, fmt.Errorf("provider %q has unsupported kind %q", id, providerConfig.Kind)
		}
		if err := registry.Register(adapter); err != nil {
			return nil, err
		}
	}
	modelRouter := router.New(cfg.AI, registry)
	workflows := workflow.NewRegistry()
	_ = workflows.Register(commitmessage.Name, commitmessage.Handler(repoRoot, modelRouter))
	_ = workflows.Register(scadscene.Name, scadscene.Handler(modelRouter))
	return &Runtime{Config: cfg.AI, Registry: registry, Router: modelRouter, Sessions: session.NewStore(100), Workflows: workflows}, nil
}
