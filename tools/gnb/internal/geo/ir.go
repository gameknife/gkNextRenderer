package geo

import "time"

// IR is the normalised, projection-free-of-charge intermediate representation:
// everything already in SCAD-local metres (+x east, +y north), everything the
// later stages need, nothing raw. Stages C/D/E read only this, so the whole
// generator can be iterated offline without touching the network.
//
// This file is an ODbL-derived database: it lives in external/geocache and is
// never committed.
type IR struct {
	Tile        string     `json:"tile"`
	Center      [2]float64 `json:"center"` // lat, lon
	SizeM       float64    `json:"sizeM"`
	GeneratedAt time.Time  `json:"generatedAt"`
	Attribution []string   `json:"attribution"`

	// Terrain is filled in by the terrain stage and read back by the emitter:
	// the .hmap carries samples but not what they mean, and the scene needs the
	// tile's datum and water plane to write a correct TERR.
	Terrain *TerrainMeta `json:"terrain,omitempty"`

	Buildings []Building `json:"buildings"`
	Roads     []Road     `json:"roads"`
	Waters    []Area     `json:"waters"`    // natural=water / riverbank polygons
	Coastline []Line     `json:"coastline"` // natural=coastline, sea on the right
	Landuse   []Area     `json:"landuse"`   // parks, forest, industrial, ...
	Piers     []Line     `json:"piers"`     // man_made=pier
	POIs      []POI      `json:"pois"`      // named places, see poi.go
}

// POI is one named place. Unlike everything else in the IR it carries no
// geometry: it is a label anchor, consumed by the .poi.json sidecar rather than
// by the .scad emitter. Sources are standalone OSM nodes, named building
// footprints, and named leisure/landuse areas — see ClassifyPOI.
type POI struct {
	ID   int64  `json:"id"`
	Name string `json:"name"`
	// Tag is the raw OSM key=value the POI was recognised by, e.g.
	// "building=church" or "railway=station".
	Tag string `json:"tag"`
	// Category is the normalised bucket the runtime colours and filters by.
	Category string `json:"category"`
	// Source is "node" | "building" | "area".
	Source string `json:"source"`
	// Pos is the label anchor in SCAD metres (+x east, +y north); for footprints
	// and areas it is the ring centroid.
	Pos [2]float64 `json:"pos"`
	// Height is the building height in metres, 0 for nodes and areas. The
	// runtime lifts a building label to its roof.
	Height float64 `json:"height,omitempty"`
	AreaM2 float64 `json:"areaM2,omitempty"`
	// Rank is the prominence score (see ScorePOI). Higher shows first when the
	// runtime caps how many labels it draws.
	Rank float64 `json:"rank"`
}

// TerrainMeta is the derived tile datum. Absolute ground elevation ranges from
// 0 m on Manhattan to ~500 m in Chengdu, so every threshold expressed as "how
// far above the ground" has to be anchored here rather than at z = 0.
type TerrainMeta struct {
	BaseElevation float64 `json:"baseElevation"`
	HasWater      bool    `json:"hasWater"`
	IsSea         bool    `json:"isSea"`
	WaterLevel    float64 `json:"waterLevel"`
}

// Ring is a closed polygon in SCAD metres (first point not repeated).
type Ring [][2]float64

// Building is one OSM building footprint with an inferred height.
type Building struct {
	ID     int64  `json:"id"`
	Name   string `json:"name,omitempty"`
	Kind   string `json:"kind"` // the building=* tag value
	Outer  Ring   `json:"outer"`
	Inners []Ring `json:"inners,omitempty"`

	Height    float64 `json:"height"`
	MinHeight float64 `json:"minHeight,omitempty"`
	// HeightSource records how Height was obtained, so the build report can
	// state how much of the skyline is real data vs. inference.
	HeightSource string  `json:"heightSource"`
	Levels       int     `json:"levels,omitempty"`
	AreaM2       float64 `json:"areaM2"`
}

// Road is a highway centreline.
type Road struct {
	ID    int64        `json:"id"`
	Name  string       `json:"name,omitempty"`
	Class string       `json:"class"` // highway=* value
	Lanes int          `json:"lanes,omitempty"`
	Width float64      `json:"width"` // metres, inferred when untagged
	Pts   [][2]float64 `json:"pts"`
	// Nodes is index-aligned with Pts: the OSM node id of each vertex. This is
	// the network topology — shared ids are exact junctions — and it must be
	// consumed before any clipping or simplification breaks the alignment.
	Nodes []int64 `json:"nodes,omitempty"`
}

// Area is a tagged polygon (water, landuse, leisure).
type Area struct {
	ID     int64  `json:"id"`
	Tag    string `json:"tag"` // e.g. "natural=water", "leisure=park"
	Name   string `json:"name,omitempty"`
	Outer  Ring   `json:"outer"`
	Inners []Ring `json:"inners,omitempty"`
}

// Line is a polyline. Closed ways (an area mapped as a way) keep the flag so
// the emitter can extrude them instead of laying a strip along them.
type Line struct {
	ID     int64        `json:"id"`
	Tag    string       `json:"tag"`
	Closed bool         `json:"closed,omitempty"`
	Pts    [][2]float64 `json:"pts"`
}

// Stats summarises an IR for the build report.
type Stats struct {
	Buildings      int
	HeightFromTag  int
	HeightFromLvls int
	HeightInferred int
	Roads          int
	WaterAreas     int
	CoastlineWays  int
	POIs           int
	POINodes       int
	TallestName    string
	TallestHeight  float64
}

// Summarize walks the IR once for reporting.
func (ir *IR) Summarize() Stats {
	s := Stats{Buildings: len(ir.Buildings), Roads: len(ir.Roads),
		WaterAreas: len(ir.Waters), CoastlineWays: len(ir.Coastline),
		POIs: len(ir.POIs)}
	for _, p := range ir.POIs {
		if p.Source == "node" {
			s.POINodes++
		}
	}
	for _, b := range ir.Buildings {
		switch b.HeightSource {
		case "tag":
			s.HeightFromTag++
		case "levels":
			s.HeightFromLvls++
		default:
			s.HeightInferred++
		}
		if b.Height > s.TallestHeight {
			s.TallestHeight = b.Height
			s.TallestName = b.Name
			if s.TallestName == "" {
				s.TallestName = b.Kind
			}
		}
	}
	return s
}
