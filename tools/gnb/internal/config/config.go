package config

import (
	"crypto/sha256"
	"encoding/hex"
	"fmt"
	"os"
	"path/filepath"

	"github.com/BurntSushi/toml"
)

type Config struct {
	GNB      GNBConfig      `toml:"gnb"`
	Vcpkg    VcpkgConfig    `toml:"vcpkg"`
	External ExternalConfig `toml:"external"`
	Paks     PaksConfig     `toml:"paks"`
	Targets  TargetsConfig  `toml:"targets"`
}

type GNBConfig struct {
	MinVersion string `toml:"min_version"`
}

type VcpkgConfig struct {
	Ref         string `toml:"ref"`
	BinaryCache string `toml:"binary_cache"`
	Root        string `toml:"root"`
}

type ExternalConfig struct {
	Streamline ExternalURLConfig `toml:"streamline"`
	TSC        TSCConfig         `toml:"tsc"`
	MoltenVK   ExternalURLConfig `toml:"moltenvk"`
	Slang      PlatformURLs      `toml:"slang"`
	VulkanSDK  VulkanSDKConfig   `toml:"vulkansdk"`
	LLM        LLMConfig         `toml:"llm"`
}

type LLMConfig struct {
	Llama  LlamaConfig   `toml:"llama"`
	Active string        `toml:"active"`
	Models []ModelConfig `toml:"models"`
	Server ServerConfig  `toml:"server"`
}

// ActiveModel returns the model selected by LLMConfig.Active. If Active is
// empty or does not match any entry, the first configured model is returned.
// Callers should treat a zero-value result as "no models configured".
func (c LLMConfig) ActiveModel() ModelConfig {
	if m, ok := c.FindModel(c.Active); ok {
		return m
	}
	if len(c.Models) > 0 {
		return c.Models[0]
	}
	return ModelConfig{}
}

// FindModel looks up a model by id. Returns the model and true on match.
func (c LLMConfig) FindModel(id string) (ModelConfig, bool) {
	if id == "" {
		return ModelConfig{}, false
	}
	for _, m := range c.Models {
		if m.ID == id {
			return m, true
		}
	}
	return ModelConfig{}, false
}

type LlamaConfig struct {
	Version           string `toml:"version"`
	WindowsVulkan     string `toml:"windows_vulkan"`
	WindowsCPU        string `toml:"windows_cpu"`
	LinuxVulkan       string `toml:"linux_vulkan"`
	LinuxCPU          string `toml:"linux_cpu"`
	MacOSArm64        string `toml:"macos_arm64"`
}

type ModelConfig struct {
	ID       string `toml:"id"`
	File     string `toml:"file"`
	URL      string `toml:"url"`
	ContextN int    `toml:"context"`
}

type ServerConfig struct {
	Host       string `toml:"host"`
	Port       int    `toml:"port"`
	GPULayers  int    `toml:"gpu_layers"`
	IdleSecs   int    `toml:"idle_seconds"`
}

type ExternalURLConfig struct {
	When string `toml:"when"`
	URL  string `toml:"url"`
}

type TSCConfig struct {
	Version    string `toml:"version"`
	Windows    string `toml:"windows"`
	Linux      string `toml:"linux"`
	MacOSAMD64 string `toml:"macos_amd64"`
	MacOSArm64 string `toml:"macos_arm64"`
}

type PlatformURLs struct {
	Windows    string `toml:"windows"`
	Linux      string `toml:"linux"`
	MacOSAMD64 string `toml:"macos_amd64"`
	MacOSArm64 string `toml:"macos_arm64"`
}

type VulkanSDKConfig struct {
	Version string `toml:"version"`
	Root    string `toml:"root"`
}

type PaksConfig struct {
	Repo       string     `toml:"repo"`
	ReleaseTag string     `toml:"release_tag"`
	Assets     []PakAsset `toml:"assets"`
}

type PakAsset struct {
	ID   string `toml:"id"`
	Name string `toml:"name"`
	Dest string `toml:"dest"`
}

type TargetsConfig struct {
	Default string   `toml:"default"`
	All     []string `toml:"all"`
}

func FindRepoRoot(start string) (string, error) {
	dir, err := filepath.Abs(start)
	if err != nil {
		return "", err
	}
	for {
		if _, err := os.Stat(filepath.Join(dir, "gnb.toml")); err == nil {
			return dir, nil
		}
		if _, err := os.Stat(filepath.Join(dir, ".git")); err == nil {
			return dir, nil
		}
		parent := filepath.Dir(dir)
		if parent == dir {
			return "", fmt.Errorf("could not find repository root from %s", start)
		}
		dir = parent
	}
}

func Load(repoRoot string) (Config, error) {
	cfg := Config{}
	_, err := toml.DecodeFile(filepath.Join(repoRoot, "gnb.toml"), &cfg)
	if err != nil {
		return cfg, err
	}
	if cfg.Targets.Default == "" {
		cfg.Targets.Default = "gkNextRenderer"
	}
	if cfg.External.VulkanSDK.Version == "" {
		cfg.External.VulkanSDK.Version = "1.4.341.1"
	}
	if cfg.External.VulkanSDK.Root == "" {
		cfg.External.VulkanSDK.Root = "external/VulkanSDK"
	}
	applyLLMDefaults(&cfg.External.LLM)
	return cfg, nil
}

func applyLLMDefaults(c *LLMConfig) {
	if c.Llama.Version == "" {
		c.Llama.Version = "b6000"
	}
	if len(c.Models) == 0 {
		c.Models = []ModelConfig{{
			ID:       "gemma-4-E4B-it-Q4_K_M",
			File:     "gemma-4-E4B-it-Q4_K_M.gguf",
			URL:      "https://huggingface.co/unsloth/gemma-4-E4B-it-GGUF/resolve/main/gemma-4-E4B-it-Q4_K_M.gguf",
			ContextN: 8192,
		}}
	}
	for i := range c.Models {
		m := &c.Models[i]
		if m.File == "" && m.ID != "" {
			m.File = m.ID + ".gguf"
		}
		if m.ContextN == 0 {
			m.ContextN = 8192
		}
	}
	if c.Active == "" && len(c.Models) > 0 {
		c.Active = c.Models[0].ID
	}
	if c.Server.Host == "" {
		c.Server.Host = "127.0.0.1"
	}
	if c.Server.Port == 0 {
		c.Server.Port = 8765
	}
	if c.Server.IdleSecs == 0 {
		c.Server.IdleSecs = 600
	}
}

func BinCacheKey(repoRoot string, cfg Config, osName string) string {
	manifest := filepath.Join(repoRoot, "vcpkg.json")
	data, _ := os.ReadFile(manifest)
	sum := sha256.Sum256(data)
	return fmt.Sprintf("%s-vcpkg-%s-%s", osName, cfg.Vcpkg.Ref, hex.EncodeToString(sum[:])[:12])
}
