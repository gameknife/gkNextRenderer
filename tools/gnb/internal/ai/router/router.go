package router

import (
	"context"
	"errors"
	"fmt"

	aiconfig "github.com/gameknife/gknextrenderer/tools/gnb/internal/ai/config"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/ai/protocol"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/ai/provider"
)

type Overrides struct {
	Profile  string
	Provider string
	Model    string
}

type Route struct {
	ProfileID string
	Provider  provider.Provider
	Model     string
	Settings  aiconfig.Profile
}

type Router struct {
	config   aiconfig.Config
	registry *provider.Registry
}

func New(c aiconfig.Config, registry *provider.Registry) *Router {
	return &Router{config: c, registry: registry}
}

func (r *Router) Resolve(o Overrides) (Route, error) {
	profileID := o.Profile
	if profileID == "" {
		profileID = r.config.DefaultProfile
	}
	profile, ok := r.config.Profiles[profileID]
	if !ok {
		return Route{}, fmt.Errorf("unknown AI profile %q", profileID)
	}
	providerID := o.Provider
	if providerID == "" {
		providerID = profile.Provider
	}
	p, ok := r.registry.Get(providerID)
	if !ok {
		return Route{}, fmt.Errorf("unknown AI provider %q", providerID)
	}
	d := p.Descriptor()
	if !d.Configured {
		return Route{}, fmt.Errorf("provider %q is not configured: %s", providerID, d.ConfiguredReason)
	}
	if !d.Available {
		return Route{}, fmt.Errorf("provider %q is unavailable", providerID)
	}
	model := o.Model
	if model == "" {
		model = profile.Model
	}
	if model == "" {
		model = d.DefaultModel
	}
	return Route{ProfileID: profileID, Provider: p, Model: model, Settings: profile}, nil
}

func (r *Router) Chat(ctx context.Context, o Overrides, req protocol.ChatRequest, sink protocol.EventSink) (protocol.ChatResponse, Route, error) {
	route, err := r.Resolve(o)
	if err != nil {
		return protocol.ChatResponse{}, Route{}, err
	}
	requestForRoute := req
	requestForRoute.Model = route.Model
	if req.Temperature == 0 {
		requestForRoute.Temperature = route.Settings.Temperature
	}
	if req.TopP == 0 {
		requestForRoute.TopP = route.Settings.TopP
	}
	if req.MaxOutputTokens == 0 {
		requestForRoute.MaxOutputTokens = route.Settings.MaxOutputTokens
	}
	emitted := false
	wrappedSink := sink
	if sink != nil {
		wrappedSink = func(ctx context.Context, event protocol.Event) error { emitted = true; return sink(ctx, event) }
	}
	resp, err := route.Provider.Chat(ctx, requestForRoute, wrappedSink)
	if err == nil || emitted || len(resp.ToolCalls) > 0 || !canFallback(err) {
		return resp, route, err
	}
	for _, fallbackID := range route.Settings.FallbackProviders {
		fallback, ok := r.registry.Get(fallbackID)
		if !ok {
			continue
		}
		descriptor := fallback.Descriptor()
		if !descriptor.Configured || !descriptor.Available {
			continue
		}
		fallbackRoute := route
		fallbackRoute.Provider = fallback
		fallbackRoute.Model = descriptor.DefaultModel
		fallbackReq := requestForRoute
		fallbackReq.Model = fallbackRoute.Model
		resp, fallbackErr := fallback.Chat(ctx, fallbackReq, wrappedSink)
		if fallbackErr == nil {
			return resp, fallbackRoute, nil
		}
		err = fallbackErr
		if emitted || len(resp.ToolCalls) > 0 || !canFallback(fallbackErr) {
			return resp, fallbackRoute, fallbackErr
		}
	}
	return resp, route, err
}

func canFallback(err error) bool {
	var structured *protocol.Error
	if !errors.As(err, &structured) {
		return false
	}
	switch structured.Category {
	case protocol.ErrorUnavailable, protocol.ErrorTimeout, protocol.ErrorRateLimit, protocol.ErrorProvider:
		return structured.Retryable
	default:
		return false
	}
}
