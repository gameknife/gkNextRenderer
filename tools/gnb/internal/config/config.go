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
}

type ExternalURLConfig struct {
	When string `toml:"when"`
	URL  string `toml:"url"`
}

type TSCConfig struct {
	Version    string `toml:"version"`
	Windows    string `toml:"windows"`
	Linux      string `toml:"linux"`
	MacOSArm64 string `toml:"macos_arm64"`
}

type PlatformURLs struct {
	Windows    string `toml:"windows"`
	Linux      string `toml:"linux"`
	MacOSArm64 string `toml:"macos_arm64"`
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
	return cfg, nil
}

func BinCacheKey(repoRoot string, cfg Config, osName string) string {
	manifest := filepath.Join(repoRoot, "vcpkg.json")
	data, _ := os.ReadFile(manifest)
	sum := sha256.Sum256(data)
	return fmt.Sprintf("%s-vcpkg-%s-%s", osName, cfg.Vcpkg.Ref, hex.EncodeToString(sum[:])[:12])
}
