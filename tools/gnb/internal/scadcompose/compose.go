package scadcompose

import (
	"fmt"
	"math"
	"sort"
	"strconv"
	"strings"
)

var terrainPalettes = map[string]bool{"temperate": true, "arid": true, "alpine": true}

var terrainBiomes = map[string]bool{
	"grass": true, "grass_dark": true, "dry_grass": true, "sand": true,
	"rock": true, "rock_high": true, "snow": true, "bed": true, "road": true, "pad": true,
}

// Result carries the generated .scad source plus non-fatal validation warnings.
type Result struct {
	Source   string
	Warnings []string
}

// Compose validates the spec against the catalog and expands it into a
// deterministic top-level .scad (same spec -> byte-identical output). The
// output is written under assets/scad/{source,proc}/generated/, so kit `use`
// paths are ../../lib/.
func Compose(spec *Spec, catalog *Catalog, specPath string, specHash string) (*Result, error) {
	result := &Result{}

	// ---- Kits ----
	if len(spec.Kits) == 0 {
		return nil, fmt.Errorf("spec declares no kits")
	}
	declared := map[string]bool{}
	var kitFiles []string
	scaleClasses := map[string]bool{}
	for _, kitName := range spec.Kits {
		short := ShortKitName(kitName)
		file, ok := catalog.KitFile(short)
		if !ok {
			return nil, fmt.Errorf("kit %q not in catalog (run `gnb scad catalog`?)", kitName)
		}
		if declared[short] {
			return nil, fmt.Errorf("kit %q declared twice", kitName)
		}
		declared[short] = true
		kitFiles = append(kitFiles, file)
		scaleClasses[catalog.ScaleClass(short)] = true
	}
	if scaleClasses["human"] && scaleClasses["city"] {
		result.Warnings = append(result.Warnings,
			"kits mix scaleClass human and city: interior-scale parts will look tiny in a city layout")
	}

	// ---- Terrain ----
	if spec.Terrain != nil && spec.Ground != nil {
		return nil, fmt.Errorf("\"terrain\" and \"ground\" are mutually exclusive — the terrain replaces the ground slab")
	}
	var terrCells [2]int
	if spec.Terrain != nil {
		if err := validateTerrain(spec.Terrain, result); err != nil {
			return nil, err
		}
		terrCells = terrainCells(spec.Terrain)
	}
	snapDefault := spec.Terrain != nil
	resolveSnap := func(where, value string) (bool, error) {
		switch value {
		case "":
			return snapDefault, nil
		case "terrain":
			if spec.Terrain == nil {
				return false, fmt.Errorf("%s: snap \"terrain\" requires a \"terrain\" section", where)
			}
			return true, nil
		case "none":
			return false, nil
		default:
			return false, fmt.Errorf("%s: snap must be \"terrain\" or \"none\" (or omitted)", where)
		}
	}

	// ---- Calls ----
	checkCall := func(where string, call Call) error {
		if call.Module == "" {
			return fmt.Errorf("%s: empty module name", where)
		}
		kit, okDefault, found := catalog.FindModule(call.Module)
		if !found {
			return fmt.Errorf("%s: module %q not found in catalog", where, call.Module)
		}
		if !declared[kit] {
			return fmt.Errorf("%s: module %q belongs to kit %q — add it to \"kits\"", where, call.Module, kit)
		}
		if !okDefault && call.Args == "" {
			result.Warnings = append(result.Warnings, fmt.Sprintf(
				"%s: %s produced no geometry with default args in the catalog; it likely needs args", where, call.Module))
		}
		return nil
	}
	checkChildren := func(where string, children []Call) error {
		if len(children) == 0 {
			return fmt.Errorf("%s: empty children", where)
		}
		for _, child := range children {
			if err := checkCall(where, child); err != nil {
				return err
			}
		}
		return nil
	}

	// ---- Block types / grids ----
	var typeNames []string
	for typeName := range spec.BlockTypes {
		typeNames = append(typeNames, typeName)
	}
	sort.Strings(typeNames)
	typeIndex := map[string]int{}
	for i, typeName := range typeNames {
		typeIndex[typeName] = i
		if err := checkChildren(fmt.Sprintf("blockTypes.%s", typeName), spec.BlockTypes[typeName]); err != nil {
			return nil, err
		}
	}
	for gi, grid := range spec.BlockGrids {
		where := fmt.Sprintf("blockGrids[%d]", gi)
		if len(grid.Layout) == 0 || len(grid.Layout[0]) == 0 {
			return nil, fmt.Errorf("%s: empty layout", where)
		}
		cols := len(grid.Layout[0])
		for r, row := range grid.Layout {
			if len(row) != cols {
				return nil, fmt.Errorf("%s: layout row %d has %d cells, expected %d", where, r, len(row), cols)
			}
			for _, typeName := range row {
				if _, ok := typeIndex[typeName]; !ok {
					return nil, fmt.Errorf("%s: unknown blockType %q", where, typeName)
				}
			}
		}
		if grid.Cell[0] <= 0 || grid.Cell[1] <= 0 {
			return nil, fmt.Errorf("%s: cell must be positive", where)
		}
	}

	// ---- Rule sanity + calls (snap resolved per rule) ----
	placementSnap := make([]bool, len(spec.Placements))
	gridSnap := make([]bool, len(spec.Grids))
	rowSnap := make([]bool, len(spec.Rows))
	ringSnap := make([]bool, len(spec.Rings))
	scatterSnap := make([]bool, len(spec.Scatters))
	alongSnap := make([]bool, len(spec.Alongs))

	for i, p := range spec.Placements {
		where := fmt.Sprintf("placements[%d]", i)
		if err := checkCall(where, p.Call); err != nil {
			return nil, err
		}
		snapped, err := resolveSnap(where, p.Snap)
		if err != nil {
			return nil, err
		}
		if p.SnapAt != nil && !snapped {
			return nil, fmt.Errorf("%s: snapAt only makes sense with snap = \"terrain\"", where)
		}
		placementSnap[i] = snapped
	}
	for i, g := range spec.Grids {
		where := fmt.Sprintf("grids[%d]", i)
		if g.Cols <= 0 || g.Rows <= 0 || g.Cell[0] <= 0 || g.Cell[1] <= 0 {
			return nil, fmt.Errorf("%s: cols/rows/cell must be positive", where)
		}
		if err := checkChildren(where, g.Children); err != nil {
			return nil, err
		}
		snapped, err := resolveSnap(where, g.Snap)
		if err != nil {
			return nil, err
		}
		gridSnap[i] = snapped
	}
	for i, r := range spec.Rows {
		where := fmt.Sprintf("rows[%d]", i)
		if r.N <= 0 {
			return nil, fmt.Errorf("%s: n must be positive", where)
		}
		if err := checkChildren(where, r.Children); err != nil {
			return nil, err
		}
		snapped, err := resolveSnap(where, r.Snap)
		if err != nil {
			return nil, err
		}
		rowSnap[i] = snapped
	}
	for i, r := range spec.Rings {
		where := fmt.Sprintf("rings[%d]", i)
		if r.N <= 0 || r.R <= 0 {
			return nil, fmt.Errorf("%s: n/r must be positive", where)
		}
		if err := checkChildren(where, r.Children); err != nil {
			return nil, err
		}
		snapped, err := resolveSnap(where, r.Snap)
		if err != nil {
			return nil, err
		}
		ringSnap[i] = snapped
	}
	for i, s := range spec.Scatters {
		where := fmt.Sprintf("scatters[%d]", i)
		if s.N <= 0 || s.Region[0] >= s.Region[2] || s.Region[1] >= s.Region[3] {
			return nil, fmt.Errorf("%s: need n > 0 and region = [x0, y0, x1, y1] with x0 < x1, y0 < y1", where)
		}
		if err := checkChildren(where, s.Children); err != nil {
			return nil, err
		}
		snapped, err := resolveSnap(where, s.Snap)
		if err != nil {
			return nil, err
		}
		if s.Where != nil {
			if !snapped {
				return nil, fmt.Errorf("%s: \"where\" filters need terrain snapping (drop snap = \"none\" or the filter)", where)
			}
			if err := validateWhere(where, s.Where); err != nil {
				return nil, err
			}
		}
		scatterSnap[i] = snapped
	}
	for i, a := range spec.Alongs {
		where := fmt.Sprintf("alongs[%d]", i)
		if len(a.Pts) < 2 || a.Step <= 0 {
			return nil, fmt.Errorf("%s: need >= 2 pts and step > 0", where)
		}
		if err := checkChildren(where, a.Children); err != nil {
			return nil, err
		}
		snapped, err := resolveSnap(where, a.Snap)
		if err != nil {
			return nil, err
		}
		alongSnap[i] = snapped
	}
	if spec.Terrain != nil && len(spec.BlockGrids) > 0 {
		result.Warnings = append(result.Warnings,
			"blockGrids do not snap to terrain in v1: block tiles stay at z = 0")
	}

	// ---- Emit ----
	usesCombinators := len(spec.BlockGrids)+len(spec.Grids)+len(spec.Rows)+len(spec.Rings)+
		len(spec.Scatters)+len(spec.Alongs) > 0
	for _, children := range spec.BlockTypes {
		if len(children) > 1 {
			usesCombinators = true
		}
	}

	var b strings.Builder
	fmt.Fprintf(&b, "// %s.scad —— generated by `gnb scad compose` from %s\n", spec.Name, specPath)
	fmt.Fprintf(&b, "// spec sha256 %s — edit the spec and re-run compose; hand edits here will be overwritten.\n\n",
		specHash)
	fmt.Fprintf(&b, "$fn = %d;\n\n", spec.Fn)
	if usesCombinators || spec.Terrain != nil {
		b.WriteString("use <../../lib/kit_layout.scad>\n")
	}
	if spec.Terrain != nil {
		b.WriteString("use <../../lib/kit_terrain.scad>\n")
	}
	for _, file := range kitFiles {
		fmt.Fprintf(&b, "use <../../lib/%s>\n", file)
	}
	b.WriteString("\n")

	if spec.Ground != nil {
		g := spec.Ground
		top := -0.02
		if g.Z != nil {
			top = *g.Z
		}
		thickness := g.Thickness
		if thickness == 0 {
			thickness = 0.3
		}
		fmt.Fprintf(&b, "// 地面\ncolor([%s, %s, %s]) translate([0, 0, %s]) cube([%s, %s, %s], center = true);\n\n",
			num(g.Color[0]), num(g.Color[1]), num(g.Color[2]), num(top-thickness/2),
			num(g.Size[0]), num(g.Size[1]), num(thickness))
	}

	if spec.Terrain != nil {
		b.WriteString(terrainStmt(spec.Terrain, terrCells, spec.Seed))
	}

	// Block dispatch module + layout matrices.
	blockModule := sanitize(spec.Name) + "_block"
	if len(spec.BlockGrids) > 0 {
		fmt.Fprintf(&b, "// 街区类型分发（索引见 layout 常量注释）\nmodule %s(t, seed)\n{\n", blockModule)
		for _, typeName := range typeNames {
			fmt.Fprintf(&b, "    if (t == %d) { %s }\n", typeIndex[typeName],
				childrenStmt(spec.BlockTypes[typeName], "seed"))
		}
		b.WriteString("}\n\n")
	}
	for gi, grid := range spec.BlockGrids {
		layoutName := fmt.Sprintf("%s_L%d", strings.ToUpper(sanitize(spec.Name)), gi+1)
		fmt.Fprintf(&b, "// %s\n", layoutLegend(grid.Layout, typeIndex))
		fmt.Fprintf(&b, "%s = [\n", layoutName)
		for _, row := range grid.Layout {
			indices := make([]string, len(row))
			for i, typeName := range row {
				indices[i] = strconv.Itoa(typeIndex[typeName])
			}
			fmt.Fprintf(&b, "    [%s],\n", strings.Join(indices, ", "))
		}
		b.WriteString("];\n")
		fmt.Fprintf(&b, "%slay_grid(%d, %d, %s, %s, seed = %d)\n        %s(%s[$row][$col], $seed);\n\n",
			translatePrefix(grid.At), len(grid.Layout[0]), len(grid.Layout),
			num(grid.Cell[0]), num(grid.Cell[1]), grid.Seed, blockModule, layoutName)
	}

	if len(spec.Placements) > 0 {
		b.WriteString("// 显式放置\n")
		for i, p := range spec.Placements {
			b.WriteString(placementStmt(p, placementSnap[i]))
		}
		b.WriteString("\n")
	}
	for i, g := range spec.Grids {
		center := ""
		if g.Center != nil && !*g.Center {
			center = ", center = false"
		}
		snap := ""
		if gridSnap[i] {
			snap = snapPrefix(g.At)
		}
		jitter := ""
		if g.Jitter != nil {
			jitter = fmt.Sprintf("lay_jitter($seed, %s, %s, %s) ", num(g.Jitter.Dx), num(g.Jitter.Dy), num(g.Jitter.Rot))
		}
		fmt.Fprintf(&b, "%slay_grid(%d, %d, %s, %s, seed = %d%s)\n    %s%s%s\n",
			translatePrefix(g.At), g.Cols, g.Rows, num(g.Cell[0]), num(g.Cell[1]), g.Seed, center,
			snap, jitter, childrenStmt(g.Children, "$seed"))
	}
	for i, r := range spec.Rows {
		rot := ""
		if r.Rot != 0 {
			rot = fmt.Sprintf("rotate([0, 0, %s]) ", num(r.Rot))
		}
		snap := ""
		if rowSnap[i] {
			snap = snapPrefix(r.At)
		}
		fmt.Fprintf(&b, "%slay_row(%d, %s, %s, seed = %d)\n    %s%s%s\n",
			translatePrefix(r.At), r.N, num(r.Dx), num(r.Dy), r.Seed, snap, rot, childrenStmt(r.Children, "$seed"))
	}
	for i, r := range spec.Rings {
		extras := ""
		if r.Face != nil {
			extras += fmt.Sprintf(", face = %d", *r.Face)
		}
		if r.A0 != 0 {
			extras += fmt.Sprintf(", a0 = %s", num(r.A0))
		}
		snap := ""
		if ringSnap[i] {
			snap = snapPrefix(r.At)
		}
		fmt.Fprintf(&b, "%slay_ring(%d, %s, seed = %d%s)\n    %s%s\n",
			translatePrefix(r.At), r.N, num(r.R), r.Seed, extras, snap, childrenStmt(r.Children, "$seed"))
	}
	for i, s := range spec.Scatters {
		rot := ""
		if s.Rot != nil && !*s.Rot {
			rot = ", rot = false"
		}
		if scatterSnap[i] {
			// ter_scatter rejection-samples with the terrain filter and snaps
			// each accepted point to the surface.
			fmt.Fprintf(&b, "ter_scatter(TERR, %d, %d, [%s, %s, %s, %s], %s%s)\n    %s\n",
				s.Seed, s.N, num(s.Region[0]), num(s.Region[1]), num(s.Region[2]), num(s.Region[3]),
				whereFilt(s.Where), rot, childrenStmt(s.Children, "$seed"))
			continue
		}
		// Spec region is [x0, y0, x1, y1]; lay_scatter takes (n, x0, x1, y0, y1).
		fmt.Fprintf(&b, "lay_scatter(%d, %s, %s, %s, %s, seed = %d%s)\n    %s\n",
			s.N, num(s.Region[0]), num(s.Region[2]), num(s.Region[1]), num(s.Region[3]), s.Seed, rot,
			childrenStmt(s.Children, "$seed"))
	}
	for i, a := range spec.Alongs {
		points := make([]string, len(a.Pts))
		for pi, pt := range a.Pts {
			points[pi] = fmt.Sprintf("[%s, %s]", num(pt[0]), num(pt[1]))
		}
		offset := ""
		if a.Offset != 0 {
			offset = fmt.Sprintf(", offset = %s", num(a.Offset))
		}
		if alongSnap[i] {
			fmt.Fprintf(&b, "ter_along(TERR, [%s], step = %s, seed = %d%s)\n    %s\n",
				strings.Join(points, ", "), num(a.Step), a.Seed, offset, childrenStmt(a.Children, "$seed"))
			continue
		}
		fmt.Fprintf(&b, "lay_along([%s], step = %s, seed = %d%s)\n    %s\n",
			strings.Join(points, ", "), num(a.Step), a.Seed, offset, childrenStmt(a.Children, "$seed"))
	}

	result.Source = b.String()
	return result, nil
}

// ---- Terrain helpers ----

func terrainCells(t *Terrain) [2]int {
	if t.Cells != nil {
		return *t.Cells
	}
	clampCells := func(v float64) int {
		c := int(math.Round(v / 2))
		if c < 4 {
			c = 4
		}
		if c > 256 {
			c = 256
		}
		return c
	}
	return [2]int{clampCells(t.Size[0]), clampCells(t.Size[1])}
}

func validateTerrain(t *Terrain, result *Result) error {
	if t.Size[0] <= 0 || t.Size[1] <= 0 {
		return fmt.Errorf("terrain.size must be [sizeX, sizeY] with positive values")
	}
	if t.Cells != nil {
		for axis := 0; axis < 2; axis++ {
			if t.Cells[axis] < 4 || t.Cells[axis] > 256 {
				return fmt.Errorf("terrain.cells[%d] = %d out of range 4..256", axis, t.Cells[axis])
			}
		}
	}
	if t.Base != nil {
		if t.Base.Relief < 0 {
			return fmt.Errorf("terrain.base.relief must be >= 0")
		}
		if t.Base.Roughness != nil && (*t.Base.Roughness < 0 || *t.Base.Roughness > 1) {
			return fmt.Errorf("terrain.base.roughness must be within 0..1")
		}
	}
	if t.Palette != "" && !terrainPalettes[t.Palette] {
		return fmt.Errorf("terrain.palette %q unknown — use temperate, arid or alpine", t.Palette)
	}

	hx := t.Size[0] * 0.5
	hy := t.Size[1] * 0.5
	inDomain := func(p [2]float64) bool {
		return p[0] >= -hx && p[0] <= hx && p[1] >= -hy && p[1] <= hy
	}
	for i, f := range t.Features {
		where := fmt.Sprintf("terrain.features[%d] (%s)", i, f.Type)
		switch f.Type {
		case "mountain", "plateau":
			if f.Radius <= 0 || f.Height <= 0 {
				return fmt.Errorf("%s: needs radius > 0 and height > 0", where)
			}
			if !inDomain(f.At) {
				return fmt.Errorf("%s: at %v outside the terrain size %v (coordinates are centered on the origin)", where, f.At, t.Size)
			}
			if f.Rugged < 0 || f.Rugged > 1 {
				return fmt.Errorf("%s: rugged must be within 0..1", where)
			}
			if math.Abs(f.At[0])+f.Radius > hx*1.15 || math.Abs(f.At[1])+f.Radius > hy*1.15 {
				result.Warnings = append(result.Warnings, fmt.Sprintf("%s: radius extends well past the map edge", where))
			}
		case "lake":
			if f.Radius <= 0 || f.Depth <= 0 {
				return fmt.Errorf("%s: needs radius > 0 and depth > 0", where)
			}
			if !inDomain(f.At) {
				return fmt.Errorf("%s: at %v outside the terrain size %v", where, f.At, t.Size)
			}
		case "ridge":
			if len(f.Pts) < 2 || f.Width <= 0 || f.Height <= 0 {
				return fmt.Errorf("%s: needs >= 2 pts, width > 0 and height > 0", where)
			}
		case "river":
			if len(f.Pts) < 2 || f.Width <= 0 || f.Depth <= 0 {
				return fmt.Errorf("%s: needs >= 2 pts (upstream first), width > 0 and depth > 0", where)
			}
		case "road":
			if len(f.Pts) < 2 || f.Width <= 0 {
				return fmt.Errorf("%s: needs >= 2 pts and width > 0", where)
			}
		case "pad":
			if f.Size[0] <= 0 || f.Size[1] <= 0 {
				return fmt.Errorf("%s: needs size = [w, d] with positive values", where)
			}
			if !inDomain(f.At) {
				return fmt.Errorf("%s: at %v outside the terrain size %v", where, f.At, t.Size)
			}
		default:
			return fmt.Errorf("%s: unknown feature type — use mountain, ridge, plateau, lake, river, road or pad", where)
		}
		for pi, p := range f.Pts {
			if !inDomain(p) {
				return fmt.Errorf("%s: pts[%d] %v outside the terrain size %v (coordinates are centered on the origin)", where, pi, p, t.Size)
			}
		}
	}

	// Pads flattening across a river corridor cut the channel — usually an
	// authoring mistake (roads/pads should stop at the bank).
	for i, pad := range t.Features {
		if pad.Type != "pad" {
			continue
		}
		for j, river := range t.Features {
			if river.Type != "river" {
				continue
			}
			threshold := 0.5*math.Max(pad.Size[0], pad.Size[1]) + river.Width*0.5
			if distToPolyline(pad.At, river.Pts) < threshold {
				result.Warnings = append(result.Warnings, fmt.Sprintf(
					"terrain.features[%d] (pad) overlaps the river corridor of features[%d]: the flatten will dam the channel", i, j))
			}
		}
	}
	return nil
}

func validateWhere(where string, w *ScatterWhere) error {
	if w.HMin != nil && w.HMax != nil && *w.HMin > *w.HMax {
		return fmt.Errorf("%s: where.hMin > where.hMax can never match", where)
	}
	if w.SlopeMax != nil && (*w.SlopeMax <= 0 || *w.SlopeMax > 90) {
		return fmt.Errorf("%s: where.slopeMax must be within (0, 90] degrees", where)
	}
	for _, biome := range w.Biome {
		if !terrainBiomes[biome] {
			return fmt.Errorf("%s: unknown biome %q — use grass, grass_dark, dry_grass, sand, rock, rock_high, snow, bed, road or pad", where, biome)
		}
	}
	return nil
}

// whereFilt renders the ter_scatter filter list [hMin, hMax, slopeMax,
// avoidWater, [biomes]] with explicit defaults (deterministic output).
func whereFilt(w *ScatterWhere) string {
	hMin, hMax, slopeMax, avoidWater := -99999.0, 99999.0, 90.0, 0.0
	var biomes []string
	if w != nil {
		if w.HMin != nil {
			hMin = *w.HMin
		}
		if w.HMax != nil {
			hMax = *w.HMax
		}
		if w.SlopeMax != nil {
			slopeMax = *w.SlopeMax
		}
		if w.AvoidWater != nil {
			avoidWater = *w.AvoidWater
		}
		for _, biome := range w.Biome {
			biomes = append(biomes, fmt.Sprintf("%q", biome))
		}
	}
	return fmt.Sprintf("[%s, %s, %s, %s, [%s]]",
		num(hMin), num(hMax), num(slopeMax), num(avoidWater), strings.Join(biomes, ", "))
}

func terrainStmt(t *Terrain, cells [2]int, defaultSeed int) string {
	seed := defaultSeed
	if t.Seed != nil {
		seed = *t.Seed
	}
	baseHeight, relief, roughness := 0.0, 1.0, 0.5
	if t.Base != nil {
		baseHeight = t.Base.Height
		relief = t.Base.Relief
		if t.Base.Roughness != nil {
			roughness = *t.Base.Roughness
		}
	}
	waterLevel := "undef"
	if t.WaterLevel != nil {
		waterLevel = num(*t.WaterLevel)
	}
	palette := t.Palette
	if palette == "" {
		palette = "temperate"
	}

	var b strings.Builder
	b.WriteString("// 地形(gk_terrain 引擎扩展;查询:gk_terrain_height / gk_terrain_info)\n")
	fmt.Fprintf(&b, "TERR = [\"gkterr1\", [%s, %s], [%d, %d], %d, [%s, %s, %s], %s, %q, [\n",
		num(t.Size[0]), num(t.Size[1]), cells[0], cells[1], seed,
		num(baseHeight), num(relief), num(roughness), waterLevel, palette)
	for _, f := range t.Features {
		b.WriteString("    ")
		b.WriteString(terrainFeatureLiteral(f))
		b.WriteString(",\n")
	}
	b.WriteString("]];\ngk_terrain(TERR);\n\n")
	return b.String()
}

func terrainFeatureLiteral(f TerrainFeature) string {
	pt := func(p [2]float64) string { return fmt.Sprintf("[%s, %s]", num(p[0]), num(p[1])) }
	pts := func() string {
		parts := make([]string, len(f.Pts))
		for i, p := range f.Pts {
			parts[i] = pt(p)
		}
		return "[" + strings.Join(parts, ", ") + "]"
	}
	switch f.Type {
	case "mountain":
		return fmt.Sprintf("[\"mountain\", %s, %s, %s, %s]", pt(f.At), num(f.Radius), num(f.Height), num(f.Rugged))
	case "plateau":
		return fmt.Sprintf("[\"plateau\", %s, %s, %s]", pt(f.At), num(f.Radius), num(f.Height))
	case "lake":
		return fmt.Sprintf("[\"lake\", %s, %s, %s]", pt(f.At), num(f.Radius), num(f.Depth))
	case "ridge":
		return fmt.Sprintf("[\"ridge\", %s, %s, %s]", pts(), num(f.Width), num(f.Height))
	case "river":
		return fmt.Sprintf("[\"river\", %s, %s, %s]", pts(), num(f.Width), num(f.Depth))
	case "road":
		return fmt.Sprintf("[\"road\", %s, %s]", pts(), num(f.Width))
	case "pad":
		return fmt.Sprintf("[\"pad\", %s, [%s, %s], %s]", pt(f.At), num(f.Size[0]), num(f.Size[1]), num(f.Rot))
	}
	return "[]"
}

func snapPrefix(at [2]float64) string {
	return fmt.Sprintf("ter_snap(TERR, [%s, %s]) ", num(at[0]), num(at[1]))
}

func distToPolyline(p [2]float64, pts [][2]float64) float64 {
	best := math.MaxFloat64
	for i := 0; i+1 < len(pts); i++ {
		ax, ay := pts[i][0], pts[i][1]
		dx, dy := pts[i+1][0]-ax, pts[i+1][1]-ay
		len2 := dx*dx + dy*dy
		t := 0.0
		if len2 > 1e-12 {
			t = ((p[0]-ax)*dx + (p[1]-ay)*dy) / len2
			t = math.Max(0, math.Min(1, t))
		}
		qx, qy := ax+dx*t, ay+dy*t
		d := math.Hypot(p[0]-qx, p[1]-qy)
		if d < best {
			best = d
		}
	}
	return best
}

// childrenStmt renders one child as a direct call, several as a lay_pick.
func childrenStmt(children []Call, seedExpr string) string {
	if len(children) == 1 {
		return children[0].scad() + ";"
	}
	var calls []string
	for _, child := range children {
		calls = append(calls, child.scad()+";")
	}
	return fmt.Sprintf("lay_pick(%s) { %s }", seedExpr, strings.Join(calls, " "))
}

func placementStmt(p Placement, snapped bool) string {
	var b strings.Builder
	if snapped {
		// snapAt lets e.g. a bridge sample the bank height instead of the
		// carved river bed under its own center.
		sx, sy := p.At[0], p.At[1]
		if p.SnapAt != nil {
			sx, sy = p.SnapAt[0], p.SnapAt[1]
		}
		fmt.Fprintf(&b, "ter_place(TERR, %s, %s) ", num(sx), num(sy))
		if p.SnapAt != nil {
			fmt.Fprintf(&b, "translate([%s, %s, 0]) ", num(p.At[0]-sx), num(p.At[1]-sy))
		}
	} else {
		fmt.Fprintf(&b, "translate([%s, %s, 0]) ", num(p.At[0]), num(p.At[1]))
	}
	if p.Rot != 0 {
		fmt.Fprintf(&b, "rotate([0, 0, %s]) ", num(p.Rot))
	}
	if p.Scale != 0 && p.Scale != 1 {
		fmt.Fprintf(&b, "scale([%s, %s, %s]) ", num(p.Scale), num(p.Scale), num(p.Scale))
	}
	b.WriteString(p.Call.scad())
	b.WriteString(";\n")
	return b.String()
}

func translatePrefix(at [2]float64) string {
	if at[0] == 0 && at[1] == 0 {
		return ""
	}
	return fmt.Sprintf("translate([%s, %s, 0])\n    ", num(at[0]), num(at[1]))
}

func layoutLegend(layout [][]string, typeIndex map[string]int) string {
	names := make([]string, len(typeIndex))
	for name, index := range typeIndex {
		names[index] = fmt.Sprintf("%d=%s", index, name)
	}
	return strings.Join(names, " ")
}

func sanitize(name string) string {
	var b strings.Builder
	for _, r := range name {
		if (r >= 'a' && r <= 'z') || (r >= 'A' && r <= 'Z') || (r >= '0' && r <= '9') || r == '_' {
			b.WriteRune(r)
		} else {
			b.WriteRune('_')
		}
	}
	out := b.String()
	if out == "" || (out[0] >= '0' && out[0] <= '9') {
		out = "s_" + out
	}
	return out
}

// num formats a float without trailing noise (deterministic).
func num(v float64) string {
	return strconv.FormatFloat(v, 'g', -1, 64)
}
