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

// ---- Terrain (SCAD Terrain M2) ----

func testTerrainKit(t *testing.T) *Catalog {
	t.Helper()
	doc := `{
	  "version": 1,
	  "kits": [
	    { "name": "kit_overhill", "file": "kit_overhill.scad", "scaleClass": "mid",
	      "modules": [ { "name": "oh_nature_pine", "ok": true },
	                   { "name": "oh_bldg_cabin", "ok": true },
	                   { "name": "oh_prop_bridge", "ok": true },
	                   { "name": "oh_prop_fence_log", "ok": true } ] }
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

func floatPtr(v float64) *float64 { return &v }

func terrainSpec() *Spec {
	return &Spec{
		Name: "valley", Fn: 12, Seed: 11, Kits: []string{"overhill"},
		Terrain: &Terrain{
			Size: [2]float64{240, 200},
			Features: []TerrainFeature{
				{Type: "mountain", At: [2]float64{-70, 62}, Radius: 46, Height: 24, Rugged: 0.6},
				{Type: "river", Pts: [][2]float64{{-52, 52}, {2, -52}}, Width: 7, Depth: 1.8},
				{Type: "pad", At: [2]float64{58, -24}, Size: [2]float64{34, 24}, Rot: 8},
			},
		},
	}
}

func TestComposeTerrainConstAndSnaps(t *testing.T) {
	spec := terrainSpec()
	spec.Placements = []Placement{
		{Call: Call{Module: "oh_bldg_cabin", Args: "seed = 1"}, At: [2]float64{50, -20}, Rot: 172},
		{Call: Call{Module: "oh_prop_bridge", Args: "L = 13"}, At: [2]float64{-1.5, -30.5},
			SnapAt: &[2]float64{-9, -30.5}},
		{Call: Call{Module: "oh_nature_pine"}, At: [2]float64{10, 10}, Snap: "none"},
	}
	spec.Scatters = []ScatterRule{{
		Region: [4]float64{-115, -95, 115, 95}, N: 90, Seed: 21,
		Where:    &ScatterWhere{HMin: floatPtr(0.3), HMax: floatPtr(13), SlopeMax: floatPtr(26), AvoidWater: floatPtr(3), Biome: []string{"grass", "grass_dark"}},
		Children: []Call{{Module: "oh_nature_pine", Args: "seed = $seed"}},
	}}
	spec.Alongs = []AlongRule{{
		Pts: [][2]float64{{-100, -42}, {-44, -34.5}}, Step: 4.6, Seed: 9,
		Children: []Call{{Module: "oh_prop_fence_log", Args: "len = 4.2"}},
	}}
	spec.Grids = []GridRule{{
		At: [2]float64{50, -20}, Cols: 2, Rows: 2, Cell: [2]float64{8, 8}, Seed: 3,
		Children: []Call{{Module: "oh_nature_pine"}},
	}}

	result := mustCompose(t, spec, testTerrainKit(t))
	for _, want := range []string{
		"use <../lib/kit_terrain.scad>",
		`TERR = ["gkterr1", [240, 200], [120, 100], 11, [0, 1, 0.5], undef, "temperate", [`,
		`["mountain", [-70, 62], 46, 24, 0.6],`,
		`["river", [[-52, 52], [2, -52]], 7, 1.8],`,
		`["pad", [58, -24], [34, 24], 8],`,
		"gk_terrain(TERR);",
		"ter_place(TERR, 50, -20) rotate([0, 0, 172]) oh_bldg_cabin(seed = 1);",
		"ter_place(TERR, -9, -30.5) translate([7.5, 0, 0]) oh_prop_bridge(L = 13);",
		"translate([10, 10, 0]) oh_nature_pine();",
		`ter_scatter(TERR, 21, 90, [-115, -95, 115, 95], [0.3, 13, 26, 3, ["grass", "grass_dark"]])`,
		"ter_along(TERR, [[-100, -42], [-44, -34.5]], step = 4.6, seed = 9)",
		"ter_snap(TERR, [50, -20]) oh_nature_pine();",
	} {
		if !strings.Contains(result.Source, want) {
			t.Errorf("missing %q in:\n%s", want, result.Source)
		}
	}
}

func TestComposeTerrainDeterministic(t *testing.T) {
	first := mustCompose(t, terrainSpec(), testTerrainKit(t))
	second := mustCompose(t, terrainSpec(), testTerrainKit(t))
	if first.Source != second.Source {
		t.Error("terrain compose output not byte-stable")
	}
}

func TestComposeTerrainValidation(t *testing.T) {
	catalog := testTerrainKit(t)

	ground := terrainSpec()
	ground.Ground = &Ground{Size: [2]float64{10, 10}}
	if _, err := Compose(ground, catalog, "spec.json", "x"); err == nil ||
		!strings.Contains(err.Error(), "mutually exclusive") {
		t.Errorf("want ground/terrain exclusivity error, got %v", err)
	}

	badFeature := terrainSpec()
	badFeature.Terrain.Features = append(badFeature.Terrain.Features, TerrainFeature{Type: "volcano"})
	if _, err := Compose(badFeature, catalog, "spec.json", "x"); err == nil ||
		!strings.Contains(err.Error(), "unknown feature type") {
		t.Errorf("want unknown feature error, got %v", err)
	}

	outOfDomain := terrainSpec()
	outOfDomain.Terrain.Features[1].Pts = [][2]float64{{-52, 52}, {0, -300}}
	if _, err := Compose(outOfDomain, catalog, "spec.json", "x"); err == nil ||
		!strings.Contains(err.Error(), "outside the terrain size") {
		t.Errorf("want out-of-domain error, got %v", err)
	}

	badCells := terrainSpec()
	badCells.Terrain.Cells = &[2]int{500, 100}
	if _, err := Compose(badCells, catalog, "spec.json", "x"); err == nil ||
		!strings.Contains(err.Error(), "4..256") {
		t.Errorf("want cells range error, got %v", err)
	}

	badSnap := terrainSpec()
	badSnap.Placements = []Placement{{Call: Call{Module: "oh_nature_pine"}, Snap: "float"}}
	if _, err := Compose(badSnap, catalog, "spec.json", "x"); err == nil ||
		!strings.Contains(err.Error(), "snap must be") {
		t.Errorf("want snap value error, got %v", err)
	}

	snapNoTerrain := &Spec{
		Name: "flat", Fn: 12, Kits: []string{"overhill"},
		Placements: []Placement{{Call: Call{Module: "oh_nature_pine"}, Snap: "terrain"}},
	}
	if _, err := Compose(snapNoTerrain, catalog, "spec.json", "x"); err == nil ||
		!strings.Contains(err.Error(), "requires a \"terrain\" section") {
		t.Errorf("want snap-needs-terrain error, got %v", err)
	}

	badWhere := terrainSpec()
	badWhere.Scatters = []ScatterRule{{
		Region: [4]float64{-10, -10, 10, 10}, N: 5,
		Where:    &ScatterWhere{Biome: []string{"lava"}},
		Children: []Call{{Module: "oh_nature_pine"}},
	}}
	if _, err := Compose(badWhere, catalog, "spec.json", "x"); err == nil ||
		!strings.Contains(err.Error(), "unknown biome") {
		t.Errorf("want biome error, got %v", err)
	}

	hRange := terrainSpec()
	hRange.Scatters = []ScatterRule{{
		Region: [4]float64{-10, -10, 10, 10}, N: 5,
		Where:    &ScatterWhere{HMin: floatPtr(5), HMax: floatPtr(1)},
		Children: []Call{{Module: "oh_nature_pine"}},
	}}
	if _, err := Compose(hRange, catalog, "spec.json", "x"); err == nil ||
		!strings.Contains(err.Error(), "can never match") {
		t.Errorf("want hMin/hMax error, got %v", err)
	}
}

func TestComposeTerrainPadRiverWarning(t *testing.T) {
	spec := terrainSpec()
	// Pad sits right on the river path -> the flatten dams the channel.
	spec.Terrain.Features[2].At = [2]float64{-25, 0}
	result := mustCompose(t, spec, testTerrainKit(t))
	found := false
	for _, warning := range result.Warnings {
		if strings.Contains(warning, "dam the channel") {
			found = true
		}
	}
	if !found {
		t.Errorf("want pad/river overlap warning, got %v", result.Warnings)
	}
}
