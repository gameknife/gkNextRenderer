package geo

import (
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"os"
	"path/filepath"
	"sort"
	"time"
)

// Paths resolves the on-disk layout for one tile (design §3).
//
//	external/geocache/<tile>/   raw downloads + IR   (gitignored, ODbL-derived)
//	assets/geo/<tile>/          .scad + .hmap + poi.json + attribution
//
// Tile output is bulky (a few hundred KB per tile, half of it binary) and fully
// reproducible from `gnb geo make`, so assets/geo is gitignored and shipped as
// assets/paks/geo.pak instead — see `gnb geo pak`. Keeping all four artefacts in
// one per-tile directory is what makes that single pak boundary possible.
type Paths struct {
	RepoRoot string
	Tile     string
}

// GeoAssetRoot is the single runtime-root-relative directory every tile artefact
// lives under. The C++ side repeats it as kGeoAssetDir in GeoTileCatalog.cpp.
const GeoAssetRoot = "assets/geo"

// LandmarksRef is the hand-maintained per-OSM-id height override table. It is an
// input rather than output, so it is the one file under assets/geo that is
// committed.
const LandmarksRef = GeoAssetRoot + "/landmarks.json"

// PakRef is where `gnb geo pak` writes the shippable pak.
const PakRef = "assets/paks/geo.pak"

func NewPaths(repoRoot, tile string) Paths { return Paths{RepoRoot: repoRoot, Tile: tile} }

// ListTiles returns the names of every complete tile under assets/geo, sorted.
// A directory missing any of the four artefacts is skipped rather than reported:
// it is a half-written `gnb geo build`, not something worth packing or loading.
func ListTiles(repoRoot string) ([]string, error) {
	root := filepath.Join(repoRoot, filepath.FromSlash(GeoAssetRoot))
	entries, err := os.ReadDir(root)
	if os.IsNotExist(err) {
		return nil, nil
	}
	if err != nil {
		return nil, err
	}
	tiles := make([]string, 0, len(entries))
	for _, entry := range entries {
		if !entry.IsDir() {
			continue
		}
		p := NewPaths(repoRoot, entry.Name())
		if fileExists(p.ScadPath()) && fileExists(p.HmapPath()) && fileExists(p.POIPath()) {
			tiles = append(tiles, entry.Name())
		}
	}
	sort.Strings(tiles)
	return tiles, nil
}

func (p Paths) CacheDir() string {
	return filepath.Join(p.RepoRoot, "external", "geocache", p.Tile)
}
func (p Paths) DemDir() string   { return filepath.Join(p.RepoRoot, "external", "geocache", "_dem") }
func (p Paths) OsmPath() string  { return filepath.Join(p.CacheDir(), "osm", "overpass.json") }
func (p Paths) IRPath() string   { return filepath.Join(p.CacheDir(), "tile.json") }
func (p Paths) MetaPath() string { return filepath.Join(p.CacheDir(), "meta.json") }

func (p Paths) AssetDir() string {
	return filepath.Join(p.RepoRoot, GeoAssetRoot, p.Tile)
}
func (p Paths) HmapPath() string { return filepath.Join(p.AssetDir(), "terrain.hmap") }
func (p Paths) POIPath() string  { return filepath.Join(p.AssetDir(), "poi.json") }
func (p Paths) AttributionPath() string {
	return filepath.Join(p.AssetDir(), "ATTRIBUTION.md")
}
func (p Paths) ScadPath() string {
	return filepath.Join(p.AssetDir(), p.Tile+".scad")
}

// HmapAssetRef is the runtime-root-relative path the TERR literal carries.
func (p Paths) HmapAssetRef() string {
	return GeoAssetRoot + "/" + p.Tile + "/terrain.hmap"
}

// ScadAssetRef is the runtime-root-relative path of the emitted scene, i.e. what
// `--scene` and the pak entry name both use.
func (p Paths) ScadAssetRef() string {
	return GeoAssetRoot + "/" + p.Tile + "/" + p.Tile + ".scad"
}

// Meta records provenance for a fetched tile.
type Meta struct {
	Tile      string     `json:"tile"`
	Lat       float64    `json:"lat"`
	Lon       float64    `json:"lon"`
	SizeM     float64    `json:"sizeM"`
	BBox      [4]float64 `json:"bbox"` // south, west, north, east
	FetchedAt time.Time  `json:"fetchedAt"`
	Sources   []Source   `json:"sources"`
}

// Source is one downloaded artefact.
type Source struct {
	Kind    string `json:"kind"` // "dem" | "osm"
	URL     string `json:"url"`
	Path    string `json:"path"` // repo-relative
	Bytes   int64  `json:"bytes"`
	SHA256  string `json:"sha256"`
	License string `json:"license"`
}

func writeJSON(path string, v any) error {
	if err := os.MkdirAll(filepath.Dir(path), 0o755); err != nil {
		return err
	}
	data, err := json.MarshalIndent(v, "", "  ")
	if err != nil {
		return err
	}
	return os.WriteFile(path, append(data, '\n'), 0o644)
}

func readJSON(path string, v any) error {
	data, err := os.ReadFile(path)
	if err != nil {
		return err
	}
	return json.Unmarshal(data, v)
}

func fileExists(path string) bool {
	info, err := os.Stat(path)
	return err == nil && !info.IsDir()
}

func sha256File(path string) (string, int64, error) {
	f, err := os.Open(path)
	if err != nil {
		return "", 0, err
	}
	defer f.Close()
	h := sha256.New()
	n, err := io.Copy(h, f)
	if err != nil {
		return "", 0, err
	}
	return hex.EncodeToString(h.Sum(nil)), n, nil
}

// httpGetToFile downloads url into dst unless dst already exists (the cache is
// the point: Overpass and the SRTM mirror both ask callers not to re-fetch).
func httpGetToFile(url, dst string, logf func(string, ...any)) (bool, error) {
	if fileExists(dst) {
		return false, nil
	}
	if err := os.MkdirAll(filepath.Dir(dst), 0o755); err != nil {
		return false, err
	}
	logf("GET %s", url)
	client := &http.Client{Timeout: 10 * time.Minute}
	req, err := http.NewRequest(http.MethodGet, url, nil)
	if err != nil {
		return false, err
	}
	req.Header.Set("User-Agent", userAgent)
	resp, err := client.Do(req)
	if err != nil {
		return false, err
	}
	defer resp.Body.Close()
	if resp.StatusCode != http.StatusOK {
		return false, fmt.Errorf("%s: HTTP %d", url, resp.StatusCode)
	}
	tmp := dst + ".part"
	f, err := os.Create(tmp)
	if err != nil {
		return false, err
	}
	if _, err := io.Copy(f, resp.Body); err != nil {
		f.Close()
		os.Remove(tmp)
		return false, err
	}
	if err := f.Close(); err != nil {
		os.Remove(tmp)
		return false, err
	}
	return true, os.Rename(tmp, dst)
}

// userAgent identifies the tool to Overpass and the tile mirrors, as their
// usage policies require.
const userAgent = "gkNextRenderer-gnb-geo/1.0 (+https://github.com/gameknife/gkNextRenderer)"
