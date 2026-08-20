package geo

import (
	"fmt"
	"math"
	"sort"
	"strconv"
	"strings"
)

// EmitOptions tunes stage E.
type EmitOptions struct {
	// BlockSizeM groups buildings into module calls. Keeping each generated
	// Model well under 65535*3 indices matters: above that the engine skips the
	// physics mesh entirely (AGENT_GUIDE/ScadTerrain.md).
	BlockSizeM float64
	// SimplifyToleranceM / MaxFootprintVerts bound the footprint complexity.
	SimplifyToleranceM float64
	MaxFootprintVerts  int
	// MinFootprintM2 drops sheds and mapping noise.
	MinFootprintM2 float64
	// SkirtM sinks every building below the terrain so a sloped site does not
	// show daylight under the walls.
	SkirtM float64
	// RoadClasses selects which highways become terrain road operators. These
	// cut and fill the heightfield, which is what an arterial actually does to
	// a hillside; the count is bounded because every operator costs a polyline
	// query per terrain cell.
	RoadClasses []string
	MaxRoads    int
	// SurfaceClasses selects which highways get street-surface geometry. This
	// is the whole drivable network, not just the arterials: at 5.7 m per
	// terrain cell the biome painting alone cannot show a street grid.
	SurfaceClasses []string
	MaxSurfaces    int
	// RoadsPerModule bounds how many streets share one Model.
	RoadsPerModule int
	// Detail turns on the kit_geo_city / kit_road decoration layer: facades,
	// roofs, sidewalks and street furniture. Off emits the bare OSM extrusion,
	// which is what the pipeline shipped before and stays useful for A/B and as
	// an escape hatch when a tile misbehaves.
	Detail bool
	// ModuleTriBudget caps the estimated triangles in one generated module.
	//
	// This is the single most load-bearing number in the emitter: a Model at or
	// above 65535 triangles is skipped by the physics cook (Scene.Build.cpp), so
	// an over-budget block still renders and you walk straight through it. With
	// bare prisms a 100 m block was never close; with facades and street
	// furniture it is, so blocks and street chunks are cut by estimated
	// triangles rather than by count. The margin covers the estimate's error.
	ModuleTriBudget int
	// TreeDensityM2 is one tree per this many square metres of green space.
	TreeDensityM2 float64
	MaxTrees      int
	// PierWidthM is used when a pier way carries no width.
	PierWidthM float64
}

// DefaultEmitOptions matches the budget in design §6.
func DefaultEmitOptions() EmitOptions {
	return EmitOptions{
		BlockSizeM:         100,
		SimplifyToleranceM: 0.5,
		MaxFootprintVerts:  24,
		MinFootprintM2:     12,
		SkirtM:             1.5,
		RoadClasses:        []string{"motorway", "trunk", "primary", "secondary", "tertiary"},
		MaxRoads:           80,
		SurfaceClasses: []string{
			"motorway", "motorway_link", "trunk", "trunk_link", "primary", "primary_link",
			"secondary", "secondary_link", "tertiary", "tertiary_link",
			"residential", "unclassified", "living_street", "service",
		},
		MaxSurfaces:     600,
		RoadsPerModule:  80,
		TreeDensityM2:   95,
		MaxTrees:        420,
		PierWidthM:      11,
		Detail:          true,
		ModuleTriBudget: 44000,
	}
}

// EmitReport summarises the generated scene.
type EmitReport struct {
	Buildings    int
	DroppedEdge  int // footprint crosses the tile boundary
	DroppedSmall int // below MinFootprintM2
	Blocks       int
	Roads        int
	Surfaces     int
	Junctions    int
	Piers        int
	Trees        int
	Triangles    int
	TallestName  string
	TallestH     float64
}

func (r EmitReport) String() string {
	return fmt.Sprintf("%d buildings in %d blocks (dropped %d off-tile, %d too small), "+
		"%d road operators, %d street runs + %d junctions, %d piers, %d trees, ~%d triangles",
		r.Buildings, r.Blocks, r.DroppedEdge, r.DroppedSmall,
		r.Roads, r.Surfaces, r.Junctions, r.Piers, r.Trees, r.Triangles)
}

// Muted palette: under the path tracer's daylight an albedo of 0.5 already
// reads as white, so facades live in the 0.10..0.30 band (ScadAssetPlaybook).
var buildingPalette = [][3]float64{
	{0.33, 0.32, 0.30}, // pale concrete
	{0.26, 0.26, 0.27}, // grey concrete
	{0.30, 0.28, 0.25}, // warm render
	{0.20, 0.23, 0.26}, // glass curtain wall
	{0.28, 0.25, 0.22}, // tile cladding
	{0.24, 0.26, 0.24}, // green-grey
	{0.17, 0.18, 0.20}, // dark glass
	{0.34, 0.31, 0.28}, // light stone
}

// placed is one building that survived filtering, with its simplified rings
// and the spatial block it belongs to.
type placed struct {
	b      Building
	ring   Ring
	inners []Ring
	block  [2]int
}

// Emit renders the whole scene file.
func Emit(tile Tile, ir *IR, grid *HeightGrid, hmapRef string, opt EmitOptions) (string, EmitReport) {
	var report EmitReport
	half := tile.SizeM / 2

	// ---- buildings: filter, simplify, bucket by block -----------------------
	var kept []placed
	for _, b := range ir.Buildings {
		// The whole footprint has to be on the tile: a building that only has
		// its centroid inside ends up cantilevered over the edge of the world.
		minX, minY, maxX, maxY := BoundsOf(b.Outer)
		if minX < -half || minY < -half || maxX > half || maxY > half {
			report.DroppedEdge++
			continue
		}
		if b.AreaM2 < opt.MinFootprintM2 {
			report.DroppedSmall++
			continue
		}
		ring := EnsureCCW(Simplify(b.Outer, opt.SimplifyToleranceM, opt.MaxFootprintVerts))
		if len(ring) < 3 {
			report.DroppedSmall++
			continue
		}
		c := Centroid(ring)
		var inners []Ring
		for _, hole := range b.Inners {
			h := Simplify(hole, opt.SimplifyToleranceM, opt.MaxFootprintVerts)
			if len(h) >= 3 && RingArea(h) > 4 {
				// A hole must wind opposite to the outer ring for earcut.
				if SignedArea(h) > 0 {
					h = reverseRing(h)
				}
				inners = append(inners, h)
			}
		}
		bx := int(math.Floor((c[0] + half) / opt.BlockSizeM))
		by := int(math.Floor((c[1] + half) / opt.BlockSizeM))
		kept = append(kept, placed{b: b, ring: ring, inners: inners, block: [2]int{bx, by}})
		if b.Height > report.TallestH {
			report.TallestH = b.Height
			report.TallestName = b.Name
		}
	}
	report.Buildings = len(kept)

	blocks := map[[2]int][]placed{}
	for _, p := range kept {
		blocks[p.block] = append(blocks[p.block], p)
	}
	blockKeys := make([][2]int, 0, len(blocks))
	for k := range blocks {
		blockKeys = append(blockKeys, k)
	}
	sort.Slice(blockKeys, func(i, j int) bool {
		if blockKeys[i][1] != blockKeys[j][1] {
			return blockKeys[i][1] < blockKeys[j][1]
		}
		return blockKeys[i][0] < blockKeys[j][0]
	})

	roads := selectRoads(ir, half, opt)
	report.Roads = len(roads)
	surfaces := selectSurfaces(ir, half, opt)
	runs, junctions := BuildRoadNetwork(surfaces, half, opt)
	report.Surfaces = len(runs)
	report.Junctions = len(junctions)
	piers := selectPiers(ir, half, opt)
	report.Piers = len(piers)
	waterLevel := 0.0
	if ir.Terrain != nil {
		waterLevel = ir.Terrain.WaterLevel
	}
	trees := scatterTrees(ir, half, tile.Seed, opt)
	report.Trees = len(trees)

	// ---- file ---------------------------------------------------------------
	var s strings.Builder
	writeHeader(&s, tile, ir, grid, report)
	// The scene lives at assets/geo/<tile>/, so the shared kit libraries under
	// assets/scad/lib are two levels up and back down.
	s.WriteString("use <../../scad/lib/gk_camera.scad>\nuse <../../scad/lib/kit_road.scad>\n")
	if opt.Detail {
		s.WriteString("use <../../scad/lib/kit_geo_city.scad>\n")
	}
	s.WriteString("\n$fn = 8;\n\n")
	writeTerrain(&s, tile, hmapRef, roads, ir.Terrain)

	s.WriteString("// Ground height under a footprint: the terrain mesh is the single source of\n")
	s.WriteString("// truth for elevation, so the base is sampled at evaluation time rather than\n")
	s.WriteString("// baked by the generator (a baked value would drift from the rendered mesh).\n")
	s.WriteString("function gz(x0, y0, x1, y1) = min(\n")
	s.WriteString("    gk_terrain_height(TERR, x0, y0), gk_terrain_height(TERR, x1, y0),\n")
	s.WriteString("    gk_terrain_height(TERR, x0, y1), gk_terrain_height(TERR, x1, y1),\n")
	s.WriteString("    gk_terrain_height(TERR, (x0 + x1) / 2, (y0 + y1) / 2));\n\n")

	blockModules := writeBlocks(&s, blocks, blockKeys, tile, opt, &report)
	report.Blocks = len(blockModules)

	surfaceModules := writeStreetSurfaces(&s, runs, junctions, opt, &report)
	pierModule := writePiers(&s, piers, opt, waterLevel, &report)
	treeModule := writeTrees(&s, trees, &report)

	s.WriteString("// ================= assembly =================\n")
	s.WriteString("gk_terrain(TERR);\n")
	for _, name := range surfaceModules {
		fmt.Fprintf(&s, "%s();\n", name)
	}
	if pierModule != "" {
		fmt.Fprintf(&s, "%s();\n", pierModule)
	}
	for _, name := range blockModules {
		fmt.Fprintf(&s, "%s();\n", name)
	}
	if treeModule != "" {
		fmt.Fprintf(&s, "%s();\n", treeModule)
	}
	s.WriteString("\n")
	writeCameras(&s, tile, ir, grid)

	report.Triangles += 2 * tile.Cells * tile.Cells
	return s.String(), report
}

func writeHeader(s *strings.Builder, tile Tile, ir *IR, grid *HeightGrid, report EmitReport) {
	lo, hi := grid.Bounds()
	stats := ir.Summarize()
	fmt.Fprintf(s, "// %s.scad — generated by `gnb geo`, do not edit by hand.\n//\n", tile.Name)
	fmt.Fprintf(s, "// Real-world tile: %.5f, %.5f — %.0f x %.0f m, %d x %d terrain cells (%.1f m/cell).\n",
		tile.Lat, tile.Lon, tile.SizeM, tile.SizeM, tile.Cells, tile.Cells,
		tile.SizeM/float64(tile.Cells))
	water := "no water body"
	if ir.Terrain != nil && ir.Terrain.HasWater {
		kind := "inland water"
		if ir.Terrain.IsSea {
			kind = "sea level"
		}
		water = fmt.Sprintf("%s %.1f m", kind, ir.Terrain.WaterLevel)
	}
	base := 0.0
	if ir.Terrain != nil {
		base = ir.Terrain.BaseElevation
	}
	fmt.Fprintf(s, "// Elevation %.1f .. %.1f m, datum %.1f m, %s.\n", lo, hi, base, water)
	s.WriteString("// SCAD +x east, +y north, 1 unit = 1 m.\n//\n")
	s.WriteString("// Data sources — attribution is required, keep this block:\n")
	for _, a := range ir.Attribution {
		fmt.Fprintf(s, "//   %s\n", a)
	}
	fmt.Fprintf(s, "//\n// %d buildings; heights: %d tagged, %d from levels, %d inferred.\n",
		stats.Buildings, stats.HeightFromTag, stats.HeightFromLvls, stats.HeightInferred)
	fmt.Fprintf(s, "// Regenerate: gnb geo make --name %s --at %.5f,%.5f --size %.0f --cells %d --profile %s --seed %d\n\n",
		tile.Name, tile.Lat, tile.Lon, tile.SizeM, tile.Cells, tile.Profile, tile.Seed)
}

func writeTerrain(s *strings.Builder, tile Tile, hmapRef string, roads []Road, meta *TerrainMeta) {
	base := 0.0
	water := "undef"
	if meta != nil {
		base = meta.BaseElevation
		if meta.HasWater {
			water = fnum(meta.WaterLevel)
		}
	}
	fmt.Fprintf(s, "TERR = [\"gkterr1\", [%s, %s], [%d, %d], %d,\n",
		fnum(tile.SizeM), fnum(tile.SizeM), tile.Cells, tile.Cells, tile.Seed)
	// The base height is the tile datum, not zero: the palette's biome bands are
	// measured from it, so an inland city at 500 m would otherwise be painted as
	// if it were a 500 m mountain rising out of the sea.
	// "urban": concrete on the flat low ground, green on the hills, no snow cap.
	fmt.Fprintf(s, "    [%s, 0, 0], %s, \"urban\",\n", fnum(base), water)
	fmt.Fprintf(s, "    [[\"hmap\", \"%s\", \"set\", 1, 0]", hmapRef)
	for _, r := range roads {
		s.WriteString(",\n     [\"road\", ")
		writePoints(s, r.Pts)
		fmt.Fprintf(s, ", %s]", fnum(r.Width))
	}
	s.WriteString("]];\n\n")
}

// writeBlocks emits the building modules and returns their names in call order.
//
// Buildings are grouped spatially first (BlockSizeM, for BLAS and culling), then
// each block is cut into as many modules as the triangle budget needs. The cut
// is what keeps physics alive: see EmitOptions.ModuleTriBudget.
func writeBlocks(s *strings.Builder, blocks map[[2]int][]placed, blockKeys [][2]int,
	tile Tile, opt EmitOptions, report *EmitReport) []string {
	dp := DetailProfileFor(tile.Profile)
	hp, ok := Profiles[tile.Profile]
	if !ok {
		hp = Profiles["default"]
	}
	if opt.Detail {
		s.WriteString("// Buildings. The footprint and the height come from OSM; the facade scheme,\n")
		s.WriteString("// the roof and the oriented bounding box the ridge runs along are decided in\n")
		s.WriteString("// the generator (detail.go) and turned into geometry by kit_geo_city.scad.\n")
		s.WriteString("// gc_bld(pts, z, h, [facade, wallTone, glassTone, floorH, seed],\n")
		s.WriteString("//        [roofKind, roofTone, rise, ridgeFrac, clutter, anchorX, anchorY, anchorR],\n")
		s.WriteString("//        obb, skirt, paths)\n\n")
	}

	var names []string
	for _, key := range blockKeys {
		group := blocks[key]
		sort.SliceStable(group, func(i, j int) bool { return group[i].b.ID < group[j].b.ID })

		part, acc, open := 0, 0, false
		for _, p := range group {
			var tri int
			var st buildingStyle
			if opt.Detail {
				st = classifyBuilding(p.b, p.ring, p.inners, dp, hp, tile.Seed)
				verts := len(p.ring)
				for _, h := range p.inners {
					verts += len(h)
				}
				tri = styleTriangles(st, verts, ringPerimeter(p.ring), math.Max(3, p.b.Height))
			} else {
				verts := len(p.ring)
				for _, h := range p.inners {
					verts += len(h)
				}
				tri = 4*verts - 4
			}

			if open && acc+tri > opt.ModuleTriBudget {
				s.WriteString("    }\n}\n\n")
				open = false
			}
			if !open {
				name := fmt.Sprintf("blk_r%dc%d", key[1], key[0])
				if part > 0 {
					name = fmt.Sprintf("%s_%d", name, part)
				}
				part++
				names = append(names, name)
				// gk_flatten: every user module call would otherwise become its
				// own Node, Model and collider — one per building.
				fmt.Fprintf(s, "module %s()\n{\n    gk_flatten()\n    {\n", name)
				open, acc = true, 0
			}

			if opt.Detail {
				writeBuildingDetail(s, p.b, p.ring, p.inners, st, opt.SkirtM)
			} else {
				writeBuilding(s, p.b, p.ring, p.inners, opt.SkirtM)
			}
			acc += tri
			report.Triangles += tri
		}
		if open {
			s.WriteString("    }\n}\n\n")
		}
	}
	return names
}

// writeBuildingDetail emits one gc_bld() call: the footprint plus the style
// vector the kit needs. The geometry itself never appears in the tile file —
// that is the point of the split, and it is why the scene file did not grow
// when buildings gained facades.
func writeBuildingDetail(s *strings.Builder, b Building, ring Ring, inners []Ring,
	st buildingStyle, skirt float64) {
	minX, minY, maxX, maxY := BoundsOf(ring)
	height := math.Max(3, b.Height)

	if b.Name != "" {
		fmt.Fprintf(s, "        // %s (%.0fm, %s)\n", sanitizeComment(b.Name), b.Height, b.HeightSource)
	}
	all, paths := flattenRings(ring, inners)

	s.WriteString("        gc_bld(")
	writePoints(s, all)
	fmt.Fprintf(s, ", gz(%s, %s, %s, %s), %s,\n",
		fnum(minX), fnum(minY), fnum(maxX), fnum(maxY), fnum(height))
	fmt.Fprintf(s, "            [%d, %d, %d, %s, %d], [%d, %d, %s, %s, %d, %s, %s, %s], ",
		st.Facade, st.WallTone, st.GlassTone, fnum(st.FloorH), st.Seed,
		st.RoofKind, st.RoofTone, fnum(st.RoofRise), fnum(st.RidgeFrac), st.Clutter,
		fnum(st.Anchor[0]), fnum(st.Anchor[1]), fnum(st.Anchor[2]))
	if st.HasOBB {
		fmt.Fprintf(s, "[%s, %s, %s, %s, %s]",
			fnum(st.OBB[0]), fnum(st.OBB[1]), fnum(st.OBB[2]), fnum(st.OBB[3]), fnum(st.OBB[4]))
	} else {
		s.WriteString("[]")
	}
	fmt.Fprintf(s, ", %s", fnum(skirt))
	if len(paths) > 1 {
		s.WriteString(", ")
		writePaths(s, paths)
	}
	s.WriteString(");\n")
}

// flattenRings packs an outer ring plus its holes into the one point list and
// index paths a polygon() needs.
func flattenRings(ring Ring, inners []Ring) (Ring, [][]int) {
	all := append(Ring{}, ring...)
	paths := [][]int{indexRange(0, len(ring))}
	for _, hole := range inners {
		paths = append(paths, indexRange(len(all), len(hole)))
		all = append(all, hole...)
	}
	return all, paths
}

func writePaths(s *strings.Builder, paths [][]int) {
	s.WriteString("[")
	for i, p := range paths {
		if i > 0 {
			s.WriteString(", ")
		}
		s.WriteString("[")
		for j, idx := range p {
			if j > 0 {
				s.WriteString(", ")
			}
			s.WriteString(strconv.Itoa(idx))
		}
		s.WriteString("]")
	}
	s.WriteString("]")
}

// writeBuilding emits one extruded footprint and returns its triangle estimate.
// This is the --no-detail path: a bare prism, exactly what the pipeline emitted
// before the decoration layer existed.
func writeBuilding(s *strings.Builder, b Building, ring Ring, inners []Ring, skirt float64) int {
	minX, minY, maxX, maxY := BoundsOf(ring)
	c := buildingPalette[int(uint64(b.ID)%uint64(len(buildingPalette)))]
	height := math.Max(3, b.Height) + skirt

	if b.Name != "" {
		fmt.Fprintf(s, "        // %s (%.0fm, %s)\n", sanitizeComment(b.Name), b.Height, b.HeightSource)
	}
	fmt.Fprintf(s, "        color([%s, %s, %s]) translate([0, 0, gz(%s, %s, %s, %s) - %s])\n",
		fnum(c[0]), fnum(c[1]), fnum(c[2]),
		fnum(minX), fnum(minY), fnum(maxX), fnum(maxY), fnum(skirt))
	fmt.Fprintf(s, "            linear_extrude(height = %s) polygon(points = ", fnum(height))

	all, paths := flattenRings(ring, inners)
	writePoints(s, all)
	if len(paths) > 1 {
		s.WriteString(", paths = ")
		writePaths(s, paths)
	}
	s.WriteString(");\n")

	// Prism: 2*(v-2) per cap plus 2 per side quad.
	v := len(all)
	return 4*v - 4
}

// Street surface colours: asphalt for the carriageway, a lighter kerb tone for
// the service network so the hierarchy reads from above.
var (
	asphaltColor = [3]float64{0.09, 0.09, 0.10}
	serviceColor = [3]float64{0.14, 0.14, 0.14}
	pierColor    = [3]float64{0.30, 0.29, 0.27}
)

// writeStreetSurfaces emits the drivable network as continuous carriageway
// ribbons plus filled intersections, and returns the module names to call.
//
// Topology is decided here (roadnet.go: junction nodes, run splitting, trimming,
// mitring); geometry rules live in kit_road.scad. That split is the point — a
// change to how every road meets the ground, or a new kind of road marking, is
// one edit in the kit for every tile at once.
func writeStreetSurfaces(s *strings.Builder, runs []RoadRun, junctions []Junction,
	opt EmitOptions, report *EmitReport) []string {
	if len(runs) == 0 {
		return nil
	}
	s.WriteString("// Street network. Each run is one continuous ribbon (a single polyhedron,\n")
	s.WriteString("// no per-segment seams); each junction is a filled patch the approaches were\n")
	s.WriteString("// trimmed back from. Geometry rules: assets/scad/lib/kit_road.scad.\n\n")

	// Split by surface colour first so each module has a single material. The
	// service group is the back-alley network: no centre line, no crosswalks, no
	// sidewalk and no street furniture — a lane behind a block does not have a
	// kerb, and decorating them all is what would blow the budget.
	groups := [][]RoadRun{nil, nil}
	for _, r := range runs {
		if r.Class == "service" || r.Class == "living_street" {
			groups[1] = append(groups[1], r)
		} else {
			groups[0] = append(groups[0], r)
		}
	}
	colors := [][3]float64{asphaltColor, serviceColor}

	var names []string
	for gi, group := range groups {
		decorated := opt.Detail && gi == 0
		for _, chunk := range chunkRuns(group, opt, decorated) {
			name := fmt.Sprintf("streets_%d", len(names))
			names = append(names, name)
			c := colors[gi]
			fmt.Fprintf(s, "module %s() { rd_network(TERR, [%s, %s, %s], [\n",
				name, fnum(c[0]), fnum(c[1]), fnum(c[2]))
			for i, r := range chunk {
				if i > 0 {
					s.WriteString(",\n")
				}
				s.WriteString("    [")
				writePoints(s, r.Left)
				s.WriteString(", ")
				writePoints(s, r.Right)
				s.WriteString("]")
				report.Triangles += runTriangles(r, decorated)
			}
			s.WriteString("],\n    [],\n    [")
			for i, r := range chunk {
				if i > 0 {
					s.WriteString(", ")
				}
				s.WriteString(fnum(r.Width))
			}
			// markings, sidewalks, props, seed
			fmt.Fprintf(s, "], %t, %t, %t, %d); }\n\n",
				gi == 0, decorated, decorated, len(names))
		}
	}

	// Intersections get their own modules. They used to ride along in the first
	// street module; with sidewalks and props in the mix that one Model would
	// cross the physics limit and the whole first chunk of the city would lose
	// its collider.
	for _, chunk := range chunkJunctions(junctions, opt) {
		name := fmt.Sprintf("junctions_%d", len(names))
		names = append(names, name)
		fmt.Fprintf(s, "module %s() { rd_network(TERR, [%s, %s, %s], [],\n    [",
			name, fnum(asphaltColor[0]), fnum(asphaltColor[1]), fnum(asphaltColor[2]))
		for i, j := range chunk {
			if i > 0 {
				s.WriteString(", ")
			}
			writePoints(s, j.Ring)
			report.Triangles += junctionTriangles(j, opt.Detail)
		}
		fmt.Fprintf(s, "],\n    [], false, false, %t, %d); }\n\n", opt.Detail, len(names))
	}
	return names
}

// chunkRuns cuts a colour group into modules that stay under the triangle
// budget, never exceeding RoadsPerModule either.
func chunkRuns(group []RoadRun, opt EmitOptions, decorated bool) [][]RoadRun {
	var out [][]RoadRun
	var cur []RoadRun
	acc := 0
	for _, r := range group {
		tri := runTriangles(r, decorated)
		if len(cur) > 0 && (acc+tri > opt.ModuleTriBudget || len(cur) >= opt.RoadsPerModule) {
			out = append(out, cur)
			cur, acc = nil, 0
		}
		cur = append(cur, r)
		acc += tri
	}
	if len(cur) > 0 {
		out = append(out, cur)
	}
	return out
}

func chunkJunctions(junctions []Junction, opt EmitOptions) [][]Junction {
	var out [][]Junction
	var cur []Junction
	acc := 0
	for _, j := range junctions {
		tri := junctionTriangles(j, opt.Detail)
		if len(cur) > 0 && acc+tri > opt.ModuleTriBudget {
			out = append(out, cur)
			cur, acc = nil, 0
		}
		cur = append(cur, j)
		acc += tri
	}
	if len(cur) > 0 {
		out = append(out, cur)
	}
	return out
}

// runTriangles mirrors what kit_road builds for one run: a closed prism over n
// stations (top, bottom, two sides, two caps), a dash every 10 m on wide roads,
// and — when the run is decorated — four more ribbons for the kerb and the
// footway, one prop every rd_PROP_STEP stations, and a zebra at each end.
func runTriangles(r RoadRun, decorated bool) int {
	stations := len(r.Left)
	if stations < 2 {
		return 0
	}
	ribbon := (4*(stations-1) + 2) * 2
	tris := ribbon
	if r.Width >= 9 {
		tris += stations * 12
	}
	if decorated {
		tris += 4 * ribbon // 2 kerb strips + 2 footway strips
		tris += (stations/6 + 1) * 60
		if r.Width >= 9 {
			tris += 2 * 5 * 12 // two crosswalks, five stripes each
		}
	}
	return tris
}

func junctionTriangles(j Junction, detail bool) int {
	tris := 4 * len(j.Ring)
	if detail {
		minX, minY, maxX, maxY := BoundsOf(j.Ring)
		if math.Max(maxX-minX, maxY-minY) >= 16 {
			tris += 4 * 110 // up to four traffic lights
		}
	}
	return tris
}

// writePiers emits harbour piers: a solid deck standing in the water. Closed
// ways are deck outlines and get extruded; open ways get a slab strip.
func writePiers(s *strings.Builder, piers []Line, opt EmitOptions, waterLevel float64,
	report *EmitReport) string {
	if len(piers) == 0 {
		return ""
	}
	s.WriteString("// Piers: solid decks from the water line up. They stand in dredged water,\n")
	s.WriteString("// so they sit on the tile's water plane rather than snapping to the bed.\n")
	s.WriteString("module piers() {\n")
	fmt.Fprintf(s, "    WL = %s;\n", fnum(waterLevel))
	fmt.Fprintf(s, "    color([%s, %s, %s]) {\n", fnum(pierColor[0]), fnum(pierColor[1]), fnum(pierColor[2]))
	for _, p := range piers {
		if p.Closed {
			s.WriteString("        translate([0, 0, WL - 1]) linear_extrude(height = 4.2) polygon(points = ")
			writePoints(s, p.Pts)
			s.WriteString(");\n")
			report.Triangles += 4*len(p.Pts) - 4
			continue
		}
		for i := 0; i+1 < len(p.Pts); i++ {
			a, b := p.Pts[i], p.Pts[i+1]
			dx, dy := b[0]-a[0], b[1]-a[1]
			length := math.Hypot(dx, dy)
			if length < 1 {
				continue
			}
			fmt.Fprintf(s, "        translate([%s, %s, WL + 1.1]) rotate([0, 0, %s]) cube([%s, %s, 4.2], center = true);\n",
				fnum((a[0]+b[0])/2), fnum((a[1]+b[1])/2),
				fnum(math.Atan2(dy, dx)*180/math.Pi), fnum(length), fnum(opt.PierWidthM))
			report.Triangles += 12
		}
	}
	s.WriteString("    }\n}\n\n")
	return "piers"
}

// writeTrees emits the green-space planting. Positions are baked (Go owns the
// polygon test) but the height is not: every tree snaps to the terrain at
// evaluation time.
//
// The foliage is local geometry rather than kit_city_hd's hc_nature_tree: that
// kit's leaf colour is 0.76 green, which blows out to a neon dot under
// path-traced daylight at city scale.
func writeTrees(s *strings.Builder, trees []treeSpot, report *EmitReport) string {
	if len(trees) == 0 {
		return ""
	}
	s.WriteString("// Green-space planting (leisure=park/garden, landuse=grass/forest).\n")
	s.WriteString("// spots = [[x, y, scale, variant], ...]; one module, one Node.\n")
	s.WriteString("TREEC = [[0.13, 0.20, 0.09], [0.16, 0.23, 0.11],\n")
	s.WriteString("         [0.11, 0.17, 0.08], [0.18, 0.25, 0.12]];\n")
	s.WriteString("module veg(spots) {\n")
	s.WriteString("    for (k = [0 : len(spots) - 1])\n")
	s.WriteString("        let (p = spots[k], x = p[0], y = p[1], s = p[2], i = p[3],\n")
	s.WriteString("             z = gk_terrain_height(TERR, x, y)) {\n")
	s.WriteString("            color([0.19, 0.14, 0.10])\n")
	s.WriteString("                translate([x, y, z]) cylinder(h = 1.7 * s, r = 0.22 * s, $fn = 5);\n")
	s.WriteString("            color(TREEC[i % 4])\n")
	s.WriteString("                translate([x, y, z + 2.4 * s]) sphere(r = 1.6 * s, $fn = 6);\n")
	s.WriteString("            color(TREEC[(i + 1) % 4])\n")
	s.WriteString("                translate([x + 0.7 * s, y + 0.4 * s, z + 3.2 * s]) sphere(r = 1.0 * s, $fn = 5);\n")
	s.WriteString("        }\n}\n\n")
	s.WriteString("module veg_all() { veg([\n")
	for i, t := range trees {
		if i > 0 {
			s.WriteString(",\n")
		}
		fmt.Fprintf(s, "    [%s, %s, %s, %d]", fnum(t.x), fnum(t.y), fnum(t.scale), t.variant)
		report.Triangles += 90
	}
	s.WriteString("]); }\n\n")
	return "veg_all"
}

// treeSpot is one baked planting position.
type treeSpot struct {
	x, y    float64
	scale   float64
	variant int
}

// greenTags are the areas worth planting.
var greenTags = map[string]bool{
	"leisure=park": true, "leisure=garden": true, "leisure=pitch": false,
	"landuse=grass": true, "landuse=forest": true, "landuse=village_green": true,
	"landuse=recreation_ground": true, "landuse=cemetery": true,
}

// scatterTrees rejection-samples inside the green polygons. Deterministic: the
// PRNG is seeded from the tile seed and the OSM id, and areas are walked in the
// IR's (id-sorted) order.
func scatterTrees(ir *IR, half float64, seed int, opt EmitOptions) []treeSpot {
	var out []treeSpot
	for _, area := range ir.Landuse {
		if !greenTags[area.Tag] {
			continue
		}
		minX, minY, maxX, maxY := BoundsOf(area.Outer)
		if minX < -half || minY < -half || maxX > half || maxY > half {
			continue
		}
		area2 := RingArea(area.Outer)
		want := int(area2 / opt.TreeDensityM2)
		if want < 1 {
			continue
		}
		rng := uint64(seed)*0x9E3779B97F4A7C15 ^ uint64(area.ID)
		next := func() float64 {
			rng ^= rng << 13
			rng ^= rng >> 7
			rng ^= rng << 17
			return float64(rng%1000000) / 1000000.0
		}
		// Bounded attempts: a long thin park should not spin here forever.
		for attempts, placed := 0, 0; attempts < want*24 && placed < want; attempts++ {
			x := minX + next()*(maxX-minX)
			y := minY + next()*(maxY-minY)
			if !PointInRing(area.Outer, x, y) {
				continue
			}
			inHole := false
			for _, hole := range area.Inners {
				if PointInRing(hole, x, y) {
					inHole = true
					break
				}
			}
			if inHole {
				continue
			}
			// Forest reads as older, larger canopy than a city garden.
			scale := 1.3 + next()*1.0
			if area.Tag == "landuse=forest" {
				scale += 0.6
			}
			out = append(out, treeSpot{x: x, y: y, scale: scale, variant: int(next() * 4)})
			placed++
			if len(out) >= opt.MaxTrees {
				return out
			}
		}
	}
	return out
}

// selectSurfaces picks the drivable network for street geometry.
//
// Selection only — no clipping and no simplification. Both of those destroy the
// index alignment between Pts and Nodes, and BuildRoadNetwork needs the node ids
// intact to find the junctions. It applies them itself, per run, afterwards.
func selectSurfaces(ir *IR, half float64, opt EmitOptions) []Road {
	allowed := map[string]bool{}
	for _, c := range opt.SurfaceClasses {
		allowed[c] = true
	}
	var out []Road
	for _, r := range ir.Roads {
		if !allowed[r.Class] {
			continue
		}
		// Cheap reject: a way with no vertex anywhere near the tile.
		minX, minY, maxX, maxY := BoundsOf(r.Pts)
		if maxX < -half || minX > half || maxY < -half || minY > half {
			continue
		}
		out = append(out, r)
	}
	// Widest first, so the budget keeps the roads that carry the picture.
	sort.SliceStable(out, func(i, j int) bool {
		if out[i].Width != out[j].Width {
			return out[i].Width > out[j].Width
		}
		return out[i].ID < out[j].ID
	})
	if len(out) > opt.MaxSurfaces {
		out = out[:opt.MaxSurfaces]
	}
	sort.SliceStable(out, func(i, j int) bool { return out[i].ID < out[j].ID })
	return out
}

// selectPiers keeps the piers that fit on the tile.
func selectPiers(ir *IR, half float64, opt EmitOptions) []Line {
	var out []Line
	for _, p := range ir.Piers {
		minX, minY, maxX, maxY := BoundsOf(p.Pts)
		if minX < -half || minY < -half || maxX > half || maxY > half {
			continue
		}
		if p.Closed && len(p.Pts) < 3 {
			continue
		}
		out = append(out, p)
	}
	return out
}

// selectRoads picks the arterial network and turns it into terrain operators.
func selectRoads(ir *IR, half float64, opt EmitOptions) []Road {
	classRank := map[string]int{}
	for i, c := range opt.RoadClasses {
		classRank[c] = len(opt.RoadClasses) - i
	}
	var out []Road
	for _, r := range ir.Roads {
		rank, ok := classRank[r.Class]
		if !ok {
			continue
		}
		pts := clipToSquare(r.Pts, half)
		if len(pts) < 2 {
			continue
		}
		pts = Simplify(Ring(pts), 4, 24)
		if len(pts) < 2 {
			continue
		}
		r.Pts = pts
		r.Lanes = rank
		out = append(out, r)
	}
	// Keep the most important roads when the budget bites: every operator costs
	// one polyline distance query per terrain cell.
	sort.SliceStable(out, func(i, j int) bool {
		if out[i].Lanes != out[j].Lanes {
			return out[i].Lanes > out[j].Lanes
		}
		return out[i].ID < out[j].ID
	})
	if len(out) > opt.MaxRoads {
		out = out[:opt.MaxRoads]
	}
	sort.SliceStable(out, func(i, j int) bool { return out[i].ID < out[j].ID })
	return out
}

// clipToSquare keeps the longest run of a polyline that stays inside the tile.
func clipToSquare(pts [][2]float64, half float64) [][2]float64 {
	var best, run [][2]float64
	for _, p := range pts {
		if math.Abs(p[0]) <= half && math.Abs(p[1]) <= half {
			run = append(run, p)
			continue
		}
		if len(run) > len(best) {
			best = run
		}
		run = nil
	}
	if len(run) > len(best) {
		best = run
	}
	return best
}

func writeCameras(s *strings.Builder, tile Tile, ir *IR, grid *HeightGrid) {
	_, hi := grid.Bounds()
	half := tile.SizeM / 2
	tallest := [2]float64{0, 0}
	best := 0.0
	for _, b := range ir.Buildings {
		if b.Height > best {
			best = b.Height
			tallest = Centroid(b.Outer)
		}
	}
	// Everything here is relative to the tile datum. Building heights are
	// heights, terrain samples are elevations: mixing them frames a 500 m
	// inland city from a point half a kilometre underground.
	ground := 0.0
	if ir.Terrain != nil {
		ground = ir.Terrain.BaseElevation
	}
	relief := hi - ground
	eyeZ := ground + math.Max(relief, best) + tile.SizeM*0.45

	fmt.Fprintf(s, "// Camera markers (no geometry). The first one is the default view.\n")
	fmt.Fprintf(s, "gk_camera_lookat([%s, %s, %s], [0, 0, %s], \"overview\", 48);\n",
		fnum(-half*1.25), fnum(-half*1.45), fnum(eyeZ), fnum(ground+best*0.35))
	fmt.Fprintf(s, "gk_camera_lookat([%s, %s, %s], [%s, %s, %s], \"skyline\", 42);\n",
		fnum(tallest[0]-half*0.9), fnum(tallest[1]-half*1.1), fnum(ground+math.Max(best*0.55, 40)),
		fnum(tallest[0]), fnum(tallest[1]), fnum(ground+best*0.7))
	fmt.Fprintf(s, "gk_camera_lookat([%s, %s, %s], [%s, %s, %s], \"street\", 60);\n",
		fnum(tallest[0]+70), fnum(tallest[1]+70), fnum(ground+14.0),
		fnum(tallest[0]), fnum(tallest[1]), fnum(ground+6.0))
}

// ---------------------------------------------------------------------------
// formatting helpers
// ---------------------------------------------------------------------------

func writePoints(s *strings.Builder, pts [][2]float64) {
	s.WriteString("[")
	for i, p := range pts {
		if i > 0 {
			s.WriteString(", ")
		}
		fmt.Fprintf(s, "[%s, %s]", fnum(p[0]), fnum(p[1]))
	}
	s.WriteString("]")
}

// fnum prints a compact, locale-free, deterministic number.
func fnum(v float64) string {
	if math.Abs(v) < 5e-3 {
		return "0"
	}
	s := strconv.FormatFloat(v, 'f', 2, 64)
	s = strings.TrimRight(s, "0")
	return strings.TrimSuffix(s, ".")
}

func indexRange(start, n int) []int {
	out := make([]int, n)
	for i := range out {
		out[i] = start + i
	}
	return out
}

func reverseRing(r Ring) Ring {
	out := append(Ring{}, r...)
	for i, j := 0, len(out)-1; i < j; i, j = i+1, j-1 {
		out[i], out[j] = out[j], out[i]
	}
	return out
}

// sanitizeComment keeps a building name on one line and out of the parser.
func sanitizeComment(name string) string {
	name = strings.ReplaceAll(name, "\n", " ")
	name = strings.ReplaceAll(name, "*/", "")
	if len(name) > 90 {
		name = name[:90]
	}
	return strings.TrimSpace(name)
}
