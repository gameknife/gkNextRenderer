package geo

import "math"

// Polygon helpers. Everything works on [][2]float64 in metres; rings are
// implicitly closed (the first point is not repeated).

// SignedArea is positive for counter-clockwise rings.
func SignedArea(ring Ring) float64 {
	if len(ring) < 3 {
		return 0
	}
	var a float64
	for i := range ring {
		j := (i + 1) % len(ring)
		a += ring[i][0]*ring[j][1] - ring[j][0]*ring[i][1]
	}
	return a / 2
}

// RingArea is the unsigned area in square metres.
func RingArea(ring Ring) float64 { return math.Abs(SignedArea(ring)) }

// EnsureCCW flips the ring in place if it winds clockwise.
func EnsureCCW(ring Ring) Ring {
	if SignedArea(ring) < 0 {
		for i, j := 0, len(ring)-1; i < j; i, j = i+1, j-1 {
			ring[i], ring[j] = ring[j], ring[i]
		}
	}
	return ring
}

// Centroid of a ring (area-weighted; falls back to the vertex mean for
// degenerate rings).
func Centroid(ring Ring) [2]float64 {
	a := SignedArea(ring)
	if math.Abs(a) < 1e-9 {
		var sx, sy float64
		for _, p := range ring {
			sx += p[0]
			sy += p[1]
		}
		n := math.Max(1, float64(len(ring)))
		return [2]float64{sx / n, sy / n}
	}
	var cx, cy float64
	for i := range ring {
		j := (i + 1) % len(ring)
		cross := ring[i][0]*ring[j][1] - ring[j][0]*ring[i][1]
		cx += (ring[i][0] + ring[j][0]) * cross
		cy += (ring[i][1] + ring[j][1]) * cross
	}
	return [2]float64{cx / (6 * a), cy / (6 * a)}
}

// BoundsOf returns the axis-aligned bounds of a point list.
func BoundsOf(pts [][2]float64) (minX, minY, maxX, maxY float64) {
	if len(pts) == 0 {
		return 0, 0, 0, 0
	}
	minX, minY = pts[0][0], pts[0][1]
	maxX, maxY = minX, minY
	for _, p := range pts[1:] {
		minX = math.Min(minX, p[0])
		minY = math.Min(minY, p[1])
		maxX = math.Max(maxX, p[0])
		maxY = math.Max(maxY, p[1])
	}
	return
}

// PointInRing is the standard even-odd crossing test.
func PointInRing(ring Ring, x, y float64) bool {
	inside := false
	for i, j := 0, len(ring)-1; i < len(ring); j, i = i, i+1 {
		yi, yj := ring[i][1], ring[j][1]
		if (yi > y) == (yj > y) {
			continue
		}
		xi := ring[i][0] + (y-yi)/(yj-yi)*(ring[j][0]-ring[i][0])
		if x < xi {
			inside = !inside
		}
	}
	return inside
}

// Simplify runs Douglas-Peucker on a closed ring, then caps the vertex count by
// raising the tolerance until it fits. Rings that would collapse below a
// triangle are returned unchanged.
func Simplify(ring Ring, tolerance float64, maxVerts int) Ring {
	if len(ring) <= 4 {
		return ring
	}
	out := simplifyClosed(ring, tolerance)
	for len(out) > maxVerts && tolerance < 64 {
		tolerance *= 1.7
		out = simplifyClosed(ring, tolerance)
	}
	if len(out) < 3 {
		return ring
	}
	return out
}

// simplifyClosed splits the ring at its two most distant vertices so
// Douglas-Peucker (an open-polyline algorithm) cannot degenerate it to a point.
func simplifyClosed(ring Ring, tolerance float64) Ring {
	anchor := 0
	far := 0
	best := -1.0
	for i := 1; i < len(ring); i++ {
		d := dist2(ring[anchor], ring[i])
		if d > best {
			best = d
			far = i
		}
	}
	first := append(Ring{}, ring[anchor:far+1]...)
	second := append(Ring{}, ring[far:]...)
	second = append(second, ring[anchor])

	a := douglasPeucker(first, tolerance)
	b := douglasPeucker(second, tolerance)
	// Drop the duplicated joins (last of a == first of b, last of b == first of a).
	out := append(Ring{}, a...)
	if len(b) > 2 {
		out = append(out, b[1:len(b)-1]...)
	}
	return out
}

func douglasPeucker(pts Ring, tolerance float64) Ring {
	if len(pts) < 3 {
		return pts
	}
	maxDist := 0.0
	index := 0
	for i := 1; i < len(pts)-1; i++ {
		d := perpDistance(pts[i], pts[0], pts[len(pts)-1])
		if d > maxDist {
			maxDist = d
			index = i
		}
	}
	if maxDist <= tolerance {
		return Ring{pts[0], pts[len(pts)-1]}
	}
	left := douglasPeucker(pts[:index+1], tolerance)
	right := douglasPeucker(pts[index:], tolerance)
	return append(left[:len(left)-1], right...)
}

func perpDistance(p, a, b [2]float64) float64 {
	dx, dy := b[0]-a[0], b[1]-a[1]
	den := dx*dx + dy*dy
	if den < 1e-12 {
		return math.Hypot(p[0]-a[0], p[1]-a[1])
	}
	t := ((p[0]-a[0])*dx + (p[1]-a[1])*dy) / den
	t = clampF(t, 0, 1)
	return math.Hypot(p[0]-(a[0]+t*dx), p[1]-(a[1]+t*dy))
}

func dist2(a, b [2]float64) float64 {
	dx, dy := a[0]-b[0], a[1]-b[1]
	return dx*dx + dy*dy
}

// DedupeRing drops consecutive points closer than eps and the closing repeat.
func DedupeRing(ring Ring, eps float64) Ring {
	if len(ring) == 0 {
		return ring
	}
	out := Ring{ring[0]}
	for _, p := range ring[1:] {
		if dist2(p, out[len(out)-1]) > eps*eps {
			out = append(out, p)
		}
	}
	// An OSM closed way repeats the first node at the end.
	for len(out) > 1 && dist2(out[0], out[len(out)-1]) <= eps*eps {
		out = out[:len(out)-1]
	}
	return out
}

// SegmentDistance returns the distance from p to segment ab.
func SegmentDistance(p, a, b [2]float64) float64 { return perpDistance(p, a, b) }

// PolylineDistance returns the distance from p to the nearest segment.
func PolylineDistance(pts [][2]float64, x, y float64) float64 {
	if len(pts) == 0 {
		return math.Inf(1)
	}
	if len(pts) == 1 {
		return math.Hypot(x-pts[0][0], y-pts[0][1])
	}
	best := math.Inf(1)
	p := [2]float64{x, y}
	for i := 0; i+1 < len(pts); i++ {
		if d := perpDistance(p, pts[i], pts[i+1]); d < best {
			best = d
		}
	}
	return best
}
