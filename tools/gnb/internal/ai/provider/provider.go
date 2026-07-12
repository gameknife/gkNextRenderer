package provider

import (
	"context"
	"fmt"
	"sort"
	"sync"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/ai/protocol"
)

type Capabilities struct {
	Streaming        bool `json:"streaming"`
	NativeTools      bool `json:"nativeTools"`
	StructuredOutput bool `json:"structuredOutput"`
	ReasoningControl bool `json:"reasoningControl"`
	JSONMode         bool `json:"jsonMode"`
}

type Descriptor struct {
	ID               string       `json:"id"`
	Kind             string       `json:"kind"`
	DisplayName      string       `json:"displayName"`
	Models           []string     `json:"models,omitempty"`
	DefaultModel     string       `json:"defaultModel,omitempty"`
	Capabilities     Capabilities `json:"capabilities"`
	Configured       bool         `json:"configured"`
	ConfiguredReason string       `json:"configuredReason,omitempty"`
	Available        bool         `json:"available"`
}

type Provider interface {
	Descriptor() Descriptor
	Chat(context.Context, protocol.ChatRequest, protocol.EventSink) (protocol.ChatResponse, error)
}

type Registry struct {
	mu        sync.RWMutex
	providers map[string]Provider
}

func NewRegistry() *Registry { return &Registry{providers: map[string]Provider{}} }

func (r *Registry) Register(p Provider) error {
	d := p.Descriptor()
	if d.ID == "" {
		return fmt.Errorf("provider id is required")
	}
	r.mu.Lock()
	defer r.mu.Unlock()
	if _, exists := r.providers[d.ID]; exists {
		return fmt.Errorf("provider %q already registered", d.ID)
	}
	r.providers[d.ID] = p
	return nil
}

func (r *Registry) Get(id string) (Provider, bool) {
	r.mu.RLock()
	defer r.mu.RUnlock()
	p, ok := r.providers[id]
	return p, ok
}

func (r *Registry) Descriptors() []Descriptor {
	r.mu.RLock()
	defer r.mu.RUnlock()
	out := make([]Descriptor, 0, len(r.providers))
	for _, p := range r.providers {
		out = append(out, p.Descriptor())
	}
	sort.Slice(out, func(i, j int) bool { return out[i].ID < out[j].ID })
	return out
}
