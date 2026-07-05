package dashboard

import (
	"net/http/httptest"
	"strings"
	"testing"
)

func TestHandleTabGraphRendersGraphControls(t *testing.T) {
	s := setupDocsRepo(t)
	req := httptest.NewRequest("GET", "/tab/graph", nil)
	req.SetPathValue("kind", "graph")
	rec := httptest.NewRecorder()

	s.handleTab(rec, req)

	if rec.Code != 200 {
		t.Fatalf("status = %d (%s), want 200", rec.Code, rec.Body.String())
	}
	body := rec.Body.String()
	for _, want := range []string{
		`data-target-graph`,
		`data-graph-search`,
		`data-graph-svg`,
		`data-graph-refresh`,
		`data-graph-depth="2"`,
	} {
		if !strings.Contains(body, want) {
			t.Fatalf("graph response missing %q:\n%s", want, body)
		}
	}
}
