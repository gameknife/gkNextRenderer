package geo

import (
	"math"
	"sort"
)

// Road network topology: turn OSM centre lines into carriageway ribbons and
// filled intersections.
//
// The input already carries the topology we need — an Overpass `out body geom`
// response gives every way a `nodes` array index-aligned with its geometry, so
// junctions are exact shared node ids rather than a coordinate-proximity guess.
// That has to be used *before* clipping and simplification, which both destroy
// the index alignment.
//
// The pipeline per tile:
//
//	classify junction nodes  -> split ways into runs -> trim each run back from
//	its junctions -> clip + simplify -> mitre into left/right edges
//	                                 -> hull the trimmed ends into a patch
//
// The result is one continuous ribbon per run (a single polyhedron in the kit,
// no per-segment seams or overlaps) plus one polygon per intersection.

// RoadRun is one continuous stretch of carriageway between intersections.
type RoadRun struct {
	Width float64
	Class string
	ID    int64
	// Left and Right are the mitred carriageway edges, same length, ordered
	// along the direction of travel.
	Left  [][2]float64
	Right [][2]float64
}

// Junction is a filled intersection: the convex hull of the carriageway ends
// that were trimmed back from it.
type Junction struct {
	Ring   [][2]float64
	Width  float64 // widest approach, for reporting
	Center [2]float64
}

const (
	// A corner sharper than this cannot be mitred without the inner edge
	// folding back on itself, so the run is split and the corner is filled by a
	// patch instead — the same machinery intersections use.
	sharpTurnDeg = 60.0
	// Miter length cap, in half-widths. Without it a hairpin sends the outer
	// edge to infinity.
	maxMiterRatio = 2.5
	// A run shorter than this after trimming is entirely inside its junctions.
	minRunLengthM = 2.0
	// Ribbon station spacing, about one terrain cell. The surface interpolates
	// linearly between stations, so this is what bounds how far the carriageway
	// can cut into a rise between two samples.
	stationStepM = 5.0
)

// nodeUse is one occurrence of a node in one way.
type nodeUse struct {
	road     int
	index    int
	interior bool
}

// BuildRoadNetwork turns selected centre lines into ribbons plus intersections.
func BuildRoadNetwork(roads []Road, half float64, opt EmitOptions) ([]RoadRun, []Junction) {
	uses := map[int64][]nodeUse{}
	for ri, r := range roads {
		if len(r.Nodes) != len(r.Pts) {
			continue // no usable topology for this way
		}
		for i, id := range r.Nodes {
			uses[id] = append(uses[id], nodeUse{
				road: ri, index: i, interior: i > 0 && i < len(r.Pts)-1,
			})
		}
	}

	// A junction needs at least two ways meeting and at least three approach
	// directions. Two ways meeting end-to-end (a street changing its name) has
	// only two approaches and needs no patch; a plain interior vertex of a
	// single way has two approaches but only one way.
	isJunction := map[int64]bool{}
	for id, us := range uses {
		if len(us) < 2 {
			continue
		}
		approaches := 0
		for _, u := range us {
			if u.interior {
				approaches += 2
			} else {
				approaches++
			}
		}
		if approaches >= 3 {
			isJunction[id] = true
		}
	}

	type approach struct {
		nodeID int64
		left   [2]float64
		right  [2]float64
		width  float64
		at     [2]float64
	}
	var runs []RoadRun
	approaches := map[int64][]approach{}
	// Sharp corners get a synthetic junction id so they reuse the patch path.
	nextSyntheticID := int64(-1)

	for _, r := range roads {
		hasNodes := len(r.Nodes) == len(r.Pts)
		// Split indices: interior junction nodes and corners too sharp to mitre.
		splits := map[int]int64{}
		for i := 1; i < len(r.Pts)-1; i++ {
			if hasNodes && isJunction[r.Nodes[i]] {
				splits[i] = r.Nodes[i]
				continue
			}
			if turnAngleDeg(r.Pts[i-1], r.Pts[i], r.Pts[i+1]) > sharpTurnDeg {
				splits[i] = nextSyntheticID
				nextSyntheticID--
			}
		}

		// Walk the way, cutting at every split index.
		start := 0
		for cut := 1; cut <= len(r.Pts)-1; cut++ {
			_, isSplit := splits[cut]
			if !isSplit && cut != len(r.Pts)-1 {
				continue
			}
			seg := r.Pts[start : cut+1]
			startID := int64(0)
			endID := int64(0)
			if id, ok := splits[start]; ok {
				startID = id
			} else if hasNodes && isJunction[r.Nodes[start]] {
				startID = r.Nodes[start]
			}
			if id, ok := splits[cut]; ok {
				endID = id
			} else if hasNodes && isJunction[r.Nodes[cut]] {
				endID = r.Nodes[cut]
			}

			trimStart := 0.0
			trimEnd := 0.0
			if startID != 0 {
				trimStart = junctionRadius(startID, uses, roads, r.Width)
			}
			if endID != 0 {
				trimEnd = junctionRadius(endID, uses, roads, r.Width)
			}
			trimmed, okTrim := trimPolyline(seg, trimStart, trimEnd)
			start = cut

			if !okTrim {
				continue
			}
			clipped := clipToSquare(trimmed, half)
			if len(clipped) < 2 {
				continue
			}
			simplified := Simplify(Ring(clipped), 2.0, 48)
			if len(simplified) < 2 || polylineLength(simplified) < minRunLengthM {
				continue
			}
			// Simplify removes survey noise; densify puts the stations back at
			// terrain resolution. Without this a straight run spans the whole
			// hillside in one chord and sinks into every rise along the way —
			// exactly the failure the per-segment version had, reintroduced by
			// making the ribbon follow the *simplified* vertices.
			stations := densifyPolyline(simplified, stationStepM)
			left, right := offsetPolyline(stations, r.Width/2)
			run := RoadRun{Width: r.Width, Class: r.Class, ID: r.ID, Left: left, Right: right}
			runs = append(runs, run)

			// Record the cross-sections facing each junction. Only if the run's
			// end survived clipping at (approximately) the trimmed position.
			if startID != 0 && samePoint(simplified[0], trimmed[0]) {
				approaches[startID] = append(approaches[startID], approach{
					nodeID: startID, left: left[0], right: right[0],
					width: r.Width, at: seg[0],
				})
			}
			last := len(simplified) - 1
			if endID != 0 && samePoint(simplified[last], trimmed[len(trimmed)-1]) {
				approaches[endID] = append(approaches[endID], approach{
					nodeID: endID, left: left[last], right: right[last],
					width: r.Width, at: seg[len(seg)-1],
				})
			}
		}
	}

	ids := make([]int64, 0, len(approaches))
	for id := range approaches {
		ids = append(ids, id)
	}
	sort.Slice(ids, func(i, j int) bool { return ids[i] < ids[j] })

	var junctions []Junction
	for _, id := range ids {
		as := approaches[id]
		if len(as) < 2 {
			continue // a lone approach has nothing to bridge to
		}
		pts := make([][2]float64, 0, len(as)*2)
		widest := 0.0
		var cx, cy float64
		for _, a := range as {
			pts = append(pts, a.left, a.right)
			widest = math.Max(widest, a.width)
			cx += a.at[0]
			cy += a.at[1]
		}
		center := [2]float64{cx / float64(len(as)), cy / float64(len(as))}
		if math.Abs(center[0]) > half || math.Abs(center[1]) > half {
			continue
		}
		ring := convexHull(pts)
		if len(ring) < 3 {
			continue
		}
		junctions = append(junctions, Junction{Ring: ring, Width: widest, Center: center})
	}
	return runs, junctions
}

// junctionRadius is how far back a carriageway stops short of an intersection:
// far enough to clear the widest crossing carriageway.
func junctionRadius(id int64, uses map[int64][]nodeUse, roads []Road, own float64) float64 {
	widest := own
	for _, u := range uses[id] {
		widest = math.Max(widest, roads[u.road].Width)
	}
	// Half the crossing width plus a small margin; capped so a motorway
	// junction does not swallow the streets around it.
	return math.Min(widest*0.55+0.5, 22.0)
}

// trimPolyline removes `head` metres from the start and `tail` from the end.
func trimPolyline(pts [][2]float64, head, tail float64) ([][2]float64, bool) {
	total := polylineLength(pts)
	if total <= head+tail+minRunLengthM {
		return nil, false
	}
	return substringPolyline(pts, head, total-tail), true
}

// substringPolyline returns the piece of the polyline between two arc lengths.
func substringPolyline(pts [][2]float64, from, to float64) [][2]float64 {
	var out [][2]float64
	travelled := 0.0
	for i := 0; i+1 < len(pts); i++ {
		a, b := pts[i], pts[i+1]
		segLen := math.Hypot(b[0]-a[0], b[1]-a[1])
		if segLen < 1e-9 {
			continue
		}
		segStart, segEnd := travelled, travelled+segLen
		travelled = segEnd
		if segEnd < from || segStart > to {
			continue
		}
		lerp := func(d float64) [2]float64 {
			t := clampF((d-segStart)/segLen, 0, 1)
			return [2]float64{a[0] + (b[0]-a[0])*t, a[1] + (b[1]-a[1])*t}
		}
		p0 := lerp(math.Max(from, segStart))
		p1 := lerp(math.Min(to, segEnd))
		if len(out) == 0 {
			out = append(out, p0)
		}
		if !samePoint(out[len(out)-1], p1) {
			out = append(out, p1)
		}
	}
	return out
}

func polylineLength(pts [][2]float64) float64 {
	total := 0.0
	for i := 0; i+1 < len(pts); i++ {
		total += math.Hypot(pts[i+1][0]-pts[i][0], pts[i+1][1]-pts[i][1])
	}
	return total
}

// turnAngleDeg is the deflection at b, 0 for a straight line.
func turnAngleDeg(a, b, c [2]float64) float64 {
	u := math.Atan2(b[1]-a[1], b[0]-a[0])
	v := math.Atan2(c[1]-b[1], c[0]-b[0])
	d := math.Abs(v-u) * 180 / math.Pi
	if d > 180 {
		d = 360 - d
	}
	return d
}

// offsetPolyline mitres a centre line into its left and right carriageway
// edges. The miter length is capped so a tight corner cannot throw the outer
// edge off to infinity; corners sharp enough for that were already split.
func offsetPolyline(pts [][2]float64, half float64) (left, right [][2]float64) {
	n := len(pts)
	normals := make([][2]float64, n-1)
	for i := 0; i+1 < n; i++ {
		dx, dy := pts[i+1][0]-pts[i][0], pts[i+1][1]-pts[i][1]
		l := math.Hypot(dx, dy)
		if l < 1e-9 {
			normals[i] = [2]float64{0, 0}
			continue
		}
		normals[i] = [2]float64{-dy / l, dx / l} // left of travel
	}
	for i := 0; i < n; i++ {
		var m [2]float64
		switch {
		case i == 0:
			m = normals[0]
		case i == n-1:
			m = normals[n-2]
		default:
			m = [2]float64{normals[i-1][0] + normals[i][0], normals[i-1][1] + normals[i][1]}
		}
		l := math.Hypot(m[0], m[1])
		if l < 1e-9 {
			m = normals[min(i, n-2)]
			l = 1
		}
		m[0] /= l
		m[1] /= l
		scale := half
		if i > 0 && i < n-1 {
			// cos(theta/2) between the bisector and either segment normal.
			cos := m[0]*normals[i][0] + m[1]*normals[i][1]
			scale = half / math.Max(1/maxMiterRatio, cos)
		}
		left = append(left, [2]float64{pts[i][0] + m[0]*scale, pts[i][1] + m[1]*scale})
		right = append(right, [2]float64{pts[i][0] - m[0]*scale, pts[i][1] - m[1]*scale})
	}
	return left, right
}

// densifyPolyline inserts intermediate vertices so no span exceeds maxStep.
func densifyPolyline(pts [][2]float64, maxStep float64) [][2]float64 {
	if len(pts) < 2 {
		return pts
	}
	out := [][2]float64{pts[0]}
	for i := 0; i+1 < len(pts); i++ {
		a, b := pts[i], pts[i+1]
		length := math.Hypot(b[0]-a[0], b[1]-a[1])
		n := int(math.Ceil(length / maxStep))
		if n < 1 {
			n = 1
		}
		for j := 1; j <= n; j++ {
			t := float64(j) / float64(n)
			out = append(out, [2]float64{a[0] + (b[0]-a[0])*t, a[1] + (b[1]-a[1])*t})
		}
	}
	return out
}

func samePoint(a, b [2]float64) bool {
	return math.Abs(a[0]-b[0]) < 0.05 && math.Abs(a[1]-b[1]) < 0.05
}

// convexHull is a monotone chain over the approach corner points. A hull rather
// than an angular sort: it can never self-intersect, and for a real junction the
// two are the same ring anyway.
func convexHull(pts [][2]float64) [][2]float64 {
	if len(pts) < 3 {
		return nil
	}
	p := append([][2]float64{}, pts...)
	sort.Slice(p, func(i, j int) bool {
		if p[i][0] != p[j][0] {
			return p[i][0] < p[j][0]
		}
		return p[i][1] < p[j][1]
	})
	cross := func(o, a, b [2]float64) float64 {
		return (a[0]-o[0])*(b[1]-o[1]) - (a[1]-o[1])*(b[0]-o[0])
	}
	build := func(src [][2]float64) [][2]float64 {
		var out [][2]float64
		for _, q := range src {
			for len(out) >= 2 && cross(out[len(out)-2], out[len(out)-1], q) <= 0 {
				out = out[:len(out)-1]
			}
			out = append(out, q)
		}
		return out
	}
	lower := build(p)
	upper := build(reversePoints(p))
	if len(lower) < 2 || len(upper) < 2 {
		return nil
	}
	return append(lower[:len(lower)-1], upper[:len(upper)-1]...)
}

func reversePoints(p [][2]float64) [][2]float64 {
	out := make([][2]float64, len(p))
	for i := range p {
		out[i] = p[len(p)-1-i]
	}
	return out
}
