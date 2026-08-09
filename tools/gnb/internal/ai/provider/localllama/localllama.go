package localllama

import (
	"context"
	"fmt"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/ai/protocol"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/ai/provider"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/ai/provider/openaicompat"
	baseconfig "github.com/gameknife/gknextrenderer/tools/gnb/internal/config"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/llm"
)

type Adapter struct {
	repoRoot string
	config   baseconfig.LLMConfig
	id       string
	leases   *LeaseManager
}

func New(id, repoRoot string, cfg baseconfig.LLMConfig) *Adapter {
	adapter := &Adapter{id: id, repoRoot: repoRoot, config: cfg}
	adapter.leases = newLeaseManager(func(selected baseconfig.LLMConfig) serverControl { return llm.NewServer(repoRoot, selected) })
	return adapter
}

func (a *Adapter) Descriptor() provider.Descriptor {
	models := make([]string, 0, len(a.config.Models))
	for _, model := range a.config.Models {
		models = append(models, model.ID)
	}
	active := a.config.ActiveModel().ID
	configured := active != ""
	reason := ""
	if !configured {
		reason = "no models configured in [[external.llm.models]]"
	}
	return provider.Descriptor{
		ID: a.id, Kind: "llama-cpp", DisplayName: "Local Llama", Models: models,
		DefaultModel: active, Configured: configured, ConfiguredReason: reason, Available: true,
		Capabilities: provider.Capabilities{Streaming: true, StructuredOutput: true, ReasoningControl: true, JSONMode: true},
	}
}

func (a *Adapter) Chat(ctx context.Context, req protocol.ChatRequest, sink protocol.EventSink) (protocol.ChatResponse, error) {
	cfg, err := llm.SelectModel(a.config, req.Model)
	if err != nil {
		return protocol.ChatResponse{}, err
	}
	lease, info, err := a.leases.Acquire(ctx, cfg)
	if err != nil {
		return protocol.ChatResponse{}, fmt.Errorf("start LocalLlama: %w", err)
	}
	defer lease.Release()
	server := llm.NewServer(a.repoRoot, cfg)
	client := openaicompat.New(openaicompat.Config{ID: a.id, Endpoint: info.BaseURL() + "/v1", APIKey: "local", DefaultModel: req.Model, ChatTemplateKwargs: true})
	response, err := client.Chat(ctx, req, sink)
	if err == nil {
		err = a.leases.Validate(lease, server.Status())
	}
	return response, err
}
