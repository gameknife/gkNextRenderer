// Package scadcompose implements the L2 layer of the SCAD scene-compose design
// (docs/designs/scad-scene-compose-design.md §6): a JSON scene spec is validated
// against the kit catalog (assets/scad/lib/catalog.json) and expanded into a
// plain top-level .scad that uses the kit libraries and the kit_layout
// combinators. The generator is a thin, deterministic template: no geometry, no
// random decisions (seeds are pushed down to the scad layer), no engine calls.
package scadcompose

import (
	"encoding/json"
	"fmt"
	"os"
	"strings"
)

// Call is one module invocation: either the bare module name ("oc_prop_well")
// or an object {"module": "...", "args": "seed = $seed"}.
type Call struct {
	Module string `json:"module"`
	Args   string `json:"args"`
}

func (c *Call) UnmarshalJSON(data []byte) error {
	type callAlias Call
	if len(data) > 0 && data[0] == '"' {
		var name string
		if err := json.Unmarshal(data, &name); err != nil {
			return err
		}
		// LLM-authored specs sometimes double-encode the object form as a
		// string ("{\"module\": ...}"); unwrap it instead of bouncing the spec.
		trimmed := strings.TrimSpace(name)
		if strings.HasPrefix(trimmed, "{") {
			var alias callAlias
			if err := json.Unmarshal([]byte(trimmed), &alias); err == nil && alias.Module != "" {
				*c = Call(alias)
				return nil
			}
		}
		c.Module = name
		return nil
	}
	var alias callAlias
	if err := json.Unmarshal(data, &alias); err != nil {
		return err
	}
	*c = Call(alias)
	return nil
}

func (c Call) scad() string {
	return fmt.Sprintf("%s(%s)", c.Module, c.Args)
}

// Ground is an optional single slab under the scene.
type Ground struct {
	Size      [2]float64 `json:"size"`
	Color     [3]float64 `json:"color"`
	Z         *float64   `json:"z"`         // slab top sits at z (default -0.02)
	Thickness float64    `json:"thickness"` // default 0.3
}

// TerrainBase shapes the base plain of a heightfield terrain.
type TerrainBase struct {
	Height    float64  `json:"height"`
	Relief    float64  `json:"relief"`    // fbm amplitude; 0 = flat
	Roughness *float64 `json:"roughness"` // 0..1, default 0.5
}

// TerrainFeature is one ordered heightfield operator (see
// docs/designs/scad-terrain-design.md §4/§5.1 for the semantics).
type TerrainFeature struct {
	Type   string       `json:"type"` // mountain|ridge|plateau|lake|river|road|pad
	At     [2]float64   `json:"at"`
	Size   [2]float64   `json:"size"` // pad
	Rot    float64      `json:"rot"`  // pad
	Radius float64      `json:"radius"`
	Height float64      `json:"height"`
	Depth  float64      `json:"depth"`
	Width  float64      `json:"width"`
	Rugged float64      `json:"rugged"`
	Pts    [][2]float64 `json:"pts"`
}

// Terrain is the optional heightfield section (mutually exclusive with
// Ground). Expanded into a TERR constant + gk_terrain(TERR) call.
type Terrain struct {
	Size       [2]float64       `json:"size"`
	Cells      *[2]int          `json:"cells"`      // default round(size/2), 4..256
	Seed       *int             `json:"seed"`       // default spec seed
	Base       *TerrainBase     `json:"base"`
	WaterLevel *float64         `json:"waterLevel"` // global sea level
	Palette    string           `json:"palette"`    // temperate|arid|alpine
	Features   []TerrainFeature `json:"features"`
}

// ScatterWhere filters terrain-snapped scatter candidates.
type ScatterWhere struct {
	HMin       *float64 `json:"hMin"`
	HMax       *float64 `json:"hMax"`
	SlopeMax   *float64 `json:"slopeMax"`   // degrees
	AvoidWater *float64 `json:"avoidWater"` // probe distance; <0 allows water
	Biome      []string `json:"biome"`      // whitelist (empty = any)
}

// Placement is one explicit landmark placement.
type Placement struct {
	Call
	At     [2]float64  `json:"at"`
	Rot    float64     `json:"rot"`
	Scale  float64     `json:"scale"` // 0 -> 1.0
	Snap   string      `json:"snap"`  // ""(default)|"terrain"|"none"
	SnapAt *[2]float64 `json:"snapAt"` // sample terrain height here instead of at
	                                   // (bridges: anchor on the bank, not the bed)
}

func (p *Placement) UnmarshalJSON(data []byte) error {
	type placementAlias struct {
		Module string      `json:"module"`
		Args   string      `json:"args"`
		At     [2]float64  `json:"at"`
		Rot    float64     `json:"rot"`
		Scale  float64     `json:"scale"`
		Snap   string      `json:"snap"`
		SnapAt *[2]float64 `json:"snapAt"`
	}
	var alias placementAlias
	if err := json.Unmarshal(data, &alias); err != nil {
		return err
	}
	p.Module = alias.Module
	p.Args = alias.Args
	p.At = alias.At
	p.Rot = alias.Rot
	p.Scale = alias.Scale
	p.Snap = alias.Snap
	p.SnapAt = alias.SnapAt
	return nil
}

// Jitter adds a per-instance lay_jitter wrapper inside grid cells.
type Jitter struct {
	Dx  float64 `json:"dx"`
	Dy  float64 `json:"dy"`
	Rot float64 `json:"rot"`
}

// GridRule places children on a lay_grid.
type GridRule struct {
	At       [2]float64 `json:"at"`
	Cols     int        `json:"cols"`
	Rows     int        `json:"rows"`
	Cell     [2]float64 `json:"cell"`
	Seed     int        `json:"seed"`
	Center   *bool      `json:"center"`
	Jitter   *Jitter    `json:"jitter"`
	Snap     string     `json:"snap"`
	Children []Call     `json:"children"`
}

// RowRule places children on a lay_row.
type RowRule struct {
	At       [2]float64 `json:"at"`
	N        int        `json:"n"`
	Dx       float64    `json:"dx"`
	Dy       float64    `json:"dy"`
	Rot      float64    `json:"rot"` // extra child rotation (e.g. parking angle)
	Seed     int        `json:"seed"`
	Snap     string     `json:"snap"`
	Children []Call     `json:"children"`
}

// RingRule places children on a lay_ring.
type RingRule struct {
	At       [2]float64 `json:"at"`
	N        int        `json:"n"`
	R        float64    `json:"r"`
	Face     *int       `json:"face"` // default 1 (front toward center)
	A0       float64    `json:"a0"`
	Seed     int        `json:"seed"`
	Snap     string     `json:"snap"`
	Children []Call     `json:"children"`
}

// ScatterRule scatters children in a rectangle.
type ScatterRule struct {
	Region   [4]float64    `json:"region"` // [x0, y0, x1, y1] — min corner, max corner
	N        int           `json:"n"`
	Seed     int           `json:"seed"`
	Rot      *bool         `json:"rot"` // default true
	Snap     string        `json:"snap"`
	Where    *ScatterWhere `json:"where"` // terrain filter (needs snap=terrain)
	Children []Call        `json:"children"`
}

// AlongRule places children along a polyline.
type AlongRule struct {
	Pts      [][2]float64 `json:"pts"`
	Step     float64      `json:"step"`
	Offset   float64      `json:"offset"`
	Seed     int          `json:"seed"`
	Snap     string       `json:"snap"`
	Children []Call       `json:"children"`
}

// BlockGrid instantiates a named-type layout matrix on a lay_grid — the
// city-scale form (habor_city_v2's V2_LAYOUT pattern).
type BlockGrid struct {
	At     [2]float64 `json:"at"`
	Cell   [2]float64 `json:"cell"`
	Seed   int        `json:"seed"`
	Layout [][]string `json:"layout"` // rows of blockType names; dims define the grid
}

// Spec is the v1 scene spec. Strict JSON (no comments).
type Spec struct {
	Name       string            `json:"name"`
	Fn         int               `json:"fn"`   // default 12
	Seed       int               `json:"seed"` // reserved default seed
	Kits       []string          `json:"kits"` // short ("old_city") or full ("kit_old_city") names
	Ground     *Ground           `json:"ground"`
	Terrain    *Terrain          `json:"terrain"`
	BlockTypes map[string][]Call `json:"blockTypes"`
	BlockGrids []BlockGrid       `json:"blockGrids"`
	Placements []Placement       `json:"placements"`
	Grids      []GridRule        `json:"grids"`
	Rows       []RowRule         `json:"rows"`
	Rings      []RingRule        `json:"rings"`
	Scatters   []ScatterRule     `json:"scatters"`
	Alongs     []AlongRule       `json:"alongs"`
}

// ParseSpec parses spec JSON bytes (strict: unknown fields are errors).
// `origin` labels error messages (a path or e.g. "llm-reply").
func ParseSpec(raw []byte, origin string) (*Spec, error) {
	decoder := json.NewDecoder(strings.NewReader(string(raw)))
	decoder.DisallowUnknownFields()
	spec := &Spec{}
	if err := decoder.Decode(spec); err != nil {
		return nil, fmt.Errorf("%s: %w", origin, err)
	}
	if spec.Name == "" {
		return nil, fmt.Errorf("%s: spec has no \"name\"", origin)
	}
	if spec.Fn == 0 {
		spec.Fn = 12
	}
	return spec, nil
}

// LoadSpec reads and parses a spec file.
func LoadSpec(path string) (*Spec, error) {
	raw, err := os.ReadFile(path)
	if err != nil {
		return nil, err
	}
	return ParseSpec(raw, path)
}
