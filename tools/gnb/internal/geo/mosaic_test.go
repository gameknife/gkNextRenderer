package geo

import (
	"math"
	"strings"
	"testing"
	"time"
)

func TestMosaicNormalizeRejectsSizesThatCannotTile(t *testing.T) {
	cases := []struct {
		name string
		size float64
		want string
	}{
		{"not a multiple", 2500, "multiple"},
		{"even grid has no centre", 2000, "centre part"},
		{"beyond the cap", 9000, "cap is"},
	}
	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			m := Mosaic{Name: "x", Lat: 40.758, Lon: -73.9855, SizeM: tc.size}
			err := m.Normalize()
			if err == nil || !strings.Contains(err.Error(), tc.want) {
				t.Fatalf("size %.0f: want an error containing %q, got %v", tc.size, tc.want, err)
			}
		})
	}

	m := Mosaic{Name: "ok", Lat: 40.758, Lon: -73.9855}
	if err := m.Normalize(); err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	if m.SizeM != PartSizeM || m.Grid() != 1 {
		t.Fatalf("defaults not applied: %+v", m)
	}
	if m.FullRings != 1 || m.MediumRings != 2 {
		t.Fatalf("level-of-detail defaults not applied: %+v", m)
	}
}

// The part id has to survive a resize: it is the cache directory name, and a
// renamed directory means re-downloading a response that is already on disk.
func TestPartIdsAreAnchoredOnTheCentre(t *testing.T) {
	small := Mosaic{Name: "x", Lat: 40.758, Lon: -73.9855, SizeM: 1000}
	big := Mosaic{Name: "x", Lat: 40.758, Lon: -73.9855, SizeM: 5000}
	if err := small.Normalize(); err != nil {
		t.Fatal(err)
	}
	if err := big.Normalize(); err != nil {
		t.Fatal(err)
	}
	if small.CentrePart().ID != "p0_0" || big.CentrePart().ID != "p0_0" {
		t.Fatalf("centre id moved with the size: %q vs %q",
			small.CentrePart().ID, big.CentrePart().ID)
	}
	corner := big.PartAt(0, 0)
	if corner.ID != "pm2_m2" {
		t.Fatalf("south-west corner id = %q, want pm2_m2", corner.ID)
	}
	if corner.OffsetX != -2000 || corner.OffsetY != -2000 {
		t.Fatalf("south-west corner offset = (%.0f, %.0f), want (-2000, -2000)",
			corner.OffsetX, corner.OffsetY)
	}
	if corner.Ring != 2 {
		t.Fatalf("south-west corner ring = %d, want 2", corner.Ring)
	}
}

// Neighbouring centres are derived through the area's plane, so the step is
// exactly one part. Derived per part they are not: metres-per-degree varies
// with latitude and the error shows up as a seam.
func TestPartCentresStepExactlyOnePart(t *testing.T) {
	m := Mosaic{Name: "x", Lat: 40.758, Lon: -73.9855, SizeM: 3000}
	if err := m.Normalize(); err != nil {
		t.Fatal(err)
	}
	proj := NewProj(m.Lat, m.Lon)
	for _, part := range m.Parts() {
		x, y := proj.Forward(part.Lat, part.Lon)
		if math.Abs(x-part.OffsetX) > 0.01 || math.Abs(y-part.OffsetY) > 0.01 {
			t.Fatalf("part %s: centre round-trips to (%.3f, %.3f), want (%.0f, %.0f)",
				part.ID, x, y, part.OffsetX, part.OffsetY)
		}
	}
}

// A part is a tile in the area's frame: its geometry is part-local, but the
// projection is shared, so the same lat/lon seen from two parts differs by
// exactly their offset.
func TestPartTilesShareOneProjection(t *testing.T) {
	m := Mosaic{Name: "x", Lat: 40.758, Lon: -73.9855, SizeM: 3000}
	if err := m.Normalize(); err != nil {
		t.Fatal(err)
	}
	centre := m.TileFor(m.CentrePart())
	east := m.TileFor(m.PartAt(2, 1))
	const lat, lon = 40.7595, -73.9840
	cx, cy := centre.Project(lat, lon)
	ex, ey := east.Project(lat, lon)
	if math.Abs((cx-ex)-PartSizeM) > 0.01 || math.Abs(cy-ey) > 0.01 {
		t.Fatalf("the same point differs by (%.3f, %.3f) between neighbours, want (%.0f, 0)",
			cx-ex, cy-ey, PartSizeM)
	}
}

func TestLODFollowsTheRing(t *testing.T) {
	m := Mosaic{Name: "x", Lat: 40.758, Lon: -73.9855, SizeM: 5000}
	if err := m.Normalize(); err != nil {
		t.Fatal(err)
	}
	for ring, want := range map[int]LOD{0: LODFull, 1: LODMedium, 2: LODFar} {
		if got := m.LODForRing(ring); got != want {
			t.Fatalf("ring %d -> %s, want %s", ring, got, want)
		}
	}
	// The reduced levels have to actually drop the expensive layers, or the
	// rings are decoration on a load time that is still linear.
	base := DefaultEmitOptions()
	if !base.Detail || !base.StreetDetail || !base.Trees {
		t.Fatal("the full level is not the full tile")
	}
	medium := base.WithLOD(LODMedium)
	if !medium.Detail || medium.StreetDetail {
		t.Fatalf("medium should keep facades and drop street decoration: %+v", medium)
	}
	far := base.WithLOD(LODFar)
	if far.Detail || far.StreetDetail || far.Trees || far.MinFootprintM2 <= base.MinFootprintM2 {
		t.Fatalf("far should be bare prisms with the small buildings filtered: %+v", far)
	}
}

// Adjacent parts must not merely agree along the seam, they must hold the same
// samples. This is what makes the seam exact whatever the DSM filtering did.
func TestSubGridSharesItsEdgeSamples(t *testing.T) {
	const cells = 4
	const grid = 3
	n := cells*grid + 1
	field := NewHeightGrid(n, n, -1500, -1500, PartSizeM/cells, PartSizeM/cells)
	for row := 0; row < n; row++ {
		for col := 0; col < n; col++ {
			field.Values[row*n+col] = float64(row*100 + col)
		}
	}
	west := field.SubGrid(0*cells, 1*cells, cells, -PartSizeM/2, -PartSizeM/2)
	east := field.SubGrid(1*cells, 1*cells, cells, -PartSizeM/2, -PartSizeM/2)
	for row := 0; row <= cells; row++ {
		got := west.At(cells, row) // west part's east edge
		want := east.At(0, row)    // east part's west edge
		if got != want {
			t.Fatalf("row %d: seam samples differ (%.1f vs %.1f)", row, got, want)
		}
	}
	if west.Cols != cells+1 || west.OriginX != -PartSizeM/2 {
		t.Fatalf("slice geometry wrong: %d cols at origin %.1f", west.Cols, west.OriginX)
	}
}

// Street stations are 5 m apart, so dropping the outside vertices leaves a gap
// of up to two stations in the middle of a road that crosses a seam.
func TestClipToSquareCutsAtTheBoundary(t *testing.T) {
	const half = 500.0
	line := [][2]float64{{-800, 0}, {-100, 0}, {100, 0}, {800, 0}}
	got := clipToSquare(line, half)
	if len(got) != 4 {
		t.Fatalf("clipped to %d points, want 4 (both crossings inserted): %v", len(got), got)
	}
	if math.Abs(got[0][0]+half) > 1e-6 || math.Abs(got[len(got)-1][0]-half) > 1e-6 {
		t.Fatalf("ends are not on the boundary: %v .. %v", got[0], got[len(got)-1])
	}

	// A single segment can cross the whole square without either end inside.
	crossing := clipToSquare([][2]float64{{-900, 10}, {900, 10}}, half)
	if len(crossing) != 2 ||
		math.Abs(crossing[0][0]+half) > 1e-6 || math.Abs(crossing[1][0]-half) > 1e-6 {
		t.Fatalf("a segment spanning the square was not clipped: %v", crossing)
	}

	if out := clipToSquare([][2]float64{{-900, 0}, {-800, 0}}, half); len(out) != 0 {
		t.Fatalf("a polyline entirely outside should clip to nothing, got %v", out)
	}
}

// Shrinking must not leave a heightfield behind: assets/geo/<name> is packed
// wholesale, so a part the scene no longer references would still ship.
func TestPartIDPatternMatchesOnlyPartIds(t *testing.T) {
	for _, id := range []string{"p0_0", "pm1_1", "p2_m2", "pm12_m12"} {
		if !partIDPattern.MatchString(id) {
			t.Errorf("%q should be recognised as a part id", id)
		}
	}
	for _, other := range []string{"", "parts", "p0", "p0_0/x", "..", "poi.json", "pX_0"} {
		if partIDPattern.MatchString(other) {
			t.Errorf("%q must not be treated as a part id", other)
		}
	}
	m := Mosaic{Name: "x", Lat: 40.758, Lon: -73.9855, SizeM: 3000}
	if err := m.Normalize(); err != nil {
		t.Fatal(err)
	}
	for _, part := range m.Parts() {
		if !partIDPattern.MatchString(part.ID) {
			t.Fatalf("generated id %q does not match the prune pattern", part.ID)
		}
	}
}

// The status endpoint is how a fetch of 25 parts stays inside the mirror's
// rate limit instead of being dropped by it.
func TestOverpassStatusParsing(t *testing.T) {
	if got := overpassStatusURL(OverpassEndpoint); got != "https://overpass-api.de/api/status" {
		t.Fatalf("status url = %q", got)
	}
	if got := overpassStatusURL("https://example.org/notoverpass"); got != "" {
		t.Fatalf("a non-interpreter url should disable the check, got %q", got)
	}

	// "Rate limit: 0" is a mirror that does not meter per IP at all; it must
	// not be mistaken for one that has run out of slots.
	if !noRateLimitPattern.MatchString("Connected as: 1\nRate limit: 0\n") {
		t.Fatal("an unmetered mirror was not recognised")
	}
	if noRateLimitPattern.MatchString("Rate limit: 2\n2 slots available now.\n") {
		t.Fatal("a metered mirror must not read as unmetered")
	}

	free := "Connected as: 2589753945\nRate limit: 2\n2 slots available now.\n"
	if m := slotsAvailablePattern.FindStringSubmatch(free); m == nil || m[1] != "2" {
		t.Fatalf("free slots not recognised in %q", free)
	}

	busy := "Rate limit: 2\n" +
		"Slot available after: 2026-08-21T11:53:11Z, in 105 seconds.\n" +
		"Slot available after: 2026-08-21T11:52:30Z, in 64 seconds.\n"
	if slotsAvailablePattern.MatchString(busy) {
		t.Fatal("a queued status must not read as slots available")
	}
	waits := slotFreesInPattern.FindAllStringSubmatch(busy, -1)
	if len(waits) != 2 || waits[0][1] != "105" || waits[1][1] != "64" {
		t.Fatalf("queued slot times not recognised: %v", waits)
	}
}

// The pacing floor is the whole reason a 9- or 25-request area completes at
// all, so the resolution of the option must not silently drop it.
func TestOverpassIntervalResolution(t *testing.T) {
	if got := (Options{}).overpassInterval(); got != OverpassMinInterval {
		t.Fatalf("unset interval = %s, want the measured default %s", got, OverpassMinInterval)
	}
	if got := DefaultOptions("").overpassInterval(); got != OverpassMinInterval {
		t.Fatalf("default options interval = %s", got)
	}
	if got := (Options{OverpassInterval: 5 * time.Second}).overpassInterval(); got != 5*time.Second {
		t.Fatalf("explicit interval = %s, want 5s", got)
	}
	// Negative is the explicit "a local instance needs no pacing".
	if got := (Options{OverpassInterval: -1}).overpassInterval(); got != 0 {
		t.Fatalf("disabled interval = %s, want 0", got)
	}
}

// The pacer has to hold across mirrors and retries, not just between parts.
func TestOverpassPacerSpacesConsecutiveRequests(t *testing.T) {
	overpassPace.lastPost = time.Time{}
	quiet := func(string, ...any) {}

	start := time.Now()
	waitForOverpassPace(40*time.Millisecond, quiet) // first request never waits
	if elapsed := time.Since(start); elapsed > 20*time.Millisecond {
		t.Fatalf("the first request waited %s", elapsed)
	}
	waitForOverpassPace(40*time.Millisecond, quiet)
	if elapsed := time.Since(start); elapsed < 40*time.Millisecond {
		t.Fatalf("the second request went out after %s, want at least 40ms", elapsed)
	}

	overpassPace.lastPost = time.Time{}
	start = time.Now()
	waitForOverpassPace(0, quiet)
	waitForOverpassPace(0, quiet)
	if elapsed := time.Since(start); elapsed > 20*time.Millisecond {
		t.Fatalf("a zero interval must not pace, waited %s", elapsed)
	}
	overpassPace.lastPost = time.Time{}
}
