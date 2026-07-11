package scadcompose

import (
	"fmt"
	"sort"
	"strconv"
	"strings"
)

// Result carries the generated .scad source plus non-fatal validation warnings.
type Result struct {
	Source   string
	Warnings []string
}

// Compose validates the spec against the catalog and expands it into a
// deterministic top-level .scad (same spec -> byte-identical output). The
// output is written under assets/scad/gen/, so kit `use` paths are ../lib/.
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

	// ---- Rule sanity + calls ----
	for i, p := range spec.Placements {
		if err := checkCall(fmt.Sprintf("placements[%d]", i), p.Call); err != nil {
			return nil, err
		}
	}
	for i, g := range spec.Grids {
		where := fmt.Sprintf("grids[%d]", i)
		if g.Cols <= 0 || g.Rows <= 0 || g.Cell[0] <= 0 || g.Cell[1] <= 0 {
			return nil, fmt.Errorf("%s: cols/rows/cell must be positive", where)
		}
		if err := checkChildren(where, g.Children); err != nil {
			return nil, err
		}
	}
	for i, r := range spec.Rows {
		where := fmt.Sprintf("rows[%d]", i)
		if r.N <= 0 {
			return nil, fmt.Errorf("%s: n must be positive", where)
		}
		if err := checkChildren(where, r.Children); err != nil {
			return nil, err
		}
	}
	for i, r := range spec.Rings {
		where := fmt.Sprintf("rings[%d]", i)
		if r.N <= 0 || r.R <= 0 {
			return nil, fmt.Errorf("%s: n/r must be positive", where)
		}
		if err := checkChildren(where, r.Children); err != nil {
			return nil, err
		}
	}
	for i, s := range spec.Scatters {
		where := fmt.Sprintf("scatters[%d]", i)
		if s.N <= 0 || s.Region[0] >= s.Region[1] || s.Region[2] >= s.Region[3] {
			return nil, fmt.Errorf("%s: need n > 0 and region x0 < x1, y0 < y1", where)
		}
		if err := checkChildren(where, s.Children); err != nil {
			return nil, err
		}
	}
	for i, a := range spec.Alongs {
		where := fmt.Sprintf("alongs[%d]", i)
		if len(a.Pts) < 2 || a.Step <= 0 {
			return nil, fmt.Errorf("%s: need >= 2 pts and step > 0", where)
		}
		if err := checkChildren(where, a.Children); err != nil {
			return nil, err
		}
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
	if usesCombinators {
		b.WriteString("use <../lib/kit_layout.scad>\n")
	}
	for _, file := range kitFiles {
		fmt.Fprintf(&b, "use <../lib/%s>\n", file)
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
		for _, p := range spec.Placements {
			b.WriteString(placementStmt(p))
		}
		b.WriteString("\n")
	}
	for _, g := range spec.Grids {
		center := ""
		if g.Center != nil && !*g.Center {
			center = ", center = false"
		}
		jitter := ""
		if g.Jitter != nil {
			jitter = fmt.Sprintf("lay_jitter($seed, %s, %s, %s) ", num(g.Jitter.Dx), num(g.Jitter.Dy), num(g.Jitter.Rot))
		}
		fmt.Fprintf(&b, "%slay_grid(%d, %d, %s, %s, seed = %d%s)\n    %s%s\n",
			translatePrefix(g.At), g.Cols, g.Rows, num(g.Cell[0]), num(g.Cell[1]), g.Seed, center,
			jitter, childrenStmt(g.Children, "$seed"))
	}
	for _, r := range spec.Rows {
		rot := ""
		if r.Rot != 0 {
			rot = fmt.Sprintf("rotate([0, 0, %s]) ", num(r.Rot))
		}
		fmt.Fprintf(&b, "%slay_row(%d, %s, %s, seed = %d)\n    %s%s\n",
			translatePrefix(r.At), r.N, num(r.Dx), num(r.Dy), r.Seed, rot, childrenStmt(r.Children, "$seed"))
	}
	for _, r := range spec.Rings {
		extras := ""
		if r.Face != nil {
			extras += fmt.Sprintf(", face = %d", *r.Face)
		}
		if r.A0 != 0 {
			extras += fmt.Sprintf(", a0 = %s", num(r.A0))
		}
		fmt.Fprintf(&b, "%slay_ring(%d, %s, seed = %d%s)\n    %s\n",
			translatePrefix(r.At), r.N, num(r.R), r.Seed, extras, childrenStmt(r.Children, "$seed"))
	}
	for _, s := range spec.Scatters {
		rot := ""
		if s.Rot != nil && !*s.Rot {
			rot = ", rot = false"
		}
		fmt.Fprintf(&b, "lay_scatter(%d, %s, %s, %s, %s, seed = %d%s)\n    %s\n",
			s.N, num(s.Region[0]), num(s.Region[1]), num(s.Region[2]), num(s.Region[3]), s.Seed, rot,
			childrenStmt(s.Children, "$seed"))
	}
	for _, a := range spec.Alongs {
		points := make([]string, len(a.Pts))
		for i, pt := range a.Pts {
			points[i] = fmt.Sprintf("[%s, %s]", num(pt[0]), num(pt[1]))
		}
		offset := ""
		if a.Offset != 0 {
			offset = fmt.Sprintf(", offset = %s", num(a.Offset))
		}
		fmt.Fprintf(&b, "lay_along([%s], step = %s, seed = %d%s)\n    %s\n",
			strings.Join(points, ", "), num(a.Step), a.Seed, offset, childrenStmt(a.Children, "$seed"))
	}

	result.Source = b.String()
	return result, nil
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

func placementStmt(p Placement) string {
	var b strings.Builder
	fmt.Fprintf(&b, "translate([%s, %s, 0]) ", num(p.At[0]), num(p.At[1]))
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
