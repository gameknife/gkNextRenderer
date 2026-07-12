package scadcompose

import (
	"encoding/json"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func testCatalog(t *testing.T) *Catalog {
	t.Helper()
	doc := `{
	  "version": 1,
	  "kits": [
	    { "name": "kit_old_city", "file": "kit_old_city.scad", "scaleClass": "mid",
	      "modules": [ { "name": "oc_bldg_house", "ok": true },
	                   { "name": "oc_prop_well", "ok": true },
	                   { "name": "oc_part_text_cn", "ok": false } ] },
	    { "name": "kit_city_hd", "file": "kit_city_hd.scad", "scaleClass": "city",
	      "modules": [ { "name": "hc_prop_lamp", "ok": true },
	                   { "name": "hc_veh_car", "ok": true } ] },
	    { "name": "kit_office", "file": "kit_office.scad", "scaleClass": "human",
	      "modules": [ { "name": "of_furn_sofa", "ok": true } ] }
	  ]
	}`
	path := filepath.Join(t.TempDir(), "catalog.json")
	if err := os.WriteFile(path, []byte(doc), 0o644); err != nil {
		t.Fatal(err)
	}
	catalog, err := LoadCatalog(path)
	if err != nil {
		t.Fatal(err)
	}
	return catalog
}

func mustCompose(t *testing.T, spec *Spec, catalog *Catalog) *Result {
	t.Helper()
	result, err := Compose(spec, catalog, "spec.json", "abcdef123456")
	if err != nil {
		t.Fatalf("compose failed: %v", err)
	}
	return result
}

func TestComposePlacementOnly(t *testing.T) {
	spec := &Spec{
		Name: "demo", Fn: 12, Kits: []string{"old_city"},
		Placements: []Placement{{Call: Call{Module: "oc_bldg_house", Args: "seed = 3"}, At: [2]float64{5, -8}, Rot: 90}},
	}
	result := mustCompose(t, spec, testCatalog(t))
	for _, want := range []string{
		"use <../lib/kit_old_city.scad>",
		"translate([5, -8, 0]) rotate([0, 0, 90]) oc_bldg_house(seed = 3);",
	} {
		if !strings.Contains(result.Source, want) {
			t.Errorf("missing %q in:\n%s", want, result.Source)
		}
	}
	if strings.Contains(result.Source, "kit_layout") {
		t.Errorf("placement-only spec should not use kit_layout:\n%s", result.Source)
	}
}

func TestComposeUnknownModule(t *testing.T) {
	spec := &Spec{Name: "demo", Kits: []string{"old_city"},
		Placements: []Placement{{Call: Call{Module: "oc_nope"}}}}
	if _, err := Compose(spec, testCatalog(t), "s", "h"); err == nil || !strings.Contains(err.Error(), "not found") {
		t.Fatalf("expected not-found error, got %v", err)
	}
}

func TestComposeUndeclaredKit(t *testing.T) {
	spec := &Spec{Name: "demo", Kits: []string{"old_city"},
		Placements: []Placement{{Call: Call{Module: "hc_prop_lamp"}}}}
	if _, err := Compose(spec, testCatalog(t), "s", "h"); err == nil || !strings.Contains(err.Error(), "add it") {
		t.Fatalf("expected undeclared-kit error, got %v", err)
	}
}

func TestComposeBlockGrid(t *testing.T) {
	spec := &Spec{
		Name: "mini", Kits: []string{"old_city", "city_hd"},
		BlockTypes: map[string][]Call{
			"res":  {{Module: "oc_bldg_house", Args: "seed = $seed"}},
			"lamp": {{Module: "hc_prop_lamp"}, {Module: "hc_veh_car"}},
		},
		BlockGrids: []BlockGrid{{Cell: [2]float64{56, 50}, Seed: 5,
			Layout: [][]string{{"res", "lamp"}, {"lamp", "res"}}}},
	}
	result := mustCompose(t, spec, testCatalog(t))
	for _, want := range []string{
		"module mini_block(t, seed)",
		"if (t == 1) { oc_bldg_house(seed = $seed); }",
		"if (t == 0) { lay_pick(seed) { hc_prop_lamp(); hc_veh_car(); } }",
		"MINI_L1 = [",
		"    [1, 0],",
		"lay_grid(2, 2, 56, 50, seed = 5)",
		"mini_block(MINI_L1[$row][$col], $seed);",
		"use <../lib/kit_layout.scad>",
	} {
		if !strings.Contains(result.Source, want) {
			t.Errorf("missing %q in:\n%s", want, result.Source)
		}
	}
}

func TestComposeRaggedLayout(t *testing.T) {
	spec := &Spec{
		Name: "bad", Kits: []string{"old_city"},
		BlockTypes: map[string][]Call{"res": {{Module: "oc_bldg_house"}}},
		BlockGrids: []BlockGrid{{Cell: [2]float64{10, 10},
			Layout: [][]string{{"res", "res"}, {"res"}}}},
	}
	if _, err := Compose(spec, testCatalog(t), "s", "h"); err == nil || !strings.Contains(err.Error(), "row 1") {
		t.Fatalf("expected ragged-layout error, got %v", err)
	}
}

func TestComposeUnknownBlockType(t *testing.T) {
	spec := &Spec{
		Name: "bad", Kits: []string{"old_city"},
		BlockTypes: map[string][]Call{"res": {{Module: "oc_bldg_house"}}},
		BlockGrids: []BlockGrid{{Cell: [2]float64{10, 10}, Layout: [][]string{{"nope"}}}},
	}
	if _, err := Compose(spec, testCatalog(t), "s", "h"); err == nil || !strings.Contains(err.Error(), "unknown blockType") {
		t.Fatalf("expected unknown-blockType error, got %v", err)
	}
}

func TestComposeScaleClassWarning(t *testing.T) {
	spec := &Spec{
		Name: "mix", Kits: []string{"city_hd", "office"},
		Placements: []Placement{
			{Call: Call{Module: "hc_prop_lamp"}},
			{Call: Call{Module: "of_furn_sofa"}},
		},
	}
	result := mustCompose(t, spec, testCatalog(t))
	found := false
	for _, warning := range result.Warnings {
		if strings.Contains(warning, "scaleClass") {
			found = true
		}
	}
	if !found {
		t.Errorf("expected scaleClass warning, got %v", result.Warnings)
	}
}

func TestComposeNoDefaultGeometryWarning(t *testing.T) {
	spec := &Spec{
		Name: "warn", Kits: []string{"old_city"},
		Placements: []Placement{{Call: Call{Module: "oc_part_text_cn"}}},
	}
	result := mustCompose(t, spec, testCatalog(t))
	if len(result.Warnings) == 0 || !strings.Contains(result.Warnings[0], "needs args") {
		t.Errorf("expected needs-args warning, got %v", result.Warnings)
	}
}

func TestComposeGridWithJitterAndScatter(t *testing.T) {
	rotOff := false
	spec := &Spec{
		Name: "town", Kits: []string{"old_city"},
		Grids: []GridRule{{At: [2]float64{-42, 36}, Cols: 4, Rows: 3, Cell: [2]float64{16, 14}, Seed: 7,
			Jitter:   &Jitter{Dx: 1.2, Dy: 1, Rot: 12},
			Children: []Call{{Module: "oc_bldg_house", Args: "seed = $seed"}}}},
		Scatters: []ScatterRule{{Region: [4]float64{-80, -58, 80, -22}, N: 16, Seed: 11, Rot: &rotOff,
			Children: []Call{{Module: "oc_prop_well"}}}},
	}
	result := mustCompose(t, spec, testCatalog(t))
	for _, want := range []string{
		"translate([-42, 36, 0])",
		"lay_grid(4, 3, 16, 14, seed = 7)",
		"lay_jitter($seed, 1.2, 1, 12) oc_bldg_house(seed = $seed);",
		"lay_scatter(16, -80, 80, -58, -22, seed = 11, rot = false)",
	} {
		if !strings.Contains(result.Source, want) {
			t.Errorf("missing %q in:\n%s", want, result.Source)
		}
	}
}

func TestComposeDeterministic(t *testing.T) {
	spec := &Spec{
		Name: "det", Kits: []string{"old_city", "city_hd"},
		BlockTypes: map[string][]Call{
			"a": {{Module: "oc_bldg_house"}}, "b": {{Module: "hc_prop_lamp"}},
			"c": {{Module: "oc_prop_well"}},
		},
		BlockGrids: []BlockGrid{{Cell: [2]float64{10, 10}, Layout: [][]string{{"a", "b", "c"}}}},
	}
	catalog := testCatalog(t)
	first := mustCompose(t, spec, catalog)
	second := mustCompose(t, spec, catalog)
	if first.Source != second.Source {
		t.Error("compose output is not deterministic")
	}
}

func TestCallUnmarshalForms(t *testing.T) {
	var calls []Call
	if err := json.Unmarshal([]byte(`["oc_prop_well", {"module": "oc_bldg_house", "args": "seed = 1"}]`), &calls); err != nil {
		t.Fatal(err)
	}
	if calls[0].Module != "oc_prop_well" || calls[1].Args != "seed = 1" {
		t.Errorf("unexpected calls: %+v", calls)
	}
}

func TestCallUnmarshalDoubleEncoded(t *testing.T) {
	// LLM artifact: the object form wrapped in a string must be unwrapped.
	var calls []Call
	if err := json.Unmarshal([]byte(`["{\"module\": \"oc_bldg_house\", \"args\": \"seed = $seed\"}"]`), &calls); err != nil {
		t.Fatal(err)
	}
	if calls[0].Module != "oc_bldg_house" || calls[0].Args != "seed = $seed" {
		t.Errorf("double-encoded call not unwrapped: %+v", calls)
	}
}

func TestLoadSpecRejectsUnknownFields(t *testing.T) {
	path := filepath.Join(t.TempDir(), "bad.json")
	if err := os.WriteFile(path, []byte(`{"name": "x", "kits": ["a"], "typo": 1}`), 0o644); err != nil {
		t.Fatal(err)
	}
	if _, err := LoadSpec(path); err == nil || !strings.Contains(err.Error(), "typo") {
		t.Fatalf("expected unknown-field error, got %v", err)
	}
}
