package validate

import "testing"

func TestCompare(t *testing.T) {
	tests := []struct {
		actual   any
		op       string
		expected any
		want     bool
	}{
		{3.0, "ge", 2.0, true}, {"Running", "eq", "Running", true},
		{"abcdef", "contains", "bcd", true}, {1.0, "gt", 2.0, false},
	}
	for _, tt := range tests {
		if got := compare(tt.actual, tt.op, tt.expected); got != tt.want {
			t.Fatalf("compare(%v,%s,%v)=%v", tt.actual, tt.op, tt.expected, got)
		}
	}
}

func TestNormalizePoints(t *testing.T) {
	p := map[string]any{"to": map[string]any{"norm": []any{0.5, 0.25}}, "at": map[string]any{"px": []any{12.0, 34.0}}}
	normalizePoints(p, 1280, 720)
	to := p["to"].([]any)
	if to[0] != 640.0 || to[1] != 180.0 {
		t.Fatalf("to=%v", to)
	}
	at := p["at"].([]any)
	if at[0] != 12.0 || at[1] != 34.0 {
		t.Fatalf("at=%v", at)
	}
}
