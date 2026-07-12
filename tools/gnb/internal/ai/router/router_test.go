package router

import (
	"context"
	"testing"

	aiconfig "github.com/gameknife/gknextrenderer/tools/gnb/internal/ai/config"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/ai/protocol"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/ai/provider"
)

type fakeProvider struct {
	descriptor provider.Descriptor
	seen       protocol.ChatRequest
	response   protocol.ChatResponse
	err        error
}

func (p *fakeProvider) Descriptor() provider.Descriptor { return p.descriptor }
func (p *fakeProvider) Chat(_ context.Context, req protocol.ChatRequest, _ protocol.EventSink) (protocol.ChatResponse, error) {
	p.seen = req
	if p.response.Content != "" || p.err != nil {
		return p.response, p.err
	}
	return protocol.ChatResponse{Content: "ok"}, nil
}

func TestFallbackRequiresExplicitProfileAndRetryablePreOutputError(t *testing.T) {
	registry := provider.NewRegistry()
	primary := &fakeProvider{descriptor: provider.Descriptor{ID: "primary", Configured: true, Available: true}, err: &protocol.Error{Category: protocol.ErrorUnavailable, Message: "down", Retryable: true}}
	fallback := &fakeProvider{descriptor: provider.Descriptor{ID: "fallback", Configured: true, Available: true, DefaultModel: "fallback-model"}, response: protocol.ChatResponse{Content: "recovered"}}
	_ = registry.Register(primary)
	_ = registry.Register(fallback)
	cfg := aiconfig.Config{DefaultProfile: "p", Profiles: map[string]aiconfig.Profile{"p": {Provider: "primary", FallbackProviders: []string{"fallback"}}}}
	resp, route, err := New(cfg, registry).Chat(context.Background(), Overrides{}, protocol.ChatRequest{}, nil)
	if err != nil || resp.Content != "recovered" || route.Provider.Descriptor().ID != "fallback" || fallback.seen.Model != "fallback-model" {
		t.Fatalf("resp=%#v route=%#v err=%v", resp, route, err)
	}
	primary.err = &protocol.Error{Category: protocol.ErrorAuthentication, Message: "bad key"}
	_, route, err = New(cfg, registry).Chat(context.Background(), Overrides{}, protocol.ChatRequest{}, nil)
	if err == nil || route.Provider.Descriptor().ID != "primary" {
		t.Fatalf("auth must not fallback: route=%#v err=%v", route, err)
	}
}

func TestResolvePrecedenceAndProfileDefaults(t *testing.T) {
	registry := provider.NewRegistry()
	local := &fakeProvider{descriptor: provider.Descriptor{ID: "local", Configured: true, Available: true, DefaultModel: "local-default"}}
	external := &fakeProvider{descriptor: provider.Descriptor{ID: "external", Configured: true, Available: true, DefaultModel: "external-default"}}
	_ = registry.Register(local)
	_ = registry.Register(external)
	cfg := aiconfig.Config{DefaultProfile: "general", Providers: map[string]aiconfig.ProviderConfig{"local": {}, "external": {}}, Profiles: map[string]aiconfig.Profile{"general": {Provider: "local", Model: "profile-model", Temperature: .4, MaxOutputTokens: 99}}}
	r := New(cfg, registry)
	_, route, err := r.Chat(context.Background(), Overrides{Provider: "external", Model: "override-model"}, protocol.ChatRequest{}, nil)
	if err != nil {
		t.Fatal(err)
	}
	if route.Provider.Descriptor().ID != "external" || external.seen.Model != "override-model" || external.seen.Temperature != .4 || external.seen.MaxOutputTokens != 99 {
		t.Fatalf("route=%#v request=%#v", route, external.seen)
	}
}
