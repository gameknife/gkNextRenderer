package geo

import "math"

// Detail is stage D's second half: deciding what each building *looks* like,
// as opposed to how tall it is.
//
// Nothing here emits geometry. The generator classifies and measures — which
// facade scheme, which roof, and the minimum-area rectangle a pitched roof
// needs a ridge direction from — and assets/scad/lib/kit_geo_city.scad turns
// that into triangles. Same split as roadnet.go / kit_road.scad, and for the
// same reason: "make every building in every tile look different" then has to
// be one edit in one file instead of a regeneration of every tile.
//
// Everything is derived from the OSM tags, the footprint and the tile seed, so
// it stays deterministic: the same tile regenerates byte for byte.

// buildingStyle is exactly what one gc_bld() call carries.
type buildingStyle struct {
	Facade    int     // index into gc_FAC
	WallTone  int     // index into gc_WALL
	GlassTone int     // index into gc_GLASS
	FloorH    float64 // storey height, drives the spandrel spacing
	Seed      int

	RoofKind  int // 0 none, 1 flat + parapet, 2 pitched
	RoofTone  int // index into gc_ROOF
	RoofRise  float64
	RidgeFrac float64 // 1 = gable, < 1 = hipped, -> 0 = pyramid
	Clutter   int     // 0..3, roof plant density

	OBB    [5]float64 // cx, cy, w, d, angDeg — w >= d, w is the ridge axis
	HasOBB bool
	// Anchor is a point that is certainly inside the footprint, with the
	// clearance radius around it. Roof plant is placed here rather than in the
	// oriented box: the box of an L-shaped building covers the notch, and a
	// water tank scattered into the notch hangs in mid-air over the street.
	//
	// That is not only ugly. The nav grid finds a cell's floor by casting down
	// from above, so an object floating 40 m over the pavement makes those cells
	// report a floor 40 m up — a step no neighbour can take — and the street is
	// severed. It cost a failing Test_GeoCityWalkable to find.
	Anchor [3]float64 // x, y, radius
}

// DetailProfile is the regional half of the look. It is keyed by the same
// --profile as HeightProfile: a European old town, a mainland Chinese district
// and Hong Kong differ far more in *roofline* than in storey height, and the
// roofline is what reads first from the air.
type DetailProfile struct {
	Name string
	// GlassMinH: above this a commercial building becomes a curtain-wall tower.
	GlassMinH float64
	// MasonryMaxH: below this, punched masonry windows (deep spandrels, wide
	// piers). Zero disables the scheme — mainland/HK mid-rise is banded
	// concrete, not stone with holes in it.
	MasonryMaxH float64
	// A pitched roof is only plausible on a small, low, roughly rectangular
	// footprint. Everything else gets a flat roof with a parapet, which is what
	// a dense city actually has.
	PitchedMaxH       float64
	PitchedMaxAreaM2  float64
	MinRectangularity float64
	PitchDeg          [2]float64
	// HipBias: 0 = always gabled, 1 = always hipped. Elongated footprints skew
	// gabled regardless; this decides the square-ish ones.
	HipBias   float64
	RoofTones []int
	WallTones []int
	// ClutterBoost lifts every roof one or two notches: Hong Kong rooftops are
	// a forest of water tanks and antennas, and leaving them bare is the single
	// most obvious tell when looking down at the tile.
	ClutterBoost int
}

// DetailProfiles are keyed by --profile, same as Profiles.
var DetailProfiles = map[string]DetailProfile{
	"default": {
		Name:              "default",
		GlassMinH:         55,
		MasonryMaxH:       32,
		PitchedMaxH:       14,
		PitchedMaxAreaM2:  900,
		MinRectangularity: 0.68,
		PitchDeg:          [2]float64{29, 42},
		HipBias:           0.35,
		RoofTones:         []int{0, 1, 2, 4},
		WallTones:         []int{0, 2, 3, 4, 7},
		ClutterBoost:      0,
	},
	// European old towns. The one rule that matters here: **the pitched roof
	// threshold has to clear the perimeter block**, ~20 m. With "default"'s 14 m
	// every Haussmann block in Paris came out flat-topped and grey, which is the
	// single most obvious thing wrong with a European tile seen from the air —
	// that city is a field of steep zinc and slate roofs.
	//
	// The rise cap in classifyBuilding is doing real work here: a 40-degree
	// pitch over a 20 m deep block would be 8 m of roof, and capping it at 6 m
	// is roughly what a mansard does anyway.
	"europe": {
		Name:              "europe",
		GlassMinH:         60,
		MasonryMaxH:       34,
		PitchedMaxH:       26,
		PitchedMaxAreaM2:  2200,
		MinRectangularity: 0.66,
		PitchDeg:          [2]float64{34, 48},
		HipBias:           0.5,
		RoofTones:         []int{1, 3, 0, 4},
		WallTones:         []int{0, 2, 7, 4, 3},
		ClutterBoost:      0,
	},
	// Mainland Chinese districts: flat concrete roofs almost everywhere, low
	// grey-tile hips on the surviving low-rise, and solar water heaters on top
	// of the mid-rise slabs — the clutter boost is what puts those there.
	"china": {
		Name:              "china",
		GlassMinH:         60,
		MasonryMaxH:       0,
		PitchedMaxH:       10,
		PitchedMaxAreaM2:  520,
		MinRectangularity: 0.72,
		PitchDeg:          [2]float64{17, 27},
		HipBias:           0.75,
		RoofTones:         []int{2, 3, 5},
		WallTones:         []int{0, 1, 2, 3, 7},
		ClutterBoost:      1,
	},
	// Hong Kong: towers start low, and the roofline is flat essentially
	// everywhere — even the village houses are three flat-roofed storeys, so the
	// pitched threshold is low enough that only sheds reach it. Every roof
	// carries tanks and masts.
	"hongkong": {
		Name:              "hongkong",
		GlassMinH:         45,
		MasonryMaxH:       0,
		PitchedMaxH:       5.5,
		PitchedMaxAreaM2:  320,
		MinRectangularity: 0.75,
		PitchDeg:          [2]float64{14, 22},
		HipBias:           0.5,
		RoofTones:         []int{2, 3, 1},
		WallTones:         []int{1, 3, 5, 6, 0},
		ClutterBoost:      2,
	},
}

// DetailProfileFor falls back to "default" so an unknown --profile still emits
// a plausible city rather than nothing.
func DetailProfileFor(name string) DetailProfile {
	if dp, ok := DetailProfiles[name]; ok {
		return dp
	}
	return DetailProfiles["default"]
}

// Kinds that never deserve a window grid: sheds, garages, canopies.
var plainKinds = map[string]bool{
	"shed": true, "hut": true, "garage": true, "garages": true, "carport": true,
	"roof": true, "kiosk": true, "service": true, "toilets": true, "container": true,
}

// Kinds with big blank walls and a ribbon of glass near the top.
var industrialKinds = map[string]bool{
	"industrial": true, "warehouse": true, "factory": true, "hangar": true,
	"depot": true, "storage_tank": true, "parking": true, "construction": true,
}

var coolWallTones = []int{1, 3, 6}
var glassTowerTones = []int{1, 0}
var glassOtherTones = []int{2, 3, 0}

// RoofAnchor returns a point inside the footprint together with the distance
// from it to the nearest edge — including the edges of any courtyard.
//
// This is the "pole of inaccessibility" done cheaply: the centroid plus a coarse
// grid, keeping whichever candidate is inside and furthest from an edge. Exact
// would be a medial axis; a footprint simplified to 24 vertices does not need
// one, and everything downstream only asks "is there room for a water tank".
func RoofAnchor(ring Ring, inners []Ring) [3]float64 {
	clearance := func(p [2]float64) float64 {
		if !PointInRing(ring, p[0], p[1]) {
			return -1
		}
		for _, hole := range inners {
			if PointInRing(hole, p[0], p[1]) {
				return -1
			}
		}
		best := math.Inf(1)
		edges := append([]Ring{ring}, inners...)
		for _, r := range edges {
			for i := range r {
				a, b := r[i], r[(i+1)%len(r)]
				if d := SegmentDistance(p, a, b); d < best {
					best = d
				}
			}
		}
		if math.IsInf(best, 1) {
			return -1
		}
		return best
	}

	best := [3]float64{}
	bestR := -1.0
	consider := func(p [2]float64) {
		if r := clearance(p); r > bestR {
			bestR, best = r, [3]float64{p[0], p[1], r}
		}
	}

	consider(Centroid(ring))
	minX, minY, maxX, maxY := BoundsOf(ring)
	const n = 7
	for i := 1; i <= n; i++ {
		for j := 1; j <= n; j++ {
			consider([2]float64{
				minX + (maxX-minX)*float64(i)/float64(n+1),
				minY + (maxY-minY)*float64(j)/float64(n+1),
			})
		}
	}
	if bestR <= 0 {
		return [3]float64{}
	}
	return best
}

// classifyBuilding picks the whole look for one footprint.
func classifyBuilding(b Building, ring Ring, inners []Ring, dp DetailProfile, hp HeightProfile, seed int) buildingStyle {
	h := math.Max(3, b.Height)
	area := RingArea(ring)
	r0 := mix64(uint64(b.ID)*0x9E3779B97F4A7C15 ^ uint64(seed))
	r1 := mix64(r0 + 0x2545F4914F6CDD1D)
	r2 := mix64(r1 + 0x9E3779B97F4A7C15)

	floorH := hp.FloorHeight[b.Kind]
	if floorH <= 0 {
		floorH = hp.FloorHeight[""]
	}
	if floorH <= 0 {
		floorH = 3.2
	}

	st := buildingStyle{
		FloorH: floorH,
		Seed:   int(r0 % 90000),
	}

	// The oriented box and the inside-anchor are measured up front: the facade
	// needs the anchor too, because the shell is inset by the relief depth and a
	// footprint thinner than twice that would fold in on itself.
	obb, rect := MinAreaRect(ring)
	st.OBB = obb
	st.HasOBB = obb[2] > 0.5 && obb[3] > 0.5
	st.Anchor = RoofAnchor(ring, inners)

	// ---- facade -------------------------------------------------------------
	switch {
	case h < 4.5 || area < 45 || plainKinds[b.Kind] || st.Anchor[2] < 0.9:
		st.Facade = 0
	case industrialKinds[b.Kind]:
		st.Facade = 4
	case h >= dp.GlassMinH:
		st.Facade = 1
	case dp.MasonryMaxH > 0 && h <= dp.MasonryMaxH:
		st.Facade = 3
	case h <= 9.5:
		st.Facade = 5
	default:
		st.Facade = 2
	}

	pool := dp.WallTones
	if st.Facade == 1 {
		pool = coolWallTones
	}
	st.WallTone = pool[int(r1%uint64(len(pool)))]
	if st.Facade == 1 {
		st.GlassTone = glassTowerTones[int(r2%uint64(len(glassTowerTones)))]
	} else {
		st.GlassTone = glassOtherTones[int(r2%uint64(len(glassOtherTones)))]
	}

	// ---- roof ---------------------------------------------------------------
	st.RoofTone = dp.RoofTones[int(r1>>13%uint64(len(dp.RoofTones)))]

	pitched := st.HasOBB &&
		h <= dp.PitchedMaxH &&
		area <= dp.PitchedMaxAreaM2 &&
		rect >= dp.MinRectangularity &&
		obb[3] > 3.0

	if pitched {
		st.RoofKind = 2
		u := float64(r2>>17%1000) / 1000.0
		pitch := dp.PitchDeg[0] + (dp.PitchDeg[1]-dp.PitchDeg[0])*u
		// Rise is measured from the eave, which overhangs by gc_EAVE().
		st.RoofRise = math.Tan(pitch*math.Pi/180) * (obb[3]/2 + 0.45)
		st.RoofRise = math.Min(st.RoofRise, math.Min(6.0, obb[2]*0.6))
		ratio := obb[2] / math.Max(obb[3], 0.1)
		hip := ratio < 1.35 || float64(r0>>21%1000)/1000.0 < dp.HipBias
		if hip {
			st.RidgeFrac = clamp((obb[2]-obb[3])/math.Max(obb[2], 0.1), 0.08, 0.75)
		} else {
			st.RidgeFrac = 1.0
		}
		return st
	}

	st.RoofKind = 1
	switch {
	case h >= 90:
		st.Clutter = 3
	case h >= 35:
		st.Clutter = 2
	case h >= 12:
		st.Clutter = 1
	}
	st.Clutter += dp.ClutterBoost
	if st.Clutter > 3 {
		st.Clutter = 3
	}
	// A 40 m² roof cannot host a stair penthouse and a mast without looking
	// like a scrapyard.
	if area < 70 && st.Clutter > 1 {
		st.Clutter = 1
	}
	if area < 40 {
		st.Clutter = 0
	}
	// No room to stand something clear of the parapet: leave the roof bare
	// rather than let an item hang over the edge (see buildingStyle.Anchor).
	if st.Anchor[2] < 2.5 {
		st.Clutter = 0
	}
	return st
}

func clamp(v, lo, hi float64) float64 { return math.Max(lo, math.Min(hi, v)) }

// mix64 is splitmix64's finaliser: a cheap, well-distributed integer hash. The
// OSM id alone is a terrible seed — ids are sequential, so consecutive
// buildings would get consecutive palette entries and the street would stripe.
func mix64(v uint64) uint64 {
	v ^= v >> 33
	v *= 0xff51afd7ed558ccd
	v ^= v >> 33
	v *= 0xc4ceb9fe1a85ec53
	v ^= v >> 33
	return v
}

// ---------------------------------------------------------------------------
// minimum-area bounding rectangle
// ---------------------------------------------------------------------------

// MinAreaRect returns the minimum-area oriented bounding box of a footprint as
// [cx, cy, w, d, angleDeg] with w >= d, plus the rectangularity (footprint area
// / rectangle area, 1 for a true rectangle).
//
// The ridge of a pitched roof runs along the long axis of the building, and
// "long axis" is only meaningful for a footprint that is close to a rectangle
// in the first place — hence the rectangularity, which is what decides whether
// a building gets a pitched roof at all. A general polygon would need a
// straight skeleton, which is not worth it for buildings that are, in practice,
// nearly all boxes.
//
// By the rotating-calipers theorem the optimal rectangle shares an edge with
// the convex hull, so trying every hull edge is exact, not a heuristic.
func MinAreaRect(ring Ring) (obb [5]float64, rectangularity float64) {
	// convexHull lives in roadnet.go; Ring is [][2]float64 so it takes it as is.
	hull := Ring(convexHull(ring))
	if len(hull) < 3 {
		minX, minY, maxX, maxY := BoundsOf(ring)
		w, d := maxX-minX, maxY-minY
		if w < d {
			return [5]float64{(minX + maxX) / 2, (minY + maxY) / 2, d, w, 90}, 1
		}
		return [5]float64{(minX + maxX) / 2, (minY + maxY) / 2, w, d, 0}, 1
	}

	bestArea := math.Inf(1)
	var bcx, bcy, bw, bd, bang float64
	for i := range hull {
		a, b := hull[i], hull[(i+1)%len(hull)]
		dx, dy := b[0]-a[0], b[1]-a[1]
		l := math.Hypot(dx, dy)
		if l < 1e-9 {
			continue
		}
		ux, uy := dx/l, dy/l
		minU, maxU := math.Inf(1), math.Inf(-1)
		minV, maxV := math.Inf(1), math.Inf(-1)
		for _, p := range hull {
			qx, qy := p[0]-a[0], p[1]-a[1]
			u := qx*ux + qy*uy
			v := -qx*uy + qy*ux
			minU, maxU = math.Min(minU, u), math.Max(maxU, u)
			minV, maxV = math.Min(minV, v), math.Max(maxV, v)
		}
		w, d := maxU-minU, maxV-minV
		if area := w * d; area < bestArea {
			bestArea = area
			cu, cv := (minU+maxU)/2, (minV+maxV)/2
			bcx = a[0] + ux*cu - uy*cv
			bcy = a[1] + uy*cu + ux*cv
			bw, bd = w, d
			bang = math.Atan2(uy, ux) * 180 / math.Pi
		}
	}
	if math.IsInf(bestArea, 1) || bestArea < 1e-6 {
		return [5]float64{}, 0
	}
	if bw < bd {
		bw, bd = bd, bw
		bang += 90
	}
	// Keep the angle in a compact range so the emitted number stays short and
	// two identical buildings never differ by a full turn.
	for bang >= 180 {
		bang -= 180
	}
	for bang < 0 {
		bang += 180
	}
	return [5]float64{bcx, bcy, bw, bd, bang}, RingArea(ring) / bestArea
}

// ---------------------------------------------------------------------------
// triangle estimate
// ---------------------------------------------------------------------------

// styleTriangles mirrors what kit_geo_city builds for one building. It exists
// for one reason: the emitter groups buildings into modules, and a Model with
// 65535 triangles or more is **silently skipped by the physics cook** — the
// block renders and you walk straight through it. Grouping by triangle budget
// instead of by a fixed count is what keeps that from happening once buildings
// stopped being 40-triangle prisms.
//
// It only has to be right to within a factor of ~1.3; the budget carries the
// margin.
func styleTriangles(st buildingStyle, verts int, perimeter, h float64) int {
	slab := 4*verts - 4
	tris := slab // shell

	f := facadePreset(st.Facade)
	plinthTop, crownH := 0.0, 0.0
	if f.plinth && h > st.FloorH*2.2 {
		plinthTop = math.Min(st.FloorH*1.15, 5.0)
	}
	if f.crown && h > 6 {
		crownH = 0.7
	}
	body := h - plinthTop - crownH
	if body > 1.0 {
		if f.mullPitch > 0.5 && f.mullW > 0.01 && f.relief > 0.001 {
			// One box per mullion; the kit puts floor(L/pitch)+1 on each edge,
			// so the per-edge rounding adds roughly one per vertex.
			tris += 12 * (int(perimeter/f.mullPitch) + verts)
		}
		if f.bandH > 0.01 {
			n := int(body / st.FloorH)
			if n >= 1 {
				step := 1 + (n-1)/48
				tris += slab * (1 + (n-1)/step)
			}
		}
	}
	if plinthTop > 0.1 {
		tris += 2 * slab
	}
	if crownH > 0.1 {
		tris += slab
	}

	switch st.RoofKind {
	case 1:
		tris += slab       // roof deck
		tris += 12 * verts // parapet ring
		if st.Clutter > 0 {
			n := st.Clutter*2 + 1
			if n > 7 {
				n = 7
			}
			tris += n * 48
			if st.Clutter >= 3 {
				tris += 40
			}
		}
	case 2:
		tris += 8
	}
	return tris
}

// facadePreset mirrors gc_FAC(). Two copies of one table is a real risk, so
// keep the comment: if a preset changes in the kit, change it here too — the
// only thing that breaks is the triangle budget, which fails silently as a
// missing collider.
type facadeSpec struct {
	bandH, relief, mullW, mullPitch float64
	plinth, crown, shellGlass       bool
}

func facadePreset(i int) facadeSpec {
	presets := []facadeSpec{
		{0.00, 0.00, 0.00, 0.0, false, false, false},
		{0.55, 0.12, 0.32, 2.9, true, true, true},
		{1.00, 0.18, 0.55, 3.3, true, true, true},
		{1.45, 0.28, 0.95, 3.2, true, true, true},
		{1.05, 0.15, 0.90, 7.5, false, true, false},
		{0.80, 0.16, 0.62, 3.6, true, false, true},
	}
	return presets[((i%len(presets))+len(presets))%len(presets)]
}

// ringPerimeter is needed by the triangle estimate; the mullion count scales
// with the length of wall, not with the vertex count.
func ringPerimeter(ring Ring) float64 {
	if len(ring) < 2 {
		return 0
	}
	total := 0.0
	for i := range ring {
		a, b := ring[i], ring[(i+1)%len(ring)]
		total += math.Hypot(b[0]-a[0], b[1]-a[1])
	}
	return total
}
