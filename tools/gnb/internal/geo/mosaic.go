package geo

import (
	"fmt"
	"math"
	"os"
	"strconv"
	"strings"
)

// Mosaic is an area built from a square grid of 1 km parts.
//
// Why parts rather than one bigger tile: the terrain grid is capped at 176
// cells per axis (above 180^2 the engine drops the physics mesh), so a single
// 3 km TERR would be 17 m per cell. Tiling keeps 5.7 m per cell at any area
// size — the resolution a walkable street needs — and gives the renderer a
// spatial hierarchy for free.
//
// What is *not* per part: the projection (see Frame), the DSM->DTM filtering,
// the water plane and the datum. Those are computed once over the whole area
// and sliced afterwards; running them per part leaves a step at every seam
// (measured on two adjacent Manhattan parts: mean 0.24 m, max 0.98 m — enough
// to crack the ground visually and to cut the nav grid in two).
type Mosaic struct {
	Name    string
	Lat     float64 // centre of the whole area
	Lon     float64
	SizeM   float64 // total side length; an odd multiple of PartSizeM
	Cells   int     // terrain cells per axis *per part*
	Profile string
	Seed    int

	// FullRings / MediumRings are the level-of-detail radii, in parts, measured
	// as the Chebyshev distance from the centre part. Everything below
	// FullRings is emitted at full detail, below MediumRings without street
	// decoration, and beyond that as bare prisms. Evaluating a dense part at
	// full detail costs ~6.3 s, so a 5x5 with no LOD is a three-minute load.
	FullRings   int
	MediumRings int
}

// PartSizeM is the edge length of one part. It is fixed rather than tunable:
// the whole point of the grid is that a part is exactly the 1 km tile the rest
// of the pipeline was built and validated around.
const PartSizeM = 1000.0

// MaxGrid bounds the grid at 7x7 (49 km^2). Nothing in the generator breaks
// above it, but the scene does: even at the far level of detail a part costs
// ~1.2 s of evaluation and ~40 nodes.
const MaxGrid = 7

// MosaicFormat versions the manifest.
const MosaicFormat = "gkgeomosaic1"

// Normalize fills in defaults and validates the request.
func (m *Mosaic) Normalize() error {
	m.Name = strings.TrimSpace(m.Name)
	if m.Name == "" {
		return fmt.Errorf("mosaic needs a --name")
	}
	if m.SizeM <= 0 {
		m.SizeM = PartSizeM
	}
	grid := int(math.Round(m.SizeM / PartSizeM))
	if math.Abs(m.SizeM-float64(grid)*PartSizeM) > 1e-6 {
		return fmt.Errorf("size %.0fm is not a multiple of %.0fm — an area is built "+
			"from whole 1 km parts", m.SizeM, PartSizeM)
	}
	if grid%2 == 0 {
		return fmt.Errorf("size %.0fm gives a %dx%d grid with no centre part; use an "+
			"odd multiple of %.0fm (1000, 3000, 5000, ...)", m.SizeM, grid, grid, PartSizeM)
	}
	if grid > MaxGrid {
		return fmt.Errorf("size %.0fm is a %dx%d grid; the cap is %dx%d",
			m.SizeM, grid, grid, MaxGrid, MaxGrid)
	}
	m.SizeM = float64(grid) * PartSizeM
	if m.FullRings <= 0 {
		m.FullRings = 1
	}
	if m.MediumRings <= 0 {
		m.MediumRings = 2
	}
	if m.MediumRings < m.FullRings {
		m.MediumRings = m.FullRings
	}
	// Everything else is validated by the part tile, which is the real request.
	probe := m.TileFor(m.CentrePart())
	return probe.Normalize()
}

// Grid is the number of parts per axis.
func (m Mosaic) Grid() int {
	g := int(math.Round(m.SizeM / PartSizeM))
	if g < 1 {
		return 1
	}
	return g
}

// BBox is the area footprint in degrees, padded by padM on every side.
func (m Mosaic) BBox(padM float64) BBox {
	p := NewProj(m.Lat, m.Lon)
	half := m.SizeM/2 + padM
	dLat := half / p.metresPerLat
	dLon := half / p.metresPerLon
	return BBox{
		South: m.Lat - dLat, West: m.Lon - dLon,
		North: m.Lat + dLat, East: m.Lon + dLon,
	}
}

// Part is one 1 km cell of the grid.
type Part struct {
	Col, Row int    // 0-based from the south-west corner
	ID       string // "p<col>_<row>"; also the SCAD symbol suffix
	OffsetX  float64
	OffsetY  float64
	Lat, Lon float64
	// Ring is the Chebyshev distance from the centre part: 0 is the centre,
	// 1 the eight around it, and so on. It selects the level of detail.
	Ring int
}

// CentrePart is the part the mosaic is named for — the one a `gnb geo make`
// at this centre would have produced on its own.
func (m Mosaic) CentrePart() Part {
	c := (m.Grid() - 1) / 2
	return m.PartAt(c, c)
}

// PartAt builds one part descriptor.
func (m Mosaic) PartAt(col, row int) Part {
	c := (m.Grid() - 1) / 2
	p := Part{
		Col: col, Row: row,
		// The id is the offset from the centre, not the grid index, so it does
		// not move when the area is resized: the centre part is p0_0 at every
		// size, and growing 3x3 to 5x5 keeps all nine cached responses instead
		// of renaming them out from under themselves.
		ID:      fmt.Sprintf("p%s_%s", partTag(col-c), partTag(row-c)),
		OffsetX: float64(col-c) * PartSizeM,
		OffsetY: float64(row-c) * PartSizeM,
		Ring:    maxInt(absInt(col-c), absInt(row-c)),
	}
	// The centre is derived through the mosaic's own plane, so the step between
	// two neighbouring centres is exactly PartSizeM.
	p.Lat, p.Lon = NewProj(m.Lat, m.Lon).Inverse(p.OffsetX, p.OffsetY)
	return p
}

// Parts enumerates the grid in a stable order (south to north, west to east).
func (m Mosaic) Parts() []Part {
	g := m.Grid()
	out := make([]Part, 0, g*g)
	for row := 0; row < g; row++ {
		for col := 0; col < g; col++ {
			out = append(out, m.PartAt(col, row))
		}
	}
	return out
}

// TileFor turns a part into the tile request the existing per-tile stages take.
// The part borrows the mosaic's tangent plane; its geometry stays part-local.
func (m Mosaic) TileFor(p Part) Tile {
	return Tile{
		Name:    m.Name,
		Lat:     p.Lat,
		Lon:     p.Lon,
		SizeM:   PartSizeM,
		Cells:   m.Cells,
		Profile: m.Profile,
		// Parts must not share a jitter seed, or the terrain of every part
		// carries the same vertex noise and the repetition is visible from the
		// air. The seam vertices are unjittered either way (see FScadTerrain),
		// so this does not reopen the seam.
		Seed: m.Seed + p.Col*73 + p.Row*179,
		Frame: Frame{
			Shared:    true,
			OriginLat: m.Lat,
			OriginLon: m.Lon,
			OffsetX:   p.OffsetX,
			OffsetY:   p.OffsetY,
		},
	}
}

// LOD is how much decoration a part gets.
type LOD int

const (
	// LODFull is the tile as the single-tile pipeline emits it: facades, roofs,
	// sidewalks, street furniture, trees.
	LODFull LOD = iota
	// LODMedium keeps building facades but drops street decoration, which is
	// where two thirds of the evaluation time goes (measured on a dense
	// Manhattan part: 4.31 s of 6.3 s, for 217k of 968k triangles).
	LODMedium
	// LODFar is bare prisms and plain carriageways, with the small buildings
	// filtered out. It is what the horizon needs and nothing more.
	LODFar
)

func (l LOD) String() string {
	switch l {
	case LODFull:
		return "full"
	case LODMedium:
		return "medium"
	default:
		return "far"
	}
}

// LODForRing maps a ring index onto a level of detail.
func (m Mosaic) LODForRing(ring int) LOD {
	switch {
	case ring < m.FullRings:
		return LODFull
	case ring < m.MediumRings:
		return LODMedium
	default:
		return LODFar
	}
}

// MosaicFile is the manifest written next to the scene. It is what `gnb geo
// grow` reads to know what already exists, and what a runtime consumer reads
// to know that one scene holds several terrains.
type MosaicFile struct {
	Format      string       `json:"format"`
	Name        string       `json:"name"`
	Center      [2]float64   `json:"center"` // lat, lon
	SizeM       float64      `json:"sizeM"`
	PartSizeM   float64      `json:"partSizeM"`
	Grid        int          `json:"grid"`
	Cells       int          `json:"cells"`
	Profile     string       `json:"profile"`
	Seed        int          `json:"seed"`
	FullRings   int          `json:"fullRings"`
	MediumRings int          `json:"mediumRings"`
	Attribution []string     `json:"attribution"`
	Parts       []MosaicPart `json:"parts"`
}

// MosaicPart is one entry of the manifest.
type MosaicPart struct {
	ID     string     `json:"id"`
	Col    int        `json:"col"`
	Row    int        `json:"row"`
	Ring   int        `json:"ring"`
	LOD    string     `json:"lod"`
	Offset [2]float64 `json:"offset"` // part centre in mosaic metres
	Center [2]float64 `json:"center"` // lat, lon
	Hmap   string     `json:"hmap"`   // runtime-root-relative
}

// ToMosaic reconstructs the request from a manifest, so `grow` can re-run the
// pipeline with the same centre, profile and seed as the first pass.
func (f MosaicFile) ToMosaic() Mosaic {
	return Mosaic{
		Name: f.Name, Lat: f.Center[0], Lon: f.Center[1],
		SizeM: f.SizeM, Cells: f.Cells, Profile: f.Profile, Seed: f.Seed,
		FullRings: f.FullRings, MediumRings: f.MediumRings,
	}
}

// LoadMosaic reads back the manifest of an area that already exists on disk.
// The second return says whether there was one: an area generated before the
// manifest existed, or one that was never generated, is not an error.
func LoadMosaic(repoRoot, name string) (Mosaic, bool, error) {
	paths := NewPaths(repoRoot, name)
	var f MosaicFile
	if err := readJSON(paths.MosaicPath(), &f); err != nil {
		if os.IsNotExist(err) {
			return Mosaic{}, false, nil
		}
		return Mosaic{}, false, err
	}
	if f.Format != MosaicFormat {
		return Mosaic{}, false, fmt.Errorf("%s: unknown manifest format %q",
			paths.MosaicPath(), f.Format)
	}
	return f.ToMosaic(), true, nil
}

// LoadFetchMeta recovers what is known about an area whose manifest predates
// this format: the fetch metadata has always carried the centre and the size.
func LoadFetchMeta(repoRoot, name string) (*Meta, bool, error) {
	var meta Meta
	if err := readJSON(NewPaths(repoRoot, name).MetaPath(), &meta); err != nil {
		if os.IsNotExist(err) {
			return nil, false, nil
		}
		return nil, false, err
	}
	return &meta, true, nil
}

// OutputFiles lists everything the area writes under assets/geo/<name>, so the
// command layer can mirror the lot into the build tree without knowing the
// per-part layout.
func (m Mosaic) OutputFiles(repoRoot string) []string {
	paths := NewPaths(repoRoot, m.Name)
	single := m.Grid() == 1
	out := []string{paths.ScadPath(), paths.POIPath(), paths.AttributionPath(), paths.MosaicPath()}
	for _, part := range m.Parts() {
		out = append(out, paths.PartHmapPath(part.ID, single))
	}
	return out
}

// partTag encodes a signed offset as an identifier fragment: the id is both a
// directory name and a SCAD symbol suffix, and a symbol cannot carry a minus.
func partTag(v int) string {
	if v < 0 {
		return "m" + strconv.Itoa(-v)
	}
	return strconv.Itoa(v)
}

func absInt(v int) int {
	if v < 0 {
		return -v
	}
	return v
}

func maxInt(a, b int) int {
	if a > b {
		return a
	}
	return b
}
