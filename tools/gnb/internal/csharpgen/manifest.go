package csharpgen

import (
	"encoding/json"
	"fmt"
	"os"
)

// ManifestPath is the committed reflection snapshot that drives Components.g.cs.
//
// It is committed rather than produced during the build on purpose: generating it would make code
// generation depend on a working engine binary, and `gnb csharpgen --check` has to be able to run
// before — and independently of — the build it guards. Refresh it with `gnb csharpgen --refresh`,
// which runs `gkNextRenderer --dump-reflection`. A unit test compares the committed file against
// live reflection, so a stale snapshot fails the test suite rather than silently generating
// wrappers for properties that no longer exist.
const ManifestPath = "src/Modules/NextDotNet/ReflectionManifest.json"

// Manifest is the parsed form of ReflectionManifest.json.
type Manifest struct {
	Version int             `json:"version"`
	Types   []ReflectedType `json:"types"`
}

// ReflectedType is one component (or the node itself) with its reflected properties.
type ReflectedType struct {
	Name       string              `json:"name"`
	TypeID     uint32              `json:"typeId"`
	Kind       string              `json:"kind"` // "component" or "node"
	Properties []ReflectedProperty `json:"properties"`
}

// ReflectedProperty is one entt::meta data member.
type ReflectedProperty struct {
	Name          string  `json:"name"`
	PropID        uint32  `json:"propId"`
	Type          string  `json:"type"`
	ReadOnly      bool    `json:"readOnly"`
	Hidden        bool    `json:"hidden"`
	ScriptExposed bool    `json:"scriptExposed"`
	DisplayName   string  `json:"displayName"`
	Category      string  `json:"category"`
	Tooltip       string  `json:"tooltip"`
	ElementType   string  `json:"elementType"`
	Min           float64 `json:"min"`
	Max           float64 `json:"max"`
}

// IsNode reports whether this entry describes the node itself rather than a component. Node
// properties are emitted directly onto NodeRef, because a node handle already *is* the node.
func (t ReflectedType) IsNode() bool { return t.Kind == "node" }

// ParseManifest reads and validates the committed reflection manifest.
func ParseManifest(path string) (Manifest, error) {
	data, err := os.ReadFile(path)
	if err != nil {
		return Manifest{}, err
	}

	var manifest Manifest
	if err := json.Unmarshal(data, &manifest); err != nil {
		return Manifest{}, fmt.Errorf("%s: %w", ManifestPath, err)
	}
	if manifest.Version != 1 {
		return Manifest{}, fmt.Errorf("%s: unsupported manifest version %d", ManifestPath, manifest.Version)
	}
	if len(manifest.Types) == 0 {
		return Manifest{}, fmt.Errorf("%s: no reflected types", ManifestPath)
	}

	nodeCount := 0
	for _, reflected := range manifest.Types {
		if reflected.Name == "" {
			return Manifest{}, fmt.Errorf("%s: a type has no name", ManifestPath)
		}
		switch reflected.Kind {
		case "component":
		case "node":
			nodeCount++
		default:
			return Manifest{}, fmt.Errorf("%s: %s has unknown kind %q", ManifestPath, reflected.Name, reflected.Kind)
		}
	}
	// More than one would mean two types claim to be the node, and the generator would emit
	// conflicting members on NodeRef.
	if nodeCount > 1 {
		return Manifest{}, fmt.Errorf("%s: %d types are marked as the node kind", ManifestPath, nodeCount)
	}
	return manifest, nil
}
