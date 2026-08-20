package geo

import (
	"math"
	"sort"
	"strconv"
	"strings"
)

// Points of interest — the layer that turns a tile from scenery into a place
// you can be told where you are in.
//
// Three sources feed it, in descending order of usefulness as a label anchor:
//
//  1. named building footprints — the best anchor there is, because they have a
//     height, so the label can float over the roof rather than sink into it;
//  2. named leisure/landuse areas — parks and squares, anchored at the centroid;
//  3. standalone OSM nodes — subway entrances, statues, viewpoints; everything a
//     map labels but never draws a footprint for.
//
// The result leaves the IR through poi.json (a committed produced work next to
// terrain.hmap) rather than through the .scad: labels are runtime data, and
// baking 500 of them into the scene as marker modules would cost 500 nodes for
// something no renderer ever draws.

// POIFormat is the sidecar's version tag; the runtime rejects anything else.
const POIFormat = "gkgeopoi1"

// POIFile is the sidecar at assets/geo/<tile>/poi.json, packed into geo.pak
// alongside the tile it labels.
//
// Positions are SCAD metres (+x east, +y north) — the same frame as the .scad
// and the .hmap. The engine's mapping is (x, z) = (x, -y) with world Y taken
// from the terrain; doing that conversion here would make the file disagree with
// every other artefact in the directory.
type POIFile struct {
	Format      string     `json:"format"`
	Tile        string     `json:"tile"`
	Center      [2]float64 `json:"center"` // lat, lon
	SizeM       float64    `json:"sizeM"`
	Attribution []string   `json:"attribution"`
	POIs        []POI      `json:"pois"`
}

// POI categories. The runtime maps these to label colours and to the filter
// toggles, so the set is deliberately small and stable.
const (
	CatLandmark  = "landmark"
	CatTransport = "transport"
	CatCulture   = "culture"
	CatEducation = "education"
	CatHealth    = "health"
	CatWorship   = "worship"
	CatCivic     = "civic"
	CatCommerce  = "commerce"
	CatLodging   = "lodging"
	CatPark      = "park"
	CatPlace     = "place"
	CatOther     = "other"
)

// POICategories is the ordered, canonical list (UI filter order).
var POICategories = []string{
	CatLandmark, CatTransport, CatCulture, CatEducation, CatHealth,
	CatWorship, CatCivic, CatCommerce, CatLodging, CatPark, CatPlace, CatOther,
}

// classifyRules maps an OSM key=value onto a category. Only keys that reach the
// POI layer at all appear here; anything unmatched falls through to CatOther,
// which is a visible-but-deprioritised bucket rather than a drop.
var classifyRules = map[string]map[string]string{
	"tourism": {
		"attraction": CatLandmark, "museum": CatCulture, "artwork": CatCulture,
		"gallery": CatCulture, "viewpoint": CatLandmark, "zoo": CatCulture,
		"theme_park": CatCulture, "aquarium": CatCulture, "hotel": CatLodging,
		"information": CatOther,
	},
	"railway": {
		"station": CatTransport, "halt": CatTransport,
		"subway_entrance": CatTransport, "tram_stop": CatTransport,
	},
	"public_transport": {"station": CatTransport},
	"amenity": {
		"university": CatEducation, "college": CatEducation, "school": CatEducation,
		"library": CatEducation, "hospital": CatHealth, "clinic": CatHealth,
		"theatre": CatCulture, "cinema": CatCulture, "arts_centre": CatCulture,
		"museum": CatCulture, "townhall": CatCivic, "courthouse": CatCivic,
		"police": CatCivic, "fire_station": CatCivic, "embassy": CatCivic,
		"community_centre": CatCivic, "place_of_worship": CatWorship,
		"marketplace": CatCommerce, "conference_centre": CatCommerce,
		"exhibition_centre": CatCulture, "casino": CatCommerce,
		"ferry_terminal": CatTransport, "bus_station": CatTransport,
	},
	"place": {
		"square": CatPlace, "neighbourhood": CatPlace, "suburb": CatPlace,
		"quarter": CatPlace, "island": CatPlace, "islet": CatPlace, "locality": CatPlace,
	},
	"natural":  {"peak": CatLandmark, "beach": CatPlace, "cape": CatPlace},
	"man_made": {"tower": CatLandmark, "lighthouse": CatLandmark, "obelisk": CatLandmark},
	"historic": {}, // any value: handled below
	"leisure": {
		"park": CatPark, "garden": CatPark, "nature_reserve": CatPark,
		"pitch": CatPark, "playground": CatPark, "sports_centre": CatCulture,
		"stadium": CatLandmark, "marina": CatTransport, "common": CatPark,
	},
	"landuse": {
		"grass": CatPark, "forest": CatPark, "recreation_ground": CatPark,
		"cemetery": CatPlace, "village_green": CatPark,
	},
	"building": {
		"church": CatWorship, "cathedral": CatWorship, "chapel": CatWorship,
		"mosque": CatWorship, "temple": CatWorship, "synagogue": CatWorship,
		"hospital": CatHealth, "school": CatEducation, "university": CatEducation,
		"college": CatEducation, "dormitory": CatEducation,
		"train_station": CatTransport, "transportation": CatTransport,
		"civic": CatCivic, "public": CatCivic, "government": CatCivic,
		"hotel": CatLodging, "retail": CatCommerce, "commercial": CatCommerce,
		"office": CatCommerce, "supermarket": CatCommerce, "stadium": CatLandmark,
	},
}

// ClassifyPOI returns the category for an OSM tag pair.
func ClassifyPOI(key, value string) string {
	// A historic=* of any value is a landmark; the value vocabulary is long and
	// enumerating it buys nothing.
	if key == "historic" && value != "" {
		return CatLandmark
	}
	if byValue, ok := classifyRules[key]; ok {
		if cat, ok := byValue[value]; ok {
			return cat
		}
	}
	return CatOther
}

// classifyNode picks the most specific category a POI node's tags support. Tag
// order matters — a node tagged both amenity=place_of_worship and
// historic=church should read as worship, not as a generic landmark — so the
// keys are probed in priority order rather than by map iteration.
var poiNodeKeyPriority = []string{
	"railway", "public_transport", "amenity", "tourism", "historic",
	"man_made", "natural", "place",
}

func classifyNode(tags map[string]string) (tag, category string, ok bool) {
	for _, key := range poiNodeKeyPriority {
		value := tags[key]
		if value == "" {
			continue
		}
		cat := ClassifyPOI(key, value)
		if cat == CatOther && key != "historic" {
			continue
		}
		return key + "=" + value, cat, true
	}
	return "", "", false
}

// categoryWeight biases the prominence score so that a small named church
// outranks a large anonymous office block. Values are relative only.
var categoryWeight = map[string]float64{
	CatLandmark: 3.0, CatCulture: 2.2, CatWorship: 2.0, CatTransport: 1.9,
	CatCivic: 1.6, CatEducation: 1.4, CatHealth: 1.4, CatPark: 1.2,
	CatPlace: 1.1, CatLodging: 1.0, CatCommerce: 0.9, CatOther: 0.6,
}

// ScorePOI ranks a POI for display priority. The runtime draws a bounded number
// of labels, so this decides which ones survive: physical prominence (a tower is
// visible from far away, a footprint's area stands in for how much of the view
// it occupies) scaled by what kind of place it is.
func ScorePOI(p POI) float64 {
	score := 1.0
	if p.Height > 0 {
		score += p.Height / 25.0
	}
	if p.AreaM2 > 0 {
		score += math.Sqrt(p.AreaM2) / 30.0
	}
	return score * categoryWeight[p.Category]
}

// minNamedAreaM2 keeps a named flowerbed out of the label layer while letting a
// genuine square through.
const minNamedAreaM2 = 1500

// poiDedupeRadius is how close a node POI has to be to a same-named footprint
// POI to be considered the same place. OSM routinely carries both — the
// building way and a separate node for the venue inside it — and the footprint
// is the better anchor because it has a height.
const poiDedupeRadius = 60.0

// CollectPOIs derives the POI layer from an already-normalised IR plus the raw
// POI nodes, which have no other consumer. Output is deterministic: ranked, then
// tie-broken by OSM id.
func CollectPOIs(ir *IR, nodes []osmElement, project func(lat, lon float64) (float64, float64),
	halfSize float64) []POI {
	var out []POI

	for _, b := range ir.Buildings {
		if b.Name == "" || len(b.Outer) < 3 {
			continue
		}
		c := Centroid(b.Outer)
		out = append(out, POI{
			ID: b.ID, Name: b.Name, Tag: "building=" + b.Kind,
			Category: ClassifyPOI("building", b.Kind), Source: "building",
			Pos: c, Height: b.Height, AreaM2: b.AreaM2,
		})
	}

	for _, a := range ir.Landuse {
		if a.Name == "" || len(a.Outer) < 3 {
			continue
		}
		area := RingArea(a.Outer)
		if area < minNamedAreaM2 {
			continue
		}
		key, value, _ := strings.Cut(a.Tag, "=")
		out = append(out, POI{
			ID: a.ID, Name: a.Name, Tag: a.Tag,
			Category: ClassifyPOI(key, value), Source: "area",
			Pos: Centroid(a.Outer), AreaM2: area,
		})
	}

	for _, e := range nodes {
		if e.Type != "node" || e.Tags["name"] == "" {
			continue
		}
		tag, category, ok := classifyNode(e.Tags)
		if !ok {
			continue
		}
		x, y := project(e.Lat, e.Lon)
		out = append(out, POI{
			ID: e.ID, Name: e.Tags["name"], Tag: tag,
			Category: category, Source: "node", Pos: [2]float64{x, y},
		})
	}

	// The query box is padded past the tile so edge geometry is complete; a label
	// for a place outside the terrain would point at nothing.
	kept := out[:0]
	for _, p := range out {
		if math.Abs(p.Pos[0]) <= halfSize && math.Abs(p.Pos[1]) <= halfSize {
			kept = append(kept, p)
		}
	}
	out = dedupePOIs(kept)

	for i := range out {
		out[i].Rank = ScorePOI(out[i])
	}
	sort.SliceStable(out, func(i, j int) bool {
		if out[i].Rank != out[j].Rank {
			return out[i].Rank > out[j].Rank
		}
		return out[i].ID < out[j].ID
	})
	return out
}

// dedupePOIs drops node POIs that duplicate a nearby footprint or area POI of
// the same name. Footprints are appended first, so a stable pass that prefers
// the earlier entry keeps the anchor with a height.
func dedupePOIs(in []POI) []POI {
	out := make([]POI, 0, len(in))
	for _, p := range in {
		duplicate := false
		for _, kept := range out {
			if !strings.EqualFold(kept.Name, p.Name) {
				continue
			}
			if dist2(kept.Pos, p.Pos) <= poiDedupeRadius*poiDedupeRadius {
				duplicate = true
				break
			}
		}
		if !duplicate {
			out = append(out, p)
		}
	}
	return out
}

// CountPOIsByCategory is the build report's data-quality line for this layer.
func CountPOIsByCategory(pois []POI) map[string]int {
	counts := make(map[string]int, len(POICategories))
	for _, p := range pois {
		counts[p.Category]++
	}
	return counts
}

// FormatPOICounts renders the per-category tally in canonical order, skipping
// empty buckets.
func FormatPOICounts(pois []POI) string {
	counts := CountPOIsByCategory(pois)
	var parts []string
	for _, category := range POICategories {
		if n := counts[category]; n > 0 {
			parts = append(parts, category+" "+strconv.Itoa(n))
		}
	}
	if len(parts) == 0 {
		return "none"
	}
	return strings.Join(parts, ", ")
}
