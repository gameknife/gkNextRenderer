// Package csharpgen turns src/Modules/NextDotNet/EngineApi.def.h into the managed side's
// Engine.g.cs. The def file is the single source of truth for the binding surface (design 4.1);
// this package is the only thing allowed to write the generated C#.
package csharpgen

import (
	"fmt"
	"os"
	"regexp"
	"strings"
)

// Entry is one GK_API line from the def file.
type Entry struct {
	Namespace string
	Name      string
	Return    string
	Params    []Param
	Defaults  map[string]string
}

// Symbol is the C++ field name in FEngineApi, e.g. "UI_DrawText".
func (e Entry) Symbol() string { return e.Namespace + "_" + e.Name }

// Param is one declared parameter, with its C++ type split into constness and pointer-ness.
type Param struct {
	Type    string // as written, e.g. "const FVec3*"
	Name    string
	IsConst bool
	IsPtr   bool
	Base    string // type without const/pointer, e.g. "FVec3"
}

var (
	apiLine   = regexp.MustCompile(`^\s*GK_API\s*\(([^,]+),\s*([^,]+),\s*([^,]+),\s*\((.*)\)\s*\)\s*(?://=\s*(.*))?$`)
	spaceRuns = regexp.MustCompile(`\s+`)
)

// ParseFile reads a def file and returns its entries in declaration order. Order is significant:
// it is the field order of FEngineApi, so the generated struct must preserve it exactly.
func ParseFile(path string) ([]Entry, error) {
	data, err := os.ReadFile(path)
	if err != nil {
		return nil, err
	}
	return Parse(string(data))
}

// Parse extracts entries from def-file text.
func Parse(text string) ([]Entry, error) {
	var entries []Entry
	for lineNumber, line := range strings.Split(text, "\n") {
		line = strings.TrimRight(line, "\r")
		match := apiLine.FindStringSubmatch(line)
		if match == nil {
			continue
		}

		entry := Entry{
			Namespace: strings.TrimSpace(match[1]),
			Name:      strings.TrimSpace(match[2]),
			Return:    strings.TrimSpace(match[3]),
			Defaults:  map[string]string{},
		}

		params, err := parseParams(match[4])
		if err != nil {
			return nil, fmt.Errorf("%s:%d: %w", path(text), lineNumber+1, err)
		}
		entry.Params = params

		if defaults := strings.TrimSpace(match[5]); defaults != "" {
			for _, pair := range strings.Split(defaults, ",") {
				name, value, ok := strings.Cut(strings.TrimSpace(pair), ":")
				if !ok {
					return nil, fmt.Errorf("line %d: malformed default %q (want name:value)", lineNumber+1, pair)
				}
				entry.Defaults[strings.TrimSpace(name)] = strings.TrimSpace(value)
			}
		}

		if err := entry.validate(); err != nil {
			return nil, fmt.Errorf("line %d: %s: %w", lineNumber+1, entry.Symbol(), err)
		}
		entries = append(entries, entry)
	}

	if len(entries) == 0 {
		return nil, fmt.Errorf("no GK_API entries found")
	}
	return entries, nil
}

// path is a placeholder so error messages stay useful when Parse is called on in-memory text.
func path(string) string { return "EngineApi.def.h" }

func parseParams(text string) ([]Param, error) {
	text = strings.TrimSpace(text)
	if text == "" {
		return nil, nil
	}

	var params []Param
	for _, chunk := range strings.Split(text, ",") {
		chunk = spaceRuns.ReplaceAllString(strings.TrimSpace(chunk), " ")
		if chunk == "" {
			return nil, fmt.Errorf("empty parameter")
		}

		fields := strings.Split(chunk, " ")
		if len(fields) < 2 {
			return nil, fmt.Errorf("parameter %q has no name", chunk)
		}

		name := fields[len(fields)-1]
		typeText := strings.Join(fields[:len(fields)-1], " ")

		// "FVec2* outSize" and "FVec2 *outSize" both mean the same thing; normalise the star onto
		// the type so the rest of the generator only has to look at Param.IsPtr.
		if strings.HasPrefix(name, "*") {
			name = strings.TrimPrefix(name, "*")
			typeText += "*"
		}

		param := Param{Type: typeText, Name: name}
		param.IsConst = strings.HasPrefix(typeText, "const ")
		param.IsPtr = strings.HasSuffix(typeText, "*")
		param.Base = strings.TrimSuffix(strings.TrimPrefix(typeText, "const "), "*")
		param.Base = strings.TrimSpace(param.Base)
		params = append(params, param)
	}
	return params, nil
}

func (e Entry) validate() error {
	if e.Namespace == "" || e.Name == "" || e.Return == "" {
		return fmt.Errorf("namespace, name and return type are all required")
	}
	// The type rules in the def file header are enforced here rather than trusted, because a
	// violation only shows up as a NativeAOT failure or, worse, as silent memory corruption.
	if e.Return == "bool" {
		return fmt.Errorf("bool must not cross the boundary; use GkBool")
	}
	for _, param := range e.Params {
		if param.Base == "bool" {
			return fmt.Errorf("parameter %q: bool must not cross the boundary; use GkBool", param.Name)
		}
		if _, known := rawTypes[param.Base]; !known {
			return fmt.Errorf("parameter %q: unknown type %q", param.Name, param.Type)
		}
		if param.IsPtr && !param.IsConst && !strings.HasPrefix(param.Name, "out") && param.Base != "char" && param.Base != "uint8_t" {
			return fmt.Errorf("parameter %q: non-const pointer must be an out-parameter named out*", param.Name)
		}
	}
	for name := range e.Defaults {
		if !e.hasParam(name) {
			return fmt.Errorf("default declared for unknown parameter %q", name)
		}
	}
	return nil
}

func (e Entry) hasParam(name string) bool {
	for _, param := range e.Params {
		if param.Name == name {
			return true
		}
	}
	return false
}
