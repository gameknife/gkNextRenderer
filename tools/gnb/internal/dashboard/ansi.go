package dashboard

import (
	"fmt"
	"html"
	"strconv"
	"strings"
)

// ansiToHTML converts a single line of subprocess output containing ANSI
// CSI/SGR escape sequences into HTML. Color state is reset between lines:
// in the wild (cmake / msbuild / catch2) colors are typically scoped to a
// single line anyway, so per-line scoping keeps the implementation simple
// while remaining visually correct.
//
// Supported SGR codes: 0 (reset), 1 (bold), 22 (no bold), 4 (underline),
// 24 (no underline), 30-37 / 90-97 (fg), 40-47 / 100-107 (bg), and the
// 38;2;r;g;b / 48;2;r;g;b 24-bit color extensions. Cursor / erase /
// 256-color sequences are stripped silently.
func ansiToHTML(line string) string {
	var out strings.Builder
	state := newAnsiState()
	i := 0
	for i < len(line) {
		c := line[i]
		// Strip carriage returns produced by some tools (e.g. msbuild
		// progress lines). They would clobber the visual on the client.
		if c == '\r' {
			i++
			continue
		}
		if c != 0x1b { // not ESC
			out.WriteString(html.EscapeString(string(c)))
			i++
			continue
		}
		// We have an ESC. Look for CSI: ESC '[' params final-byte
		if i+1 >= len(line) || line[i+1] != '[' {
			i++ // unknown escape, skip ESC
			continue
		}
		// Find the final byte: a byte in 0x40-0x7E
		end := i + 2
		for end < len(line) {
			b := line[end]
			if b >= 0x40 && b <= 0x7E {
				break
			}
			end++
		}
		if end >= len(line) {
			break
		}
		final := line[end]
		params := line[i+2 : end]
		i = end + 1
		if final != 'm' {
			// Non-SGR: cursor moves, erase, etc. Drop silently.
			continue
		}
		state.apply(params, &out)
	}
	state.closeAll(&out)
	return out.String()
}

type ansiState struct {
	bold       bool
	underline  bool
	fg         string // CSS color or "" for default
	bg         string
	openSpans  int
}

func newAnsiState() *ansiState { return &ansiState{} }

func (s *ansiState) apply(params string, out *strings.Builder) {
	codes := splitParams(params)
	for i := 0; i < len(codes); i++ {
		n := codes[i]
		switch {
		case n == 0:
			s.reset(out)
		case n == 1:
			s.set(out, func() { s.bold = true })
		case n == 22:
			s.set(out, func() { s.bold = false })
		case n == 4:
			s.set(out, func() { s.underline = true })
		case n == 24:
			s.set(out, func() { s.underline = false })
		case n >= 30 && n <= 37:
			s.set(out, func() { s.fg = basicColor(n - 30) })
		case n >= 90 && n <= 97:
			s.set(out, func() { s.fg = brightColor(n - 90) })
		case n == 39:
			s.set(out, func() { s.fg = "" })
		case n >= 40 && n <= 47:
			s.set(out, func() { s.bg = basicColor(n - 40) })
		case n >= 100 && n <= 107:
			s.set(out, func() { s.bg = brightColor(n - 100) })
		case n == 49:
			s.set(out, func() { s.bg = "" })
		case n == 38 || n == 48:
			// Extended color: 38;5;n (256) or 38;2;r;g;b (truecolor).
			if i+1 >= len(codes) {
				return
			}
			mode := codes[i+1]
			var color string
			switch mode {
			case 5:
				if i+2 >= len(codes) {
					return
				}
				color = palette256(codes[i+2])
				i += 2
			case 2:
				if i+4 >= len(codes) {
					return
				}
				color = fmt.Sprintf("rgb(%d,%d,%d)", clamp8(codes[i+2]), clamp8(codes[i+3]), clamp8(codes[i+4]))
				i += 4
			default:
				return
			}
			isFg := n == 38
			s.set(out, func() {
				if isFg {
					s.fg = color
				} else {
					s.bg = color
				}
			})
		}
	}
}

func (s *ansiState) set(out *strings.Builder, mut func()) {
	s.closeAll(out)
	mut()
	s.openSpan(out)
}

func (s *ansiState) reset(out *strings.Builder) {
	s.closeAll(out)
	s.bold, s.underline, s.fg, s.bg = false, false, "", ""
}

func (s *ansiState) openSpan(out *strings.Builder) {
	if !s.bold && !s.underline && s.fg == "" && s.bg == "" {
		return
	}
	var sb strings.Builder
	sb.WriteString(`<span style="`)
	if s.fg != "" {
		sb.WriteString("color:")
		sb.WriteString(s.fg)
		sb.WriteByte(';')
	}
	if s.bg != "" {
		sb.WriteString("background:")
		sb.WriteString(s.bg)
		sb.WriteByte(';')
	}
	if s.bold {
		sb.WriteString("font-weight:600;")
	}
	if s.underline {
		sb.WriteString("text-decoration:underline;")
	}
	sb.WriteString(`">`)
	out.WriteString(sb.String())
	s.openSpans++
}

func (s *ansiState) closeAll(out *strings.Builder) {
	for s.openSpans > 0 {
		out.WriteString("</span>")
		s.openSpans--
	}
}

func splitParams(s string) []int {
	if s == "" {
		return []int{0}
	}
	parts := strings.Split(s, ";")
	out := make([]int, 0, len(parts))
	for _, p := range parts {
		if p == "" {
			out = append(out, 0)
			continue
		}
		n, err := strconv.Atoi(p)
		if err != nil {
			n = 0
		}
		out = append(out, n)
	}
	return out
}

func clamp8(n int) int {
	if n < 0 {
		return 0
	}
	if n > 255 {
		return 255
	}
	return n
}

// Match a VS Code-ish dark theme palette; tuned to be readable on #0a0d14.
func basicColor(i int) string {
	switch i {
	case 0:
		return "#4d525c" // black -> dim grey so it stays visible on dark bg
	case 1:
		return "#ef4444" // red
	case 2:
		return "#22c55e" // green
	case 3:
		return "#eab308" // yellow
	case 4:
		return "#3b82f6" // blue
	case 5:
		return "#c084fc" // magenta
	case 6:
		return "#06b6d4" // cyan
	case 7:
		return "#e6e9ef" // white
	}
	return ""
}

func brightColor(i int) string {
	switch i {
	case 0:
		return "#6b7384"
	case 1:
		return "#fca5a5"
	case 2:
		return "#86efac"
	case 3:
		return "#fde047"
	case 4:
		return "#93c5fd"
	case 5:
		return "#d8b4fe"
	case 6:
		return "#67e8f9"
	case 7:
		return "#ffffff"
	}
	return ""
}

// palette256 covers the 16 base + 216 cube + 24 grayscale 256-color palette.
func palette256(n int) string {
	switch {
	case n < 8:
		return basicColor(n)
	case n < 16:
		return brightColor(n - 8)
	case n < 232:
		idx := n - 16
		r := idx / 36
		g := (idx / 6) % 6
		b := idx % 6
		conv := func(v int) int {
			if v == 0 {
				return 0
			}
			return 55 + v*40
		}
		return fmt.Sprintf("rgb(%d,%d,%d)", conv(r), conv(g), conv(b))
	default:
		v := 8 + (n-232)*10
		return fmt.Sprintf("rgb(%d,%d,%d)", v, v, v)
	}
}
