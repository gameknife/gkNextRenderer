// Package scadgen drives the optional LLM scene-generation loop documented in
// docs/designs/scad-scene-compose-design.md: the kit catalog becomes a parts
// menu in the prompt, the model answers with scene-spec JSON, and compose
// validation errors are fed back for self-repair until the spec expands cleanly.
package scadgen

import (
	"encoding/json"
	"fmt"
	"os"
	"strings"
)

// menuCatalog is the slice of catalog.json that the prompt menu needs.
type menuCatalog struct {
	Kits []struct {
		Name       string `json:"name"`
		ScaleClass string `json:"scaleClass"`
		Modules    []struct {
			Name      string    `json:"name"`
			Category  string    `json:"category"`
			Params    string    `json:"params"`
			Footprint []float64 `json:"footprint"`
			Height    float64   `json:"height"`
			Ok        bool      `json:"ok"`
		} `json:"modules"`
	} `json:"kits"`
}

// BuildKitMenu renders the catalog as a compact parts menu for the prompt:
// one line per module with signature and default-args bounding size. Modules
// without default-arg geometry (ok=false) are omitted — the model cannot know
// their mandatory params reliably.
func BuildKitMenu(catalogPath string) (string, error) {
	raw, err := os.ReadFile(catalogPath)
	if err != nil {
		return "", fmt.Errorf("cannot read kit catalog (run `gnb scad catalog` first): %w", err)
	}
	var catalog menuCatalog
	if err := json.Unmarshal(raw, &catalog); err != nil {
		return "", fmt.Errorf("%s: %w", catalogPath, err)
	}

	var b strings.Builder
	for _, kit := range catalog.Kits {
		short := strings.TrimPrefix(kit.Name, "kit_")
		fmt.Fprintf(&b, "## kit \"%s\" (scaleClass %s)\n", short, kit.ScaleClass)
		currentCategory := ""
		for _, module := range kit.Modules {
			if !module.Ok {
				continue
			}
			if module.Category != currentCategory {
				currentCategory = module.Category
				fmt.Fprintf(&b, "[%s]\n", currentCategory)
			}
			size := ""
			if len(module.Footprint) == 2 {
				size = fmt.Sprintf(" %gx%g h%g", module.Footprint[0], module.Footprint[1], module.Height)
			}
			fmt.Fprintf(&b, "%s(%s)%s\n", module.Name, module.Params, size)
		}
		b.WriteString("\n")
	}
	return b.String(), nil
}
