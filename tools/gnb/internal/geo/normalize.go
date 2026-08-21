package geo

import (
	"math"
	"sort"
	"strconv"
	"strings"
	"time"
)

// Normalize turns raw Overpass elements into the IR: projected to SCAD metres,
// rings assembled, heights inferred.
func Normalize(tile Tile, elements []osmElement, profile HeightProfile,
	landmarks map[int64]float64) *IR {
	// Tile.Project, not Proj.Forward: a mosaic part borrows the area's tangent
	// plane and subtracts its own offset, so its coordinates stay part-local
	// while the metric frame stays shared with its neighbours.
	project := func(g []osmPoint) [][2]float64 {
		out := make([][2]float64, 0, len(g))
		for _, q := range g {
			x, y := tile.Project(q.Lat, q.Lon)
			out = append(out, [2]float64{x, y})
		}
		return out
	}

	ir := &IR{
		Tile:        tile.Name,
		Center:      [2]float64{tile.Lat, tile.Lon},
		SizeM:       tile.SizeM,
		GeneratedAt: time.Now().UTC(),
		Attribution: []string{osmLicense, srtmLicense},
	}

	for _, e := range elements {
		tags := e.Tags
		if tags == nil {
			tags = map[string]string{}
		}
		switch {
		case tags["building"] != "":
			if b, ok := buildingFrom(e, project, tags, profile, landmarks); ok {
				ir.Buildings = append(ir.Buildings, b)
			}
		case tags["highway"] != "" && e.Type == "way":
			// Points and node ids must be deduplicated together: the ids are the
			// junction topology, and they are only meaningful while they stay
			// index-aligned with the geometry.
			pts, nodes := dedupeWay(project(e.Geometry), e.Nodes, 0.2)
			if len(pts) < 2 {
				continue
			}
			ir.Roads = append(ir.Roads, Road{
				ID: e.ID, Name: tags["name"], Class: tags["highway"],
				Lanes: atoiDefault(tags["lanes"], 0),
				Width: roadWidth(tags),
				Pts:   pts,
				Nodes: nodes,
			})
		case tags["natural"] == "coastline" && e.Type == "way":
			pts := DedupeRing(project(e.Geometry), 0.2)
			if len(pts) >= 2 {
				ir.Coastline = append(ir.Coastline, Line{ID: e.ID, Tag: "natural=coastline", Pts: pts})
			}
		case tags["natural"] == "water" || tags["waterway"] == "riverbank":
			ir.Waters = append(ir.Waters, areasFrom(e, project, "natural=water")...)
		case tags["man_made"] == "pier" && e.Type == "way":
			// DedupeRing strips the repeated closing node, so detect closure on
			// the raw geometry first: a closed pier way is a deck outline.
			closed := len(e.Geometry) > 3 &&
				e.Geometry[0].Lat == e.Geometry[len(e.Geometry)-1].Lat &&
				e.Geometry[0].Lon == e.Geometry[len(e.Geometry)-1].Lon
			pts := DedupeRing(project(e.Geometry), 0.2)
			if len(pts) >= 2 {
				ir.Piers = append(ir.Piers, Line{ID: e.ID, Tag: "man_made=pier", Closed: closed, Pts: pts})
			}
		case tags["landuse"] != "" || tags["leisure"] != "":
			tag := "landuse=" + tags["landuse"]
			if tags["landuse"] == "" {
				tag = "leisure=" + tags["leisure"]
			}
			areas := areasFrom(e, project, tag)
			for i := range areas {
				areas[i].Name = tags["name"]
			}
			ir.Landuse = append(ir.Landuse, areas...)
		}
	}

	inferNeighbourHeights(ir.Buildings, profile)
	// Stable output regardless of Overpass ordering, so re-running the
	// generator on the same cache produces a byte-identical .scad.
	sort.SliceStable(ir.Buildings, func(i, j int) bool { return ir.Buildings[i].ID < ir.Buildings[j].ID })
	sort.SliceStable(ir.Roads, func(i, j int) bool { return ir.Roads[i].ID < ir.Roads[j].ID })
	sort.SliceStable(ir.Waters, func(i, j int) bool { return ir.Waters[i].ID < ir.Waters[j].ID })
	sort.SliceStable(ir.Coastline, func(i, j int) bool { return ir.Coastline[i].ID < ir.Coastline[j].ID })
	sort.SliceStable(ir.Landuse, func(i, j int) bool { return ir.Landuse[i].ID < ir.Landuse[j].ID })

	// POIs are derived last: the footprint and area layers are their primary
	// sources, and both have to be sorted first for the output to be stable.
	ir.POIs = CollectPOIs(ir, elements, tile.Project, tile.SizeM/2)
	return ir
}

// assembleRings stitches the member ways of an OSM multipolygon relation into
// closed rings.
//
// This is not optional bookkeeping. A relation's boundary is split across
// several *open* ways that only form a ring end-to-end: the Seine is eight open
// outer members. Treating the longest member as a ring on its own produces a
// polygon with no relation to the water, which floods half the tile.
func assembleRings(members []osmMember, role string,
	project func([]osmPoint) [][2]float64) []Ring {
	var chains [][][2]float64
	for _, m := range members {
		if m.Type != "way" || len(m.Geometry) < 2 {
			continue
		}
		// "" is the legacy spelling of "outer".
		if m.Role != role && !(role == "outer" && m.Role == "") {
			continue
		}
		chains = append(chains, project(m.Geometry))
	}

	const joinEps = 0.5 // metres; shared nodes project to identical coordinates
	near := func(a, b [2]float64) bool { return dist2(a, b) <= joinEps*joinEps }

	var rings []Ring
	used := make([]bool, len(chains))
	for i := range chains {
		if used[i] {
			continue
		}
		used[i] = true
		ring := append([][2]float64{}, chains[i]...)
		for {
			if len(ring) > 2 && near(ring[0], ring[len(ring)-1]) {
				break // closed
			}
			extended := false
			for j := range chains {
				if used[j] {
					continue
				}
				c := chains[j]
				switch {
				case near(ring[len(ring)-1], c[0]):
					ring = append(ring, c[1:]...)
				case near(ring[len(ring)-1], c[len(c)-1]):
					for k := len(c) - 2; k >= 0; k-- {
						ring = append(ring, c[k])
					}
				default:
					continue
				}
				used[j] = true
				extended = true
				break
			}
			if !extended {
				break
			}
		}
		if len(ring) > 2 && near(ring[0], ring[len(ring)-1]) {
			if r := DedupeRing(ring, 0.2); len(r) >= 3 {
				rings = append(rings, r)
			}
		}
		// An unclosed chain means the relation is only partly in the response;
		// dropping it is right, because closing it invents geometry.
	}
	return rings
}

// largestRing picks the ring a single extruded prism / flooded area should use.
func largestRing(rings []Ring) Ring {
	var best Ring
	for _, r := range rings {
		if RingArea(r) > RingArea(best) {
			best = r
		}
	}
	return best
}

func buildingFrom(e osmElement, project func([]osmPoint) [][2]float64, tags map[string]string,
	profile HeightProfile, landmarks map[int64]float64) (Building, bool) {
	b := Building{ID: e.ID, Name: tags["name"], Kind: tags["building"]}
	switch e.Type {
	case "way":
		b.Outer = DedupeRing(project(e.Geometry), 0.2)
	case "relation":
		// Several outer rings are possible; a single extruded prism can only be
		// the largest one.
		b.Outer = largestRing(assembleRings(e.Members, "outer", project))
		for _, hole := range assembleRings(e.Members, "inner", project) {
			if len(b.Outer) >= 3 && PointInRing(b.Outer, hole[0][0], hole[0][1]) {
				b.Inners = append(b.Inners, hole)
			}
		}
	}
	if len(b.Outer) < 3 {
		return b, false
	}
	b.AreaM2 = RingArea(b.Outer)
	b.Height, b.HeightSource, b.Levels = inferHeight(e.ID, tags, profile, landmarks)
	b.MinHeight = parseLength(tags["min_height"])
	return b, true
}

// areasFrom yields one Area per closed outer ring. Unlike a building, a water
// or landuse relation can legitimately cover several disjoint patches, and
// collapsing them to the largest one loses real geometry.
func areasFrom(e osmElement, project func([]osmPoint) [][2]float64, tag string) []Area {
	if e.Type == "way" {
		ring := DedupeRing(project(e.Geometry), 0.2)
		if len(ring) < 3 {
			return nil
		}
		return []Area{{ID: e.ID, Tag: tag, Outer: ring}}
	}
	outers := assembleRings(e.Members, "outer", project)
	inners := assembleRings(e.Members, "inner", project)
	var out []Area
	for _, outer := range outers {
		a := Area{ID: e.ID, Tag: tag, Outer: outer}
		for _, hole := range inners {
			if PointInRing(outer, hole[0][0], hole[0][1]) {
				a.Inners = append(a.Inners, hole)
			}
		}
		out = append(out, a)
	}
	return out
}

// ---------------------------------------------------------------------------
// Height model
// ---------------------------------------------------------------------------

// HeightProfile supplies the fallbacks used when OSM carries no height.
// Named profiles let a dense Asian downtown and a European old town use the
// same generator without either looking absurd.
type HeightProfile struct {
	Name string
	// FloorHeight per building class; "" is the catch-all.
	FloorHeight map[string]float64
	// Default height per building=* value when nothing else is known.
	Default map[string]float64
	// Fallback when the kind is unknown too.
	Fallback float64
	// RoofHeight is added on top of levels * floorHeight.
	RoofHeight float64
}

// Profiles are keyed by --profile.
var Profiles = map[string]HeightProfile{
	"default": {
		Name: "default",
		FloorHeight: map[string]float64{
			"":            3.2,
			"apartments":  3.1,
			"residential": 3.1,
			"house":       3.0,
			"commercial":  4.0,
			"office":      4.0,
			"retail":      4.4,
			"industrial":  5.2,
			"warehouse":   5.6,
			"hotel":       3.3,
		},
		Default: map[string]float64{
			"yes":            12,
			"house":          7,
			"garage":         3,
			"shed":           3,
			"hut":            3,
			"roof":           4,
			"kiosk":          3.5,
			"retail":         9,
			"commercial":     22,
			"office":         28,
			"apartments":     28,
			"residential":    20,
			"hotel":          30,
			"industrial":     12,
			"warehouse":      11,
			"school":         14,
			"hospital":       30,
			"church":         16,
			"train_station":  14,
			"transportation": 12,
			"civic":          16,
			"public":         16,
			"construction":   20,
			"parking":        12,
			"service":        4,
		},
		Fallback:   12,
		RoofHeight: 1.2,
	},
	// European old towns: a uniform 5..7 storey perimeter block. Heights cluster
	// far more tightly than in "default", which has to cover North American
	// downtowns too, and the fallback for an untagged building is a block in the
	// street wall rather than a generic 12 m box.
	"europe": {
		Name: "europe",
		FloorHeight: map[string]float64{
			"":            3.3,
			"apartments":  3.2,
			"residential": 3.2,
			"house":       3.0,
			"commercial":  3.8,
			"office":      3.6,
			"retail":      4.2,
			"industrial":  5.0,
			"hotel":       3.3,
		},
		Default: map[string]float64{
			"yes":            17, // the perimeter block: 5 storeys plus a roof
			"house":          9,
			"garage":         3,
			"shed":           3,
			"roof":           4,
			"kiosk":          3.5,
			"retail":         11,
			"commercial":     19,
			"office":         24,
			"apartments":     20,
			"residential":    18,
			"hotel":          21,
			"industrial":     12,
			"warehouse":      11,
			"school":         15,
			"hospital":       24,
			"church":         24,
			"cathedral":      42,
			"train_station":  18,
			"transportation": 12,
			"civic":          19,
			"public":         19,
			"construction":   18,
			"parking":        12,
			"service":        4,
		},
		Fallback:   17,
		RoofHeight: 2.4,
	},
	// Mainland Chinese cities: mid-rise slab housing dominates, and OSM height
	// coverage is thin, so these fallbacks carry most of the skyline rather than
	// just filling gaps. They are plausible defaults for the building class, not
	// measurements — `gnb geo build` reports how many buildings depend on them.
	"china": {
		Name: "china",
		FloorHeight: map[string]float64{
			"":            3.0,
			"apartments":  2.9,
			"residential": 2.9,
			"commercial":  4.0,
			"office":      3.8,
			"retail":      4.4,
			"industrial":  5.0,
			"hotel":       3.2,
		},
		Default: map[string]float64{
			"yes":            21, // a 6..8 storey block, the default urban fabric
			"house":          9,
			"roof":           4,
			"retail":         15,
			"commercial":     40,
			"office":         60,
			"apartments":     45,
			"residential":    36,
			"hotel":          45,
			"industrial":     12,
			"warehouse":      10,
			"school":         18,
			"hospital":       36,
			"university":     24,
			"dormitory":      21,
			"civic":          18,
			"public":         18,
			"transportation": 12,
			"construction":   40,
			"service":        4,
			"parking":        15,
		},
		Fallback:   21,
		RoofHeight: 1.0,
	},
	// Hong Kong style: residential towers are the tall ones, not the offices.
	"hongkong": {
		Name: "hongkong",
		FloorHeight: map[string]float64{
			"":            3.1,
			"apartments":  3.0,
			"residential": 3.0,
			"commercial":  4.2,
			"office":      4.2,
			"retail":      4.6,
			"industrial":  5.0,
			"hotel":       3.2,
		},
		Default: map[string]float64{
			"yes":            26,
			"house":          8,
			"roof":           5,
			"retail":         14,
			"commercial":     45,
			"office":         60,
			"apartments":     92,
			"residential":    82,
			"hotel":          70,
			"industrial":     28,
			"warehouse":      18,
			"school":         20,
			"hospital":       40,
			"civic":          24,
			"public":         24,
			"transportation": 12,
			"construction":   45,
			"service":        5,
			"parking":        20,
		},
		Fallback:   26,
		RoofHeight: 1.5,
	},
}

// inferHeight applies the priority chain from design §5.3.
func inferHeight(id int64, tags map[string]string, profile HeightProfile,
	landmarks map[int64]float64) (height float64, source string, levels int) {
	if h, ok := landmarks[id]; ok && h > 0 {
		return h, "landmark", 0
	}
	if h := parseLength(tags["height"]); h > 0 {
		return h, "tag", 0
	}
	if n := atoiDefault(tags["building:levels"], 0); n > 0 {
		floor, ok := profile.FloorHeight[tags["building"]]
		if !ok {
			floor = profile.FloorHeight[""]
		}
		return float64(n)*floor + profile.RoofHeight, "levels", n
	}
	if h, ok := profile.Default[tags["building"]]; ok {
		return h, "default", 0
	}
	return profile.Fallback, "default", 0
}

// inferNeighbourHeights replaces the flat per-kind default with the local
// median for buildings that had no data, so a block reads as one neighbourhood
// instead of a plateau of identical boxes. Deterministic: the median comes from
// ID-sorted neighbours, and untouched buildings keep their "default" source.
func inferNeighbourHeights(buildings []Building, profile HeightProfile) {
	type known struct {
		c [2]float64
		h float64
	}
	var real []known
	for _, b := range buildings {
		if b.HeightSource == "tag" || b.HeightSource == "levels" || b.HeightSource == "landmark" {
			real = append(real, known{Centroid(b.Outer), b.Height})
		}
	}
	if len(real) < 4 {
		return
	}
	const radius = 120.0
	for i := range buildings {
		b := &buildings[i]
		if b.HeightSource != "default" {
			continue
		}
		c := Centroid(b.Outer)
		var near []float64
		for _, k := range real {
			if math.Hypot(k.c[0]-c[0], k.c[1]-c[1]) <= radius {
				near = append(near, k.h)
			}
		}
		if len(near) < 3 {
			continue
		}
		sort.Float64s(near)
		median := near[len(near)/2]
		// Blend toward the neighbourhood so a small shed next to a tower does
		// not become a tower itself; footprint area is the sanity check.
		scale := clampF(math.Sqrt(b.AreaM2/220.0), 0.35, 1.15)
		blended := 0.5*b.Height + 0.5*median*scale
		if blended > 4 {
			b.Height = blended
			b.HeightSource = "neighbor"
		}
	}
}

// parseLength reads OSM length tags: "415.8", "96 m", "12.5m".
func parseLength(v string) float64 {
	v = strings.TrimSpace(strings.ToLower(v))
	if v == "" {
		return 0
	}
	v = strings.TrimSuffix(strings.TrimSpace(strings.TrimSuffix(v, "m")), " ")
	f, err := strconv.ParseFloat(strings.TrimSpace(v), 64)
	if err != nil || f <= 0 || f > 1200 {
		return 0
	}
	return f
}

func atoiDefault(v string, def int) int {
	n, err := strconv.Atoi(strings.TrimSpace(v))
	if err != nil {
		return def
	}
	return n
}

// roadWidth infers a carriageway width from the class and lane count.
func roadWidth(tags map[string]string) float64 {
	if w := parseLength(tags["width"]); w > 0 {
		return w
	}
	if lanes := atoiDefault(tags["lanes"], 0); lanes > 0 {
		return math.Max(4, float64(lanes)*3.4)
	}
	switch tags["highway"] {
	case "motorway", "trunk":
		return 16
	case "primary":
		return 14
	case "secondary":
		return 11
	case "tertiary":
		return 9
	case "residential", "unclassified":
		return 7
	case "service":
		return 4.5
	}
	return 3
}

// dedupeWay drops consecutive coincident vertices from an open way, keeping the
// node id array aligned. A road is never closed the way a ring is, so unlike
// DedupeRing the first and last points are both kept.
func dedupeWay(pts [][2]float64, nodes []int64, eps float64) ([][2]float64, []int64) {
	if len(pts) == 0 {
		return nil, nil
	}
	aligned := len(nodes) == len(pts)
	outPts := [][2]float64{pts[0]}
	var outNodes []int64
	if aligned {
		outNodes = []int64{nodes[0]}
	}
	for i := 1; i < len(pts); i++ {
		last := outPts[len(outPts)-1]
		if math.Hypot(pts[i][0]-last[0], pts[i][1]-last[1]) <= eps {
			continue
		}
		outPts = append(outPts, pts[i])
		if aligned {
			outNodes = append(outNodes, nodes[i])
		}
	}
	return outPts, outNodes
}
