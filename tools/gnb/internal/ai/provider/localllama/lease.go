package localllama

import (
	"context"
	"fmt"
	"sync"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/ai/protocol"
	baseconfig "github.com/gameknife/gknextrenderer/tools/gnb/internal/config"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/llm"
)

type serverControl interface {
	EnsureRunning(context.Context) (llm.ServerInfo, error)
	Status() llm.ServerInfo
}

type serverFactory func(baseconfig.LLMConfig) serverControl

type Lease struct {
	Model      string
	Generation uint64
	PID        int
	manager    *LeaseManager
	once       sync.Once
}

func (l *Lease) Release() { l.once.Do(func() { l.manager.release(l) }) }

type LeaseManager struct {
	mu          sync.Mutex
	changed     chan struct{}
	factory     serverFactory
	activeModel string
	generation  uint64
	pid         int
	leases      int
	preparing   bool
}

func newLeaseManager(factory serverFactory) *LeaseManager {
	return &LeaseManager{factory: factory, changed: make(chan struct{})}
}

func (m *LeaseManager) Acquire(ctx context.Context, cfg baseconfig.LLMConfig) (*Lease, llm.ServerInfo, error) {
	model := cfg.ActiveModel().ID
	if model == "" {
		return nil, llm.ServerInfo{}, fmt.Errorf("no LocalLlama model selected")
	}
	for {
		m.mu.Lock()
		if !m.preparing && (m.leases == 0 || m.activeModel == model) {
			m.preparing = true
			control := m.factory(cfg)
			m.mu.Unlock()
			info, err := control.EnsureRunning(ctx)
			m.mu.Lock()
			m.preparing = false
			close(m.changed)
			m.changed = make(chan struct{})
			if err != nil {
				m.mu.Unlock()
				return nil, info, err
			}
			if m.leases > 0 && m.activeModel != model {
				m.mu.Unlock()
				continue
			}
			if m.activeModel != model || m.pid != info.PID {
				m.generation++
				m.activeModel = model
				m.pid = info.PID
			}
			m.leases++
			lease := &Lease{Model: model, Generation: m.generation, PID: info.PID, manager: m}
			m.mu.Unlock()
			return lease, info, nil
		}
		changed := m.changed
		m.mu.Unlock()
		select {
		case <-ctx.Done():
			return nil, llm.ServerInfo{}, &protocol.Error{Category: protocol.ErrorModelBusy, Message: "LocalLlama model busy", Retryable: true}
		case <-changed:
		}
	}
}

func (m *LeaseManager) release(lease *Lease) {
	m.mu.Lock()
	defer m.mu.Unlock()
	if lease.Generation == m.generation && m.leases > 0 {
		m.leases--
	}
	if m.leases == 0 {
		close(m.changed)
		m.changed = make(chan struct{})
	}
}

func (m *LeaseManager) Validate(lease *Lease, info llm.ServerInfo) error {
	m.mu.Lock()
	defer m.mu.Unlock()
	if lease.Generation != m.generation || lease.PID != info.PID || !info.Running {
		return &protocol.Error{Category: protocol.ErrorUnavailable, Message: "LocalLlama server generation changed during request", Retryable: true}
	}
	return nil
}
