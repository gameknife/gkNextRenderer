package geo

import (
	"fmt"
	"image"
	"image/color"
	"image/png"
	"math"
	"os"
	"path/filepath"
	"sort"
)

// TerrainReport records what the DEM stage had to fix, so the operator can see
// at a glance whether the data was any good.
type TerrainReport struct {
	Samples        int
	Despiked       int // isolated source outliers replaced
	BuildingMasked int // samples inside a footprint (DSM contamination)
	GroundFiltered int // cells lowered by the progressive morphological filter
	SeaCells       int
	MinLand        float64
	MaxLand        float64
	MinBed         float64
	// Water is the tile's single water surface (see WaterPlan).
	Water WaterPlan
	// BaseElevation anchors everything the scene expresses relative to "the
	// ground here": the TERR base height, and through it the palette's biome
	// bands. Absolute elevation varies from 0 m (Manhattan) to ~500 m
	// (Chengdu), so nothing downstream may assume it is near zero.
	BaseElevation float64
	// CoastalGradient is the median rise-over-distance of the near-shore land
	// (20..300 m inland). It is the cheapest available check on whether the
	// DSM -> DTM filtering actually worked: a waterfront should be close to
	// flat, so a high value means roof returns survived the filter. Zero when
	// the tile has no coastline.
	CoastalGradient float64
}

func (r TerrainReport) String() string {
	s := fmt.Sprintf("%d samples: %d despiked, %d building-masked, %d ground-filtered, %d water; "+
		"land %.1f..%.1fm, base %.1fm",
		r.Samples, r.Despiked, r.BuildingMasked, r.GroundFiltered, r.SeaCells,
		r.MinLand, r.MaxLand, r.BaseElevation)
	switch {
	case r.Water.IsSea:
		s += fmt.Sprintf(", sea level %.1fm, bed %.1fm", r.Water.Level, r.MinBed)
	case r.Water.HasWater:
		s += fmt.Sprintf(", inland water %.1fm, bed %.1fm", r.Water.Level, r.MinBed)
	default:
		s += ", no water"
	}
	if r.Water.Skipped > 0 {
		s += fmt.Sprintf(" (%d minor water bodies skipped)", r.Water.Skipped)
	}
	if r.CoastalGradient > 0 {
		s += fmt.Sprintf("; near-shore gradient %.0f%%", r.CoastalGradient*100)
	}
	return s
}

// CoastalGradientWarning is a hint, not a verdict: the metric cannot separate a
// genuinely steep natural coast from residual rooftop bias. It fires only at a
// gradient no reclaimed, built-up waterfront could plausibly have. Even below
// the threshold the absolute elevations in a dense downtown should be treated
// as approximate — see design §5.1.
func (r TerrainReport) CoastalGradientWarning() bool {
	return r.CoastalGradient > 0.15
}

// MeanSeaLevel: SRTM heights are orthometric, so the open sea really is at 0.
const MeanSeaLevel = 0.0

const (
	seaMinDepth   = 2.0  // right at the shore
	seaMaxDepth   = 12.0 // shipping channel
	seaDepthSlope = 0.15 // metres of depth per metre from shore
	// Inland water is shallow and its bed is not a shipping channel.
	inlandMinDepth = 1.0
	inlandMaxDepth = 4.0
	// A cell can only be flooded if the DEM agrees it is near the water plane;
	// this keeps a hill behind a curving coastline from being submerged.
	floodBandAboveWater = 20.0
	// Dry land never sits below the water plane.
	minLandFreeboard = 0.4
	// Water bodies smaller than this are ornamental (fountains, memorial pools,
	// hotel ponds). Carving them to a bed and giving them the tile's water plane
	// turns them into pits — measured on Lower Manhattan, where the two 2 944 m²
	// 9/11 Memorial pools became 5 m holes in the middle of the island.
	minWaterAreaM2 = 6000.0
)

// ElevationSource is whatever can answer "how high is the ground at this
// lat/lon" — the SRTM Sampler in production, a stub in tests, and the seam a
// higher-resolution DTM provider would plug into.
type ElevationSource interface {
	At(lat, lon float64) (float64, bool)
}

// WaterPlan is the tile's single water surface. TERR carries one global water
// level, so the generator picks the dominant body and reports the rest.
//
// This is what makes an inland city work at all: the water plane cannot be
// assumed to sit at z = 0. Chengdu's Jin River is at ~500 m and Paris's Seine
// at ~26 m; with a hard-coded sea level their rivers either vanish (every cell
// fails the "is it low enough" test) or appear as a canyon far below the town.
type WaterPlan struct {
	HasWater bool
	IsSea    bool    // fed by a coastline rather than an inland polygon
	Level    float64 // water surface elevation, metres
	MinDepth float64
	MaxDepth float64
	Skipped  int // minor water bodies left out of the mask
}

// planWater decides the tile's water surface from the OSM geometry plus the
// (already filtered) elevation model.
func planWater(ir *IR, g *HeightGrid, elevation []float64) WaterPlan {
	// A coastline means the tile touches the sea, whose level we know exactly.
	if len(ir.Coastline) > 0 {
		return WaterPlan{HasWater: true, IsSea: true, Level: MeanSeaLevel,
			MinDepth: seaMinDepth, MaxDepth: seaMaxDepth}
	}

	// Otherwise the largest water polygon sets the level. The DEM over open
	// water reads the surface, so the median of the samples inside the body is
	// the water elevation.
	var best *Area
	bestArea := 0.0
	skipped := 0
	for i := range ir.Waters {
		area := RingArea(ir.Waters[i].Outer)
		if area < minWaterAreaM2 {
			skipped++
			continue
		}
		if area > bestArea {
			bestArea = area
			best = &ir.Waters[i]
		}
	}
	if best == nil {
		return WaterPlan{Skipped: skipped}
	}

	var inside []float64
	for row := 0; row < g.Rows; row++ {
		for col := 0; col < g.Cols; col++ {
			if PointInRing(best.Outer, g.PosX(col), g.PosY(row)) {
				inside = append(inside, elevation[row*g.Cols+col])
			}
		}
	}
	if len(inside) == 0 {
		// The body is inside the fetch pad but misses every grid sample.
		return WaterPlan{Skipped: skipped}
	}
	sort.Float64s(inside)
	return WaterPlan{
		HasWater: true, Level: inside[len(inside)/2],
		MinDepth: inlandMinDepth, MaxDepth: inlandMaxDepth, Skipped: skipped,
	}
}

// BuildTerrain samples the DEM onto the tile grid and cleans it up:
// despike -> building mask + inpaint -> morphological opening -> sea bed.
//
// The building mask is the important one. SRTM is a surface model: its C-band
// return sits on rooftops, so using it raw puts every tower on its own mesa and
// then stacks the OSM building on top of that. Because we already have the
// footprints, masking them out and interpolating from the surrounding ground
// recovers a usable bare-earth model for free.
func BuildTerrain(tile Tile, ir *IR, sampler ElevationSource, debugDir string) (*HeightGrid, TerrainReport, error) {
	return BuildTerrainField(TerrainArea{
		SizeM: tile.SizeM, Cells: tile.Cells, Unproject: tile.Unproject,
	}, ir, sampler, debugDir)
}

// TerrainArea is the ground BuildTerrainField works over. For a standalone tile
// that is the tile; for an area it is the whole grid of parts at once.
//
// The distinction matters because every step below this line is a
// *neighbourhood* operation: the morphological ground filter reaches 12 cells
// (68 m), the footprint inpaint pulls from whatever is around the hole, the
// blur averages a 5x5, and the water plan and datum are single numbers derived
// from the whole sample set. Run per part, each of those clamps at the part
// border and the two sides of a seam disagree — measured on two adjacent
// Manhattan parts, mean 0.24 m and max 0.98 m, which is a visible crack and a
// nav-grid cut. Run once over the area and sliced afterwards, the shared edge
// samples are literally the same numbers.
type TerrainArea struct {
	SizeM float64 // side length of the whole area
	Cells int     // terrain cells per axis over the whole area
	// Unproject maps area-local metres back to WGS84 for the DEM sampler.
	Unproject func(x, y float64) (lat, lon float64)
}

// BuildTerrainField resamples the DEM over the area, strips it from a DSM to a
// DTM, plans the water and returns the vertex grid plus a data-quality report.
// The IR it reads (footprints, water polygons, coastline) must be in the same
// area-local frame as the grid.
func BuildTerrainField(area TerrainArea, ir *IR, sampler ElevationSource, debugDir string) (*HeightGrid, TerrainReport, error) {
	n := area.Cells + 1 // vertex grid: cells+1 samples per axis
	cell := area.SizeM / float64(area.Cells)
	origin := -area.SizeM / 2
	grid := NewHeightGrid(n, n, origin, origin, cell, cell)
	report := TerrainReport{Samples: n * n}

	raw := make([]float64, n*n)
	for row := 0; row < n; row++ {
		for col := 0; col < n; col++ {
			lat, lon := area.Unproject(grid.PosX(col), grid.PosY(row))
			v, ok := sampler.At(lat, lon)
			if !ok {
				v = 0
			}
			raw[row*n+col] = v
		}
	}
	writeDebugPNG(debugDir, "01_dem_raw.png", raw, n, n)

	work := append([]float64(nil), raw...)
	report.Despiked = despike(work, n, n)

	// --- DSM -> DTM ---
	// Order matters: the morphological filter runs first, on the raw surface,
	// so the footprint inpaint afterwards draws from already-cleaned ground
	// rather than interpolating neighbouring roof returns *into* the
	// footprints. On the reference tile the filter itself is what moves the
	// numbers (a Central reference point drops from 52.6 m to 24.7 m); the
	// ordering is worth a further metre or two and costs nothing.
	report.GroundFiltered = progressiveGroundFilter(work, n, n, cell)
	writeDebugPNG(debugDir, "02_dem_ground.png", work, n, n)

	masked := make([]bool, n*n)
	for _, b := range ir.Buildings {
		markRing(masked, grid, b.Outer, 1.0)
	}
	for _, m := range masked {
		if m {
			report.BuildingMasked++
		}
	}
	inpaint(work, masked, n, n)
	// The source posts every ~30 m and we resample to ~5.7 m, so anything
	// sharper than the source spacing is interpolation artefact plus residual
	// DSM noise. Left in, it produces 1..3 m steps between adjacent metres,
	// which fragments a nav grid into disconnected patches (measured: 28% of
	// 1 m transitions exceeded a 0.6 m step before this was widened). Two
	// passes at radius 2 bring the surface back to something a city street
	// plausibly is, without touching the macro relief.
	work = boxBlur(work, n, n, 2)
	work = boxBlur(work, n, n, 2)
	writeDebugPNG(debugDir, "02b_dem_unbuilt.png", work, n, n)

	// --- Water ---
	plan := planWater(ir, grid, work)
	report.Water = plan
	sea := classifyWater(ir, grid, work, plan)
	shoreDist := distanceToLand(sea, n, n, cell)
	for i := range work {
		if !sea[i] {
			// Land must stay above the water plane. SRTM dips slightly below it
			// near a shore and the filtering pushes it further down; without
			// this the quayside renders as a flooded strip.
			if plan.HasWater {
				work[i] = math.Max(work[i], plan.Level+minLandFreeboard)
			}
			continue
		}
		report.SeaCells++
		depth := math.Min(plan.MinDepth+seaDepthSlope*shoreDist[i], plan.MaxDepth)
		work[i] = plan.Level - depth
	}

	report.MinLand, report.MaxLand = math.Inf(1), math.Inf(-1)
	report.MinBed = math.Inf(1)
	for i, v := range work {
		if sea[i] {
			report.MinBed = math.Min(report.MinBed, v)
			continue
		}
		report.MinLand = math.Min(report.MinLand, v)
		report.MaxLand = math.Max(report.MaxLand, v)
	}
	if math.IsInf(report.MinLand, 1) {
		report.MinLand, report.MaxLand = 0, 0
	}
	if math.IsInf(report.MinBed, 1) {
		report.MinBed = 0
	}

	// The scene datum: the water plane when there is one, otherwise the low
	// percentile of the land (a robust "floor of this town" that a single dip
	// cannot drag down).
	if plan.HasWater {
		report.BaseElevation = plan.Level
	} else {
		sorted := append([]float64(nil), work...)
		sort.Float64s(sorted)
		report.BaseElevation = sorted[len(sorted)/20]
	}

	report.CoastalGradient = coastalGradient(work, sea, n, n, cell, plan.Level)

	copy(grid.Values, work)
	writeDebugPNG(debugDir, "03_terrain_final.png", work, n, n)
	return grid, report, nil
}

// coastalGradient is the median height/distance of land 20..300 m from the
// shore. A built-up waterfront is close to flat, so this is a cheap proxy for
// how much rooftop bias survived the DSM filtering.
func coastalGradient(v []float64, sea []bool, w, h int, cellM float64, waterLevel float64) float64 {
	hasSea := false
	for _, s := range sea {
		if s {
			hasSea = true
			break
		}
	}
	if !hasSea {
		return 0
	}
	land := make([]bool, len(sea))
	for i := range sea {
		land[i] = !sea[i]
	}
	distToSea := distanceToLand(land, w, h, cellM) // roles swapped: land is the "sea" here
	var ratios []float64
	for i := range v {
		if sea[i] || distToSea[i] < 20 || distToSea[i] > 300 {
			continue
		}
		ratios = append(ratios, math.Max(0, v[i]-waterLevel)/distToSea[i])
	}
	if len(ratios) == 0 {
		return 0
	}
	sort.Float64s(ratios)
	return ratios[len(ratios)/2]
}

// despike replaces samples that disagree with every neighbour by a large
// margin — the signature of source corruption rather than a real cliff.
func despike(v []float64, w, h int) int {
	fixed := 0
	out := append([]float64(nil), v...)
	for row := 0; row < h; row++ {
		for col := 0; col < w; col++ {
			i := row*w + col
			var neigh []float64
			for dr := -1; dr <= 1; dr++ {
				for dc := -1; dc <= 1; dc++ {
					if dr == 0 && dc == 0 {
						continue
					}
					r, c := row+dr, col+dc
					if r < 0 || r >= h || c < 0 || c >= w {
						continue
					}
					neigh = append(neigh, v[r*w+c])
				}
			}
			if len(neigh) < 5 {
				continue
			}
			sort.Float64s(neigh)
			median := neigh[len(neigh)/2]
			if math.Abs(v[i]-median) < 25 {
				continue
			}
			isolated := true
			for _, nv := range neigh {
				if math.Abs(v[i]-nv) < 15 {
					isolated = false
					break
				}
			}
			if isolated {
				out[i] = median
				fixed++
			}
		}
	}
	copy(v, out)
	return fixed
}

// markRing rasterises a polygon into a boolean mask, dilated by padM metres so
// the roof edge does not leak back in through a bilinear tap.
func markRing(mask []bool, g *HeightGrid, ring Ring, padM float64) {
	if len(ring) < 3 {
		return
	}
	minX, minY, maxX, maxY := BoundsOf(ring)
	c0 := int(math.Floor((minX - padM - g.OriginX) / g.CellX))
	c1 := int(math.Ceil((maxX + padM - g.OriginX) / g.CellX))
	r0 := int(math.Floor((minY - padM - g.OriginY) / g.CellY))
	r1 := int(math.Ceil((maxY + padM - g.OriginY) / g.CellY))
	for row := max(0, r0); row <= min(g.Rows-1, r1); row++ {
		for col := max(0, c0); col <= min(g.Cols-1, c1); col++ {
			x, y := g.PosX(col), g.PosY(row)
			if PointInRing(ring, x, y) || ringDistance(ring, x, y) <= padM {
				mask[row*g.Cols+col] = true
			}
		}
	}
}

func ringDistance(ring Ring, x, y float64) float64 {
	closed := append(append([][2]float64{}, ring...), ring[0])
	return PolylineDistance(closed, x, y)
}

// inpaint fills masked samples from the nearest unmasked ones (inverse distance
// over a widening ring). Deterministic and dependency-free.
func inpaint(v []float64, mask []bool, w, h int) {
	type todo struct{ row, col int }
	var pending []todo
	for row := 0; row < h; row++ {
		for col := 0; col < w; col++ {
			if mask[row*w+col] {
				pending = append(pending, todo{row, col})
			}
		}
	}
	if len(pending) == 0 || len(pending) == w*h {
		return
	}
	for _, t := range pending {
		var acc, weight float64
		for radius := 1; radius <= 24 && weight == 0; radius++ {
			for dr := -radius; dr <= radius; dr++ {
				for dc := -radius; dc <= radius; dc++ {
					// Only the shell of the square, so the nearest ground wins.
					if abs(dr) != radius && abs(dc) != radius {
						continue
					}
					r, c := t.row+dr, t.col+dc
					if r < 0 || r >= h || c < 0 || c >= w || mask[r*w+c] {
						continue
					}
					wgt := 1.0 / float64(radius)
					acc += v[r*w+c] * wgt
					weight += wgt
				}
			}
		}
		if weight > 0 {
			v[t.row*w+t.col] = acc / weight
		}
	}
}

func morphOpen(v []float64, w, h, radius int) []float64 {
	return dilate(erode(v, w, h, radius), w, h, radius)
}

// Progressive morphological filter (the standard DSM -> DTM approach, after
// Zhang et al.): open the surface with a geometrically growing structuring
// element and, at each scale, replace anything standing higher above the
// opened surface than a slope-derived threshold.
//
// The OSM footprint mask alone is not enough in a dense downtown. SRTM posts
// every ~30 m, so a tower's roof return smears well beyond its outline, and the
// cells just outside the footprint keep roof-level heights — which the inpaint
// then happily interpolates back in. Measured on the Hong Kong reference tile,
// the footprint mask alone left the Sheung Wan waterfront sitting at 40..70 m
// instead of 5..15 m.
var groundFilterWindows = []int{1, 2, 4, 8, 12}

const (
	// A genuine slope this steep is believable; anything steeper over the
	// window is treated as a structure. Hong Kong's Mid-Levels run to ~30%.
	groundFilterSlope    = 0.32
	groundFilterBaseM    = 1.2
	groundFilterMaxDropM = 22.0
)

func progressiveGroundFilter(v []float64, w, h int, cellM float64) int {
	lowered := 0
	for _, radius := range groundFilterWindows {
		opened := morphOpen(v, w, h, radius)
		windowM := float64(2*radius) * cellM
		threshold := math.Min(groundFilterBaseM+groundFilterSlope*windowM, groundFilterMaxDropM)
		for i := range v {
			if v[i]-opened[i] > threshold {
				v[i] = opened[i]
				lowered++
			}
		}
	}
	return lowered
}

func erode(v []float64, w, h, radius int) []float64 {
	return morph(v, w, h, radius, math.Min)
}

func dilate(v []float64, w, h, radius int) []float64 {
	return morph(v, w, h, radius, math.Max)
}

// morph runs a min/max over a square window. It is separable and done in two
// passes: a min (or max) over a clipped rectangle is the same number whichever
// axis is reduced first, so this is bit-for-bit what the naive double loop
// produced — but (2r+1)+(2r+1) taps instead of (2r+1)^2. At radius 12 that is
// 50 taps rather than 625, which is the difference between seconds and minutes
// once the grid is 881^2 (a 5x5 area) instead of 177^2.
func morph(v []float64, w, h, radius int, pick func(a, b float64) float64) []float64 {
	tmp := make([]float64, len(v))
	for row := 0; row < h; row++ {
		base := row * w
		for col := 0; col < w; col++ {
			lo := col - radius
			if lo < 0 {
				lo = 0
			}
			hi := col + radius
			if hi > w-1 {
				hi = w - 1
			}
			best := v[base+lo]
			for c := lo + 1; c <= hi; c++ {
				best = pick(best, v[base+c])
			}
			tmp[base+col] = best
		}
	}
	out := make([]float64, len(v))
	for row := 0; row < h; row++ {
		lo := row - radius
		if lo < 0 {
			lo = 0
		}
		hi := row + radius
		if hi > h-1 {
			hi = h - 1
		}
		for col := 0; col < w; col++ {
			best := tmp[lo*w+col]
			for r := lo + 1; r <= hi; r++ {
				best = pick(best, tmp[r*w+col])
			}
			out[row*w+col] = best
		}
	}
	return out
}

func boxBlur(v []float64, w, h, radius int) []float64 {
	out := make([]float64, len(v))
	for row := 0; row < h; row++ {
		for col := 0; col < w; col++ {
			var sum float64
			var n int
			for dr := -radius; dr <= radius; dr++ {
				for dc := -radius; dc <= radius; dc++ {
					r, c := row+dr, col+dc
					if r < 0 || r >= h || c < 0 || c >= w {
						continue
					}
					sum += v[r*w+c]
					n++
				}
			}
			out[row*w+col] = sum / float64(n)
		}
	}
	return out
}

// classifyWater decides which samples are open water.
//
// The DEM is not trusted for this: SRTM over water is noisy (2..9m across
// Victoria Harbour in the reference tile). OSM is: water polygons are explicit,
// and a coastline way carries land on its left by convention, so the side of
// the nearest coastline segment tells us which way the sea is.
func classifyWater(ir *IR, g *HeightGrid, elevation []float64, plan WaterPlan) []bool {
	sea := make([]bool, g.Cols*g.Rows)
	if !plan.HasWater {
		return sea
	}
	for row := 0; row < g.Rows; row++ {
		for col := 0; col < g.Cols; col++ {
			i := row*g.Cols + col
			x, y := g.PosX(col), g.PosY(row)
			// The band is relative to the tile's water plane, not to sea level:
			// an absolute test drops every inland river.
			if elevation[i] > plan.Level+floodBandAboveWater {
				continue
			}
			for _, wtr := range ir.Waters {
				if RingArea(wtr.Outer) < minWaterAreaM2 {
					continue
				}
				if !PointInRing(wtr.Outer, x, y) {
					continue
				}
				inHole := false
				for _, hole := range wtr.Inners {
					if PointInRing(hole, x, y) {
						inHole = true
						break
					}
				}
				if !inHole {
					sea[i] = true
					break
				}
			}
			if sea[i] || len(ir.Coastline) == 0 {
				continue
			}
			if seaSideOfCoastline(ir.Coastline, x, y) {
				sea[i] = true
			}
		}
	}
	return sea
}

// seaSideOfCoastline reports whether (x, y) lies on the seaward (right-hand)
// side of the nearest coastline segment.
func seaSideOfCoastline(lines []Line, x, y float64) bool {
	best := math.Inf(1)
	right := false
	p := [2]float64{x, y}
	for _, ln := range lines {
		for i := 0; i+1 < len(ln.Pts); i++ {
			a, b := ln.Pts[i], ln.Pts[i+1]
			d := perpDistance(p, a, b)
			if d >= best {
				continue
			}
			best = d
			cross := (b[0]-a[0])*(y-a[1]) - (b[1]-a[1])*(x-a[0])
			right = cross < 0 // negative cross product == right of the way
		}
	}
	return !math.IsInf(best, 1) && right
}

// distanceToLand is a two-pass chamfer distance transform, in metres.
func distanceToLand(sea []bool, w, h int, cell float64) []float64 {
	const big = 1e9
	d := make([]float64, len(sea))
	for i := range d {
		if sea[i] {
			d[i] = big
		}
	}
	relax := func(i, j int, cost float64) {
		if d[j]+cost < d[i] {
			d[i] = d[j] + cost
		}
	}
	diag := math.Sqrt2
	for row := 0; row < h; row++ {
		for col := 0; col < w; col++ {
			i := row*w + col
			if col > 0 {
				relax(i, i-1, 1)
			}
			if row > 0 {
				relax(i, i-w, 1)
				if col > 0 {
					relax(i, i-w-1, diag)
				}
				if col < w-1 {
					relax(i, i-w+1, diag)
				}
			}
		}
	}
	for row := h - 1; row >= 0; row-- {
		for col := w - 1; col >= 0; col-- {
			i := row*w + col
			if col < w-1 {
				relax(i, i+1, 1)
			}
			if row < h-1 {
				relax(i, i+w, 1)
				if col < w-1 {
					relax(i, i+w+1, diag)
				}
				if col > 0 {
					relax(i, i+w-1, diag)
				}
			}
		}
	}
	for i := range d {
		d[i] *= cell
	}
	return d
}

// writeDebugPNG dumps a normalised grayscale view of a stage. Best-effort: a
// failure here never fails the build.
func writeDebugPNG(dir, name string, v []float64, w, h int) {
	if dir == "" {
		return
	}
	if err := os.MkdirAll(dir, 0o755); err != nil {
		return
	}
	lo, hi := math.Inf(1), math.Inf(-1)
	for _, s := range v {
		lo = math.Min(lo, s)
		hi = math.Max(hi, s)
	}
	span := hi - lo
	if span < 1e-6 {
		span = 1
	}
	img := image.NewGray(image.Rect(0, 0, w, h))
	for row := 0; row < h; row++ {
		for col := 0; col < w; col++ {
			g := uint8(clampF((v[row*w+col]-lo)/span*255, 0, 255))
			// Flip so north is up in the image.
			img.SetGray(col, h-1-row, color.Gray{Y: g})
		}
	}
	f, err := os.Create(filepath.Join(dir, name))
	if err != nil {
		return
	}
	defer f.Close()
	_ = png.Encode(f, img)
}
