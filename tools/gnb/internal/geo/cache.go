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
	"time"
)

// Paths resolves the on-disk layout for one tile (design §3).
//
//	external/geocache/<tile>/   raw downloads + IR   (gitignored, ODbL-derived)
//	assets/scad/geo/<tile>/     .hmap + attribution  (committed, produced work)
//	assets/scad/proc/generated/<tile>.scad
type Paths struct {
	RepoRoot string
	Tile     string
}

func NewPaths(repoRoot, tile string) Paths { return Paths{RepoRoot: repoRoot, Tile: tile} }

func (p Paths) CacheDir() string {
	return filepath.Join(p.RepoRoot, "external", "geocache", p.Tile)
}
func (p Paths) DemDir() string   { return filepath.Join(p.RepoRoot, "external", "geocache", "_dem") }
func (p Paths) OsmPath() string  { return filepath.Join(p.CacheDir(), "osm", "overpass.json") }
func (p Paths) IRPath() string   { return filepath.Join(p.CacheDir(), "tile.json") }
func (p Paths) MetaPath() string { return filepath.Join(p.CacheDir(), "meta.json") }

func (p Paths) AssetDir() string {
	return filepath.Join(p.RepoRoot, "assets", "scad", "geo", p.Tile)
}
func (p Paths) HmapPath() string { return filepath.Join(p.AssetDir(), "terrain.hmap") }
func (p Paths) POIPath() string  { return filepath.Join(p.AssetDir(), "poi.json") }
func (p Paths) AttributionPath() string {
	return filepath.Join(p.AssetDir(), "ATTRIBUTION.md")
}
func (p Paths) ScadPath() string {
	return filepath.Join(p.RepoRoot, "assets", "scad", "proc", "generated", p.Tile+".scad")
}

// HmapAssetRef is the runtime-root-relative path the TERR literal carries.
func (p Paths) HmapAssetRef() string {
	return "assets/scad/geo/" + p.Tile + "/terrain.hmap"
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
