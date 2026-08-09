package config

import (
	"os"
	"path/filepath"
	"testing"
)

func TestUserOverrideSecretsAndEnvironmentPrecedence(t *testing.T) {
	dir := t.TempDir()
	overridePath := filepath.Join(dir, "gnb-ai.toml")
	if err := os.WriteFile(overridePath, []byte(`[ai.providers.corp]
kind = "openai-compatible"
endpoint = "https://example.invalid/v1"
default_model = "fixture"
api_key_env = "CORP_AI_KEY"
[ai.profiles.general]
provider = "corp"
max_output_tokens = 123
`), 0o644); err != nil {
		t.Fatal(err)
	}
	secretsPath := filepath.Join(dir, "secrets.json")
	if err := os.WriteFile(secretsPath, []byte(`{"corp":{"apiKey":"file-secret"}}`), 0o600); err != nil {
		t.Fatal(err)
	}
	t.Setenv("GNB_AI_CONFIG", overridePath)
	t.Setenv("GKNEXT_AI_SECRETS", secretsPath)
	t.Setenv("CORP_AI_KEY", "env-secret")
	c := Config{}
	ApplyDefaults(&c)
	if err := LoadUserOverride(&c); err != nil {
		t.Fatal(err)
	}
	if err := LoadSecrets(&c); err != nil {
		t.Fatal(err)
	}
	if c.Profiles["general"].Provider != "corp" || c.Profiles["general"].MaxOutputTokens != 123 {
		t.Fatalf("profile not merged: %#v", c.Profiles["general"])
	}
	key := c.ProviderAPIKey("corp")
	if key != "env-secret" {
		t.Fatalf("key=%q, want env precedence", key)
	}
	if got := Redact("token env-secret", key); got != "token [REDACTED]" {
		t.Fatalf("redact=%q", got)
	}
}
