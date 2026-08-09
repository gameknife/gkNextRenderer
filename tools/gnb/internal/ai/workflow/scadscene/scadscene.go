package scadscene

import (
	"context"
	"encoding/json"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/ai/protocol"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/ai/router"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/ai/workflow"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/scadcompose"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/scadgen"
)

const Name = "scad-scene"

type Input struct {
	CatalogPath string  `json:"catalogPath"`
	Menu        string  `json:"menu"`
	Request     string  `json:"request"`
	MaxRepairs  int     `json:"maxRepairs"`
	Temperature float64 `json:"temperature"`
	Profile     string  `json:"profile"`
	Provider    string  `json:"provider"`
	Model       string  `json:"model"`
}

type Output struct {
	Name     string          `json:"name"`
	SpecJSON json.RawMessage `json:"specJson"`
	Source   string          `json:"source"`
	Warnings []string        `json:"warnings"`
	Rounds   int             `json:"rounds"`
}

func Handler(modelRouter *router.Router) workflow.Handler {
	return func(ctx context.Context, raw json.RawMessage, sink protocol.EventSink) (json.RawMessage, error) {
		var input Input
		if err := json.Unmarshal(raw, &input); err != nil {
			return nil, err
		}
		catalog, err := scadcompose.LoadCatalog(input.CatalogPath)
		if err != nil {
			return nil, err
		}
		chat := func(chatCtx context.Context, messages []scadgen.Message) (string, error) {
			converted := make([]protocol.Message, len(messages))
			for i, message := range messages {
				converted[i] = protocol.Message{Role: protocol.Role(message.Role), Content: message.Content}
			}
			response, _, err := modelRouter.Chat(chatCtx,
				router.Overrides{Profile: input.Profile, Provider: input.Provider, Model: input.Model},
				protocol.ChatRequest{Messages: converted, Temperature: input.Temperature, MaxOutputTokens: 4096}, sink)
			return response.Content, err
		}
		outcome, err := scadgen.Generate(ctx, chat, catalog, input.Menu, input.Request, scadgen.Options{MaxRepairs: input.MaxRepairs})
		if err != nil {
			return nil, err
		}
		return json.Marshal(Output{Name: outcome.Spec.Name, SpecJSON: outcome.SpecJSON, Source: outcome.Source, Warnings: outcome.Warnings, Rounds: outcome.Rounds})
	}
}
