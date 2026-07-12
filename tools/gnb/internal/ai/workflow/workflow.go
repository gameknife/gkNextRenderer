package workflow

import (
	"context"
	"encoding/json"
	"fmt"
	"sort"
	"sync"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/ai/protocol"
)

type Handler func(context.Context, json.RawMessage, protocol.EventSink) (json.RawMessage, error)
type Registry struct {
	mu       sync.RWMutex
	handlers map[string]Handler
}

func NewRegistry() *Registry { return &Registry{handlers: map[string]Handler{}} }
func (r *Registry) Register(name string, handler Handler) error {
	if name == "" || handler == nil {
		return fmt.Errorf("workflow name and handler are required")
	}
	r.mu.Lock()
	defer r.mu.Unlock()
	if _, exists := r.handlers[name]; exists {
		return fmt.Errorf("workflow %q already registered", name)
	}
	r.handlers[name] = handler
	return nil
}
func (r *Registry) Run(ctx context.Context, name string, input any, sink protocol.EventSink) (json.RawMessage, error) {
	r.mu.RLock()
	handler, ok := r.handlers[name]
	r.mu.RUnlock()
	if !ok {
		return nil, fmt.Errorf("unknown workflow %q", name)
	}
	raw, err := json.Marshal(input)
	if err != nil {
		return nil, err
	}
	return handler(ctx, raw, sink)
}
func (r *Registry) Names() []string {
	r.mu.RLock()
	defer r.mu.RUnlock()
	out := make([]string, 0, len(r.handlers))
	for name := range r.handlers {
		out = append(out, name)
	}
	sort.Strings(out)
	return out
}
