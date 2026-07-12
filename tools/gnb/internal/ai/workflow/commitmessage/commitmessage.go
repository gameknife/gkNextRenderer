package commitmessage

import (
	"context"
	"encoding/json"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/ai/protocol"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/ai/router"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/ai/workflow"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/llm"
)

const Name = "commit-message"

type Input struct {
	MaxDiffChars int     `json:"maxDiffChars"`
	Temperature  float64 `json:"temperature"`
	Profile      string  `json:"profile"`
	Provider     string  `json:"provider"`
	Model        string  `json:"model"`
}
type Output struct {
	Message   string `json:"message"`
	Source    string `json:"source"`
	Truncated bool   `json:"truncated"`
}

func Handler(repoRoot string, modelRouter *router.Router) workflow.Handler {
	return func(ctx context.Context, raw json.RawMessage, sink protocol.EventSink) (json.RawMessage, error) {
		var input Input
		if err := json.Unmarshal(raw, &input); err != nil {
			return nil, err
		}
		result, err := llm.GenerateCommitMessageWithChat(ctx, repoRoot, input.MaxDiffChars, input.Temperature, func(chatCtx context.Context, messages []llm.ChatMessage, temp float64, maxTokens int) (string, error) {
			converted := make([]protocol.Message, len(messages))
			for i, m := range messages {
				converted[i] = protocol.Message{Role: protocol.Role(m.Role), Content: m.Content}
			}
			response, _, err := modelRouter.Chat(chatCtx, router.Overrides{Profile: input.Profile, Provider: input.Provider, Model: input.Model}, protocol.ChatRequest{Messages: converted, Temperature: temp, MaxOutputTokens: maxTokens}, sink)
			return response.Content, err
		})
		if err != nil {
			return nil, err
		}
		return json.Marshal(Output{Message: result.Message, Source: result.Source, Truncated: result.Truncated})
	}
}
