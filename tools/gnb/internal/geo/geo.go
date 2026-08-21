// Package geo turns public geographic data (SRTM elevation + OpenStreetMap
// vectors) into a gkNextEngine .scad city level.
//
// The pipeline is five decoupled stages, each independently re-runnable
// (docs/designs/geo-city-generation-design.md §3):
//
//	fetch      bbox -> raw SRTM .hgt.gz + Overpass JSON   (external/geocache)
//	normalize  -> local ENU metres, IR (tile.json)        (external/geocache)
//	terrain    -> resample + DSM strip + sea bed -> .hmap (assets/geo)
//	layout     -> height inference, simplify, block split (in memory)
//	emit       -> TERR + hmap ref + block modules -> .scad
//
// Raw downloads and the IR are ODbL-derived databases and stay out of the
// repository; only the produced .scad/.hmap are committed, each carrying the
// required attribution.
package geo

import (
	"fmt"
	"math"
	"strings"
)

// Tile is the request: a square area of ground centred on a lat/lon.
type Tile struct {
	Name    string  // cache + output identifier, e.g. "hk_victoria"
	Lat     float64 // centre latitude (WGS84 degrees)
	Lon     float64 // centre longitude
	SizeM   float64 // side length in metres
	Cells   int     // terrain grid resolution per axis (<= 176, see design §6)
	Profile string  // building height profile name
	Seed    int     // terrain seed (jitter only; the field itself is data)

	// Frame is the metric frame the tile's geometry is expressed in. The zero
	// value means "my own centre", which is what a standalone tile uses.
	Frame Frame
}

// Frame lets several tiles share one tangent plane.
//
// Metres-per-degree varies with latitude, so a neighbour centre derived from
// the neighbour's own plane does not land exactly one part away: measured at
// 40 degrees N, one 1 km step comes out 3.9 m long. A mosaic therefore owns the
// projection and every part borrows it, carrying its own centre as an offset
// inside that plane. Geometry stays part-local (the part module is placed with
// a translate), so only the projection is shared, not the coordinates.
type Frame struct {
	Shared    bool    // false: OriginLat/Lon and the offsets are ignored
	OriginLat float64 // tangent-plane origin (the mosaic centre)
	OriginLon float64
	OffsetX   float64 // this tile's centre inside the frame, metres east
	OffsetY   float64 // ... metres north
}

// Proj is the tangent plane the tile's coordinates are measured in.
func (t Tile) Proj() Proj {
	if t.Frame.Shared {
		return NewProj(t.Frame.OriginLat, t.Frame.OriginLon)
	}
	return NewProj(t.Lat, t.Lon)
}

// Offset is where the tile centre sits inside its frame; local coordinates are
// frame coordinates minus this.
func (t Tile) Offset() (x, y float64) {
	if t.Frame.Shared {
		return t.Frame.OffsetX, t.Frame.OffsetY
	}
	return 0, 0
}

// Project maps WGS84 degrees straight to this tile's local metres.
func (t Tile) Project(lat, lon float64) (x, y float64) {
	px, py := t.Proj().Forward(lat, lon)
	ox, oy := t.Offset()
	return px - ox, py - oy
}

// Unproject is the inverse of Project.
func (t Tile) Unproject(x, y float64) (lat, lon float64) {
	ox, oy := t.Offset()
	return t.Proj().Inverse(x+ox, y+oy)
}

// DefaultCells keeps a 1km tile at ~5.7m per cell while staying under the
// 180^2 limit above which the engine drops the terrain physics mesh.
const DefaultCells = 176

// Normalize fills in defaults and validates the request.
func (t *Tile) Normalize() error {
	t.Name = strings.TrimSpace(t.Name)
	if t.Name == "" {
		return fmt.Errorf("tile needs a --name")
	}
	if t.Lat < -60 || t.Lat > 60 {
		// SRTM only covers 60S..60N.
		return fmt.Errorf("latitude %.4f is outside SRTM coverage (60S..60N)", t.Lat)
	}
	if t.Lon < -180 || t.Lon > 180 {
		return fmt.Errorf("longitude %.4f out of range", t.Lon)
	}
	if t.SizeM <= 0 {
		t.SizeM = 1000
	}
	if t.SizeM > 8000 {
		return fmt.Errorf("size %.0fm is beyond what a single .scad scene should hold", t.SizeM)
	}
	if t.Cells <= 0 {
		t.Cells = DefaultCells
	}
	if t.Cells < 8 || t.Cells > 256 {
		return fmt.Errorf("cells %d out of range (8..256)", t.Cells)
	}
	if t.Cells > 180 {
		return fmt.Errorf("cells %d exceeds 180: the engine skips the terrain "+
			"physics mesh above 180^2 (see AGENT_GUIDE/ScadTerrain.md)", t.Cells)
	}
	if t.Profile == "" {
		t.Profile = "default"
	}
	return nil
}

// BBox is a lat/lon rectangle.
type BBox struct {
	South, West, North, East float64
}

// BBox returns the tile footprint, padded by padM metres on every side (the
// terrain resample needs a margin so bilinear taps at the border stay inside
// real data, and buildings straddling the edge still get clipped correctly).
func (t Tile) BBox(padM float64) BBox {
	p := NewProj(t.Lat, t.Lon)
	half := t.SizeM/2 + padM
	dLat := half / p.metresPerLat
	dLon := half / p.metresPerLon
	return BBox{
		South: t.Lat - dLat,
		West:  t.Lon - dLon,
		North: t.Lat + dLat,
		East:  t.Lon + dLon,
	}
}

func (b BBox) String() string {
	return fmt.Sprintf("%.6f,%.6f,%.6f,%.6f", b.South, b.West, b.North, b.East)
}

// Contains reports whether a point is inside the box.
func (b BBox) Contains(lat, lon float64) bool {
	return lat >= b.South && lat <= b.North && lon >= b.West && lon <= b.East
}

// Proj is a local east-north-up tangent plane centred on (lat0, lon0):
// SCAD +x is east, +y is north, units are metres. Over a few kilometres the
// linearisation error is well under a centimetre.
type Proj struct {
	lat0, lon0                 float64
	metresPerLat, metresPerLon float64
}

// NewProj builds the tangent plane at the given origin.
func NewProj(lat0, lon0 float64) Proj {
	phi := lat0 * math.Pi / 180
	// WGS84 metres-per-degree series.
	mLat := 111132.92 - 559.82*math.Cos(2*phi) + 1.175*math.Cos(4*phi) - 0.0023*math.Cos(6*phi)
	mLon := 111412.84*math.Cos(phi) - 93.5*math.Cos(3*phi) + 0.118*math.Cos(5*phi)
	if mLon < 1 {
		mLon = 1 // degenerate near the poles; SRTM does not reach there anyway
	}
	return Proj{lat0: lat0, lon0: lon0, metresPerLat: mLat, metresPerLon: mLon}
}

// Forward maps WGS84 degrees to local metres.
func (p Proj) Forward(lat, lon float64) (x, y float64) {
	return (lon - p.lon0) * p.metresPerLon, (lat - p.lat0) * p.metresPerLat
}

// Inverse maps local metres back to WGS84 degrees.
func (p Proj) Inverse(x, y float64) (lat, lon float64) {
	return p.lat0 + y/p.metresPerLat, p.lon0 + x/p.metresPerLon
}

// MetresPerDegree exposes the scale factors (used by the DEM resampler).
func (p Proj) MetresPerDegree() (perLat, perLon float64) {
	return p.metresPerLat, p.metresPerLon
}
