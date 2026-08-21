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
	"regexp"
	"strconv"
	"strings"
	"sync"
	"time"
)

// OverpassEndpoint is the default mirror: the reference instance. Overpass asks
// callers to cache aggressively, identify themselves and space their queries;
// all three are handled here (the cache, userAgent, and the slot check plus
// overpassPolitePause below).
//
// It serves a run of 9 or 25 part requests fine. The failure that looks like it
// does not is a temporary per-IP block, and it is earned by hammering — a burst
// of retries with no slot available will do it, after which even a trivial
// query gets its connection dropped. waitForOverpassSlot is what keeps the
// steady state polite enough not to trip that.
const OverpassEndpoint = "https://overpass-api.de/api/interpreter"

// OverpassMirrors is the rotation a retry walks through.
//
// One tile was one request and a mirror having a bad minute was an
// inconvenience. An area is 9 or 25 requests back to back, which is enough to
// exhaust a mirror's per-client budget on its own — and a mirror that has had
// enough does not say so politely: it answers 429, or 5xx, or drops the
// connection mid-body. Retrying the same host through that is just waiting.
// Each attempt therefore moves to the next mirror, so the backoff doubles as
// the recovery time for the one before it.
var OverpassMirrors = []string{
	OverpassEndpoint,
	"https://overpass.kumi.systems/api/interpreter",
	"https://overpass.private.coffee/api/interpreter",
}

// Deliberately not in the rotation: regional instances. overpass.osm.ch serves
// Switzerland only, and it does not say so — it answers 200 with a
// syntactically perfect result holding zero elements. Pointed at Manhattan it
// wrote seven empty part caches that the pipeline happily built a bare
// heightfield from. Any mirror added here has to be a planet instance, and
// emptyResult below is the seatbelt for the next time one is not.

// mirrorRotation puts the caller's choice first and keeps the rest as fallback.
// --overpass-endpoint is how someone works around a mirror that keeps refusing
// one particular box, so it has to win the first attempt; it is not a reason to
// give up if that mirror is the one having the bad day.
func mirrorRotation(preferred string) []string {
	out := []string{}
	if strings.TrimSpace(preferred) != "" {
		out = append(out, preferred)
	}
	for _, m := range OverpassMirrors {
		if m != preferred {
			out = append(out, m)
		}
	}
	return out
}

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
const overpassAttempts = 8

// overpassMaxBackoff caps the wait. Doubling from 15 s reaches four minutes by
// the sixth attempt, which is longer than a mirror ever needs and long enough
// that a 25-part area looks hung.
const overpassMaxBackoff = 90 * time.Second

// OverpassMinInterval is the floor on how often this process posts a query.
//
// Measured against the reference instance: one request per 30 seconds goes
// through indefinitely — a whole run of areas was generated at that cadence
// without a single refusal — while a burst faster than it earns a temporary
// per-IP block that costs far more than the time it saved. The block is what
// makes a big area look like it fails for being big: once you have it, even
// `[out:json];out count;` gets its connection dropped, so the bbox is a red
// herring.
//
// An area is 9 or 25 requests, so this is about four minutes for a 3x3 and
// twelve for a 5x5. Predictable is worth more than fast here, and nothing is
// re-fetched: every part is cached as it lands.
const OverpassMinInterval = 30 * time.Second

// overpassPace enforces that floor process-wide — across parts, across retries
// and across mirrors — so no path can post faster than the cadence by accident.
var overpassPace struct {
	mu       sync.Mutex
	lastPost time.Time
}

func waitForOverpassPace(interval time.Duration, logf func(string, ...any)) {
	if interval <= 0 {
		return
	}
	overpassPace.mu.Lock()
	defer overpassPace.mu.Unlock()
	if !overpassPace.lastPost.IsZero() {
		if wait := interval - time.Since(overpassPace.lastPost); wait > 0 {
			logf("overpass: pacing — %s until the next request", wait.Round(time.Second))
			time.Sleep(wait)
		}
	}
	overpassPace.lastPost = time.Now()
}

// overpassSlotMaxWait caps how long one slot check will hold the fetch. Past
// this the mirror is not merely busy, and moving to the next one in the
// rotation is the better move.
const overpassSlotMaxWait = 3 * time.Minute

// slotsAvailablePattern / slotFreesInPattern read the two forms /api/status
// reports: "2 slots available now." or one "Slot available after: <time>, in
// <n> seconds." line per queued slot.
var (
	noRateLimitPattern    = regexp.MustCompile(`(?m)^Rate limit:\s*0\s*$`)
	slotsAvailablePattern = regexp.MustCompile(`(\d+)\s+slots? available now`)
	slotFreesInPattern    = regexp.MustCompile(`in\s+(\d+)\s+seconds`)
)

// overpassStatusURL turns an interpreter endpoint into its status endpoint.
// Returns "" for a URL that does not look like one, which disables the check.
func overpassStatusURL(endpoint string) string {
	const suffix = "/interpreter"
	if !strings.HasSuffix(endpoint, suffix) {
		return ""
	}
	return strings.TrimSuffix(endpoint, suffix) + "/status"
}

// waitForOverpassSlot asks the mirror whether it has a query slot for us, and
// waits exactly as long as it says to if it does not.
//
// This is the difference between fetching 25 parts in ten minutes and in an
// hour. Overpass rate-limits per IP by slots (the reference instance grants 2),
// and a client that keeps posting while it has none gets its connections
// dropped — which reads as "the request was too big" but happens just as
// readily to a trivial query. Asking first replaces guessed backoff with the
// server's own number.
//
// Best-effort throughout: a mirror that does not serve /api/status, or answers
// something unparseable, is simply not waited for. The retry-and-rotate path
// stays as the fallback.
func waitForOverpassSlot(endpoint string, logf func(string, ...any)) {
	statusURL := overpassStatusURL(endpoint)
	if statusURL == "" {
		return
	}
	client := &http.Client{Timeout: 20 * time.Second}
	req, err := http.NewRequest(http.MethodGet, statusURL, nil)
	if err != nil {
		return
	}
	req.Header.Set("User-Agent", userAgent)
	resp, err := client.Do(req)
	if err != nil {
		return
	}
	defer resp.Body.Close()
	if resp.StatusCode != http.StatusOK {
		return
	}
	body, err := io.ReadAll(io.LimitReader(resp.Body, 64<<10))
	if err != nil {
		return
	}
	status := string(body)
	if noRateLimitPattern.MatchString(status) {
		// "Rate limit: 0" is a mirror that does not meter per IP at all. It has
		// no slots to report, so it must not fall through to the "no slot"
		// reading below.
		return
	}
	if m := slotsAvailablePattern.FindStringSubmatch(status); m != nil {
		if n, convErr := strconv.Atoi(m[1]); convErr == nil && n > 0 {
			return
		}
	}
	// No slot right now: the mirror lists when each queued one frees. The
	// soonest is the one we want, plus a second so we do not race it.
	wait := time.Duration(0)
	for _, m := range slotFreesInPattern.FindAllStringSubmatch(status, -1) {
		secs, convErr := strconv.Atoi(m[1])
		if convErr != nil {
			continue
		}
		d := time.Duration(secs)*time.Second + time.Second
		if wait == 0 || d < wait {
			wait = d
		}
	}
	if wait <= 0 {
		return
	}
	if wait > overpassSlotMaxWait {
		logf("overpass: no slot for %s within %s — moving on to the next mirror", statusURL, wait)
		return
	}
	logf("overpass: no slot right now, the mirror frees one in %s", wait)
	time.Sleep(wait)
}

// errOverpassBusy marks the retryable failures: the rate-limit page, a 429, and
// the 5xx a mirror returns when its slots are full.
var errOverpassBusy = errors.New("overpass is busy")

// errOverpassEmpty marks a valid response that contained nothing. It is not
// necessarily wrong — open water really is empty — but it is what a regional
// mirror returns for a box outside its extract, and that failure is silent all
// the way to a city with no buildings in it. So it moves to the next mirror
// and is only accepted once every mirror agrees.
var errOverpassEmpty = errors.New("overpass returned no elements")

// FetchOverpass downloads the raw response into dst unless a cache produced by
// the *same query* is already there. Returns whether a request was actually
// made.
func FetchOverpass(endpoint string, b BBox, dst string, interval time.Duration,
	logf func(string, ...any)) (bool, error) {
	query := BuildOverpassQuery(b)
	if fileExists(dst) && cachedQueryMatches(dst, query) {
		return false, nil
	}
	mirrors := mirrorRotation(endpoint)
	if fileExists(dst) {
		logf("overpass: cached response predates the current query — re-fetching")
	}
	if err := os.MkdirAll(filepath.Dir(dst), 0o755); err != nil {
		return false, err
	}

	var lastErr error
	var emptyBody []byte
	save := func(body []byte) (bool, error) {
		if err := os.WriteFile(dst, body, 0o644); err != nil {
			return false, err
		}
		return true, os.WriteFile(queryFingerprintPath(dst), []byte(query), 0o644)
	}
	for attempt := 1; attempt <= overpassAttempts; attempt++ {
		mirror := mirrors[(attempt-1)%len(mirrors)]
		if attempt > 1 {
			// Only after something already went wrong. In the steady state the
			// cadence above is what keeps us welcome, and asking the status
			// endpoint every time would add a request per part to do it.
			waitForOverpassSlot(mirror, logf)
		}
		waitForOverpassPace(interval, logf)
		logf("POST %s (bbox %s, attempt %d/%d)", mirror, b, attempt, overpassAttempts)
		body, err := postOverpass(mirror, query)
		if err == nil {
			return save(body)
		}
		if errors.Is(err, errOverpassEmpty) {
			emptyBody = body
			if attempt < len(mirrors) {
				logf("overpass: %s returned no elements for this box — trying another mirror", mirror)
				continue
			}
			logf("overpass: every mirror returned an empty result — taking it at face value")
			return save(body)
		}
		lastErr = err
		if !errors.Is(err, errOverpassBusy) || attempt == overpassAttempts {
			break
		}
		// Exponential backoff. The mirror's slot scheduler frees up on the order
		// of tens of seconds, so start well above a typical HTTP retry.
		wait := time.Duration(1<<(attempt-1)) * 15 * time.Second
		if wait > overpassMaxBackoff {
			wait = overpassMaxBackoff
		}
		logf("overpass: %v — retrying in %s", err, wait)
		time.Sleep(wait)
	}
	if lastErr == nil && emptyBody != nil {
		return save(emptyBody)
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
		// A mirror that gives up mid-body drops the connection or resets the
		// HTTP/2 stream. That is the same transient condition as a refused
		// connect, and it has to be retried rather than reported as a failure.
		return nil, fmt.Errorf("%w: %v", errOverpassBusy, err)
	}
	switch resp.StatusCode {
	case http.StatusOK:
	case http.StatusTooManyRequests:
		return nil, fmt.Errorf("%w: HTTP %d", errOverpassBusy, resp.StatusCode)
	default:
		// Every 5xx is retryable. The query is a fixed template that differs
		// between parts only in the bounding box, so a server error on one part
		// and success on its neighbour is the mirror, not the request — and
		// fetching an area is 9 or 25 requests in a row, which is exactly when
		// a mirror starts dropping them at the front proxy.
		if resp.StatusCode >= 500 {
			return nil, fmt.Errorf("%w: HTTP %d %s", errOverpassBusy, resp.StatusCode, snippet(body))
		}
		return nil, fmt.Errorf("overpass HTTP %d: %s", resp.StatusCode, snippet(body))
	}
	if !bytes.Contains(body, []byte(`"elements"`)) {
		// HTTP 200 with an HTML body is the mirror's rate-limit page. Echoing the
		// markup back at the user says nothing; naming the condition does.
		return nil, fmt.Errorf("%w: HTTP 200 but the body is not an Overpass result "+
			"(the mirror served its rate-limit page)", errOverpassBusy)
	}
	if emptyResult(body) {
		return body, errOverpassEmpty
	}
	return body, nil
}

// emptyResult reports a well-formed response that returned nothing at all.
func emptyResult(body []byte) bool {
	i := bytes.Index(body, []byte(`"elements"`))
	if i < 0 {
		return false
	}
	rest := bytes.TrimSpace(body[i+len(`"elements"`):])
	rest = bytes.TrimPrefix(rest, []byte(":"))
	rest = bytes.TrimSpace(rest)
	return bytes.HasPrefix(rest, []byte("[]"))
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
