package localllama

import (
	"context"
	"errors"
	"sync"
	"testing"
	"time"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/ai/protocol"
	baseconfig "github.com/gameknife/gknextrenderer/tools/gnb/internal/config"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/llm"
)

type fakeServerState struct {
	mu     sync.Mutex
	model  string
	pid    int
	starts int
}
type fakeServer struct {
	state *fakeServerState
	model string
}

func (s *fakeServer) EnsureRunning(context.Context) (llm.ServerInfo, error) {
	s.state.mu.Lock()
	defer s.state.mu.Unlock()
	if s.state.model != s.model {
		s.state.pid++
		s.state.model = s.model
		s.state.starts++
	}
	return llm.ServerInfo{PID: s.state.pid, Model: s.model, Running: true}, nil
}
func (s *fakeServer) Status() llm.ServerInfo {
	s.state.mu.Lock()
	defer s.state.mu.Unlock()
	return llm.ServerInfo{PID: s.state.pid, Model: s.state.model, Running: true}
}
func leaseConfig(model string) baseconfig.LLMConfig {
	return baseconfig.LLMConfig{Active: model, Models: []baseconfig.ModelConfig{{ID: model}}}
}

func TestLeaseSameModelSharesGenerationAndDifferentModelWaits(t *testing.T) {
	state := &fakeServerState{pid: 40}
	manager := newLeaseManager(func(c baseconfig.LLMConfig) serverControl {
		return &fakeServer{state: state, model: c.ActiveModel().ID}
	})
	one, _, err := manager.Acquire(context.Background(), leaseConfig("a"))
	if err != nil {
		t.Fatal(err)
	}
	two, _, err := manager.Acquire(context.Background(), leaseConfig("a"))
	if err != nil {
		t.Fatal(err)
	}
	if one.Generation != two.Generation || state.starts != 1 {
		t.Fatalf("same model leases: %#v %#v starts=%d", one, two, state.starts)
	}
	acquired := make(chan *Lease, 1)
	go func() { lease, _, _ := manager.Acquire(context.Background(), leaseConfig("b")); acquired <- lease }()
	select {
	case <-acquired:
		t.Fatal("different model acquired while old leases active")
	case <-time.After(20 * time.Millisecond):
	}
	one.Release()
	two.Release()
	select {
	case lease := <-acquired:
		if lease.Model != "b" || lease.Generation == one.Generation {
			t.Fatalf("bad switched lease: %#v", lease)
		}
		lease.Release()
	case <-time.After(time.Second):
		t.Fatal("different model did not acquire after release")
	}
}

func TestLeaseModelBusyOnDeadline(t *testing.T) {
	state := &fakeServerState{pid: 1}
	manager := newLeaseManager(func(c baseconfig.LLMConfig) serverControl {
		return &fakeServer{state: state, model: c.ActiveModel().ID}
	})
	lease, _, _ := manager.Acquire(context.Background(), leaseConfig("a"))
	defer lease.Release()
	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Millisecond)
	defer cancel()
	_, _, err := manager.Acquire(ctx, leaseConfig("b"))
	var structured *protocol.Error
	if !errors.As(err, &structured) || structured.Category != protocol.ErrorModelBusy {
		t.Fatalf("expected model_busy, got %v", err)
	}
}
