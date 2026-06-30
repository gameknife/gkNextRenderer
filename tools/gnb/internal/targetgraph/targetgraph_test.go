package targetgraph

import (
	"os"
	"path/filepath"
	"reflect"
	"strings"
	"testing"
)

func TestSelectDot(t *testing.T) {
	dir := t.TempDir()
	base := filepath.Join(dir, "cmake-targets.dot")
	writeFile(t, base, `digraph "test" {
    "node0" [ label = "gkNextEditor", shape = egg ];
    "node1" [ label = "gkNextEngine", shape = octagon ];
    "node2" [ label = "Tool", shape = octagon ];
    "node0" -> "node1" [ style = dotted ] // gkNextEditor -> gkNextEngine
    "node2" -> "node0" [ style = dotted ] // Tool -> gkNextEditor
}
`)

	got, err := selectDot(base, dir, "", false, false)
	if err != nil {
		t.Fatalf("select all: %v", err)
	}
	if got == base {
		t.Fatalf("select all returned unnormalized base graph")
	}
	if data := readFile(t, got); !strings.Contains(data, "gkNextEditor") || !strings.Contains(data, "gkNextEngine") {
		t.Fatalf("select all produced unexpected graph:\n%s", data)
	}

	got, err = selectDot(base, dir, "gkNextEditor", false, false)
	if err != nil {
		t.Fatalf("select target: %v", err)
	}
	data := readFile(t, got)
	if !strings.Contains(data, "gkNextEngine") || strings.Contains(data, "Tool") {
		t.Fatalf("dependency filter produced unexpected graph:\n%s", data)
	}

	got, err = selectDot(base, dir, "gkNextEditor", true, false)
	if err != nil {
		t.Fatalf("select dependers: %v", err)
	}
	data = readFile(t, got)
	if !strings.Contains(data, "Tool") || strings.Contains(data, "gkNextEngine") {
		t.Fatalf("dependers filter produced unexpected graph:\n%s", data)
	}
}

func TestSelectDotMissingListsExamples(t *testing.T) {
	dir := t.TempDir()
	base := filepath.Join(dir, "cmake-targets.dot")
	writeFile(t, base, `digraph "test" {
    "node0" [ label = "A", shape = egg ];
    "node1" [ label = "B", shape = egg ];
}
`)

	_, err := selectDot(base, dir, "Missing", false, false)
	if err == nil || !strings.Contains(err.Error(), "A, B") {
		t.Fatalf("expected available target examples, got %v", err)
	}
}

func TestParseDotGraph(t *testing.T) {
	path := filepath.Join(t.TempDir(), "graph.dot")
	writeFile(t, path, `digraph "test" {
    "node0" [ label = "A\nAlias", shape = egg ];
    "node1" [ label = "B", shape = octagon ];
    "node0" -> "node1" [ style = dotted ] // A -> B
}`)
	graph, err := parseDotGraph(path)
	if err != nil {
		t.Fatalf("parseDotGraph: %v", err)
	}
	if got := graph.nodes["node0"].label; got != "A\nAlias" {
		t.Fatalf("node label = %q", got)
	}
	if want := []dotEdge{{from: "node0", to: "node1", style: "dotted", line: `    "node0" -> "node1" [ style = dotted ] // A -> B`}}; !reflect.DeepEqual(graph.edges, want) {
		t.Fatalf("edges = %#v, want %#v", graph.edges, want)
	}
}

func TestLoadData(t *testing.T) {
	path := filepath.Join(t.TempDir(), "graph.dot")
	writeFile(t, path, `digraph "test" {
    "node0" [ label = "App", shape = egg ];
    "node1" [ label = "Lib", shape = octagon ];
    "node0" -> "node1" [ style = dotted ] // App -> Lib
}`)
	data, err := LoadData(path)
	if err != nil {
		t.Fatalf("LoadData: %v", err)
	}
	if len(data.Nodes) != 2 || data.Nodes[0].Label != "App" || data.Nodes[0].Kind != "Executable" {
		t.Fatalf("unexpected nodes: %#v", data.Nodes)
	}
	if want := []Edge{{From: "App", To: "Lib", Style: "dotted", Kind: "Private"}}; !reflect.DeepEqual(data.Edges, want) {
		t.Fatalf("edges = %#v, want %#v", data.Edges, want)
	}
}

func TestPruneRedundantEngineEdges(t *testing.T) {
	path := filepath.Join(t.TempDir(), "graph.dot")
	writeFile(t, path, `digraph "test" {
    "node0" [ label = "App", shape = egg ];
    "node1" [ label = "gkNextEngine", shape = octagon ];
    "node2" [ label = "Jolt::Jolt", shape = octagon ];
    "node3" [ label = "P:/repo/external/streamline/lib/x64/sl.interposer.lib", shape = septagon ];
    "node4" [ label = "sl.interposer", shape = octagon ];
    "node5" [ label = "FeatureModule", shape = octagon ];
    "node0" -> "node1" [ style = dotted ] // App -> gkNextEngine
    "node0" -> "node2" [ style = dotted ] // App -> Jolt::Jolt
    "node0" -> "node4" [ style = dotted ] // App -> sl.interposer
    "node0" -> "node5" [ style = dotted ] // App -> FeatureModule
    "node1" -> "node2" [ style = dotted ] // gkNextEngine -> Jolt::Jolt
    "node1" -> "node3" [ style = dotted ] // gkNextEngine -> P:/repo/external/streamline/lib/x64/sl.interposer.lib
}`)
	data, err := LoadData(path)
	if err != nil {
		t.Fatalf("LoadData: %v", err)
	}
	for _, edge := range data.Edges {
		if edge.From == "App" && (edge.To == "Jolt::Jolt" || edge.To == "sl.interposer") {
			t.Fatalf("redundant engine dependency edge was not pruned: %#v", edge)
		}
	}
	if !hasEdge(data.Edges, "App", "gkNextEngine") || !hasEdge(data.Edges, "App", "FeatureModule") {
		t.Fatalf("expected app architecture edges to remain: %#v", data.Edges)
	}
}

func TestDefaultOutputPath(t *testing.T) {
	dir := filepath.Join("build", "graphs")
	if got := defaultOutputPath(dir, "", false, "svg"); got != filepath.Join(dir, "all-targets.svg") {
		t.Fatalf("default all output = %q", got)
	}
	if got := defaultOutputPath(dir, "gkNextEngine", false, "png"); got != filepath.Join(dir, "gkNextEngine-deps.png") {
		t.Fatalf("default target output = %q", got)
	}
	if got := defaultOutputPath(dir, "gkNextEngine", true, "dot"); got != filepath.Join(dir, "gkNextEngine-dependers.dot") {
		t.Fatalf("default dependers output = %q", got)
	}
}

func writeFile(t *testing.T, path string, contents string) {
	t.Helper()
	if err := os.WriteFile(path, []byte(contents), 0o644); err != nil {
		t.Fatalf("write %s: %v", path, err)
	}
}

func readFile(t *testing.T, path string) string {
	t.Helper()
	data, err := os.ReadFile(path)
	if err != nil {
		t.Fatalf("read %s: %v", path, err)
	}
	return string(data)
}

func hasEdge(edges []Edge, from string, to string) bool {
	for _, edge := range edges {
		if edge.From == from && edge.To == to {
			return true
		}
	}
	return false
}
