package paks

import (
	"testing"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/config"
)

func TestSelectedGroupsMapsLegacySfxToMagicaLego(t *testing.T) {
	selected := selectedGroups(config.Config{}, []string{"sfx"})
	if !selected["sfx"] {
		t.Fatalf("expected legacy sfx selector to remain selected")
	}
	if !selected["magicalego"] {
		t.Fatalf("expected legacy sfx selector to also select magicalego")
	}
}
