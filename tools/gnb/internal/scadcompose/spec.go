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
	if len(data) > 0 && data[0] == '"' {
		var name string
		if err := json.Unmarshal(data, &name); err != nil {
			return err
		}
		c.Module = name
		return nil
	}
	type callAlias Call
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

// Placement is one explicit landmark placement.
type Placement struct {
	Call
	At    [2]float64 `json:"at"`
	Rot   float64    `json:"rot"`
	Scale float64    `json:"scale"` // 0 -> 1.0
}

func (p *Placement) UnmarshalJSON(data []byte) error {
	type placementAlias struct {
		Module string     `json:"module"`
		Args   string     `json:"args"`
		At     [2]float64 `json:"at"`
		Rot    float64    `json:"rot"`
		Scale  float64    `json:"scale"`
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
	Children []Call     `json:"children"`
}

// ScatterRule scatters children in a region.
type ScatterRule struct {
	Region   [4]float64 `json:"region"` // x0, x1, y0, y1
	N        int        `json:"n"`
	Seed     int        `json:"seed"`
	Rot      *bool      `json:"rot"` // default true
	Children []Call     `json:"children"`
}

// AlongRule places children along a polyline.
type AlongRule struct {
	Pts      [][2]float64 `json:"pts"`
	Step     float64      `json:"step"`
	Offset   float64      `json:"offset"`
	Seed     int          `json:"seed"`
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
	BlockTypes map[string][]Call `json:"blockTypes"`
	BlockGrids []BlockGrid       `json:"blockGrids"`
	Placements []Placement       `json:"placements"`
	Grids      []GridRule        `json:"grids"`
	Rows       []RowRule         `json:"rows"`
	Rings      []RingRule        `json:"rings"`
	Scatters   []ScatterRule     `json:"scatters"`
	Alongs     []AlongRule       `json:"alongs"`
}

// LoadSpec reads and parses a spec file.
func LoadSpec(path string) (*Spec, error) {
	raw, err := os.ReadFile(path)
	if err != nil {
		return nil, err
	}
	decoder := json.NewDecoder(strings.NewReader(string(raw)))
	decoder.DisallowUnknownFields()
	spec := &Spec{}
	if err := decoder.Decode(spec); err != nil {
		return nil, fmt.Errorf("%s: %w", path, err)
	}
	if spec.Name == "" {
		return nil, fmt.Errorf("%s: spec has no \"name\"", path)
	}
	if spec.Fn == 0 {
		spec.Fn = 12
	}
	return spec, nil
}
