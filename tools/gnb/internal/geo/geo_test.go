package geo

import (
	"math"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

// Everything here is fixture-driven: the test suite never touches the network.

func TestTileNormalizeRejectsUnusableRequests(t *testing.T) {
	cases := []struct {
		name string
		tile Tile
		want string
	}{
		{"no name", Tile{Lat: 22, Lon: 114}, "--name"},
		{"outside SRTM", Tile{Name: "x", Lat: 78, Lon: 15}, "SRTM coverage"},
		// 180^2 is where the engine gives up on the terrain physics mesh.
		{"too many cells", Tile{Name: "x", Lat: 22, Lon: 114, Cells: 200}, "physics mesh"},
	}
	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			err := tc.tile.Normalize()
			if err == nil || !strings.Contains(err.Error(), tc.want) {
				t.Fatalf("want error containing %q, got %v", tc.want, err)
			}
		})
	}

	tile := Tile{Name: "ok", Lat: 22.2855, Lon: 114.158}
	if err := tile.Normalize(); err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	if tile.SizeM != 1000 || tile.Cells != DefaultCells || tile.Profile != "default" {
		t.Fatalf("defaults not applied: %+v", tile)
	}
}

func TestProjRoundTripAndScale(t *testing.T) {
	p := NewProj(22.2855, 114.1580)
	x, y := p.Forward(22.2855, 114.1580)
	if math.Abs(x) > 1e-9 || math.Abs(y) > 1e-9 {
		t.Fatalf("origin should project to (0,0), got (%g, %g)", x, y)
	}
	// 500m north and east of the centre must come back to itself.
	lat, lon := p.Inverse(500, 500)
	bx, by := p.Forward(lat, lon)
	if math.Abs(bx-500) > 1e-6 || math.Abs(by-500) > 1e-6 {
		t.Fatalf("round trip drifted: (%g, %g)", bx, by)
	}
	// One degree of latitude is ~110.6km at this latitude.
	_, y1 := p.Forward(23.2855, 114.1580)
	if y1 < 110000 || y1 > 111500 {
		t.Fatalf("degree of latitude = %.0fm, expected ~110600", y1)
	}
}

func TestBBoxCoversTheRequestedSquare(t *testing.T) {
	tile := Tile{Name: "t", Lat: 22.2855, Lon: 114.158, SizeM: 1000}
	if err := tile.Normalize(); err != nil {
		t.Fatal(err)
	}
	b := tile.BBox(0)
	p := NewProj(tile.Lat, tile.Lon)
	_, north := p.Forward(b.North, tile.Lon)
	east, _ := p.Forward(tile.Lat, b.East)
	if math.Abs(north-500) > 1 || math.Abs(east-500) > 1 {
		t.Fatalf("bbox half-extents are %.1f x %.1f m, want 500", east, north)
	}
	if !b.Contains(tile.Lat, tile.Lon) {
		t.Fatal("bbox must contain its own centre")
	}
}

func TestSrtmTileName(t *testing.T) {
	cases := map[string][2]float64{
		"N22E114": {22.2855, 114.158},
		"N00E000": {0.5, 0.5},
		"S01W001": {-0.5, -0.5},
		"S34W059": {-33.4, -58.6}, // Buenos Aires
	}
	for want, ll := range cases {
		if got := SrtmTileName(ll[0], ll[1]); got != want {
			t.Errorf("SrtmTileName(%v) = %s, want %s", ll, got, want)
		}
	}
}

func TestHmapRoundTrip(t *testing.T) {
	g := NewHeightGrid(5, 4, -100, -80, 50, 40)
	for row := 0; row < g.Rows; row++ {
		for col := 0; col < g.Cols; col++ {
			g.Set(col, row, float64(col)*3.25-float64(row)*1.5)
		}
	}
	blob, err := EncodeHmap(g)
	if err != nil {
		t.Fatal(err)
	}
	// 40-byte header + int16 payload.
	if want := 40 + 5*4*2; len(blob) != want {
		t.Fatalf("blob is %d bytes, want %d", len(blob), want)
	}
	back, err := DecodeHmap(blob)
	if err != nil {
		t.Fatal(err)
	}
	if back.Cols != g.Cols || back.Rows != g.Rows ||
		back.OriginX != g.OriginX || back.CellY != g.CellY {
		t.Fatalf("header drifted: %+v", back)
	}
	for i := range g.Values {
		if math.Abs(back.Values[i]-g.Values[i]) > 0.01 {
			t.Fatalf("sample %d: %g != %g", i, back.Values[i], g.Values[i])
		}
	}
}

func TestHmapRejectsBadInput(t *testing.T) {
	if _, err := EncodeHmap(NewHeightGrid(1, 1, 0, 0, 1, 1)); err == nil {
		t.Fatal("a 1x1 grid must be rejected")
	}
	if _, err := DecodeHmap([]byte("nope")); err == nil {
		t.Fatal("garbage must be rejected")
	}
	g := NewHeightGrid(4, 4, 0, 0, 1, 1)
	blob, _ := EncodeHmap(g)
	if _, err := DecodeHmap(blob[:len(blob)-4]); err == nil {
		t.Fatal("a truncated blob must be rejected")
	}
}

// The generator and the engine must agree on what a .hmap means; this mirrors
// the FHeightGrid::Sample contract covered by [ScadTerrain][Hmap] on the C++ side.
func TestHeightGridSampleIsBilinearAndClamped(t *testing.T) {
	g := NewHeightGrid(2, 2, 0, 0, 10, 10)
	g.Set(0, 0, 0)
	g.Set(1, 0, 10)
	g.Set(0, 1, 20)
	g.Set(1, 1, 30)

	check := func(x, y, want float64) {
		t.Helper()
		if got := g.Sample(x, y); math.Abs(got-want) > 1e-9 {
			t.Errorf("Sample(%g, %g) = %g, want %g", x, y, got, want)
		}
	}
	check(0, 0, 0)
	check(10, 10, 30)
	check(5, 5, 15)
	check(5, 0, 5)
	check(-100, -100, 0) // clamped, not extrapolated
	check(999, 999, 30)
}

func TestRingGeometry(t *testing.T) {
	square := Ring{{0, 0}, {10, 0}, {10, 10}, {0, 10}}
	if a := RingArea(square); math.Abs(a-100) > 1e-9 {
		t.Fatalf("area = %g, want 100", a)
	}
	if c := Centroid(square); math.Abs(c[0]-5) > 1e-9 || math.Abs(c[1]-5) > 1e-9 {
		t.Fatalf("centroid = %v, want [5 5]", c)
	}
	if !PointInRing(square, 5, 5) || PointInRing(square, 15, 5) {
		t.Fatal("point-in-ring is wrong")
	}
	cw := Ring{{0, 0}, {0, 10}, {10, 10}, {10, 0}}
	if SignedArea(cw) >= 0 {
		t.Fatal("fixture should wind clockwise")
	}
	if SignedArea(EnsureCCW(cw)) <= 0 {
		t.Fatal("EnsureCCW did not flip the ring")
	}
}

func TestDedupeRingDropsTheClosingRepeat(t *testing.T) {
	// OSM closed ways repeat the first node; consecutive duplicates also occur.
	ring := Ring{{0, 0}, {10, 0}, {10, 0}, {10, 10}, {0, 10}, {0, 0}}
	got := DedupeRing(ring, 0.2)
	if len(got) != 4 {
		t.Fatalf("got %d points, want 4: %v", len(got), got)
	}
}

func TestSimplifyRespectsTheVertexCap(t *testing.T) {
	// A 64-gon: simplification must fit the cap without collapsing the shape.
	var circle Ring
	for i := 0; i < 64; i++ {
		a := 2 * math.Pi * float64(i) / 64
		circle = append(circle, [2]float64{50 * math.Cos(a), 50 * math.Sin(a)})
	}
	got := Simplify(circle, 0.5, 12)
	if len(got) > 12 {
		t.Fatalf("simplified to %d vertices, cap is 12", len(got))
	}
	if len(got) < 3 {
		t.Fatalf("simplification collapsed the ring: %v", got)
	}
	// A convex 12-gon inscribed in r=50 still covers most of the disc.
	if a := RingArea(got); a < 0.7*math.Pi*2500 {
		t.Fatalf("area collapsed to %.0f (circle is %.0f)", a, math.Pi*2500)
	}
}

func TestParseLength(t *testing.T) {
	cases := map[string]float64{
		"415.8":  415.8,
		"96 m":   96,
		"12.5m":  12.5,
		"":       0,
		"tall":   0,
		"-3":     0,
		"999999": 0, // implausible, treated as missing
	}
	for in, want := range cases {
		if got := parseLength(in); got != want {
			t.Errorf("parseLength(%q) = %g, want %g", in, got, want)
		}
	}
}

func TestInferHeightPriorityChain(t *testing.T) {
	profile := Profiles["hongkong"]
	landmarks := map[int64]float64{42: 400}

	h, src, _ := inferHeight(42, map[string]string{"building": "yes", "height": "30"}, profile, landmarks)
	if h != 400 || src != "landmark" {
		t.Fatalf("landmark override lost: %g %s", h, src)
	}
	h, src, _ = inferHeight(1, map[string]string{"building": "office", "height": "415.8",
		"building:levels": "10"}, profile, landmarks)
	if h != 415.8 || src != "tag" {
		t.Fatalf("height tag should win over levels: %g %s", h, src)
	}
	h, src, levels := inferHeight(1, map[string]string{"building": "apartments",
		"building:levels": "20"}, profile, landmarks)
	if src != "levels" || levels != 20 || math.Abs(h-(20*3.0+1.5)) > 1e-9 {
		t.Fatalf("levels path wrong: %g %s %d", h, src, levels)
	}
	h, src, _ = inferHeight(1, map[string]string{"building": "office"}, profile, landmarks)
	if src != "default" || h != 60 {
		t.Fatalf("per-kind default wrong: %g %s", h, src)
	}
	h, src, _ = inferHeight(1, map[string]string{"building": "something_unmapped"}, profile, landmarks)
	if src != "default" || h != profile.Fallback {
		t.Fatalf("fallback wrong: %g %s", h, src)
	}
}

func TestRoadWidth(t *testing.T) {
	if w := roadWidth(map[string]string{"highway": "primary"}); w != 14 {
		t.Errorf("primary = %g, want 14", w)
	}
	if w := roadWidth(map[string]string{"highway": "residential", "lanes": "4"}); w != 13.6 {
		t.Errorf("lane count should win: %g", w)
	}
	if w := roadWidth(map[string]string{"highway": "primary", "width": "22"}); w != 22 {
		t.Errorf("explicit width should win: %g", w)
	}
}

// ---------------------------------------------------------------------------
// End-to-end over a synthetic tile: no network, no cache, deterministic.
// ---------------------------------------------------------------------------

// squareWay builds a closed OSM way centred on (lat, lon) with a half-size in
// degrees, plus tags.
func squareWay(id int64, lat, lon, d float64, tags map[string]string) osmElement {
	return osmElement{
		Type: "way", ID: id, Tags: tags,
		Geometry: []osmPoint{
			{Lat: lat - d, Lon: lon - d},
			{Lat: lat - d, Lon: lon + d},
			{Lat: lat + d, Lon: lon + d},
			{Lat: lat + d, Lon: lon - d},
			{Lat: lat - d, Lon: lon - d},
		},
	}
}

func fixtureTile() Tile {
	tile := Tile{Name: "fixture", Lat: 22.2855, Lon: 114.158, SizeM: 400, Cells: 40,
		Profile: "hongkong", Seed: 3}
	_ = tile.Normalize()
	return tile
}

func fixtureElements() []osmElement {
	const d = 0.0004 // ~44m
	return []osmElement{
		squareWay(1, 22.2855, 114.158, d, map[string]string{
			"building": "office", "height": "120", "name": "Tower One"}),
		squareWay(2, 22.2851, 114.1575, d, map[string]string{
			"building": "apartments", "building:levels": "30"}),
		// A shed, below the minimum footprint.
		squareWay(3, 22.2858, 114.1585, 0.00001, map[string]string{"building": "shed"}),
		// Far outside the 400m tile: must be dropped by the emitter.
		squareWay(4, 22.2920, 114.1660, d, map[string]string{"building": "yes"}),
		{
			Type: "way", ID: 10, Tags: map[string]string{"highway": "primary", "lanes": "4"},
			Geometry: []osmPoint{
				{Lat: 22.2845, Lon: 114.1565}, {Lat: 22.2845, Lon: 114.1595},
			},
		},
		{
			// Coastline running west to east: land on the left (north), sea to
			// the south. Deliberately the opposite of Hong Kong so the test
			// cannot pass by accident.
			Type: "way", ID: 20, Tags: map[string]string{"natural": "coastline"},
			Geometry: []osmPoint{
				{Lat: 22.2840, Lon: 114.1540}, {Lat: 22.2840, Lon: 114.1620},
			},
		},
	}
}

func TestNormalizeProducesAStableIR(t *testing.T) {
	tile := fixtureTile()
	ir := Normalize(tile, fixtureElements(), Profiles["hongkong"], nil)

	if len(ir.Buildings) != 4 {
		t.Fatalf("got %d buildings, want 4", len(ir.Buildings))
	}
	if len(ir.Roads) != 1 || ir.Roads[0].Width != 13.6 {
		t.Fatalf("road not normalised: %+v", ir.Roads)
	}
	if len(ir.Coastline) != 1 {
		t.Fatalf("coastline lost: %+v", ir.Coastline)
	}
	// Buildings are ID-sorted so regenerating produces identical output.
	for i := 1; i < len(ir.Buildings); i++ {
		if ir.Buildings[i-1].ID > ir.Buildings[i].ID {
			t.Fatal("buildings are not ID-sorted")
		}
	}
	stats := ir.Summarize()
	if stats.HeightFromTag != 1 || stats.HeightFromLvls != 1 {
		t.Fatalf("height provenance wrong: %+v", stats)
	}
	if stats.TallestName != "Tower One" || stats.TallestHeight != 120 {
		t.Fatalf("tallest wrong: %s %.1f", stats.TallestName, stats.TallestHeight)
	}
	// The first building is centred on the tile origin.
	c := Centroid(ir.Buildings[0].Outer)
	if math.Hypot(c[0], c[1]) > 1 {
		t.Fatalf("centre building projected to %v, want ~origin", c)
	}
	if ir.Buildings[0].AreaM2 < 6000 || ir.Buildings[0].AreaM2 > 8500 {
		t.Fatalf("~88m square should be ~7700 m2, got %.0f", ir.Buildings[0].AreaM2)
	}
}

func TestClassifyWaterUsesTheCoastlineSide(t *testing.T) {
	tile := fixtureTile()
	ir := Normalize(tile, fixtureElements(), Profiles["hongkong"], nil)
	grid := NewHeightGrid(21, 21, -200, -200, 20, 20)
	flat := make([]float64, len(grid.Values)) // all at 0m, low enough to flood

	plan := planWater(ir, grid, flat)
	if !plan.IsSea || plan.Level != MeanSeaLevel {
		t.Fatalf("a tile with a coastline is at sea level: %+v", plan)
	}
	sea := classifyWater(ir, grid, flat, plan)
	// The coastline sits ~170m south of the tile centre, running west->east
	// with land on its left (north). Everything south of it is sea.
	northOfShore := 0
	southOfShore := 0
	for row := 0; row < grid.Rows; row++ {
		for col := 0; col < grid.Cols; col++ {
			y := grid.PosY(row)
			if sea[row*grid.Cols+col] {
				if y > -100 {
					northOfShore++
				} else {
					southOfShore++
				}
			}
		}
	}
	if southOfShore == 0 {
		t.Fatal("nothing south of the coastline was classified as sea")
	}
	if northOfShore != 0 {
		t.Fatalf("%d cells north of the coastline (the land side) were flooded", northOfShore)
	}
}

func TestClassifyWaterRespectsElevation(t *testing.T) {
	tile := fixtureTile()
	ir := Normalize(tile, fixtureElements(), Profiles["hongkong"], nil)
	grid := NewHeightGrid(21, 21, -200, -200, 20, 20)
	high := make([]float64, len(grid.Values))
	for i := range high {
		high[i] = 200 // a hill on the seaward side must not be flooded
	}
	plan := planWater(ir, grid, high)
	for _, wet := range classifyWater(ir, grid, high, plan) {
		if wet {
			t.Fatal("high ground was classified as sea")
		}
	}
}

// riverIR builds an inland tile: no coastline, one big river polygon, and a
// terrain sitting hundreds of metres above sea level. This is the Chengdu /
// Paris shape, and the case a hard-coded sea level gets wrong in two ways at
// once — the river never floods, and the datum for the palette is nonsense.
func riverIR(groundElevation, riverElevation float64) (*IR, *HeightGrid, []float64) {
	ir := &IR{
		Tile: "inland", Attribution: []string{osmLicense},
		Waters: []Area{{
			ID: 1, Tag: "natural=water",
			// A 60 m wide band across the middle of the tile.
			Outer: Ring{{-200, -30}, {200, -30}, {200, 30}, {-200, 30}},
		}},
	}
	grid := NewHeightGrid(41, 41, -200, -200, 10, 10)
	elev := make([]float64, len(grid.Values))
	for row := 0; row < grid.Rows; row++ {
		for col := 0; col < grid.Cols; col++ {
			y := grid.PosY(row)
			if y >= -30 && y <= 30 {
				elev[row*grid.Cols+col] = riverElevation
			} else {
				elev[row*grid.Cols+col] = groundElevation
			}
		}
	}
	return ir, grid, elev
}

func TestPlanWaterDerivesAnInlandLevelFromTheRiver(t *testing.T) {
	ir, grid, elev := riverIR(504, 496)
	plan := planWater(ir, grid, elev)
	if !plan.HasWater || plan.IsSea {
		t.Fatalf("an inland river is water but not sea: %+v", plan)
	}
	if math.Abs(plan.Level-496) > 0.5 {
		t.Fatalf("water level = %.1f, want the river surface ~496", plan.Level)
	}
	if plan.MaxDepth > seaMaxDepth {
		t.Fatalf("an inland river must not be dredged like a shipping channel: %.1f", plan.MaxDepth)
	}

	// The flood band is relative to the water plane. With an absolute sea-level
	// test every one of these cells is hundreds of metres "too high" and the
	// river disappears — the bug this fixture exists to pin down.
	wet := classifyWater(ir, grid, elev, plan)
	flooded := 0
	for i, w := range wet {
		if !w {
			continue
		}
		flooded++
		if elev[i] != 496 {
			t.Fatalf("flooded a cell at %.0f m, outside the river", elev[i])
		}
	}
	if flooded < 100 {
		t.Fatalf("only %d cells flooded; the river should span the tile", flooded)
	}
}

func TestPlanWaterSkipsOrnamentalPools(t *testing.T) {
	// Two 2 944 m² pools, the size of the 9/11 Memorial basins that used to be
	// carved into 5 m pits in the middle of Lower Manhattan.
	ir := &IR{
		Tile: "pools", Attribution: []string{osmLicense},
		Waters: []Area{
			{ID: 1, Tag: "natural=water", Outer: Ring{{0, 0}, {54, 0}, {54, 54}, {0, 54}}},
			{ID: 2, Tag: "natural=water", Outer: Ring{{-80, 0}, {-26, 0}, {-26, 54}, {-80, 54}}},
		},
	}
	grid := NewHeightGrid(41, 41, -200, -200, 10, 10)
	elev := make([]float64, len(grid.Values))
	for i := range elev {
		elev[i] = 5
	}
	plan := planWater(ir, grid, elev)
	if plan.HasWater {
		t.Fatalf("ornamental pools must not become the tile's water plane: %+v", plan)
	}
	if plan.Skipped != 2 {
		t.Fatalf("skipped %d minor bodies, want 2", plan.Skipped)
	}
	for _, wet := range classifyWater(ir, grid, elev, plan) {
		if wet {
			t.Fatal("an ornamental pool was carved into the terrain")
		}
	}
}

func TestBuildTerrainAnchorsAnInlandTileOnItsOwnDatum(t *testing.T) {
	ir, _, _ := riverIR(504, 496)
	tile := Tile{Name: "inland", Lat: 30.63, Lon: 104.09, SizeM: 400, Cells: 40, Profile: "default"}
	if err := tile.Normalize(); err != nil {
		t.Fatal(err)
	}
	grid, report, err := BuildTerrain(tile, ir, constantSampler(500), "")
	if err != nil {
		t.Fatal(err)
	}
	if !report.Water.HasWater || report.Water.IsSea {
		t.Fatalf("inland tile water plan wrong: %+v", report.Water)
	}
	// Datum follows the town, not sea level.
	if report.BaseElevation < 400 {
		t.Fatalf("base elevation %.1f collapsed to sea level", report.BaseElevation)
	}
	// And nothing was clamped down to a 0.4 m "freeboard above the sea".
	lo, _ := grid.Bounds()
	if lo < 400 {
		t.Fatalf("terrain floor %.1f m: an inland tile was clamped to sea level", lo)
	}
}

func TestDespikeRemovesIsolatedOutliersOnly(t *testing.T) {
	const n = 7
	v := make([]float64, n*n)
	for i := range v {
		v[i] = 10
	}
	v[3*n+3] = 900 // a corrupt sample
	// A legitimate plateau: half the grid raised. Must survive.
	for row := 0; row < n; row++ {
		for col := 4; col < n; col++ {
			v[row*n+col] = 90
		}
	}
	fixed := despike(v, n, n)
	if fixed != 1 {
		t.Fatalf("despike touched %d samples, want 1", fixed)
	}
	if v[3*n+3] > 100 {
		t.Fatalf("the outlier survived: %g", v[3*n+3])
	}
	if v[0*n+6] != 90 {
		t.Fatalf("the plateau was eaten: %g", v[0*n+6])
	}
}

func TestInpaintFillsMaskedSamplesFromTheirNeighbours(t *testing.T) {
	const n = 5
	v := make([]float64, n*n)
	mask := make([]bool, n*n)
	for i := range v {
		v[i] = 5
	}
	// A rooftop return in the middle: 60m of "terrain" that is really a building.
	v[2*n+2] = 60
	mask[2*n+2] = true
	inpaint(v, mask, n, n)
	if math.Abs(v[2*n+2]-5) > 1e-9 {
		t.Fatalf("masked sample = %g, want the surrounding ground level 5", v[2*n+2])
	}
}

func TestDistanceToLand(t *testing.T) {
	const n = 5
	sea := make([]bool, n*n)
	for row := 0; row < n; row++ {
		for col := 2; col < n; col++ {
			sea[row*n+col] = true
		}
	}
	d := distanceToLand(sea, n, n, 10)
	if d[0*n+1] != 0 {
		t.Fatalf("land must be at distance 0, got %g", d[0*n+1])
	}
	if math.Abs(d[0*n+2]-10) > 1e-6 {
		t.Fatalf("first sea cell = %g, want 10", d[0*n+2])
	}
	if math.Abs(d[0*n+4]-30) > 1e-6 {
		t.Fatalf("third sea cell = %g, want 30", d[0*n+4])
	}
}

func TestEmitIsDeterministicAndWellFormed(t *testing.T) {
	tile := fixtureTile()
	ir := Normalize(tile, fixtureElements(), Profiles["hongkong"], nil)
	grid := NewHeightGrid(tile.Cells+1, tile.Cells+1, -200, -200, 10, 10)
	for i := range grid.Values {
		grid.Values[i] = 6
	}
	opt := DefaultEmitOptions()

	src, report := Emit(tile, ir, grid, "assets/geo/fixture/terrain.hmap", opt)
	again, _ := Emit(tile, ir, grid, "assets/geo/fixture/terrain.hmap", opt)
	if src != again {
		t.Fatal("Emit is not deterministic")
	}

	if report.Buildings != 2 {
		t.Fatalf("emitted %d buildings, want 2 (one off-tile, one too small)", report.Buildings)
	}
	if report.DroppedEdge != 1 || report.DroppedSmall != 1 {
		t.Fatalf("drop reasons wrong: %+v", report)
	}
	if report.Roads != 1 {
		t.Fatalf("emitted %d road operators, want 1", report.Roads)
	}

	for _, want := range []string{
		`["gkterr1"`,
		`["hmap", "assets/geo/fixture/terrain.hmap", "set", 1, 0]`,
		`"urban"`,
		`["road", `,
		"function gz(",
		"gk_terrain(TERR);",
		"use <../../scad/lib/kit_geo_city.scad>",
		"gc_bld(",
		"gk_flatten()",
		"OpenStreetMap contributors",
		"gk_camera_lookat(",
		"// Tower One (120m, tag)",
	} {
		if !strings.Contains(src, want) {
			t.Errorf("generated scene is missing %q", want)
		}
	}
	// The decoration lives in the kit, not in the tile: a building is still one
	// call with a footprint, and no geometry primitive leaks into the scene file.
	if strings.Contains(src, "linear_extrude(") {
		t.Error("detailed buildings must go through gc_bld, not raw extrusions")
	}
	// Every building must sit on a terrain-sampled base, never a baked height.
	if strings.Count(src, "gz(") < 3 {
		t.Error("buildings are not anchored with gz(...) terrain samples")
	}
	// Blocks keep each Model small enough for the engine to cook physics.
	if report.Blocks < 1 || report.Blocks > report.Buildings {
		t.Fatalf("block count %d is implausible for %d buildings", report.Blocks, report.Buildings)
	}
}

func TestEmitHandlesFootprintHoles(t *testing.T) {
	tile := fixtureTile()
	ir := &IR{
		Tile: tile.Name, Attribution: []string{osmLicense},
		Buildings: []Building{{
			ID: 1, Kind: "office", Height: 40, HeightSource: "tag",
			Outer:  Ring{{-60, -60}, {60, -60}, {60, 60}, {-60, 60}},
			Inners: []Ring{{{-20, -20}, {20, -20}, {20, 20}, {-20, 20}}},
			AreaM2: 14400,
		}},
	}
	grid := NewHeightGrid(11, 11, -200, -200, 40, 40)
	src, report := Emit(tile, ir, grid, "x.hmap", DefaultEmitOptions())
	if report.Buildings != 1 {
		t.Fatalf("courtyard building was dropped: %+v", report)
	}
	if !strings.Contains(src, "[[0, 1, 2, 3], [4, 5, 6, 7]]") {
		t.Fatalf("hole was not emitted as a polygon path:\n%s", src)
	}

	// The same courtyard must survive with the decoration layer off, where the
	// emitter writes the polygon itself.
	plain := DefaultEmitOptions()
	plain.Detail = false
	bare, _ := Emit(tile, ir, grid, "x.hmap", plain)
	if !strings.Contains(bare, "paths = [[0, 1, 2, 3], [4, 5, 6, 7]]") {
		t.Fatalf("hole was lost on the --no-detail path:\n%s", bare)
	}
	if !strings.Contains(bare, "linear_extrude(") || strings.Contains(bare, "gc_bld(") {
		t.Error("--no-detail must emit the bare prism")
	}
	if strings.Contains(bare, "kit_geo_city") {
		t.Error("--no-detail must not pull in the decoration kit")
	}
}

func TestMinAreaRectFindsTheRidgeAxis(t *testing.T) {
	// A 20 x 8 rectangle rotated 30 degrees. The ridge has to come out along
	// the long side or every pitched roof in the tile is turned 90 degrees.
	const ang = 30.0
	rot := func(x, y float64) [2]float64 {
		c := math.Cos(ang * math.Pi / 180)
		s := math.Sin(ang * math.Pi / 180)
		return [2]float64{x*c - y*s + 15, x*s + y*c - 40}
	}
	ring := Ring{rot(-10, -4), rot(10, -4), rot(10, 4), rot(-10, 4)}
	obb, rect := MinAreaRect(ring)

	if math.Abs(obb[2]-20) > 0.05 || math.Abs(obb[3]-8) > 0.05 {
		t.Fatalf("obb size %.3f x %.3f, want 20 x 8", obb[2], obb[3])
	}
	if math.Abs(obb[0]-15) > 0.05 || math.Abs(obb[1]+40) > 0.05 {
		t.Fatalf("obb centre (%.3f, %.3f), want (15, -40)", obb[0], obb[1])
	}
	if d := math.Abs(obb[4] - ang); d > 0.05 && math.Abs(d-180) > 0.05 {
		t.Fatalf("obb angle %.3f, want %v", obb[4], ang)
	}
	if math.Abs(rect-1) > 1e-6 {
		t.Fatalf("a rectangle must be perfectly rectangular, got %.4f", rect)
	}

	// An L-shape must score well below the pitched-roof threshold: a straight
	// ridge over it would hang in mid-air over the notch.
	l := Ring{{0, 0}, {20, 0}, {20, 6}, {8, 6}, {8, 20}, {0, 20}}
	if _, r := MinAreaRect(l); r > 0.6 {
		t.Fatalf("L-shape rectangularity %.3f is too high to reject", r)
	}
}

func TestClassifyBuildingIsRegionalAndDeterministic(t *testing.T) {
	house := Building{ID: 41, Kind: "house", Height: 7, HeightSource: "tag", AreaM2: 96}
	ring := Ring{{0, 0}, {12, 0}, {12, 8}, {0, 8}}

	eu := classifyBuilding(house, ring, nil, DetailProfiles["default"], Profiles["default"], 7)
	if eu.RoofKind != 2 {
		t.Errorf("a small European house should get a pitched roof, got kind %d", eu.RoofKind)
	}
	if eu.RoofRise <= 0.5 {
		t.Errorf("pitched roof has no rise: %.2f", eu.RoofRise)
	}
	// 12 x 8 is elongated, so unless the hip roll wins it should be gabled;
	// either way the ridge fraction has to stay in the range the kit accepts.
	if eu.RidgeFrac <= 0 || eu.RidgeFrac > 1 {
		t.Errorf("ridge fraction %.2f out of range", eu.RidgeFrac)
	}

	hk := classifyBuilding(house, ring, nil, DetailProfiles["hongkong"], Profiles["hongkong"], 7)
	if hk.RoofKind != 1 {
		t.Errorf("a Hong Kong village house is flat-roofed, got kind %d", hk.RoofKind)
	}
	if hk.Clutter < 1 {
		t.Error("the Hong Kong clutter boost should put something on the roof")
	}

	// Tall enough to be a curtain-wall tower under either profile.
	tower := Building{ID: 42, Kind: "office", Height: 120, HeightSource: "tag", AreaM2: 2400}
	big := Ring{{0, 0}, {50, 0}, {50, 48}, {0, 48}}
	tw := classifyBuilding(tower, big, nil, DetailProfiles["default"], Profiles["default"], 7)
	if tw.Facade != 1 {
		t.Errorf("a 120 m office should be a curtain wall, got facade %d", tw.Facade)
	}
	if tw.Clutter < 3 {
		t.Errorf("a 120 m tower should carry a mast, clutter %d", tw.Clutter)
	}

	// Same inputs, same output — the tile has to regenerate byte for byte.
	again := classifyBuilding(tower, big, nil, DetailProfiles["default"], Profiles["default"], 7)
	if again != tw {
		t.Error("classifyBuilding is not deterministic")
	}
	// ...and the seed has to actually reach it.
	if other := classifyBuilding(tower, big, nil, DetailProfiles["default"], Profiles["default"], 8); other == tw {
		t.Error("the tile seed does not reach the building style")
	}
}

// The physics cook silently skips any Model at or above 65535 triangles, so an
// over-budget module renders fine and has no collider. This is the guard.
func TestEmittedModulesStayUnderThePhysicsBudget(t *testing.T) {
	tile := fixtureTile()
	ir := &IR{Tile: tile.Name, Attribution: []string{osmLicense}}
	// 120 towers in one 100 m block: far more than a bare-prism block ever was,
	// and enough that a single module would be well over the limit.
	for i := 0; i < 120; i++ {
		x := float64(i%12)*7 - 40
		y := float64(i/12)*7 - 40
		ir.Buildings = append(ir.Buildings, Building{
			ID: int64(1000 + i), Kind: "office", Height: 80, HeightSource: "tag",
			Outer:  Ring{{x, y}, {x + 6, y}, {x + 6, y + 6}, {x, y + 6}},
			AreaM2: 36,
		})
	}
	grid := NewHeightGrid(11, 11, -200, -200, 40, 40)
	src, report := Emit(tile, ir, grid, "x.hmap", DefaultEmitOptions())

	if report.Buildings != 120 {
		t.Fatalf("emitted %d buildings, want 120", report.Buildings)
	}
	if report.Blocks < 2 {
		t.Fatalf("120 detailed towers fit in %d module(s); the budget split is not working", report.Blocks)
	}
	// Every declared module must be one the assembly calls, and vice versa.
	if strings.Count(src, "module blk_r") != report.Blocks {
		t.Fatalf("%d block modules declared, report says %d",
			strings.Count(src, "module blk_r"), report.Blocks)
	}
	if strings.Count(src, "gk_flatten()") != report.Blocks {
		t.Error("every block module must wrap its buildings in gk_flatten()")
	}
}

func TestFnumIsCompactAndLocaleFree(t *testing.T) {
	cases := map[float64]string{
		0: "0", 0.001: "0", 1: "1", -1.5: "-1.5",
		12.345: "12.35", 1000: "1000", -0.25: "-0.25",
	}
	for in, want := range cases {
		if got := fnum(in); got != want {
			t.Errorf("fnum(%g) = %q, want %q", in, got, want)
		}
	}
}

func TestBuildOverpassQueryCoversEveryLayer(t *testing.T) {
	q := BuildOverpassQuery(BBox{South: 1, West: 2, North: 3, East: 4})
	for _, want := range []string{
		"[out:json]", "out body geom;",
		`way["building"]`, `relation["building"]`, `way["highway"]`,
		`way["natural"="coastline"]`, `way["natural"="water"]`,
		"(1.0000000,2.0000000,3.0000000,4.0000000)",
	} {
		if !strings.Contains(q, want) {
			t.Errorf("query is missing %q:\n%s", want, q)
		}
	}
}

func TestPathsLayoutKeepsDerivedDatabasesOutOfTheRepo(t *testing.T) {
	p := NewPaths("/repo", "hk_victoria")
	// Raw downloads and the IR are ODbL-derived: external/ is gitignored.
	for _, path := range []string{p.OsmPath(), p.IRPath(), p.MetaPath(), p.DemDir()} {
		if !strings.Contains(filepath.ToSlash(path), "/external/geocache") {
			t.Errorf("%s must live under external/geocache", path)
		}
	}
	// Produced works are runtime assets: gitignored like the cache, but under
	// assets/geo so one pak boundary covers a whole tile.
	for _, path := range []string{p.HmapPath(), p.ScadPath(), p.POIPath(), p.AttributionPath()} {
		if strings.Contains(path, "external") {
			t.Errorf("%s must not live in the cache", path)
		}
		if !strings.Contains(filepath.ToSlash(path), "/"+GeoAssetRoot+"/hk_victoria/") {
			t.Errorf("%s must live in the tile's own directory under %s", path, GeoAssetRoot)
		}
	}
	if p.HmapAssetRef() != "assets/geo/hk_victoria/terrain.hmap" {
		t.Errorf("hmap asset ref = %q", p.HmapAssetRef())
	}
	if p.ScadAssetRef() != "assets/geo/hk_victoria/hk_victoria.scad" {
		t.Errorf("scad asset ref = %q", p.ScadAssetRef())
	}
}

// constantSampler is a flat ElevationSource, so terrain tests can exercise the
// datum and water logic without a 25 MB SRTM tile.
type constantSampler float64

func (c constantSampler) At(lat, lon float64) (float64, bool) { return float64(c), true }

// A relation whose boundary is split across open member ways — the shape of the
// Seine (eight open outer members). Treating the longest member as a ring on its
// own produced a polygon unrelated to the water, which flooded half of Paris.
func TestAssembleRingsStitchesOpenMultipolygonMembers(t *testing.T) {
	seg := func(pts ...[2]float64) osmMember {
		m := osmMember{Type: "way", Role: "outer"}
		for _, p := range pts {
			m.Geometry = append(m.Geometry, osmPoint{Lat: p[1], Lon: p[0]})
		}
		return m
	}
	identity := func(g []osmPoint) [][2]float64 {
		out := make([][2]float64, 0, len(g))
		for _, q := range g {
			out = append(out, [2]float64{q.Lon, q.Lat})
		}
		return out
	}

	// A 100x100 square cut into four open ways, one of them reversed and one
	// listed out of order, plus a hole.
	members := []osmMember{
		seg([2]float64{0, 0}, [2]float64{100, 0}),
		seg([2]float64{100, 100}, [2]float64{0, 100}), // out of order
		seg([2]float64{100, 0}, [2]float64{100, 100}),
		seg([2]float64{0, 0}, [2]float64{0, 100}), // reversed
	}
	hole := osmMember{Type: "way", Role: "inner"}
	for _, p := range [][2]float64{{40, 40}, {60, 40}, {60, 60}, {40, 60}, {40, 40}} {
		hole.Geometry = append(hole.Geometry, osmPoint{Lat: p[1], Lon: p[0]})
	}
	members = append(members, hole)

	outers := assembleRings(members, "outer", identity)
	if len(outers) != 1 {
		t.Fatalf("got %d outer rings, want 1: %v", len(outers), outers)
	}
	if a := RingArea(outers[0]); math.Abs(a-10000) > 1 {
		t.Fatalf("assembled ring area %.0f, want 10000", a)
	}
	inners := assembleRings(members, "inner", identity)
	if len(inners) != 1 || math.Abs(RingArea(inners[0])-400) > 1 {
		t.Fatalf("inner ring wrong: %v", inners)
	}

	// A chain that cannot close is dropped rather than invented.
	open := []osmMember{seg([2]float64{0, 0}, [2]float64{100, 0}),
		seg([2]float64{100, 0}, [2]float64{100, 100})}
	if got := assembleRings(open, "outer", identity); len(got) != 0 {
		t.Fatalf("an unclosed chain must be dropped, got %v", got)
	}

	// End to end: the relation becomes one Area with its hole attached.
	areas := areasFrom(osmElement{Type: "relation", ID: 7, Members: members}, identity, "natural=water")
	if len(areas) != 1 || len(areas[0].Inners) != 1 {
		t.Fatalf("areasFrom lost the multipolygon structure: %+v", areas)
	}
}

// ---------------------------------------------------------------------------
// POI layer
// ---------------------------------------------------------------------------

func poiNode(id int64, lat, lon float64, tags map[string]string) osmElement {
	return osmElement{Type: "node", ID: id, Tags: tags, Lat: lat, Lon: lon}
}

func poiFixtureElements() []osmElement {
	const d = 0.0004 // ~44m
	return append(fixtureElements(),
		// A named church footprint: small, but a landmark rather than an office.
		squareWay(5, 22.2857, 114.1583, 0.0002, map[string]string{
			"building": "church", "name": "St. Fixture"}),
		// A named park, and a named flowerbed that is too small to label.
		squareWay(6, 22.2850, 114.1588, d, map[string]string{
			"leisure": "park", "name": "Fixture Gardens"}),
		squareWay(7, 22.2853, 114.1590, 0.00002, map[string]string{
			"leisure": "garden", "name": "Nameless Planter"}),
		// Standalone nodes.
		poiNode(100, 22.2853, 114.1578, map[string]string{
			"name": "Fixture Station", "railway": "station"}),
		poiNode(101, 22.2856, 114.1581, map[string]string{
			"name": "Old Fixture Wall", "historic": "city_wall"}),
		// Same name and location as the church way: OSM often carries both, and
		// only the footprint (which has a height) should survive.
		poiNode(102, 22.2857, 114.1583, map[string]string{
			"name": "St. Fixture", "amenity": "place_of_worship"}),
		// Unnamed, and a named node with no POI-worthy tag: both ignored.
		poiNode(103, 22.2854, 114.1579, map[string]string{"railway": "station"}),
		poiNode(104, 22.2854, 114.1579, map[string]string{
			"name": "A Bench", "amenity": "bench"}),
		// Named, but outside the 400 m tile.
		poiNode(105, 22.2920, 114.1660, map[string]string{
			"name": "Far Away Museum", "tourism": "museum"}),
	)
}

func findPOI(pois []POI, name string) (POI, bool) {
	for _, p := range pois {
		if p.Name == name {
			return p, true
		}
	}
	return POI{}, false
}

func TestCollectPOIsPicksNamedPlacesFromEverySource(t *testing.T) {
	tile := fixtureTile()
	ir := Normalize(tile, poiFixtureElements(), Profiles["hongkong"], nil)

	for _, want := range []struct {
		name     string
		source   string
		category string
	}{
		{"Tower One", "building", CatCommerce},
		{"St. Fixture", "building", CatWorship},
		{"Fixture Gardens", "area", CatPark},
		{"Fixture Station", "node", CatTransport},
		{"Old Fixture Wall", "node", CatLandmark},
	} {
		got, ok := findPOI(ir.POIs, want.name)
		if !ok {
			t.Errorf("missing POI %q; have %v", want.name, poiNames(ir.POIs))
			continue
		}
		if got.Source != want.source || got.Category != want.category {
			t.Errorf("%q: source=%q category=%q, want %q/%q",
				want.name, got.Source, got.Category, want.source, want.category)
		}
	}

	for _, unwanted := range []string{"Nameless Planter", "A Bench", "Far Away Museum"} {
		if _, ok := findPOI(ir.POIs, unwanted); ok {
			t.Errorf("%q should not be a POI", unwanted)
		}
	}

	// The unnamed station node contributes nothing.
	if n := countPOISource(ir.POIs, "node"); n != 2 {
		t.Errorf("got %d node POIs, want 2 (%v)", n, poiNames(ir.POIs))
	}
}

func TestCollectPOIsPrefersTheFootprintOverACoincidentNode(t *testing.T) {
	tile := fixtureTile()
	ir := Normalize(tile, poiFixtureElements(), Profiles["hongkong"], nil)

	var matches []POI
	for _, p := range ir.POIs {
		if p.Name == "St. Fixture" {
			matches = append(matches, p)
		}
	}
	if len(matches) != 1 {
		t.Fatalf("got %d POIs named St. Fixture, want 1", len(matches))
	}
	// The surviving anchor is the one that can float a label over the roof.
	if matches[0].Source != "building" || matches[0].Height <= 0 {
		t.Fatalf("kept the node instead of the footprint: %+v", matches[0])
	}
}

func TestPOIsAreRankedAndDeterministic(t *testing.T) {
	tile := fixtureTile()
	first := Normalize(tile, poiFixtureElements(), Profiles["hongkong"], nil).POIs
	second := Normalize(tile, poiFixtureElements(), Profiles["hongkong"], nil).POIs

	if len(first) != len(second) {
		t.Fatalf("POI count is not stable: %d vs %d", len(first), len(second))
	}
	for i := range first {
		if first[i].ID != second[i].ID || first[i].Rank != second[i].Rank {
			t.Fatalf("POI %d differs between runs: %+v vs %+v", i, first[i], second[i])
		}
		if i > 0 && first[i-1].Rank < first[i].Rank {
			t.Fatalf("POIs are not rank-sorted at %d", i)
		}
	}
	// A 120 m office tower outranks a subway station node; a station outranks a
	// small garden. This is the ordering the runtime's label budget relies on.
	tower, _ := findPOI(first, "Tower One")
	station, _ := findPOI(first, "Fixture Station")
	park, _ := findPOI(first, "Fixture Gardens")
	if !(tower.Rank > station.Rank && station.Rank > 0) {
		t.Errorf("tower %.2f should outrank station %.2f", tower.Rank, station.Rank)
	}
	if park.Rank <= 0 {
		t.Errorf("park rank %.2f", park.Rank)
	}
}

func TestClassifyPOIFallsBackWithoutDropping(t *testing.T) {
	if got := ClassifyPOI("building", "warehouse"); got != CatOther {
		t.Errorf("unmapped building kind = %q, want %q", got, CatOther)
	}
	if got := ClassifyPOI("historic", "anything_at_all"); got != CatLandmark {
		t.Errorf("historic=* = %q, want %q", got, CatLandmark)
	}
	if got := ClassifyPOI("amenity", "place_of_worship"); got != CatWorship {
		t.Errorf("place_of_worship = %q", got)
	}
}

func TestOverpassQueryAsksForNamedPOINodes(t *testing.T) {
	q := BuildOverpassQuery(BBox{South: 1, West: 2, North: 3, East: 4})
	for _, want := range []string{`node["name"]["railway"`, `node["name"]["historic"]`,
		`node["name"]["amenity"`, `node["name"]["tourism"`} {
		if !strings.Contains(q, want) {
			t.Errorf("query is missing %q:\n%s", want, q)
		}
	}
	// Every node selector must be name-filtered: an unnamed POI cannot be
	// labelled, and node["amenity"] unfiltered returns every bench in a downtown.
	for _, s := range poiNodeSelectors {
		if !strings.HasPrefix(s, `node["name"]`) {
			t.Errorf("POI selector %q is not filtered on name", s)
		}
	}
}

func TestOverpassCacheIsKeyedOnTheQuery(t *testing.T) {
	dir := t.TempDir()
	dst := filepath.Join(dir, "overpass.json")
	if err := os.WriteFile(dst, []byte(`{"elements":[]}`), 0o644); err != nil {
		t.Fatal(err)
	}
	bbox := BBox{South: 1, West: 2, North: 3, East: 4}
	// A cache with no fingerprint predates the current query and must not be
	// trusted: silently serving it would drop the POI layer with no error.
	if cachedQueryMatches(dst, BuildOverpassQuery(bbox)) {
		t.Error("a fingerprint-less cache must be treated as stale")
	}
	if err := os.WriteFile(queryFingerprintPath(dst),
		[]byte(BuildOverpassQuery(bbox)), 0o644); err != nil {
		t.Fatal(err)
	}
	if !cachedQueryMatches(dst, BuildOverpassQuery(bbox)) {
		t.Error("a cache fetched with the current query must be reused")
	}
	if cachedQueryMatches(dst, BuildOverpassQuery(BBox{South: 9, West: 9, North: 9, East: 9})) {
		t.Error("a different bbox must invalidate the cache")
	}
}

func TestPOIPathIsAPackagedProducedWork(t *testing.T) {
	p := NewPaths("/repo", "hk_victoria")
	if strings.Contains(p.POIPath(), "external") {
		t.Errorf("%s must not live in the ODbL-derived cache", p.POIPath())
	}
	if !strings.HasSuffix(filepath.ToSlash(p.POIPath()), "assets/geo/hk_victoria/poi.json") {
		t.Errorf("poi path = %q", p.POIPath())
	}
}

func TestListTilesSkipsIncompleteDirectories(t *testing.T) {
	root := t.TempDir()
	write := func(tile string, names ...string) {
		dir := filepath.Join(root, filepath.FromSlash(GeoAssetRoot), tile)
		if err := os.MkdirAll(dir, 0o755); err != nil {
			t.Fatal(err)
		}
		for _, name := range names {
			if err := os.WriteFile(filepath.Join(dir, name), []byte("x"), 0o644); err != nil {
				t.Fatal(err)
			}
		}
	}
	write("zurich", "zurich.scad", "terrain.hmap", "poi.json")
	write("apple_park", "apple_park.scad", "terrain.hmap", "poi.json")
	// A `gnb geo build` that never reached the emit stage: packing it would put a
	// tile in the pak that the runtime then refuses for having no scene.
	write("half_built", "terrain.hmap")

	tiles, err := ListTiles(root)
	if err != nil {
		t.Fatal(err)
	}
	want := []string{"apple_park", "zurich"}
	if strings.Join(tiles, ",") != strings.Join(want, ",") {
		t.Errorf("tiles = %v, want %v", tiles, want)
	}
}

func TestListTilesOnAFreshCheckoutIsEmpty(t *testing.T) {
	// assets/geo is gitignored, so a clone that has not fetched geo.pak or run
	// the generator has no directory at all. That is a normal state, not an error.
	tiles, err := ListTiles(t.TempDir())
	if err != nil {
		t.Fatalf("missing assets/geo must not be an error: %v", err)
	}
	if len(tiles) != 0 {
		t.Errorf("tiles = %v, want none", tiles)
	}
}

func poiNames(pois []POI) []string {
	out := make([]string, 0, len(pois))
	for _, p := range pois {
		out = append(out, p.Name)
	}
	return out
}

func countPOISource(pois []POI, source string) int {
	n := 0
	for _, p := range pois {
		if p.Source == source {
			n++
		}
	}
	return n
}
