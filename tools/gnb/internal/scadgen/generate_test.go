package scadgen

import (
	"context"
	"os"
	"path/filepath"
	"strings"
	"testing"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/scadcompose"
)

const testCatalogJSON = `{
  "version": 1,
  "kits": [
    { "name": "kit_old_city", "file": "kit_old_city.scad", "scaleClass": "mid",
      "modules": [
        { "name": "oc_bldg_house", "category": "bldg", "params": "seed = 0, L = 9, D = 6.5",
          "footprint": [10.6, 8.1], "height": 5.58, "ok": true },
        { "name": "oc_prop_well", "category": "prop", "params": "",
          "footprint": [2.6, 2.6], "height": 2.9, "ok": true },
        { "name": "oc_part_text_cn", "category": "part", "params": "label, n = 2", "ok": false }
      ] }
  ]
}`

func writeTestCatalog(t *testing.T) string {
	t.Helper()
	path := filepath.Join(t.TempDir(), "catalog.json")
	if err := os.WriteFile(path, []byte(testCatalogJSON), 0o644); err != nil {
		t.Fatal(err)
	}
	return path
}

func TestBuildKitMenu(t *testing.T) {
	menu, err := BuildKitMenu(writeTestCatalog(t))
	if err != nil {
		t.Fatal(err)
	}
	for _, want := range []string{
		`kit "old_city" (scaleClass mid)`,
		"oc_bldg_house(seed = 0, L = 9, D = 6.5) 10.6x8.1 h5.58",
		"[bldg]",
	} {
		if !strings.Contains(menu, want) {
			t.Errorf("menu missing %q:\n%s", want, menu)
		}
	}
	if strings.Contains(menu, "oc_part_text_cn") {
		t.Errorf("ok=false module should be omitted from the menu:\n%s", menu)
	}
}

func TestExtractJSON(t *testing.T) {
	cases := map[string]string{
		"{\"a\": 1}":                                  `{"a": 1}`,
		"```json\n{\"a\": 1}\n```":                    `{"a": 1}`,
		"好的，这是 spec：\n```\n{\"a\": 1}\n```\n希望有帮助": `{"a": 1}`,
		"前言 {\"a\": {\"b\": 2}} 后记":                  `{"a": {"b": 2}}`,
	}
	for input, want := range cases {
		got, err := ExtractJSON(input)
		if err != nil {
			t.Errorf("ExtractJSON(%q) error: %v", input, err)
			continue
		}
		if got != want {
			t.Errorf("ExtractJSON(%q) = %q, want %q", input, got, want)
		}
	}
	if _, err := ExtractJSON("没有 JSON"); err == nil {
		t.Error("expected error for reply without JSON")
	}
}

func TestGenerateRepairLoop(t *testing.T) {
	catalog, err := scadcompose.LoadCatalog(writeTestCatalog(t))
	if err != nil {
		t.Fatal(err)
	}
	replies := []string{
		// Round 1: made-up module -> compose error fed back.
		`{"name": "bad", "kits": ["old_city"], "placements": [{"module": "oc_bldg_castle", "at": [0, 0]}]}`,
		// Round 2: fixed.
		`{"name": "good", "kits": ["old_city"], "placements": [{"module": "oc_bldg_house", "args": "seed = 1", "at": [0, 0]}]}`,
	}
	var transcripts [][]Message
	chat := func(ctx context.Context, messages []Message) (string, error) {
		transcripts = append(transcripts, append([]Message{}, messages...))
		return replies[len(transcripts)-1], nil
	}

	outcome, err := Generate(context.Background(), chat, catalog, "menu", "一个房子", Options{})
	if err != nil {
		t.Fatal(err)
	}
	if outcome.Rounds != 2 {
		t.Errorf("expected 2 rounds, got %d", outcome.Rounds)
	}
	if outcome.Spec.Name != "good" {
		t.Errorf("unexpected spec name %q", outcome.Spec.Name)
	}
	if !strings.Contains(outcome.Source, "oc_bldg_house(seed = 1);") {
		t.Errorf("composed source missing call:\n%s", outcome.Source)
	}
	// The repair round must carry the validation error back to the model.
	repair := transcripts[1][len(transcripts[1])-1]
	if repair.Role != "user" || !strings.Contains(repair.Content, "oc_bldg_castle") {
		t.Errorf("repair prompt should quote the failing module, got: %s", repair.Content)
	}
}

func TestGenerateGivesUp(t *testing.T) {
	catalog, err := scadcompose.LoadCatalog(writeTestCatalog(t))
	if err != nil {
		t.Fatal(err)
	}
	chat := func(ctx context.Context, messages []Message) (string, error) {
		return "永远不给 JSON", nil
	}
	if _, err := Generate(context.Background(), chat, catalog, "menu", "x", Options{MaxRepairs: 1}); err == nil ||
		!strings.Contains(err.Error(), "2 rounds") {
		t.Fatalf("expected give-up error after 2 rounds, got %v", err)
	}
}
