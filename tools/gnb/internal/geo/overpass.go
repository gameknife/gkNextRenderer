package geo

import (
	"bytes"
	"encoding/json"
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
	var sb strings.Builder
	sb.WriteString("[out:json][timeout:180];\n(\n")
	for _, s := range selectors {
		sb.WriteString("  " + s + box + ";\n")
	}
	sb.WriteString(");\nout body geom;\n")
	return sb.String()
}

// FetchOverpass downloads the raw response into dst unless it is already
// cached. Returns whether a request was actually made.
func FetchOverpass(endpoint string, b BBox, dst string, logf func(string, ...any)) (bool, error) {
	if fileExists(dst) {
		return false, nil
	}
	if err := os.MkdirAll(filepath.Dir(dst), 0o755); err != nil {
		return false, err
	}
	query := BuildOverpassQuery(b)
	logf("POST %s (bbox %s)", endpoint, b)
	client := &http.Client{Timeout: 10 * time.Minute}
	req, err := http.NewRequest(http.MethodPost, endpoint,
		strings.NewReader(url.Values{"data": {query}}.Encode()))
	if err != nil {
		return false, err
	}
	req.Header.Set("Content-Type", "application/x-www-form-urlencoded")
	req.Header.Set("User-Agent", userAgent)
	resp, err := client.Do(req)
	if err != nil {
		return false, err
	}
	defer resp.Body.Close()
	body, err := io.ReadAll(resp.Body)
	if err != nil {
		return false, err
	}
	if resp.StatusCode != http.StatusOK {
		snippet := string(body)
		if len(snippet) > 400 {
			snippet = snippet[:400]
		}
		return false, fmt.Errorf("overpass HTTP %d: %s", resp.StatusCode, snippet)
	}
	if !bytes.Contains(body, []byte(`"elements"`)) {
		return false, fmt.Errorf("overpass response has no elements (rate limited?)")
	}
	return true, os.WriteFile(dst, body, 0o644)
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
	Members  []osmMember       `json:"members"`
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
