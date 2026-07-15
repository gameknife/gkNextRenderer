package session

import (
	"context"
	"testing"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/ai/protocol"
)

func TestSessionTrimCancelAndTrace(t *testing.T) {
	store := NewStore(2)
	session := store.Create("general", "p", "m")
	_ = store.Append(session.ID, protocol.Message{Content: "1"}, protocol.Message{Content: "2"}, protocol.Message{Content: "3"})
	got, _ := store.Get(session.ID)
	if len(got.Messages) != 2 || got.Messages[0].Content != "2" {
		t.Fatalf("messages=%#v", got.Messages)
	}
	ctx, err := store.StartRun(session.ID, "r", context.Background())
	if err != nil {
		t.Fatal(err)
	}
	if !store.CancelRun("r") {
		t.Fatal("cancel missing")
	}
	<-ctx.Done()
	trace := Trace{RunID: "r", Status: "cancelled"}
	store.FinishRun("r", trace)
	stored, ok := store.Trace("r")
	if !ok || stored.Status != "cancelled" {
		t.Fatalf("trace=%#v", stored)
	}
}
