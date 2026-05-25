package llm

import (
	"fmt"
	"strings"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/config"
)

// SelectModel returns a copy of cfg whose Active field is set to modelID
// (when non-empty) and validates that the model exists.
func SelectModel(cfg config.LLMConfig, modelID string) (config.LLMConfig, error) {
	if modelID == "" {
		if cfg.ActiveModel().ID == "" {
			return cfg, fmt.Errorf("no LLM model configured (set [external.llm].active in gnb.toml)")
		}
		return cfg, nil
	}
	if _, ok := cfg.FindModel(modelID); !ok {
		ids := make([]string, 0, len(cfg.Models))
		for _, m := range cfg.Models {
			ids = append(ids, m.ID)
		}
		return cfg, fmt.Errorf("unknown LLM model %q (available: %s)", modelID, strings.Join(ids, ", "))
	}
	cfg.Active = modelID
	return cfg, nil
}
