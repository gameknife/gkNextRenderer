package geo

import (
	"compress/gzip"
	"fmt"
	"io"
	"math"
	"os"
	"path/filepath"
)

// SRTM 1 arc-second tiles, served raw (big-endian int16, 3601x3601) and
// gzipped by the AWS "elevation-tiles-prod" open-data bucket.
//
// Why the raw .hgt rather than the terrarium PNG tiles from the same bucket:
// the PNG encoding was measured to carry ~1% single-pixel corruption in the
// red channel (decoding to -1600..-8600m) which is indistinguishable from real
// bathymetry by any simple rule. The int16 payload has no such problem and a
// single 1-degree tile covers a whole metropolitan area.
//
// SRTM voids are encoded as -32768 and are filled from their neighbours.
const (
	srtmURLBase = "https://s3.amazonaws.com/elevation-tiles-prod/skadi"
	srtmSamples = 3601 // 1 arc-second, inclusive of both edges
	srtmVoid    = -32768
	srtmLicense = "SRTM / NASA-USGS (public domain), via AWS elevation-tiles-prod"
)

// SrtmTileName returns the canonical name of the 1-degree tile containing the
// point, e.g. (22.28, 114.15) -> "N22E114".
func SrtmTileName(lat, lon float64) string {
	latDeg := int(math.Floor(lat))
	lonDeg := int(math.Floor(lon))
	ns, ew := 'N', 'E'
	if latDeg < 0 {
		ns = 'S'
	}
	if lonDeg < 0 {
		ew = 'W'
	}
	return fmt.Sprintf("%c%02d%c%03d", ns, abs(latDeg), ew, abs(lonDeg))
}

func srtmTileURL(name string) string {
	// skadi lays tiles out as <N22>/<N22E114.hgt.gz>.
	return fmt.Sprintf("%s/%s/%s.hgt.gz", srtmURLBase, name[:3], name)
}

// SrtmTile is one decoded 1-degree tile.
type SrtmTile struct {
	Name    string
	LatDeg  int // south edge
	LonDeg  int // west edge
	Samples []int16
}

// Sampler reads elevation across however many SRTM tiles a bbox spans.
type Sampler struct {
	tiles map[string]*SrtmTile
}

// NewSampler loads (downloading on a cache miss) every tile covering bbox.
func NewSampler(bbox BBox, demDir string, logf func(string, ...any)) (*Sampler, []Source, error) {
	s := &Sampler{tiles: map[string]*SrtmTile{}}
	var sources []Source
	for latDeg := int(math.Floor(bbox.South)); latDeg <= int(math.Floor(bbox.North)); latDeg++ {
		for lonDeg := int(math.Floor(bbox.West)); lonDeg <= int(math.Floor(bbox.East)); lonDeg++ {
			name := SrtmTileName(float64(latDeg), float64(lonDeg))
			if _, seen := s.tiles[name]; seen {
				continue
			}
			path := filepath.Join(demDir, name+".hgt.gz")
			url := srtmTileURL(name)
			if _, err := httpGetToFile(url, path, logf); err != nil {
				return nil, nil, err
			}
			tile, err := loadSrtmTile(name, latDeg, lonDeg, path)
			if err != nil {
				return nil, nil, err
			}
			s.tiles[name] = tile
			sum, size, err := sha256File(path)
			if err != nil {
				return nil, nil, err
			}
			sources = append(sources, Source{
				Kind: "dem", URL: url, Path: path, Bytes: size,
				SHA256: sum, License: srtmLicense,
			})
		}
	}
	if len(s.tiles) == 0 {
		return nil, nil, fmt.Errorf("no SRTM tile covers %s", bbox)
	}
	return s, sources, nil
}

func loadSrtmTile(name string, latDeg, lonDeg int, path string) (*SrtmTile, error) {
	f, err := os.Open(path)
	if err != nil {
		return nil, err
	}
	defer f.Close()
	zr, err := gzip.NewReader(f)
	if err != nil {
		return nil, fmt.Errorf("%s: %w", path, err)
	}
	defer zr.Close()
	raw, err := io.ReadAll(zr)
	if err != nil {
		return nil, fmt.Errorf("%s: %w", path, err)
	}
	want := srtmSamples * srtmSamples * 2
	if len(raw) != want {
		return nil, fmt.Errorf("%s: expected %d bytes of 1-arcsecond data, got %d", path, want, len(raw))
	}
	tile := &SrtmTile{Name: name, LatDeg: latDeg, LonDeg: lonDeg,
		Samples: make([]int16, srtmSamples*srtmSamples)}
	for i := range tile.Samples {
		// Big-endian, row 0 = the north edge.
		tile.Samples[i] = int16(uint16(raw[i*2])<<8 | uint16(raw[i*2+1]))
	}
	return tile, nil
}

// sampleInt returns the raw sample at integer tile coordinates, or false for a
// void / out-of-coverage read.
func (s *Sampler) sampleInt(latDeg, lonDeg, row, col int) (float64, bool) {
	// Roll over tile edges so bilinear taps near a boundary keep working.
	for row < 0 {
		row += srtmSamples - 1
		latDeg++
	}
	for row > srtmSamples-1 {
		row -= srtmSamples - 1
		latDeg--
	}
	for col < 0 {
		col += srtmSamples - 1
		lonDeg--
	}
	for col > srtmSamples-1 {
		col -= srtmSamples - 1
		lonDeg++
	}
	tile, ok := s.tiles[SrtmTileName(float64(latDeg), float64(lonDeg))]
	if !ok {
		return 0, false
	}
	v := tile.Samples[row*srtmSamples+col]
	if v == srtmVoid {
		return 0, false
	}
	return float64(v), true
}

// At bilinearly samples elevation in metres. Voids fall back to a widening
// neighbourhood search; if that fails too the second return value is false.
func (s *Sampler) At(lat, lon float64) (float64, bool) {
	latDeg := int(math.Floor(lat))
	lonDeg := int(math.Floor(lon))
	// row 0 is the north edge of the tile (lat = latDeg + 1).
	fr := (float64(latDeg) + 1 - lat) * float64(srtmSamples-1)
	fc := (lon - float64(lonDeg)) * float64(srtmSamples-1)
	r0, c0 := int(math.Floor(fr)), int(math.Floor(fc))
	tr, tc := fr-float64(r0), fc-float64(c0)

	var acc, weight float64
	corner := func(dr, dc int, w float64) {
		if v, ok := s.sampleInt(latDeg, lonDeg, r0+dr, c0+dc); ok {
			acc += v * w
			weight += w
		}
	}
	corner(0, 0, (1-tr)*(1-tc))
	corner(0, 1, (1-tr)*tc)
	corner(1, 0, tr*(1-tc))
	corner(1, 1, tr*tc)
	if weight > 1e-6 {
		return acc / weight, true
	}
	// All four corners are voids: widen the search.
	for radius := 2; radius <= 16; radius++ {
		var sum float64
		var n int
		for dr := -radius; dr <= radius; dr++ {
			for dc := -radius; dc <= radius; dc++ {
				if v, ok := s.sampleInt(latDeg, lonDeg, r0+dr, c0+dc); ok {
					sum += v
					n++
				}
			}
		}
		if n > 0 {
			return sum / float64(n), true
		}
	}
	return 0, false
}

func abs(v int) int {
	if v < 0 {
		return -v
	}
	return v
}
