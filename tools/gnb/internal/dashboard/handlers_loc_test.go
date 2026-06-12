package dashboard

import (
	"testing"
	"time"
)

func TestBuildContributionGraph(t *testing.T) {
	today := time.Date(2026, time.June, 12, 15, 30, 0, 0, time.Local)
	counts := map[string]int{
		"2025-06-08": 1,
		"2026-06-11": 4,
		"2026-06-12": 8,
		"2026-06-13": 16,
	}

	graph := buildContributionGraph(counts, today)

	if len(graph.Weeks) != 53 {
		t.Fatalf("expected 53 weeks, got %d", len(graph.Weeks))
	}
	if graph.Weeks[0].Days[0].Date != "2025-06-08" {
		t.Fatalf("unexpected chart start: %s", graph.Weeks[0].Days[0].Date)
	}
	if graph.Total != 13 {
		t.Fatalf("expected 13 commits through today, got %d", graph.Total)
	}
	if graph.Max != 8 {
		t.Fatalf("expected max daily count 8, got %d", graph.Max)
	}

	var halfDay contributionDayVM
	var maxDay contributionDayVM
	var futureDay contributionDayVM
	for _, week := range graph.Weeks {
		if len(week.Days) != 7 {
			t.Fatalf("expected 7 days per week, got %d", len(week.Days))
		}
		for _, day := range week.Days {
			switch day.Date {
			case "2026-06-11":
				halfDay = day
			case "2026-06-12":
				maxDay = day
			case "2026-06-13":
				futureDay = day
			}
		}
	}

	if halfDay.Level != 2 {
		t.Fatalf("expected half-max day at level 2, got %d", halfDay.Level)
	}
	if maxDay.Level != 4 {
		t.Fatalf("expected max day at level 4, got %d", maxDay.Level)
	}
	if !futureDay.Future || futureDay.Count != 0 || futureDay.Level != 0 {
		t.Fatalf("future day should be empty, got %+v", futureDay)
	}
	if len(graph.Months) < 12 {
		t.Fatalf("expected month labels across the year, got %d", len(graph.Months))
	}
}

func TestContributionLevel(t *testing.T) {
	tests := []struct {
		count int
		max   int
		want  int
	}{
		{count: 0, max: 10, want: 0},
		{count: 1, max: 10, want: 1},
		{count: 5, max: 10, want: 2},
		{count: 10, max: 10, want: 4},
	}

	for _, test := range tests {
		if got := contributionLevel(test.count, test.max); got != test.want {
			t.Fatalf("contributionLevel(%d, %d) = %d, want %d", test.count, test.max, got, test.want)
		}
	}
}
