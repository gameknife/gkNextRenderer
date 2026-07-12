package tool

import (
	"context"
	"encoding/json"
	"fmt"
	"sort"
	"sync"
	"time"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/ai/protocol"
)

type Context struct {
	RunID  string
	Step   int
	CallID string
}
type Handler func(context.Context, json.RawMessage, Context) (string, error)
type Entry struct {
	Descriptor protocol.ToolDescriptor
	Handler    Handler
	Timeout    time.Duration
}

type Registry struct {
	mu      sync.RWMutex
	entries map[string]Entry
}

func NewRegistry() *Registry { return &Registry{entries: map[string]Entry{}} }
func (r *Registry) Register(entry Entry) error {
	if entry.Descriptor.Name == "" {
		return fmt.Errorf("tool name is required")
	}
	if entry.Handler == nil {
		return fmt.Errorf("tool %q handler is required", entry.Descriptor.Name)
	}
	r.mu.Lock()
	defer r.mu.Unlock()
	if _, exists := r.entries[entry.Descriptor.Name]; exists {
		return fmt.Errorf("tool %q already registered", entry.Descriptor.Name)
	}
	r.entries[entry.Descriptor.Name] = entry
	return nil
}
func (r *Registry) Get(name string) (Entry, bool) {
	r.mu.RLock()
	defer r.mu.RUnlock()
	entry, ok := r.entries[name]
	return entry, ok
}
func (r *Registry) Descriptors() []protocol.ToolDescriptor {
	r.mu.RLock()
	defer r.mu.RUnlock()
	out := make([]protocol.ToolDescriptor, 0, len(r.entries))
	for _, entry := range r.entries {
		out = append(out, entry.Descriptor)
	}
	sort.Slice(out, func(i, j int) bool { return out[i].Name < out[j].Name })
	return out
}
func (r *Registry) Execute(ctx context.Context, call protocol.ToolCall, runID string, step int, defaultTimeout time.Duration) (result string, err error) {
	defer func() {
		if recovered := recover(); recovered != nil {
			result = ""
			err = fmt.Errorf("tool %s panicked: %v", call.Name, recovered)
		}
	}()
	entry, ok := r.Get(call.Name)
	if !ok {
		return "", fmt.Errorf("unknown tool %q", call.Name)
	}
	timeout := entry.Timeout
	if timeout == 0 {
		timeout = defaultTimeout
	}
	if timeout > 0 {
		var cancel context.CancelFunc
		ctx, cancel = context.WithTimeout(ctx, timeout)
		defer cancel()
	}
	arguments := json.RawMessage(call.Arguments)
	if len(arguments) == 0 {
		arguments = json.RawMessage(`{}`)
	}
	if !json.Valid(arguments) {
		return "", fmt.Errorf("invalid arguments for %s: expected JSON", call.Name)
	}
	return entry.Handler(ctx, arguments, Context{RunID: runID, Step: step, CallID: call.ID})
}
