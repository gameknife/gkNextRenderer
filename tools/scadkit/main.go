// Deprecated one-time migration utility. It originally split reusable kit
// libraries from theme scenes during the initial SCAD scene-compose migration.
// Some of those input scenes have since been removed, so this program is not a current
// regeneration path and must not be run as repository maintenance. The
// checked-in assets/scad/lib/kit_*.scad files are now the source of truth.
//
// For each job it:
//   - splits the source into top-level items (comment/string aware),
//   - moves module/function definitions into assets/scad/lib/kit_<theme>.scad
//     with a namespace prefix (hc_/oc_/of_/ap_),
//   - converts top-level constants needed by kit code into zero-arg functions
//     (`GZT = 0.15;` -> `function hc_GZT() = 0.15;`) so the kit is self-contained
//     under `use <>` semantics (the loader drops top-level assigns of used files),
//   - rewrites the original scene file to `use` the kit, keeping gameplay anchor
//     modules (name pattern *_NN, looked up by games via scene node names) and
//     the assembly statements in their original order.
//
package main

import (
	"fmt"
	"os"
	"path/filepath"
	"regexp"
	"sort"
	"strings"
)

type itemKind int

const (
	kindUse itemKind = iota
	kindAssign
	kindSpecialAssign // $fn = ...
	kindModule
	kindFunction
	kindStmt
)

type item struct {
	kind itemKind
	name string // assign/module/function name
	lead string // leading trivia (comments + blank lines)
	body string // first token through terminating ';'/'}' + trailing same-line comment
}

// ---------------------------------------------------------------------------
// Scanner
// ---------------------------------------------------------------------------

// spanScan returns the ranges of string literals and comments in text.
func spanScan(text string) (strSpans, comSpans [][2]int) {
	i, n := 0, len(text)
	for i < n {
		c := text[i]
		if c == '"' {
			start := i
			i++
			for i < n && text[i] != '"' {
				if text[i] == '\\' {
					i++
				}
				i++
			}
			i++
			strSpans = append(strSpans, [2]int{start, min(i, n)})
			continue
		}
		if c == '/' && i+1 < n && text[i+1] == '/' {
			start := i
			for i < n && text[i] != '\n' {
				i++
			}
			comSpans = append(comSpans, [2]int{start, i})
			continue
		}
		if c == '/' && i+1 < n && text[i+1] == '*' {
			start := i
			i += 2
			for i+1 < n && !(text[i] == '*' && text[i+1] == '/') {
				i++
			}
			i += 2
			comSpans = append(comSpans, [2]int{start, min(i, n)})
			continue
		}
		i++
	}
	return
}

func inSpans(spans [][2]int, pos int) bool {
	for _, s := range spans {
		if pos >= s[0] && pos < s[1] {
			return true
		}
	}
	return false
}

// splitItems splits scad source into top-level items with leading trivia.
func splitItems(text string) ([]item, error) {
	strSpans, comSpans := spanScan(text)
	skip := func(pos int) bool { return inSpans(strSpans, pos) || inSpans(comSpans, pos) }

	var items []item
	i, n := 0, len(text)
	for i < n {
		// Leading trivia: whitespace and comments.
		leadStart := i
		for i < n {
			if text[i] == ' ' || text[i] == '\t' || text[i] == '\r' || text[i] == '\n' {
				i++
				continue
			}
			if inSpans(comSpans, i) {
				for i < n && inSpans(comSpans, i) {
					i++
				}
				continue
			}
			break
		}
		if i >= n {
			// Trailing trivia attaches to a synthetic empty final item.
			if strings.TrimSpace(text[leadStart:]) != "" {
				items = append(items, item{kind: kindStmt, lead: text[leadStart:], body: ""})
			}
			break
		}
		lead := text[leadStart:i]

		// use/include directives have no ';' terminator: the item ends at '>'.
		if reUse.MatchString(text[i:]) {
			bodyStart := i
			for i < n && text[i] != '>' {
				i++
			}
			i++
			items = append(items, item{kind: kindUse, lead: lead, body: text[bodyStart:i]})
			continue
		}

		// Item body: scan to terminator at depth 0.
		bodyStart := i
		depth := 0
		for i < n {
			if skip(i) {
				i++
				continue
			}
			c := text[i]
			switch c {
			case '(', '[', '{':
				depth++
			case ')', ']':
				depth--
			case '}':
				depth--
				if depth == 0 {
					// Lookahead for else-continuation.
					j := i + 1
					for j < n && (text[j] == ' ' || text[j] == '\t' || text[j] == '\r' || text[j] == '\n' || inSpans(comSpans, j)) {
						j++
					}
					if strings.HasPrefix(text[j:], "else") {
						i = j
						continue
					}
					i++
					goto itemEnd
				}
			case ';':
				if depth == 0 {
					i++
					goto itemEnd
				}
			}
			i++
		}
	itemEnd:
		// Extend through a trailing same-line comment.
		{
			j := i
			for j < n && (text[j] == ' ' || text[j] == '\t') {
				j++
			}
			if j+1 < n && text[j] == '/' && text[j+1] == '/' {
				for j < n && text[j] != '\n' {
					j++
				}
				i = j
			}
		}
		body := text[bodyStart:i]
		it := classify(body)
		it.lead = lead
		it.body = body
		items = append(items, it)
	}
	return items, nil
}

var (
	reUse      = regexp.MustCompile(`^(use|include)\s*<`)
	reModule   = regexp.MustCompile(`^module\s+([A-Za-z_][A-Za-z0-9_]*)`)
	reFunction = regexp.MustCompile(`^function\s+([A-Za-z_][A-Za-z0-9_]*)`)
	reSpecial  = regexp.MustCompile(`^(\$[A-Za-z_][A-Za-z0-9_]*)\s*=`)
	reAssign   = regexp.MustCompile(`^([A-Za-z_][A-Za-z0-9_]*)\s*=`)
	reIdent    = regexp.MustCompile(`[A-Za-z_][A-Za-z0-9_]*`)
)

func classify(body string) item {
	switch {
	case reUse.MatchString(body):
		return item{kind: kindUse}
	case reModule.MatchString(body):
		return item{kind: kindModule, name: reModule.FindStringSubmatch(body)[1]}
	case reFunction.MatchString(body):
		return item{kind: kindFunction, name: reFunction.FindStringSubmatch(body)[1]}
	case reSpecial.MatchString(body):
		return item{kind: kindSpecialAssign, name: reSpecial.FindStringSubmatch(body)[1]}
	case reAssign.MatchString(body):
		return item{kind: kindAssign, name: reAssign.FindStringSubmatch(body)[1]}
	default:
		return item{kind: kindStmt}
	}
}

// ---------------------------------------------------------------------------
// Rename
// ---------------------------------------------------------------------------

// applyRenames replaces whole-word identifiers per the map, skipping string
// literals (comments are renamed on purpose so docs stay in sync).
func applyRenames(text string, renames map[string]string) string {
	if len(renames) == 0 {
		return text
	}
	strSpans, _ := spanScan(text)
	var out strings.Builder
	last := 0
	for _, loc := range reIdent.FindAllStringIndex(text, -1) {
		if inSpans(strSpans, loc[0]) {
			continue
		}
		// Whole-word check: reIdent is maximal-munch so boundaries are implicit.
		word := text[loc[0]:loc[1]]
		repl, ok := renames[word]
		if !ok {
			continue
		}
		out.WriteString(text[last:loc[0]])
		out.WriteString(repl)
		last = loc[1]
	}
	out.WriteString(text[last:])
	return out.String()
}

// wordRefs reports whether name occurs as a whole word outside strings.
func wordRefs(text, name string) bool {
	strSpans, _ := spanScan(text)
	for _, loc := range reIdent.FindAllStringIndex(text, -1) {
		if inSpans(strSpans, loc[0]) {
			continue
		}
		if text[loc[0]:loc[1]] == name {
			return true
		}
	}
	return false
}

// ---------------------------------------------------------------------------
// Job
// ---------------------------------------------------------------------------

type job struct {
	src          string
	kitOut       string
	consumerOut  string // "" = no consumer (pure library source)
	prefix       string // may be "" (keep already-namespaced names)
	kitName      string
	anchorRe     *regexp.Regexp    // module names to keep in the consumer
	extRenames   map[string]string // renames exported by previously processed kits
	deleteConsts map[string]bool   // consts to drop entirely (superseded by ext kit)
	kitUses      []string          // use directives inside the kit file
	consumerUses []string          // use directives injected into the consumer
	headerToKit  bool              // move the file header comment into the kit
}

type jobResult struct {
	renames    map[string]string
	constNames map[string]bool
}

func runJob(j job) (jobResult, error) {
	res := jobResult{renames: map[string]string{}, constNames: map[string]bool{}}
	raw, err := os.ReadFile(j.src)
	if err != nil {
		return res, err
	}
	text := strings.ReplaceAll(string(raw), "\r\n", "\n")
	items, err := splitItems(text)
	if err != nil {
		return res, err
	}

	// Partition modules.
	isAnchor := func(name string) bool { return j.anchorRe != nil && j.anchorRe.MatchString(name) }

	var kitModules, kitFunctions, consts []int // item indices
	var consumerIdx []int                      // everything staying in the consumer, original order
	kitBound := map[int]bool{}
	for idx, it := range items {
		switch it.kind {
		case kindModule:
			if isAnchor(it.name) {
				consumerIdx = append(consumerIdx, idx)
			} else {
				kitModules = append(kitModules, idx)
				kitBound[idx] = true
			}
		case kindFunction:
			kitFunctions = append(kitFunctions, idx)
			kitBound[idx] = true
		case kindAssign:
			consts = append(consts, idx)
		default:
			consumerIdx = append(consumerIdx, idx)
		}
	}

	// Const partition: fixpoint of "referenced by kit-bound code".
	kitConst := map[int]bool{}
	constByName := map[string]int{}
	for _, ci := range consts {
		constByName[items[ci].name] = ci
	}
	changed := true
	for changed {
		changed = false
		for _, ci := range consts {
			if kitConst[ci] || j.deleteConsts[items[ci].name] {
				continue
			}
			name := items[ci].name
			referenced := false
			for _, mi := range append(append([]int{}, kitModules...), kitFunctions...) {
				if wordRefs(items[mi].body, name) {
					referenced = true
					break
				}
			}
			if !referenced {
				for other := range kitConst {
					if wordRefs(items[other].body, name) {
						referenced = true
						break
					}
				}
			}
			if referenced {
				kitConst[ci] = true
				changed = true
			}
		}
	}
	// Consts referenced by nothing kit-bound stay in the consumer (layout data);
	// when there is no consumer they all go to the kit.
	if j.consumerOut == "" {
		for _, ci := range consts {
			if !j.deleteConsts[items[ci].name] {
				kitConst[ci] = true
			}
		}
	}

	// Build rename map.
	renames := map[string]string{}
	for k, v := range j.extRenames {
		renames[k] = v
	}
	for _, mi := range kitModules {
		if j.prefix != "" {
			renames[items[mi].name] = j.prefix + items[mi].name
		}
	}
	for _, fi := range kitFunctions {
		if j.prefix != "" {
			renames[items[fi].name] = j.prefix + items[fi].name
		}
	}
	for ci := range kitConst {
		name := items[ci].name
		renames[name] = j.prefix + name + "()"
	}
	for name := range j.deleteConsts {
		if _, ok := j.extRenames[name]; !ok {
			return res, fmt.Errorf("%s: deleteConst %q has no external rename", j.src, name)
		}
	}

	// Safety: renamed const names must not collide with params or inner assigns.
	reParamList := regexp.MustCompile(`^(?:module|function)\s+[A-Za-z_][A-Za-z0-9_]*\s*\(([^)]*)\)`)
	for _, di := range append(append([]int{}, kitModules...), kitFunctions...) {
		if m := reParamList.FindStringSubmatch(items[di].body); m != nil {
			for _, p := range strings.Split(m[1], ",") {
				p = strings.TrimSpace(strings.SplitN(p, "=", 2)[0])
				if _, clash := renames[p]; clash && strings.HasSuffix(renames[p], "()") {
					return res, fmt.Errorf("%s: param %q of %s collides with a const rename", j.src, p, items[di].name)
				}
			}
		}
	}
	reInnerAssign := regexp.MustCompile(`[;{]\s*([A-Za-z_][A-Za-z0-9_]*)\s*=[^=]`)
	for _, di := range append(append([]int{}, kitModules...), kitFunctions...) {
		for _, m := range reInnerAssign.FindAllStringSubmatch(items[di].body, -1) {
			if r, clash := renames[m[1]]; clash && strings.HasSuffix(r, "()") {
				return res, fmt.Errorf("%s: inner assign %q in %s collides with a const rename", j.src, m[1], items[di].name)
			}
		}
	}

	// ---- Emit kit ----
	var kit strings.Builder
	fmt.Fprintf(&kit, "// %s —— initially extracted from %s by the one-time tools/scadkit migration; this file is now authoritative.\n", filepath.Base(j.kitOut), filepath.Base(j.src))
	fmt.Fprintf(&kit, "// 纯零件库：只含 module/function 定义，无顶层几何。命名空间前缀 %q。\n", j.prefix)
	kit.WriteString("// 常量以零参 function 内置（use 语义不传播顶层赋值），调用方无需重申环境常量。\n")
	kit.WriteString("// 放置契约：落地件底面 z=0，带朝向件 front = -y。调用方自设 $fn（建议 12）。\n\n")
	for _, u := range j.kitUses {
		fmt.Fprintf(&kit, "use <%s>\n", u)
	}
	if len(j.kitUses) > 0 {
		kit.WriteString("\n")
	}
	if j.headerToKit && len(items) > 0 {
		kit.WriteString(applyRenames(strings.TrimLeft(items[0].lead, "\n"), renames))
	}
	for idx, it := range items {
		emitLead := it.lead
		if idx == 0 {
			// The scene header comment goes to exactly one side (pre-loop for
			// headerToKit, consumer emission otherwise).
			emitLead = ""
		}
		switch {
		case kitBound[idx]:
			kit.WriteString(applyRenames(emitLead, renames))
			kit.WriteString(applyRenames(it.body, renames))
		case it.kind == kindAssign && kitConst[idx]:
			kit.WriteString(applyRenames(emitLead, renames))
			eq := strings.Index(it.body, "=")
			rhs := applyRenames(it.body[eq+1:], renames)
			fmt.Fprintf(&kit, "function %s%s() =%s", j.prefix, it.name, rhs)
		}
	}
	if err := os.MkdirAll(filepath.Dir(j.kitOut), 0o755); err != nil {
		return res, err
	}
	if err := os.WriteFile(j.kitOut, []byte(kit.String()), 0o644); err != nil {
		return res, err
	}

	// ---- Emit consumer ----
	if j.consumerOut != "" {
		var out strings.Builder
		if len(items) > 0 && !j.headerToKit {
			out.WriteString(applyRenames(strings.TrimLeft(items[0].lead, "\n"), renames))
		}
		for _, u := range j.consumerUses {
			fmt.Fprintf(&out, "use <%s>\n", u)
		}
		out.WriteString("\n")
		for idx, it := range items {
			if kitBound[idx] || kitConst[idx] || it.kind == kindUse {
				continue
			}
			if it.kind == kindAssign && j.deleteConsts[it.name] {
				continue
			}
			lead := it.lead
			if idx == 0 {
				lead = ""
			}
			out.WriteString(applyRenames(lead, renames))
			out.WriteString(applyRenames(it.body, renames))
		}
		out.WriteString("\n")
		if err := os.WriteFile(j.consumerOut, []byte(out.String()), 0o644); err != nil {
			return res, err
		}
	}

	// ---- Report ----
	nConsts := 0
	for range kitConst {
		nConsts++
	}
	fmt.Printf("[%s] modules->kit %d, functions->kit %d, consts->kit %d, consumer items %d, renames %d\n",
		j.kitName, len(kitModules), len(kitFunctions), nConsts, len(consumerIdx), len(renames))

	res.renames = renames
	for _, ci := range consts {
		res.constNames[items[ci].name] = true
	}
	return res, nil
}

// verifyNoLeaks scans an output file for defined names that escaped renaming:
// any whole-word occurrence not immediately followed by '(' (call form) and not
// already prefixed is reported.
func verifyNoLeaks(path string, renames map[string]string) []string {
	raw, err := os.ReadFile(path)
	if err != nil {
		return []string{err.Error()}
	}
	text := string(raw)
	strSpans, _ := spanScan(text)
	var leaks []string
	for _, loc := range reIdent.FindAllStringIndex(text, -1) {
		word := text[loc[0]:loc[1]]
		repl, ok := renames[word]
		if !ok {
			continue
		}
		if inSpans(strSpans, loc[0]) {
			leaks = append(leaks, fmt.Sprintf("%s: %q inside string literal at byte %d (manual review)", path, word, loc[0]))
			continue
		}
		// Const renamed to call form NAME() with empty prefix keeps the word; a
		// following '(' means it is the declaration or a call — fine.
		if strings.HasSuffix(repl, "()") && strings.TrimPrefix(repl, word) == "()" {
			j := loc[1]
			for j < len(text) && (text[j] == ' ' || text[j] == '\t') {
				j++
			}
			if j < len(text) && text[j] == '(' {
				continue
			}
		}
		line := 1 + strings.Count(text[:loc[0]], "\n")
		leaks = append(leaks, fmt.Sprintf("%s:%d: unrenamed %q", path, line, word))
	}
	sort.Strings(leaks)
	return leaks
}

func min(a, b int) int {
	if a < b {
		return a
	}
	return b
}

func main() {
	scadDir := filepath.Join("assets", "scad")
	libDir := filepath.Join(scadDir, "lib")
	anchor := regexp.MustCompile(`^[a-z][a-z_0-9]*_[0-9]+$`)

	hd := job{
		src:         filepath.Join(scadDir, "habor_city_hd.scad"),
		kitOut:      filepath.Join(libDir, "kit_city_hd.scad"),
		prefix:      "hc_",
		kitName:     "city_hd",
		headerToKit: true,
	}
	hdRes, err := runJob(hd)
	if err != nil {
		fmt.Fprintln(os.Stderr, "hd:", err)
		os.Exit(1)
	}

	v2 := job{
		src:          filepath.Join(scadDir, "habor_city_v2.scad"),
		kitOut:       filepath.Join(libDir, "kit_city_blocks.scad"),
		consumerOut:  filepath.Join(scadDir, "habor_city_v2.scad"),
		prefix:       "", // v2_* names are already namespaced
		kitName:      "city_blocks",
		extRenames:   hdRes.renames,
		deleteConsts: hdRes.constNames,
		kitUses:      []string{"kit_city_hd.scad"},
		consumerUses: []string{"lib/kit_city_hd.scad", "lib/kit_city_blocks.scad"},
	}
	v2Res, err := runJob(v2)
	if err != nil {
		fmt.Fprintln(os.Stderr, "v2:", err)
		os.Exit(1)
	}

	themes := []struct{ file, prefix, name string }{
		{"old_city", "oc_", "old_city"},
		{"office", "of_", "office"},
		{"airport", "ap_", "airport"},
	}
	allRenames := map[string]map[string]string{
		"kit_city_hd":     hdRes.renames,
		"kit_city_blocks": v2Res.renames,
	}
	for _, t := range themes {
		jb := job{
			src:          filepath.Join(scadDir, t.file+".scad"),
			kitOut:       filepath.Join(libDir, "kit_"+t.name+".scad"),
			consumerOut:  filepath.Join(scadDir, t.file+".scad"),
			prefix:       t.prefix,
			kitName:      t.name,
			anchorRe:     anchor,
			consumerUses: []string{"lib/kit_" + t.name + ".scad"},
		}
		r, err := runJob(jb)
		if err != nil {
			fmt.Fprintln(os.Stderr, t.name+":", err)
			os.Exit(1)
		}
		allRenames["kit_"+t.name] = r.renames
	}

	// Leak verification over every emitted file.
	checks := map[string][]string{
		filepath.Join(libDir, "kit_city_hd.scad"):     {"kit_city_hd"},
		filepath.Join(libDir, "kit_city_blocks.scad"): {"kit_city_blocks"},
		filepath.Join(scadDir, "habor_city_v2.scad"):  {"kit_city_blocks"},
		filepath.Join(libDir, "kit_old_city.scad"):    {"kit_old_city"},
		filepath.Join(scadDir, "old_city.scad"):       {"kit_old_city"},
		filepath.Join(libDir, "kit_office.scad"):      {"kit_office"},
		filepath.Join(scadDir, "office.scad"):         {"kit_office"},
		filepath.Join(libDir, "kit_airport.scad"):     {"kit_airport"},
		filepath.Join(scadDir, "airport.scad"):        {"kit_airport"},
	}
	bad := false
	for path, kits := range checks {
		for _, k := range kits {
			for _, leak := range verifyNoLeaks(path, allRenames[k]) {
				fmt.Fprintln(os.Stderr, "LEAK:", leak)
				bad = true
			}
		}
	}
	if bad {
		os.Exit(2)
	}
	fmt.Println("kit split complete, no leaks")
}
