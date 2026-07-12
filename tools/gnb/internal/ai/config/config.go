package config

import (
	"bytes"
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"runtime"
	"strings"

	"github.com/BurntSushi/toml"
)

type Config struct {
	DefaultProfile string                    `toml:"default_profile"`
	Providers      map[string]ProviderConfig `toml:"providers"`
	Profiles       map[string]Profile        `toml:"profiles"`
	Secrets        map[string]string         `toml:"-"`
}

type ProviderConfig struct {
	Kind         string   `toml:"kind"`
	DisplayName  string   `toml:"display_name"`
	Endpoint     string   `toml:"endpoint"`
	DefaultModel string   `toml:"default_model"`
	Models       []string `toml:"models"`
	APIKeyEnv    string   `toml:"api_key_env"`
	Runtime      string   `toml:"runtime"`
}

type Profile struct {
	Provider          string   `toml:"provider"`
	Model             string   `toml:"model"`
	FallbackProviders []string `toml:"fallback_providers"`
	ToolSets          []string `toml:"tool_sets"`
	Temperature       float64  `toml:"temperature"`
	TopP              float64  `toml:"top_p"`
	MaxOutputTokens   int      `toml:"max_output_tokens"`
	MaxSteps          int      `toml:"max_steps"`
	MaxToolCalls      int      `toml:"max_tool_calls"`
	TimeoutSeconds    int      `toml:"timeout_seconds"`
	MaxConcurrency    int      `toml:"max_concurrency"`
}

func ApplyDefaults(c *Config) {
	if c.Providers == nil {
		c.Providers = map[string]ProviderConfig{}
	}
	if _, ok := c.Providers["localllm"]; !ok {
		c.Providers["localllm"] = ProviderConfig{Kind: "llama-cpp", DisplayName: "Local Llama", Runtime: "external.llm"}
	}
	if c.Profiles == nil {
		c.Profiles = map[string]Profile{}
	}
	if _, ok := c.Profiles["general"]; !ok {
		c.Profiles["general"] = Profile{Provider: "localllm", Temperature: 0.7, MaxOutputTokens: 2048}
	}
	if c.DefaultProfile == "" {
		c.DefaultProfile = "general"
	}
}

func (c Config) Validate() error {
	if _, ok := c.Profiles[c.DefaultProfile]; !ok {
		return fmt.Errorf("ai default profile %q is not configured", c.DefaultProfile)
	}
	for id, profile := range c.Profiles {
		if _, ok := c.Providers[profile.Provider]; !ok {
			return fmt.Errorf("ai profile %q references unknown provider %q", id, profile.Provider)
		}
	}
	return nil
}

func APIKey(p ProviderConfig) string {
	if p.APIKeyEnv == "" {
		return ""
	}
	return strings.TrimSpace(os.Getenv(p.APIKeyEnv))
}

func (c Config) ProviderAPIKey(id string) string {
	if p, ok := c.Providers[id]; ok {
		if key := APIKey(p); key != "" {
			return key
		}
	}
	return c.Secrets[id]
}

func LoadUserOverride(c *Config) error {
	path := strings.TrimSpace(os.Getenv("GNB_AI_CONFIG"))
	if path == "" {
		if dir, err := os.UserConfigDir(); err == nil {
			path = filepath.Join(dir, "gkNextEngine", "gnb-ai.toml")
		}
	}
	if path == "" {
		return nil
	}
	if _, err := os.Stat(path); err != nil {
		if os.IsNotExist(err) {
			return nil
		}
		return err
	}
	var wrapper struct {
		AI Config `toml:"ai"`
	}
	metadata, err := toml.DecodeFile(path, &wrapper)
	if err != nil {
		return fmt.Errorf("load AI override %s: %w", path, err)
	}
	override := wrapper.AI
	if !metadata.IsDefined("ai") {
		if _, err := toml.DecodeFile(path, &override); err != nil {
			return fmt.Errorf("load AI override %s: %w", path, err)
		}
	}
	merge(c, override)
	return nil
}

func LoadSecrets(c *Config) error {
	path := strings.TrimSpace(os.Getenv("GKNEXT_AI_SECRETS"))
	if path == "" {
		if runtime.GOOS == "windows" && os.Getenv("LOCALAPPDATA") != "" {
			path = filepath.Join(os.Getenv("LOCALAPPDATA"), "gkNextEngine", "ai_secrets.json")
		} else if dir, err := os.UserConfigDir(); err == nil {
			path = filepath.Join(dir, "gkNextEngine", "ai_secrets.json")
		}
	}
	if path == "" {
		return nil
	}
	raw, err := os.ReadFile(path)
	if err != nil {
		if os.IsNotExist(err) {
			return nil
		}
		return err
	}
	raw = bytes.TrimPrefix(raw, []byte{0xEF, 0xBB, 0xBF})
	var values map[string]json.RawMessage
	if err := json.Unmarshal(raw, &values); err != nil {
		return fmt.Errorf("load AI secrets %s: %w", path, err)
	}
	if c.Secrets == nil {
		c.Secrets = map[string]string{}
	}
	for id, value := range values {
		var entry struct {
			APIKey string `json:"apiKey"`
		}
		if json.Unmarshal(value, &entry) == nil && entry.APIKey != "" {
			c.Secrets[id] = entry.APIKey
		}
	}
	return nil
}

func merge(dst *Config, src Config) {
	if src.DefaultProfile != "" {
		dst.DefaultProfile = src.DefaultProfile
	}
	if dst.Providers == nil {
		dst.Providers = map[string]ProviderConfig{}
	}
	for id, p := range src.Providers {
		base := dst.Providers[id]
		if p.Kind != "" {
			base.Kind = p.Kind
		}
		if p.DisplayName != "" {
			base.DisplayName = p.DisplayName
		}
		if p.Endpoint != "" {
			base.Endpoint = p.Endpoint
		}
		if p.DefaultModel != "" {
			base.DefaultModel = p.DefaultModel
		}
		if p.Models != nil {
			base.Models = p.Models
		}
		if p.APIKeyEnv != "" {
			base.APIKeyEnv = p.APIKeyEnv
		}
		if p.Runtime != "" {
			base.Runtime = p.Runtime
		}
		dst.Providers[id] = base
	}
	if dst.Profiles == nil {
		dst.Profiles = map[string]Profile{}
	}
	for id, p := range src.Profiles {
		base := dst.Profiles[id]
		if p.Provider != "" {
			base.Provider = p.Provider
		}
		if p.Model != "" {
			base.Model = p.Model
		}
		if p.FallbackProviders != nil {
			base.FallbackProviders = p.FallbackProviders
		}
		if p.ToolSets != nil {
			base.ToolSets = p.ToolSets
		}
		if p.Temperature != 0 {
			base.Temperature = p.Temperature
		}
		if p.TopP != 0 {
			base.TopP = p.TopP
		}
		if p.MaxOutputTokens != 0 {
			base.MaxOutputTokens = p.MaxOutputTokens
		}
		if p.MaxSteps != 0 {
			base.MaxSteps = p.MaxSteps
		}
		if p.MaxToolCalls != 0 {
			base.MaxToolCalls = p.MaxToolCalls
		}
		if p.TimeoutSeconds != 0 {
			base.TimeoutSeconds = p.TimeoutSeconds
		}
		if p.MaxConcurrency != 0 {
			base.MaxConcurrency = p.MaxConcurrency
		}
		dst.Profiles[id] = base
	}
}

func Redact(input string, secrets ...string) string {
	for _, secret := range secrets {
		if secret != "" {
			input = strings.ReplaceAll(input, secret, "[REDACTED]")
		}
	}
	return input
}
