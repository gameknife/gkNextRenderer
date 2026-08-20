package geo

import (
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"strings"
	"time"
)

// Logf is the progress sink (wired to the gnb console by the command layer).
type Logf func(format string, args ...any)

// Options carries everything the CLI can override.
type Options struct {
	RepoRoot         string
	OverpassEndpoint string
	Emit             EmitOptions
	DebugImages      bool
	// Warnf receives data-quality warnings; falls back to the progress log.
	Warnf Logf
}

func (o Options) warn(format string, args ...any) {
	if o.Warnf != nil {
		o.Warnf(format, args...)
	}
}

// DefaultOptions returns the standard configuration.
func DefaultOptions(repoRoot string) Options {
	return Options{
		RepoRoot:         repoRoot,
		OverpassEndpoint: OverpassEndpoint,
		Emit:             DefaultEmitOptions(),
	}
}

// demPadM widens the download/query box so bilinear taps and buildings that
// straddle the tile edge are still covered.
const demPadM = 120

// Fetch downloads the raw DEM and OSM data for the tile (cached).
func Fetch(tile Tile, opt Options, logf Logf) (*Meta, error) {
	if err := tile.Normalize(); err != nil {
		return nil, err
	}
	paths := NewPaths(opt.RepoRoot, tile.Name)
	bbox := tile.BBox(demPadM)

	_, demSources, err := NewSampler(bbox, paths.DemDir(), logf)
	if err != nil {
		return nil, fmt.Errorf("elevation: %w", err)
	}

	fetched, err := FetchOverpass(opt.OverpassEndpoint, bbox, paths.OsmPath(), logf)
	if err != nil {
		return nil, fmt.Errorf("overpass: %w", err)
	}
	if !fetched {
		logf("overpass: cached (%s)", relTo(opt.RepoRoot, paths.OsmPath()))
	}
	sum, size, err := sha256File(paths.OsmPath())
	if err != nil {
		return nil, err
	}

	meta := &Meta{
		Tile: tile.Name, Lat: tile.Lat, Lon: tile.Lon, SizeM: tile.SizeM,
		BBox:      [4]float64{bbox.South, bbox.West, bbox.North, bbox.East},
		FetchedAt: time.Now().UTC(),
		Sources: append(demSources, Source{
			Kind: "osm", URL: opt.OverpassEndpoint, Path: paths.OsmPath(),
			Bytes: size, SHA256: sum, License: osmLicense,
		}),
	}
	for i := range meta.Sources {
		meta.Sources[i].Path = relTo(opt.RepoRoot, meta.Sources[i].Path)
	}
	if err := writeJSON(paths.MetaPath(), meta); err != nil {
		return nil, err
	}
	return meta, nil
}

// BuildResult is what stage B+C produce.
type BuildResult struct {
	IR      *IR
	Grid    *HeightGrid
	Terrain TerrainReport
	Stats   Stats
}

// Build normalises the cached OSM data into the IR and turns the DEM into the
// tile's .hmap. Requires a prior Fetch.
func Build(tile Tile, opt Options, logf Logf) (*BuildResult, error) {
	if err := tile.Normalize(); err != nil {
		return nil, err
	}
	paths := NewPaths(opt.RepoRoot, tile.Name)
	if !fileExists(paths.OsmPath()) {
		return nil, fmt.Errorf("no cached OSM data for %q — run `gnb geo fetch` first", tile.Name)
	}
	profile, ok := Profiles[tile.Profile]
	if !ok {
		return nil, fmt.Errorf("unknown height profile %q (have: %s)", tile.Profile, profileNames())
	}

	elements, err := ParseOverpass(paths.OsmPath())
	if err != nil {
		return nil, err
	}
	landmarks, err := LoadLandmarks(opt.RepoRoot)
	if err != nil {
		return nil, err
	}
	ir := Normalize(tile, elements, profile, landmarks)
	stats := ir.Summarize()
	logf("normalized: %d buildings (%d tagged / %d levels / %d inferred), %d roads, %d water areas, %d coastline ways",
		stats.Buildings, stats.HeightFromTag, stats.HeightFromLvls, stats.HeightInferred,
		stats.Roads, stats.WaterAreas, stats.CoastlineWays)
	if stats.TallestName != "" {
		logf("tallest: %s at %.0fm", stats.TallestName, stats.TallestHeight)
	}
	logf("pois: %d (%d from standalone nodes) — %s",
		stats.POIs, stats.POINodes, FormatPOICounts(ir.POIs))
	if stats.POIs == 0 {
		opt.warn("no named places in this tile — the POI layer will be empty. " +
			"Either OSM has no names here, or the cached Overpass response predates " +
			"the POI selectors (delete external/geocache/<tile>/osm and re-fetch).")
	}

	sampler, _, err := NewSampler(tile.BBox(demPadM), paths.DemDir(), logf)
	if err != nil {
		return nil, err
	}
	debugDir := ""
	if opt.DebugImages {
		debugDir = filepath.Join(paths.CacheDir(), "debug")
	}
	grid, terrainReport, err := BuildTerrain(tile, ir, sampler, debugDir)
	if err != nil {
		return nil, err
	}
	// The terrain stage derives the tile datum; the emitter reads it back from
	// the IR, so the IR is written after the terrain rather than before.
	ir.Terrain = &TerrainMeta{
		BaseElevation: terrainReport.BaseElevation,
		HasWater:      terrainReport.Water.HasWater,
		IsSea:         terrainReport.Water.IsSea,
		WaterLevel:    terrainReport.Water.Level,
	}
	if err := writeJSON(paths.IRPath(), ir); err != nil {
		return nil, err
	}
	logf("terrain: %s", terrainReport)
	if terrainReport.CoastalGradientWarning() {
		opt.warn("near-shore terrain rises %.0f%% — the source DSM's rooftop returns were "+
			"not fully removable here (dense downtown at 30 m posting). Absolute elevations "+
			"in the built-up area read high; see design §5.1.",
			terrainReport.CoastalGradient*100)
	}
	if debugDir != "" {
		logf("debug images: %s", relTo(opt.RepoRoot, debugDir))
	}

	blob, err := EncodeHmap(grid)
	if err != nil {
		return nil, err
	}
	if err := os.MkdirAll(paths.AssetDir(), 0o755); err != nil {
		return nil, err
	}
	if err := os.WriteFile(paths.HmapPath(), blob, 0o644); err != nil {
		return nil, err
	}
	logf("wrote %s (%d KB)", relTo(opt.RepoRoot, paths.HmapPath()), len(blob)/1024)

	poiFile := POIFile{
		Format: POIFormat, Tile: tile.Name,
		Center: [2]float64{tile.Lat, tile.Lon}, SizeM: tile.SizeM,
		Attribution: ir.Attribution, POIs: ir.POIs,
	}
	if err := writeJSON(paths.POIPath(), poiFile); err != nil {
		return nil, err
	}
	logf("wrote %s (%d places)", relTo(opt.RepoRoot, paths.POIPath()), len(ir.POIs))

	if err := writeAttribution(paths, tile, ir); err != nil {
		return nil, err
	}
	return &BuildResult{IR: ir, Grid: grid, Terrain: terrainReport, Stats: stats}, nil
}

// Scad renders the .scad scene from the cached IR + .hmap. Requires a prior Build.
func Scad(tile Tile, opt Options, logf Logf) (string, EmitReport, error) {
	if err := tile.Normalize(); err != nil {
		return "", EmitReport{}, err
	}
	paths := NewPaths(opt.RepoRoot, tile.Name)
	var ir IR
	if err := readJSON(paths.IRPath(), &ir); err != nil {
		return "", EmitReport{}, fmt.Errorf("no IR for %q — run `gnb geo build` first (%w)", tile.Name, err)
	}
	blob, err := os.ReadFile(paths.HmapPath())
	if err != nil {
		return "", EmitReport{}, fmt.Errorf("no .hmap for %q — run `gnb geo build` first (%w)", tile.Name, err)
	}
	grid, err := DecodeHmap(blob)
	if err != nil {
		return "", EmitReport{}, err
	}

	source, report := Emit(tile, &ir, grid, paths.HmapAssetRef(), opt.Emit)
	if err := os.MkdirAll(filepath.Dir(paths.ScadPath()), 0o755); err != nil {
		return "", report, err
	}
	if err := os.WriteFile(paths.ScadPath(), []byte(source), 0o644); err != nil {
		return "", report, err
	}
	logf("emitted: %s", report)
	return paths.ScadPath(), report, nil
}

// LoadLandmarks reads the optional per-OSM-id height override table.
func LoadLandmarks(repoRoot string) (map[int64]float64, error) {
	path := filepath.Join(repoRoot, filepath.FromSlash(LandmarksRef))
	data, err := os.ReadFile(path)
	if os.IsNotExist(err) {
		return map[int64]float64{}, nil
	}
	if err != nil {
		return nil, err
	}
	// {"way/123": 412.0, "456": 300} — the way/ prefix is optional so the file
	// can be pasted straight from an OSM URL.
	var raw map[string]float64
	if err := json.Unmarshal(data, &raw); err != nil {
		return nil, fmt.Errorf("%s: %w", path, err)
	}
	out := make(map[int64]float64, len(raw))
	for k, v := range raw {
		k = strings.TrimPrefix(strings.TrimPrefix(k, "way/"), "relation/")
		var id int64
		if _, err := fmt.Sscan(k, &id); err == nil {
			out[id] = v
		}
	}
	return out, nil
}

func writeAttribution(paths Paths, tile Tile, ir *IR) error {
	var s strings.Builder
	fmt.Fprintf(&s, "# %s — data attribution\n\n", tile.Name)
	fmt.Fprintf(&s, "Generated by `gnb geo` from public data for the tile centred on\n")
	fmt.Fprintf(&s, "%.5f, %.5f (%.0f x %.0f m).\n\n", tile.Lat, tile.Lon, tile.SizeM, tile.SizeM)
	s.WriteString("Sources:\n\n")
	for _, a := range ir.Attribution {
		fmt.Fprintf(&s, "- %s\n", a)
	}
	s.WriteString("\nThe committed files here (`terrain.hmap`, `poi.json`) and the generated scene are\n")
	s.WriteString("produced works. The raw downloads and the normalised intermediate\n")
	s.WriteString("representation are ODbL-derived databases and stay in `external/geocache/`,\n")
	s.WriteString("which is not part of the repository.\n")
	return os.WriteFile(paths.AttributionPath(), []byte(s.String()), 0o644)
}

func profileNames() string {
	names := make([]string, 0, len(Profiles))
	for k := range Profiles {
		names = append(names, k)
	}
	return strings.Join(names, ", ")
}

func relTo(root, path string) string {
	rel, err := filepath.Rel(root, path)
	if err != nil {
		return path
	}
	return filepath.ToSlash(rel)
}
