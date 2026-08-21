package geo

import (
	"encoding/json"
	"fmt"
	"math"
	"os"
	"path/filepath"
	"regexp"
	"sort"
	"strings"
	"time"
)

// Logf is the progress sink (wired to the gnb console by the command layer).
type Logf func(format string, args ...any)

// Options carries everything the CLI can override.
type Options struct {
	RepoRoot         string
	OverpassEndpoint string
	// OverpassInterval is the minimum spacing between two Overpass requests.
	// Zero means OverpassMinInterval; negative disables the pacing (a local
	// instance does not need it).
	OverpassInterval time.Duration
	Emit             EmitOptions
	DebugImages      bool
	// Warnf receives data-quality warnings; falls back to the progress log.
	Warnf Logf
}

// overpassInterval resolves the request spacing: unset means the measured
// default, negative means the caller knows better (a local instance).
func (o Options) overpassInterval() time.Duration {
	if o.OverpassInterval == 0 {
		return OverpassMinInterval
	}
	if o.OverpassInterval < 0 {
		return 0
	}
	return o.OverpassInterval
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
		OverpassInterval: OverpassMinInterval,
		Emit:             DefaultEmitOptions(),
	}
}

// demPadM widens the download/query box so bilinear taps and buildings that
// straddle the tile edge are still covered.
const demPadM = 120

// Fetch downloads the raw DEM and OSM data for every part of the area (cached).
//
// One Overpass request per 1 km part rather than one for the whole box: a
// 5x5 request would be tens of megabytes from a mirror that asks callers not to
// do that, and per-part requests are what make `gnb geo grow` incremental —
// growing 3x3 to 5x5 fetches the sixteen new parts and reuses the nine it
// already has. The DEM needs no such care: SRTM is cached as whole 1-degree
// tiles, so a bigger area almost always costs zero extra downloads.
func Fetch(m Mosaic, opt Options, logf Logf) (*Meta, error) {
	if err := m.Normalize(); err != nil {
		return nil, err
	}
	paths := NewPaths(opt.RepoRoot, m.Name)
	area := m.BBox(demPadM)

	_, demSources, err := NewSampler(area, paths.DemDir(), logf)
	if err != nil {
		return nil, fmt.Errorf("elevation: %w", err)
	}

	parts := m.Parts()
	sources := demSources
	for i, part := range parts {
		tile := m.TileFor(part)
		dst := paths.PartOsmPath(part.ID)
		adoptLegacyPartCache(paths, m, part)
		if len(parts) > 1 {
			logf("part %s (%d/%d) at %.5f,%.5f", part.ID, i+1, len(parts), part.Lat, part.Lon)
		}
		fetched, err := FetchOverpass(opt.OverpassEndpoint, tile.BBox(demPadM), dst,
			opt.overpassInterval(), logf)
		if err != nil {
			// Say what survives, because it is almost everything: each part is
			// cached the moment it lands, so re-running the same command picks
			// up where this stopped rather than starting over.
			return nil, fmt.Errorf("overpass %s (%d of %d parts already cached — "+
				"re-run the same command to continue): %w", part.ID, i, len(parts), err)
		}
		if !fetched {
			logf("overpass: cached (%s)", relTo(opt.RepoRoot, dst))
		}
		sum, size, err := sha256File(dst)
		if err != nil {
			return nil, err
		}
		sources = append(sources, Source{
			Kind: "osm", URL: opt.OverpassEndpoint, Path: dst,
			Bytes: size, SHA256: sum, License: osmLicense,
		})
	}

	meta := &Meta{
		Tile: m.Name, Lat: m.Lat, Lon: m.Lon, SizeM: m.SizeM,
		BBox:      [4]float64{area.South, area.West, area.North, area.East},
		FetchedAt: time.Now().UTC(),
		Sources:   sources,
	}
	for i := range meta.Sources {
		meta.Sources[i].Path = relTo(opt.RepoRoot, meta.Sources[i].Path)
	}
	if err := writeJSON(paths.MetaPath(), meta); err != nil {
		return nil, err
	}
	return meta, nil
}

// adoptLegacyPartCache moves a pre-area cache into the centre part's slot.
//
// Before areas existed, a tile's Overpass response lived at
// external/geocache/<tile>/osm/. The centre part of an area centred on that
// same point wants exactly that response, and re-downloading it just to move it
// is a pointless request to a mirror that rate-limits. The query fingerprint is
// moved with it, so a genuinely different box is still detected and re-fetched.
func adoptLegacyPartCache(paths Paths, m Mosaic, part Part) {
	if part != m.CentrePart() {
		return
	}
	dst := paths.PartOsmPath(part.ID)
	if fileExists(dst) {
		return
	}
	legacy := filepath.Join(paths.CacheDir(), "osm", "overpass.json")
	if !fileExists(legacy) {
		return
	}
	if err := os.MkdirAll(filepath.Dir(dst), 0o755); err != nil {
		return
	}
	if err := os.Rename(legacy, dst); err != nil {
		return
	}
	_ = os.Rename(queryFingerprintPath(legacy), queryFingerprintPath(dst))
	_ = os.Remove(filepath.Dir(legacy))
}

// BuildResult is what stage B+C produce for the whole area.
type BuildResult struct {
	Parts   []*IR
	Grids   []*HeightGrid
	Terrain TerrainReport
	Stats   Stats
	POIs    int
}

// Build normalises every part's cached OSM data into its IR, derives one
// heightfield for the whole area and slices it into the parts.
//
// The single field is the load-bearing decision (see TerrainArea): every step
// of the DSM-to-DTM chain is a neighbourhood operation and the water plane and
// datum are single numbers, so doing this per part leaves a step at every seam.
func Build(m Mosaic, opt Options, logf Logf) (*BuildResult, error) {
	if err := m.Normalize(); err != nil {
		return nil, err
	}
	paths := NewPaths(opt.RepoRoot, m.Name)
	profile, ok := Profiles[m.Profile]
	if !ok {
		return nil, fmt.Errorf("unknown height profile %q (have: %s)", m.Profile, profileNames())
	}
	landmarks, err := LoadLandmarks(opt.RepoRoot)
	if err != nil {
		return nil, err
	}

	parts := m.Parts()
	single := len(parts) == 1
	irs := make([]*IR, len(parts))
	var total Stats
	for i, part := range parts {
		osm := paths.PartOsmPath(part.ID)
		if !fileExists(osm) {
			return nil, fmt.Errorf("no cached OSM data for part %s of %q — run `gnb geo fetch` first",
				part.ID, m.Name)
		}
		elements, err := ParseOverpass(osm)
		if err != nil {
			return nil, fmt.Errorf("part %s: %w", part.ID, err)
		}
		ir := Normalize(m.TileFor(part), elements, profile, landmarks)
		irs[i] = ir
		st := ir.Summarize()
		total.Buildings += st.Buildings
		total.HeightFromTag += st.HeightFromTag
		total.HeightFromLvls += st.HeightFromLvls
		total.HeightInferred += st.HeightInferred
		total.Roads += st.Roads
		total.WaterAreas += st.WaterAreas
		total.CoastlineWays += st.CoastlineWays
		total.POIs += st.POIs
		total.POINodes += st.POINodes
		if st.TallestHeight > total.TallestHeight {
			total.TallestHeight, total.TallestName = st.TallestHeight, st.TallestName
		}
	}
	logf("normalized: %d buildings (%d tagged / %d levels / %d inferred), %d roads, %d water areas, %d coastline ways%s",
		total.Buildings, total.HeightFromTag, total.HeightFromLvls, total.HeightInferred,
		total.Roads, total.WaterAreas, total.CoastlineWays, partSuffix(len(parts)))
	if total.TallestName != "" {
		logf("tallest: %s at %.0fm", total.TallestName, total.TallestHeight)
	}

	// ---- one heightfield for the whole area, then slice ---------------------
	sampler, _, err := NewSampler(m.BBox(demPadM), paths.DemDir(), logf)
	if err != nil {
		return nil, err
	}
	debugDir := ""
	if opt.DebugImages {
		debugDir = filepath.Join(paths.CacheDir(), "debug")
	}
	areaIR := unionIR(m, parts, irs)
	areaProj := NewProj(m.Lat, m.Lon)
	field, terrainReport, err := BuildTerrainField(TerrainArea{
		SizeM:     m.SizeM,
		Cells:     m.Cells * m.Grid(),
		Unproject: areaProj.Inverse,
	}, areaIR, sampler, debugDir)
	if err != nil {
		return nil, err
	}
	logf("terrain: %s", terrainReport)
	if terrainReport.CoastalGradientWarning() {
		opt.warn("near-shore terrain rises %.0f%% — the source DSM's rooftop returns were "+
			"not fully removable here (dense downtown at 30 m posting). Absolute elevations "+
			"in the built-up area read high; see design §5.1.",
			terrainReport.CoastalGradient*100)
	}

	meta := &TerrainMeta{
		BaseElevation: terrainReport.BaseElevation,
		PaletteSpan:   math.Max(0, terrainReport.MaxLand-terrainReport.BaseElevation),
		HasWater:      terrainReport.Water.HasWater,
		IsSea:         terrainReport.Water.IsSea,
		WaterLevel:    terrainReport.Water.Level,
	}
	grids := make([]*HeightGrid, len(parts))
	hmapBytes := 0
	for i, part := range parts {
		grid := field.SubGrid(part.Col*m.Cells, part.Row*m.Cells, m.Cells,
			-PartSizeM/2, -PartSizeM/2)
		grids[i] = grid
		blob, err := EncodeHmap(grid)
		if err != nil {
			return nil, err
		}
		dst := paths.PartHmapPath(part.ID, single)
		if err := os.MkdirAll(filepath.Dir(dst), 0o755); err != nil {
			return nil, err
		}
		if err := os.WriteFile(dst, blob, 0o644); err != nil {
			return nil, err
		}
		hmapBytes += len(blob)
		// The terrain stage derives the datum, so the IR is written after it.
		irs[i].Terrain = meta
		if err := writeJSON(paths.PartIRPath(part.ID), irs[i]); err != nil {
			return nil, err
		}
	}
	logf("wrote %d heightfield(s), %d KB total", len(parts), hmapBytes/1024)

	pois := mergePOIs(parts, irs)
	poiFile := POIFile{
		Format: POIFormat, Tile: m.Name,
		Center: [2]float64{m.Lat, m.Lon}, SizeM: m.SizeM,
		Attribution: irs[0].Attribution, POIs: pois,
	}
	if err := writeJSON(paths.POIPath(), poiFile); err != nil {
		return nil, err
	}
	logf("wrote %s (%d places) — %s", relTo(opt.RepoRoot, paths.POIPath()), len(pois),
		FormatPOICounts(pois))
	if len(pois) == 0 {
		opt.warn("no named places in this area — the POI layer will be empty. " +
			"Either OSM has no names here, or the cached Overpass response predates " +
			"the POI selectors (delete external/geocache/<tile>/parts and re-fetch).")
	}

	if err := writeMosaicManifest(paths, m, single, irs[0].Attribution); err != nil {
		return nil, err
	}
	prunePartAssets(paths, m, logf)
	if err := writeAttribution(paths, m, irs[0]); err != nil {
		return nil, err
	}
	if debugDir != "" {
		logf("debug images: %s", relTo(opt.RepoRoot, debugDir))
	}
	return &BuildResult{Parts: irs, Grids: grids, Terrain: terrainReport,
		Stats: total, POIs: len(pois)}, nil
}

// unionIR gathers every part's footprints, water and coastline into one
// area-local IR for the terrain stage. Parts overlap by the query pad, so the
// same way arrives several times; it is deduplicated by OSM id rather than
// by ownership, because a footprint straddling the rim still has to be masked
// out of the DSM even though no part will emit it.
func unionIR(m Mosaic, parts []Part, irs []*IR) *IR {
	out := &IR{Tile: m.Name, Center: [2]float64{m.Lat, m.Lon}, SizeM: m.SizeM}
	if len(irs) == 1 {
		return irs[0]
	}
	seenB := map[int64]bool{}
	seenW := map[int64]bool{}
	seenC := map[int64]bool{}
	for i, part := range parts {
		dx, dy := part.OffsetX, part.OffsetY
		for _, b := range irs[i].Buildings {
			if seenB[b.ID] {
				continue
			}
			seenB[b.ID] = true
			b.Outer = shiftRing(b.Outer, dx, dy)
			b.Inners = shiftRings(b.Inners, dx, dy)
			out.Buildings = append(out.Buildings, b)
		}
		for _, w := range irs[i].Waters {
			if seenW[w.ID] {
				continue
			}
			seenW[w.ID] = true
			w.Outer = shiftRing(w.Outer, dx, dy)
			w.Inners = shiftRings(w.Inners, dx, dy)
			out.Waters = append(out.Waters, w)
		}
		for _, c := range irs[i].Coastline {
			if seenC[c.ID] {
				continue
			}
			seenC[c.ID] = true
			c.Pts = shiftRing(c.Pts, dx, dy)
			out.Coastline = append(out.Coastline, c)
		}
	}
	return out
}

func shiftRing(r Ring, dx, dy float64) Ring {
	out := make(Ring, len(r))
	for i, p := range r {
		out[i] = [2]float64{p[0] + dx, p[1] + dy}
	}
	return out
}

func shiftRings(rs []Ring, dx, dy float64) []Ring {
	if len(rs) == 0 {
		return nil
	}
	out := make([]Ring, len(rs))
	for i, r := range rs {
		out[i] = shiftRing(r, dx, dy)
	}
	return out
}

// mergePOIs lifts every part's places into area coordinates. Each part already
// clipped its own set to its own square, so the union has no gaps and no
// duplicates; the id set is checked anyway because a place sitting exactly on a
// seam passes both clips.
func mergePOIs(parts []Part, irs []*IR) []POI {
	var out []POI
	seen := map[string]bool{}
	for i, part := range parts {
		for _, p := range irs[i].POIs {
			key := fmt.Sprintf("%s/%d", p.Source, p.ID)
			if seen[key] {
				continue
			}
			seen[key] = true
			p.Pos = [2]float64{p.Pos[0] + part.OffsetX, p.Pos[1] + part.OffsetY}
			out = append(out, p)
		}
	}
	sort.SliceStable(out, func(i, j int) bool {
		if out[i].Rank != out[j].Rank {
			return out[i].Rank > out[j].Rank
		}
		return out[i].ID < out[j].ID
	})
	return out
}

// prunePartAssets removes the heightfields of parts the area no longer has.
//
// Shrinking is meant to be free — nothing in the cache is touched, so growing
// back costs no downloads — but the *outputs* of the parts that went away must
// not survive: assets/geo/<name> is packed wholesale by `gnb geo pak`, and a
// stale part would ship as a heightfield no scene references. Only directories
// under parts/ whose name is a part id are considered, and only those outside
// the current grid.
func prunePartAssets(paths Paths, m Mosaic, logf Logf) {
	if m.Grid() > 1 {
		// The flat heightfield of a former 1x1 area is an orphan once the parts
		// own theirs; it is the same trap in the other direction.
		if err := os.Remove(paths.HmapPath()); err == nil {
			logf("dropped the single-part heightfield the area outgrew")
		}
	}
	root := filepath.Join(paths.AssetDir(), "parts")
	entries, err := os.ReadDir(root)
	if err != nil {
		return
	}
	keep := map[string]bool{}
	// A 1x1 area writes its heightfield at the top of the directory, so it keeps
	// nothing under parts/ — not even its own centre, whose file from a larger
	// pass would otherwise survive as an orphan the scene never reads.
	if m.Grid() > 1 {
		for _, part := range m.Parts() {
			keep[part.ID] = true
		}
	}
	removed := 0
	for _, entry := range entries {
		if !entry.IsDir() || keep[entry.Name()] || !partIDPattern.MatchString(entry.Name()) {
			continue
		}
		if err := os.RemoveAll(filepath.Join(root, entry.Name())); err == nil {
			removed++
		}
	}
	if removed > 0 {
		logf("dropped %d part(s) the area no longer covers (their cache is kept)", removed)
	}
	// An area that shrank back to one part writes its heightfield at the top
	// again, so the empty parts/ directory should not linger either.
	if rest, err := os.ReadDir(root); err == nil && len(rest) == 0 {
		_ = os.Remove(root)
	}
}

// partIDPattern is the shape PartAt produces: p<signed>_<signed>, where a
// negative offset is spelled with a leading m.
var partIDPattern = regexp.MustCompile(`^p(m?\d+)_(m?\d+)$`)

func writeMosaicManifest(paths Paths, m Mosaic, single bool, attribution []string) error {
	f := MosaicFile{
		Format: MosaicFormat, Name: m.Name,
		Center: [2]float64{m.Lat, m.Lon}, SizeM: m.SizeM, PartSizeM: PartSizeM,
		Grid: m.Grid(), Cells: m.Cells, Profile: m.Profile, Seed: m.Seed,
		FullRings: m.FullRings, MediumRings: m.MediumRings,
		Attribution: attribution,
	}
	for _, part := range m.Parts() {
		f.Parts = append(f.Parts, MosaicPart{
			ID: part.ID, Col: part.Col, Row: part.Row, Ring: part.Ring,
			LOD:    m.LODForRing(part.Ring).String(),
			Offset: [2]float64{part.OffsetX, part.OffsetY},
			Center: [2]float64{part.Lat, part.Lon},
			Hmap:   paths.PartHmapAssetRef(part.ID, single),
		})
	}
	return writeJSON(paths.MosaicPath(), f)
}

// Scad renders the scene from the cached part IRs and heightfields.
func Scad(m Mosaic, opt Options, logf Logf) (string, EmitReport, error) {
	if err := m.Normalize(); err != nil {
		return "", EmitReport{}, err
	}
	paths := NewPaths(opt.RepoRoot, m.Name)
	parts := m.Parts()
	single := len(parts) == 1

	scenes := make([]PartScene, 0, len(parts))
	for _, part := range parts {
		var ir IR
		if err := readJSON(paths.PartIRPath(part.ID), &ir); err != nil {
			return "", EmitReport{}, fmt.Errorf("no IR for part %s of %q — run `gnb geo build` first (%w)",
				part.ID, m.Name, err)
		}
		blob, err := os.ReadFile(paths.PartHmapPath(part.ID, single))
		if err != nil {
			return "", EmitReport{}, fmt.Errorf("no .hmap for part %s of %q — run `gnb geo build` first (%w)",
				part.ID, m.Name, err)
		}
		grid, err := DecodeHmap(blob)
		if err != nil {
			return "", EmitReport{}, err
		}
		scenes = append(scenes, PartScene{
			Part: part, Tile: m.TileFor(part), IR: &ir, Grid: grid,
			HmapRef: paths.PartHmapAssetRef(part.ID, single),
			LOD:     m.LODForRing(part.Ring), Edges: m.EdgesFor(part),
		})
	}

	source, report := EmitMosaic(m, scenes, opt.Emit)
	if err := os.MkdirAll(filepath.Dir(paths.ScadPath()), 0o755); err != nil {
		return "", report, err
	}
	if err := os.WriteFile(paths.ScadPath(), []byte(source), 0o644); err != nil {
		return "", report, err
	}
	logf("emitted: %s", report)
	return paths.ScadPath(), report, nil
}

func partSuffix(n int) string {
	if n == 1 {
		return ""
	}
	return fmt.Sprintf(" across %d parts", n)
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

func writeAttribution(paths Paths, m Mosaic, ir *IR) error {
	var s strings.Builder
	fmt.Fprintf(&s, "# %s — data attribution\n\n", m.Name)
	fmt.Fprintf(&s, "Generated by `gnb geo` from public data for the area centred on\n")
	fmt.Fprintf(&s, "%.5f, %.5f (%.0f x %.0f m).\n\n", m.Lat, m.Lon, m.SizeM, m.SizeM)
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
