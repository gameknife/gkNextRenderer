package geo

import (
	"bytes"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net/http"
	"net/url"
	"os"
	"path/filepath"
	"strings"
	"time"
)

// OverpassEndpoint is the default mirror. Overpass asks callers to cache
// aggressively and identify themselves; both are handled here.
const OverpassEndpoint = "https://overpass-api.de/api/interpreter"

const osmLicense = "© OpenStreetMap contributors, ODbL 1.0"

// poiNodeSelectors are the standalone named points of interest — the things a
// map labels but never draws a footprint for: a subway entrance, a statue, a
// square. Every one of them is filtered on ["name"] because an unnamed POI is
// useless to a label layer, and on an explicit value list because a bare
// node["amenity"] in a downtown tile returns every bench, ATM and waste basket.
var poiNodeSelectors = []string{
	`node["name"]["tourism"~"^(attraction|museum|artwork|viewpoint|gallery|zoo|theme_park|aquarium|hotel|information)$"]`,
	`node["name"]["historic"]`,
	`node["name"]["amenity"~"^(university|college|school|hospital|clinic|library|theatre|cinema|arts_centre|museum|townhall|courthouse|police|fire_station|embassy|place_of_worship|marketplace|ferry_terminal|bus_station|conference_centre|exhibition_centre|casino|community_centre)$"]`,
	`node["name"]["railway"~"^(station|halt|subway_entrance|tram_stop)$"]`,
	`node["name"]["public_transport"="station"]`,
	`node["name"]["place"~"^(square|neighbourhood|suburb|quarter|island|islet|locality)$"]`,
	`node["name"]["natural"~"^(peak|beach|cape)$"]`,
	`node["name"]["man_made"~"^(tower|lighthouse|obelisk)$"]`,
}

// BuildOverpassQuery returns the QL for everything the city generator consumes.
func BuildOverpassQuery(b BBox) string {
	box := fmt.Sprintf("(%.7f,%.7f,%.7f,%.7f)", b.South, b.West, b.North, b.East)
	selectors := []string{
		`way["building"]`,
		`relation["building"]`,
		`way["highway"]`,
		`way["natural"="coastline"]`,
		`way["natural"="water"]`,
		`relation["natural"="water"]`,
		`way["waterway"="riverbank"]`,
		`way["landuse"]`,
		`way["leisure"]`,
		`way["man_made"="pier"]`,
	}
	selectors = append(selectors, poiNodeSelectors...)
	var sb strings.Builder
	sb.WriteString("[out:json][timeout:180];\n(\n")
	for _, s := range selectors {
		sb.WriteString("  " + s + box + ";\n")
	}
	sb.WriteString(");\nout body geom;\n")
	return sb.String()
}

// queryFingerprintPath is written next to the cached response. Without it a
// change to BuildOverpassQuery silently keeps serving a cache that predates the
// new selectors — the failure mode is missing data, not an error, which is the
// worst kind.
func queryFingerprintPath(dst string) string { return dst + ".query" }

func cachedQueryMatches(dst, query string) bool {
	prev, err := os.ReadFile(queryFingerprintPath(dst))
	return err == nil && string(prev) == query
}

// overpassAttempts is the retry budget for one tile. The public mirror answers
// the first request of a session with HTTP 200 and an HTML rate-limit page far
// more often than it answers with data, so retrying is the normal path rather
// than the exceptional one.
const overpassAttempts = 5

// errOverpassBusy marks the retryable failures: the rate-limit page, a 429, and
// the 504s the mirror returns when its slots are full.
var errOverpassBusy = errors.New("overpass is busy")

// FetchOverpass downloads the raw response into dst unless a cache produced by
// the *same query* is already there. Returns whether a request was actually
// made.
func FetchOverpass(endpoint string, b BBox, dst string, logf func(string, ...any)) (bool, error) {
	query := BuildOverpassQuery(b)
	if fileExists(dst) && cachedQueryMatches(dst, query) {
		return false, nil
	}
	if fileExists(dst) {
		logf("overpass: cached response predates the current query — re-fetching")
	}
	if err := os.MkdirAll(filepath.Dir(dst), 0o755); err != nil {
		return false, err
	}

	var lastErr error
	for attempt := 1; attempt <= overpassAttempts; attempt++ {
		logf("POST %s (bbox %s, attempt %d/%d)", endpoint, b, attempt, overpassAttempts)
		body, err := postOverpass(endpoint, query)
		if err == nil {
			if err := os.WriteFile(dst, body, 0o644); err != nil {
				return false, err
			}
			return true, os.WriteFile(queryFingerprintPath(dst), []byte(query), 0o644)
		}
		lastErr = err
		if !errors.Is(err, errOverpassBusy) || attempt == overpassAttempts {
			break
		}
		// Exponential backoff. The mirror's slot scheduler frees up on the order
		// of tens of seconds, so start well above a typical HTTP retry.
		wait := time.Duration(1<<(attempt-1)) * 15 * time.Second
		logf("overpass: %v — retrying in %s", err, wait)
		time.Sleep(wait)
	}
	return false, lastErr
}

func postOverpass(endpoint, query string) ([]byte, error) {
	client := &http.Client{Timeout: 10 * time.Minute}
	req, err := http.NewRequest(http.MethodPost, endpoint,
		strings.NewReader(url.Values{"data": {query}}.Encode()))
	if err != nil {
		return nil, err
	}
	req.Header.Set("Content-Type", "application/x-www-form-urlencoded")
	req.Header.Set("User-Agent", userAgent)
	resp, err := client.Do(req)
	if err != nil {
		return nil, fmt.Errorf("%w: %v", errOverpassBusy, err)
	}
	defer resp.Body.Close()
	body, err := io.ReadAll(resp.Body)
	if err != nil {
		return nil, err
	}
	switch resp.StatusCode {
	case http.StatusOK:
	case http.StatusTooManyRequests, http.StatusGatewayTimeout, http.StatusServiceUnavailable:
		return nil, fmt.Errorf("%w: HTTP %d", errOverpassBusy, resp.StatusCode)
	default:
		return nil, fmt.Errorf("overpass HTTP %d: %s", resp.StatusCode, snippet(body))
	}
	if !bytes.Contains(body, []byte(`"elements"`)) {
		// HTTP 200 with an HTML body is the mirror's rate-limit page. Echoing the
		// markup back at the user says nothing; naming the condition does.
		return nil, fmt.Errorf("%w: HTTP 200 but the body is not an Overpass result "+
			"(the mirror served its rate-limit page)", errOverpassBusy)
	}
	return body, nil
}

func snippet(body []byte) string {
	s := strings.TrimSpace(string(body))
	if len(s) > 400 {
		s = s[:400]
	}
	return s
}

// ---------------------------------------------------------------------------
// Raw response model (only the fields `out body geom` actually produces)
// ---------------------------------------------------------------------------

type osmPoint struct {
	Lat float64 `json:"lat"`
	Lon float64 `json:"lon"`
}

type osmMember struct {
	Type     string     `json:"type"`
	Ref      int64      `json:"ref"`
	Role     string     `json:"role"`
	Geometry []osmPoint `json:"geometry"`
}

type osmElement struct {
	Type     string            `json:"type"`
	ID       int64             `json:"id"`
	Tags     map[string]string `json:"tags"`
	Geometry []osmPoint        `json:"geometry"`
	Nodes    []int64           `json:"nodes"`
	Members  []osmMember       `json:"members"`
	// Standalone nodes carry their position on the element itself rather than in
	// geometry; only POI nodes reach the generator this way.
	Lat float64 `json:"lat"`
	Lon float64 `json:"lon"`
}

type osmResponse struct {
	Elements []osmElement `json:"elements"`
}

// ParseOverpass decodes a cached response.
func ParseOverpass(path string) ([]osmElement, error) {
	data, err := os.ReadFile(path)
	if err != nil {
		return nil, err
	}
	var resp osmResponse
	if err := json.Unmarshal(data, &resp); err != nil {
		return nil, fmt.Errorf("%s: %w", path, err)
	}
	return resp.Elements, nil
}
