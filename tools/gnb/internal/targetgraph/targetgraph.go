package targetgraph

import (
	"fmt"
	"io"
	"os"
	"os/exec"
	"path/filepath"
	"regexp"
	"sort"
	"strings"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/console"
)

type Options struct {
	RepoRoot  string
	Preset    string
	CMakePath string
	Target    string
	Format    string
	Output    string
	Dependers bool
	PrintCmd  bool
}

type Data struct {
	Nodes []Node `json:"nodes"`
	Edges []Edge `json:"edges"`
}

type Node struct {
	ID    string `json:"id"`
	Label string `json:"label"`
	Shape string `json:"shape"`
	Kind  string `json:"kind"`
}

type Edge struct {
	From  string `json:"from"`
	To    string `json:"to"`
	Style string `json:"style"`
	Kind  string `json:"kind"`
}

func Run(opts Options) error {
	if opts.Format == "" {
		opts.Format = "svg"
	}
	opts.Format = strings.ToLower(opts.Format)
	if !isSupportedFormat(opts.Format) {
		return fmt.Errorf("unsupported graph format %q; use svg, png, pdf, or dot", opts.Format)
	}
	if opts.Preset == "" {
		return fmt.Errorf("missing CMake preset")
	}
	if opts.CMakePath == "" {
		opts.CMakePath = "cmake"
	}

	buildDir := filepath.Join(opts.RepoRoot, "out", "build", opts.Preset)
	if _, err := os.Stat(filepath.Join(buildDir, "CMakeCache.txt")); err != nil {
		return fmt.Errorf("build directory is not configured: %s; run `gnb build --reconfigure` first", buildDir)
	}

	graphDir := filepath.Join(buildDir, "graphs")
	if !opts.PrintCmd {
		if err := os.MkdirAll(graphDir, 0o755); err != nil {
			return err
		}
	}

	baseDot := filepath.Join(graphDir, "cmake-targets.dot")
	if err := run(opts.RepoRoot, opts.PrintCmd, opts.CMakePath, "--graphviz="+baseDot, buildDir); err != nil {
		return err
	}

	selectedDot, err := selectDot(baseDot, graphDir, opts.Target, opts.Dependers, opts.PrintCmd)
	if err != nil {
		return err
	}

	outPath := opts.Output
	if outPath == "" {
		outPath = defaultOutputPath(graphDir, opts.Target, opts.Dependers, opts.Format)
	}
	if !filepath.IsAbs(outPath) {
		outPath = filepath.Join(opts.RepoRoot, outPath)
	}

	if opts.Format == "dot" {
		if err := copyFile(selectedDot, outPath, opts.PrintCmd); err != nil {
			return err
		}
		console.Success("wrote %s", outPath)
		return nil
	}

	dotPath, ok := findDot()
	if !ok {
		fallback := strings.TrimSuffix(outPath, filepath.Ext(outPath)) + ".dot"
		if err := copyFile(selectedDot, fallback, opts.PrintCmd); err != nil {
			return err
		}
		console.Warn("Graphviz `dot` not found; wrote DOT instead: %s", fallback)
		console.Info("install Graphviz and rerun to render %s", opts.Format)
		return nil
	}

	if !opts.PrintCmd {
		if err := os.MkdirAll(filepath.Dir(outPath), 0o755); err != nil {
			return err
		}
	}
	if err := run(opts.RepoRoot, opts.PrintCmd, dotPath, "-T"+opts.Format, selectedDot, "-o", outPath); err != nil {
		return err
	}
	console.Success("wrote %s", outPath)
	return nil
}

func LoadData(dotPath string) (Data, error) {
	graph, err := parseDotGraph(dotPath)
	if err != nil {
		return Data{}, err
	}
	graph = pruneRedundantEngineEdges(graph)

	ids := make([]string, 0, len(graph.nodes))
	for id := range graph.nodes {
		ids = append(ids, id)
	}
	sort.Slice(ids, func(i, j int) bool {
		return graph.nodes[ids[i]].label < graph.nodes[ids[j]].label
	})

	data := Data{
		Nodes: make([]Node, 0, len(ids)),
		Edges: make([]Edge, 0, len(graph.edges)),
	}
	for _, id := range ids {
		if strings.HasPrefix(id, "legendNode") {
			continue
		}
		node := graph.nodes[id]
		data.Nodes = append(data.Nodes, Node{
			ID:    node.id,
			Label: node.label,
			Shape: node.shape,
			Kind:  kindForShape(node.shape),
		})
	}
	for _, edge := range graph.edges {
		if strings.HasPrefix(edge.from, "legendNode") || strings.HasPrefix(edge.to, "legendNode") {
			continue
		}
		if _, ok := graph.nodes[edge.from]; !ok {
			continue
		}
		if _, ok := graph.nodes[edge.to]; !ok {
			continue
		}
		data.Edges = append(data.Edges, Edge{
			From:  graph.nodes[edge.from].label,
			To:    graph.nodes[edge.to].label,
			Style: edge.style,
			Kind:  kindForStyle(edge.style),
		})
	}
	return data, nil
}

func GenerateCMakeGraph(repoRoot string, cmakePath string, buildDir string, outDot string, printOnly bool) error {
	return run(repoRoot, printOnly, cmakePath, "--graphviz="+outDot, buildDir)
}

func selectDot(baseDot string, graphDir string, target string, dependers bool, printOnly bool) (string, error) {
	if target == "" {
		if dependers {
			return "", fmt.Errorf("--dependers requires a target")
		}
		path := filepath.Join(graphDir, "all-targets.filtered.dot")
		if printOnly {
			return path, nil
		}
		if err := writeAllDot(baseDot, path); err != nil {
			return "", err
		}
		return path, nil
	}

	path := filepath.Join(graphDir, target+"-deps.filtered.dot")
	if dependers {
		path = filepath.Join(graphDir, target+"-dependers.filtered.dot")
	}
	if printOnly {
		return path, nil
	}
	if err := writeFilteredDot(baseDot, path, target, dependers); err != nil {
		return "", err
	}
	return path, nil
}

type dotGraph struct {
	nodes map[string]dotNode
	edges []dotEdge
}

type dotNode struct {
	id    string
	label string
	shape string
	line  string
}

type dotEdge struct {
	from  string
	to    string
	style string
	line  string
}

var (
	dotNodeRE  = regexp.MustCompile(`^\s*"([^"]+)"\s+\[\s+label\s+=\s+"((?:[^"\\]|\\.)*)"(.*)\];`)
	dotEdgeRE  = regexp.MustCompile(`^\s*"([^"]+)"\s*->\s*"([^"]+)"(?:\s|$)`)
	dotShapeRE = regexp.MustCompile(`shape\s*=\s*([A-Za-z0-9_]+)`)
	dotStyleRE = regexp.MustCompile(`style\s*=\s*([A-Za-z0-9_]+)`)
)

func writeFilteredDot(baseDot string, outPath string, target string, dependers bool) error {
	graph, err := parseDotGraph(baseDot)
	if err != nil {
		return err
	}
	graph = pruneRedundantEngineEdges(graph)

	startID := ""
	for id, node := range graph.nodes {
		if node.label == target {
			startID = id
			break
		}
	}
	if startID == "" {
		names := make([]string, 0, len(graph.nodes))
		for _, node := range graph.nodes {
			names = append(names, node.label)
		}
		sort.Strings(names)
		return fmt.Errorf("target %q not found in CMake graph; available examples: %s", target, strings.Join(firstN(names, 12), ", "))
	}

	keep := reachableNodes(graph.edges, startID, dependers)
	if err := os.MkdirAll(filepath.Dir(outPath), 0o755); err != nil {
		return err
	}
	return os.WriteFile(outPath, []byte(renderFilteredDot(target, graph, keep)), 0o644)
}

func writeAllDot(baseDot string, outPath string) error {
	graph, err := parseDotGraph(baseDot)
	if err != nil {
		return err
	}
	graph = pruneRedundantEngineEdges(graph)

	keep := make(map[string]bool, len(graph.nodes))
	for id := range graph.nodes {
		keep[id] = true
	}
	if err := os.MkdirAll(filepath.Dir(outPath), 0o755); err != nil {
		return err
	}
	return os.WriteFile(outPath, []byte(renderFilteredDot("all-targets", graph, keep)), 0o644)
}

func parseDotGraph(path string) (dotGraph, error) {
	data, err := os.ReadFile(path)
	if err != nil {
		return dotGraph{}, err
	}
	graph := dotGraph{
		nodes: map[string]dotNode{},
	}
	for _, line := range strings.Split(string(data), "\n") {
		if match := dotNodeRE.FindStringSubmatch(line); match != nil {
			graph.nodes[match[1]] = dotNode{
				id:    match[1],
				label: unescapeDotLabel(match[2]),
				shape: parseDotAttr(dotShapeRE, match[3]),
				line:  line,
			}
			continue
		}
		if match := dotEdgeRE.FindStringSubmatch(line); match != nil {
			graph.edges = append(graph.edges, dotEdge{from: match[1], to: match[2], style: parseDotAttr(dotStyleRE, line), line: line})
		}
	}
	return graph, nil
}

func pruneRedundantEngineEdges(graph dotGraph) dotGraph {
	engineID := ""
	for id, node := range graph.nodes {
		if node.label == "gkNextEngine" {
			engineID = id
			break
		}
	}
	if engineID == "" {
		return graph
	}

	engineDependencyKeys := map[string]bool{}
	for _, edge := range graph.edges {
		if edge.from != engineID {
			continue
		}
		if node, ok := graph.nodes[edge.to]; ok {
			engineDependencyKeys[dependencyKey(node.label)] = true
		}
	}
	if len(engineDependencyKeys) == 0 {
		return graph
	}

	dependsOnEngine := map[string]bool{}
	for _, edge := range graph.edges {
		if edge.to == engineID {
			dependsOnEngine[edge.from] = true
		}
	}

	filtered := graph
	filtered.edges = make([]dotEdge, 0, len(graph.edges))
	for _, edge := range graph.edges {
		fromNode, fromOK := graph.nodes[edge.from]
		toNode, toOK := graph.nodes[edge.to]
		if fromOK && toOK &&
			fromNode.shape == "egg" &&
			edge.from != engineID &&
			edge.to != engineID &&
			dependsOnEngine[edge.from] &&
			engineDependencyKeys[dependencyKey(toNode.label)] {
			continue
		}
		filtered.edges = append(filtered.edges, edge)
	}
	return filtered
}

func dependencyKey(label string) string {
	lower := strings.ToLower(filepath.ToSlash(label))
	switch {
	case strings.Contains(lower, "sl.interposer"):
		return "sl.interposer"
	case strings.HasSuffix(lower, "/avif.lib") || lower == "avif":
		return "avif"
	default:
		return label
	}
}

func reachableNodes(edges []dotEdge, startID string, reverse bool) map[string]bool {
	keep := map[string]bool{startID: true}
	queue := []string{startID}
	for len(queue) > 0 {
		current := queue[0]
		queue = queue[1:]
		for _, edge := range edges {
			from, to := edge.from, edge.to
			if reverse {
				from, to = edge.to, edge.from
			}
			if from != current || keep[to] {
				continue
			}
			keep[to] = true
			queue = append(queue, to)
		}
	}
	return keep
}

func renderFilteredDot(name string, graph dotGraph, keep map[string]bool) string {
	var b strings.Builder
	b.WriteString("digraph \"")
	b.WriteString(escapeDotID(name))
	b.WriteString("\" {\n")
	b.WriteString("node [\n  fontsize = \"12\"\n];\n")

	nodeIDs := make([]string, 0, len(keep))
	for id := range keep {
		nodeIDs = append(nodeIDs, id)
	}
	sort.Strings(nodeIDs)
	for _, id := range nodeIDs {
		if node, ok := graph.nodes[id]; ok {
			b.WriteString(node.line)
			b.WriteString("\n")
		}
	}

	for _, edge := range graph.edges {
		if keep[edge.from] && keep[edge.to] {
			b.WriteString(edge.line)
			b.WriteString("\n")
		}
	}
	b.WriteString("}\n")
	return b.String()
}

func unescapeDotLabel(value string) string {
	value = strings.ReplaceAll(value, `\n`, "\n")
	value = strings.ReplaceAll(value, `\"`, `"`)
	value = strings.ReplaceAll(value, `\\`, `\`)
	return value
}

func escapeDotID(value string) string {
	return strings.ReplaceAll(value, `"`, `\"`)
}

func parseDotAttr(re *regexp.Regexp, value string) string {
	match := re.FindStringSubmatch(value)
	if match == nil {
		return ""
	}
	return match[1]
}

func kindForShape(shape string) string {
	switch shape {
	case "egg":
		return "Executable"
	case "octagon":
		return "Static Library"
	case "doubleoctagon":
		return "Shared Library"
	case "tripleoctagon":
		return "Module Library"
	case "pentagon":
		return "Interface Library"
	case "hexagon":
		return "Object Library"
	case "septagon":
		return "Unknown Library"
	case "box":
		return "Custom Target"
	default:
		return "Target"
	}
}

func kindForStyle(style string) string {
	switch style {
	case "dashed":
		return "Interface"
	case "dotted":
		return "Private"
	default:
		return "Link"
	}
}

func defaultOutputPath(graphDir string, target string, dependers bool, format string) string {
	name := "all-targets"
	if target != "" {
		name = target + "-deps"
		if dependers {
			name = target + "-dependers"
		}
	}
	return filepath.Join(graphDir, name+"."+format)
}

func copyFile(src string, dst string, printOnly bool) error {
	console.CommandLine(fmt.Sprintf("copy %s %s", src, dst))
	if printOnly {
		return nil
	}
	if err := os.MkdirAll(filepath.Dir(dst), 0o755); err != nil {
		return err
	}
	in, err := os.Open(src)
	if err != nil {
		return err
	}
	defer in.Close()

	out, err := os.Create(dst)
	if err != nil {
		return err
	}
	defer out.Close()

	if _, err := io.Copy(out, in); err != nil {
		return err
	}
	return out.Close()
}

func findDot() (string, bool) {
	if path, err := exec.LookPath("dot"); err == nil {
		return path, true
	}
	if programFiles := os.Getenv("ProgramFiles"); programFiles != "" {
		path := filepath.Join(programFiles, "Graphviz", "bin", "dot.exe")
		if _, err := os.Stat(path); err == nil {
			return path, true
		}
	}
	return "", false
}

func run(dir string, printOnly bool, name string, args ...string) error {
	console.CommandLine(strings.TrimSpace(name + " " + strings.Join(args, " ")))
	if printOnly {
		return nil
	}
	cmd := exec.Command(name, args...)
	cmd.Dir = dir
	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr
	cmd.Stdin = os.Stdin
	if err := cmd.Run(); err != nil {
		return fmt.Errorf("%s failed: %w", name, err)
	}
	return nil
}

func firstN(values []string, n int) []string {
	if len(values) <= n {
		return values
	}
	return values[:n]
}

func isSupportedFormat(format string) bool {
	switch format {
	case "dot", "svg", "png", "pdf":
		return true
	default:
		return false
	}
}
