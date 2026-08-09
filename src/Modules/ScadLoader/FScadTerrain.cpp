// FScadTerrain.cpp - Low-poly heightfield terrain backend for gk_terrain().
//
// Everything here is deterministic for a given spec: randomness comes from
// splitmix64-style integer hashing of (seed, lattice coords) only, evaluated
// in double precision. The compiled heightfield is sequential: each feature
// operator is compiled against the height function of the features before it
// (rivers sample the pre-carve terrain for their water line, pads flatten the
// terrain that the mountains already raised, ...).
#include "Modules/ScadLoader/FScadTerrain.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <unordered_map>

namespace Assets::Scad
{
    namespace
    {
        // ------------------------------------------------------------------
        // Deterministic hashing + value noise
        // ------------------------------------------------------------------
        uint64_t HashMix(uint64_t h)
        {
            h = (h ^ (h >> 30)) * 0xBF58476D1CE4E5B9ULL;
            h = (h ^ (h >> 27)) * 0x94D049BB133111EBULL;
            return h ^ (h >> 31);
        }

        uint64_t Hash2(uint64_t seed, int64_t ix, int64_t iy)
        {
            uint64_t h = seed ^ 0x8E44C7A1D3F29B6DULL;
            h = HashMix(h ^ (static_cast<uint64_t>(ix) * 0x9E3779B97F4A7C15ULL));
            h = HashMix(h ^ (static_cast<uint64_t>(iy) * 0xC2B2AE3D27D4EB4FULL));
            return h;
        }

        double Rand01(uint64_t seed, int64_t ix, int64_t iy)
        {
            return static_cast<double>(Hash2(seed, ix, iy) >> 11) * (1.0 / 9007199254740992.0);
        }

        double Fade(double t)
        {
            return t * t * (3.0 - 2.0 * t);
        }

        // Bilinear value noise in [-1, 1].
        double ValueNoise(uint64_t seed, double x, double y)
        {
            const double fx = std::floor(x);
            const double fy = std::floor(y);
            const auto ix = static_cast<int64_t>(fx);
            const auto iy = static_cast<int64_t>(fy);
            const double tx = Fade(x - fx);
            const double ty = Fade(y - fy);
            const double v00 = Rand01(seed, ix, iy);
            const double v10 = Rand01(seed, ix + 1, iy);
            const double v01 = Rand01(seed, ix, iy + 1);
            const double v11 = Rand01(seed, ix + 1, iy + 1);
            const double a = v00 + (v10 - v00) * tx;
            const double b = v01 + (v11 - v01) * tx;
            return (a + (b - a) * ty) * 2.0 - 1.0;
        }

        // fbm in roughly [-1, 1] (normalized by total gain).
        double Fbm(uint64_t seed, double x, double y, int octaves)
        {
            double sum = 0.0;
            double amp = 1.0;
            double total = 0.0;
            double fx = x;
            double fy = y;
            for (int i = 0; i < octaves; ++i)
            {
                sum += amp * ValueNoise(seed + static_cast<uint64_t>(i) * 0xA24BAED4963EE407ULL, fx, fy);
                total += amp;
                amp *= 0.5;
                fx *= 2.0;
                fy *= 2.0;
            }
            return total > 0.0 ? sum / total : 0.0;
        }

        double Clamp01(double v)
        {
            return std::min(1.0, std::max(0.0, v));
        }

        double SmoothStep01(double t)
        {
            const double c = Clamp01(t);
            return c * c * (3.0 - 2.0 * c);
        }

        // Polynomial smooth max/min (IQ style); k > 0 is the blend band.
        double SmoothMax(double a, double b, double k)
        {
            const double h = Clamp01(0.5 + 0.5 * (b - a) / k);
            return a + (b - a) * h + k * h * (1.0 - h);
        }

        double SmoothMin(double a, double b, double k)
        {
            return -SmoothMax(-a, -b, k);
        }

        // ------------------------------------------------------------------
        // Polyline helpers
        // ------------------------------------------------------------------
        std::vector<glm::dvec2> ChaikinRefine(const std::vector<glm::dvec2>& pts, int iterations)
        {
            std::vector<glm::dvec2> cur = pts;
            for (int it = 0; it < iterations && cur.size() >= 3; ++it)
            {
                std::vector<glm::dvec2> next;
                next.reserve(cur.size() * 2);
                next.push_back(cur.front());
                for (size_t i = 0; i + 1 < cur.size(); ++i)
                {
                    next.push_back(cur[i] * 0.75 + cur[i + 1] * 0.25);
                    next.push_back(cur[i] * 0.25 + cur[i + 1] * 0.75);
                }
                next.push_back(cur.back());
                cur = std::move(next);
            }
            return cur;
        }

        struct PolylineHit
        {
            double dist = 1e30;
            size_t seg = 0;
            double segT = 0.0; // [0,1] along the closest segment
        };

        PolylineHit ClosestOnPolyline(const std::vector<glm::dvec2>& pts, const glm::dvec2& p)
        {
            PolylineHit hit;
            for (size_t i = 0; i + 1 < pts.size(); ++i)
            {
                const glm::dvec2 a = pts[i];
                const glm::dvec2 d = pts[i + 1] - a;
                const double len2 = glm::dot(d, d);
                const double t = len2 > 1e-12 ? Clamp01(glm::dot(p - a, d) / len2) : 0.0;
                const glm::dvec2 q = a + d * t;
                const double dist = glm::length(p - q);
                if (dist < hit.dist)
                {
                    hit.dist = dist;
                    hit.seg = i;
                    hit.segT = t;
                }
            }
            return hit;
        }

        double InterpAlong(const std::vector<double>& values, const PolylineHit& hit)
        {
            if (values.empty()) return 0.0;
            if (hit.seg + 1 >= values.size()) return values.back();
            return values[hit.seg] + (values[hit.seg + 1] - values[hit.seg]) * hit.segT;
        }

        void BoxFilterInPlace(std::vector<double>& v, int radius)
        {
            if (v.size() < 3 || radius <= 0) return;
            std::vector<double> src = v;
            const int n = static_cast<int>(v.size());
            for (int i = 0; i < n; ++i)
            {
                double sum = 0.0;
                int count = 0;
                for (int k = -radius; k <= radius; ++k)
                {
                    const int j = i + k;
                    if (j < 0 || j >= n) continue;
                    sum += src[j];
                    ++count;
                }
                v[i] = sum / static_cast<double>(count);
            }
        }

        // ------------------------------------------------------------------
        // Palettes
        // ------------------------------------------------------------------
        struct FPaletteDef
        {
            glm::vec4 biomeColors[static_cast<size_t>(ETerrainBiome::Count)];
            glm::vec4 waterColor;
            glm::vec4 skirtColor;
            double snowFrac = 0.75;       // snow above base + frac * span
            double rockSlopeDeg = 38.0;   // steeper than this => rock
            double dryFrac = 0.45;        // dry grass above base + frac * span
            double minReliefForSnow = 6.0;
        };

        const FPaletteDef* FindPalette(const std::string& name)
        {
            static const FPaletteDef temperate = {
                {
                    {0.30f, 0.42f, 0.20f, 1.0f}, // Grass
                    {0.24f, 0.35f, 0.17f, 1.0f}, // GrassDark
                    {0.40f, 0.38f, 0.22f, 1.0f}, // DryGrass
                    {0.55f, 0.48f, 0.31f, 1.0f}, // Sand
                    {0.35f, 0.33f, 0.31f, 1.0f}, // Rock
                    {0.44f, 0.42f, 0.40f, 1.0f}, // RockHigh
                    {0.82f, 0.84f, 0.88f, 1.0f}, // Snow
                    {0.36f, 0.31f, 0.22f, 1.0f}, // Bed
                    {0.35f, 0.30f, 0.24f, 1.0f}, // Road
                    {0.33f, 0.31f, 0.27f, 1.0f}, // Pad
                },
                {0.20f, 0.42f, 0.50f, 1.0f},
                {0.30f, 0.27f, 0.23f, 1.0f},
                0.75, 38.0, 0.45, 6.0};
            static const FPaletteDef arid = {
                {
                    {0.45f, 0.42f, 0.24f, 1.0f},
                    {0.38f, 0.36f, 0.20f, 1.0f},
                    {0.50f, 0.44f, 0.26f, 1.0f},
                    {0.62f, 0.52f, 0.34f, 1.0f},
                    {0.48f, 0.30f, 0.22f, 1.0f},
                    {0.56f, 0.38f, 0.28f, 1.0f},
                    {0.82f, 0.84f, 0.88f, 1.0f},
                    {0.42f, 0.34f, 0.24f, 1.0f},
                    {0.38f, 0.32f, 0.26f, 1.0f},
                    {0.36f, 0.33f, 0.28f, 1.0f},
                },
                {0.22f, 0.44f, 0.48f, 1.0f},
                {0.34f, 0.26f, 0.20f, 1.0f},
                0.92, 34.0, 0.25, 40.0};
            static const FPaletteDef alpine = {
                {
                    {0.28f, 0.38f, 0.22f, 1.0f},
                    {0.22f, 0.32f, 0.18f, 1.0f},
                    {0.36f, 0.35f, 0.24f, 1.0f},
                    {0.48f, 0.44f, 0.34f, 1.0f},
                    {0.38f, 0.37f, 0.36f, 1.0f},
                    {0.50f, 0.50f, 0.52f, 1.0f},
                    {0.86f, 0.88f, 0.92f, 1.0f},
                    {0.32f, 0.29f, 0.24f, 1.0f},
                    {0.33f, 0.29f, 0.25f, 1.0f},
                    {0.31f, 0.30f, 0.28f, 1.0f},
                },
                {0.18f, 0.38f, 0.48f, 1.0f},
                {0.28f, 0.26f, 0.24f, 1.0f},
                0.55, 34.0, 0.35, 4.0};

            if (name == "temperate") return &temperate;
            if (name == "arid") return &arid;
            if (name == "alpine") return &alpine;
            return nullptr;
        }

        // ------------------------------------------------------------------
        // Compiled heightfield
        // ------------------------------------------------------------------
        struct FCompiledFeature
        {
            const FTerrainFeature* src = nullptr;
            uint64_t seed = 0;
            std::vector<glm::dvec2> pts; // Chaikin-refined polyline
            std::vector<double> level;   // river: water level / road: target height
            double waterLevel = 0.0;     // river avg / lake
            double bedLevel = 0.0;       // lake
            double padTarget = 0.0;      // pad flatten height
            double cosR = 1.0;
            double sinR = 0.0;
            glm::dvec2 bboxMin{-1e30, -1e30};
            glm::dvec2 bboxMax{1e30, 1e30};

            bool Rejects(const glm::dvec2& p) const
            {
                return p.x < bboxMin.x || p.x > bboxMax.x || p.y < bboxMin.y || p.y > bboxMax.y;
            }
        };

        class FFieldBuilder
        {
        public:
            explicit FFieldBuilder(const FTerrainSpec& spec) : spec_(spec)
            {
                const double minSize = std::min(spec.size.x, spec.size.y);
                baseFreq_ = 1.0 / std::max(1.0, minSize / (3.0 + 5.0 * Clamp01(spec.roughness)));
                baseOctaves_ = 2 + static_cast<int>(std::lround(Clamp01(spec.roughness) * 2.0));
            }

            void Compile()
            {
                compiled_.reserve(spec_.features.size());
                for (size_t i = 0; i < spec_.features.size(); ++i)
                {
                    const FTerrainFeature& f = spec_.features[i];
                    FCompiledFeature c;
                    c.src = &f;
                    c.seed = HashMix(spec_.seed ^ (0xF00D0000ULL + i * 0x9E3779B97F4A7C15ULL));
                    CompileFeature(f, c, compiled_.size());
                    compiled_.push_back(std::move(c));
                }
            }

            // Height with the first featureCount features applied.
            double EvalHeight(double x, double y, size_t featureCount) const
            {
                const glm::dvec2 p(x, y);
                double h = spec_.baseHeight;
                if (spec_.relief > 0.0)
                {
                    h += spec_.relief * Fbm(spec_.seed, x * baseFreq_, y * baseFreq_, baseOctaves_);
                }
                const size_t count = std::min(featureCount, compiled_.size());
                for (size_t i = 0; i < count; ++i)
                {
                    ApplyFeature(compiled_[i], p, h);
                }
                return h;
            }

            double EvalHeight(double x, double y) const { return EvalHeight(x, y, compiled_.size()); }

            const std::vector<FCompiledFeature>& Features() const { return compiled_; }

            // Water probe: highest water surface covering (x, y) given final
            // terrain height h. Returns false when the point is dry.
            bool WaterAt(const glm::dvec2& p, double h, double& outLevel) const
            {
                bool wet = false;
                double level = -1e30;
                if (spec_.hasWaterLevel && h < spec_.waterLevel - 0.02)
                {
                    level = std::max(level, spec_.waterLevel);
                    wet = true;
                }
                for (const FCompiledFeature& c : compiled_)
                {
                    const FTerrainFeature& f = *c.src;
                    if (f.type == FTerrainFeature::EType::River)
                    {
                        if (c.Rejects(p) || c.pts.size() < 2) continue;
                        const PolylineHit hit = ClosestOnPolyline(c.pts, p);
                        if (hit.dist > f.width * 0.55) continue;
                        const double wl = InterpAlong(c.level, hit);
                        if (h < wl - 0.03)
                        {
                            level = std::max(level, wl);
                            wet = true;
                        }
                    }
                    else if (f.type == FTerrainFeature::EType::Lake)
                    {
                        if (c.Rejects(p)) continue;
                        if (glm::length(p - f.at) < f.radius && h < c.waterLevel - 0.02)
                        {
                            level = std::max(level, c.waterLevel);
                            wet = true;
                        }
                    }
                }
                if (wet) outLevel = level;
                return wet;
            }

            bool NearRoadOrPad(const glm::dvec2& p, double margin) const
            {
                for (const FCompiledFeature& c : compiled_)
                {
                    const FTerrainFeature& f = *c.src;
                    if (f.type == FTerrainFeature::EType::Road)
                    {
                        if (p.x < c.bboxMin.x - margin || p.x > c.bboxMax.x + margin ||
                            p.y < c.bboxMin.y - margin || p.y > c.bboxMax.y + margin)
                        {
                            continue;
                        }
                        if (ClosestOnPolyline(c.pts, p).dist < f.width * 0.5 + margin) return true;
                    }
                    else if (f.type == FTerrainFeature::EType::Pad)
                    {
                        glm::dvec2 local;
                        PadLocal(c, f, p, local);
                        if (std::abs(local.x) < f.size.x * 0.5 + margin &&
                            std::abs(local.y) < f.size.y * 0.5 + margin)
                        {
                            return true;
                        }
                    }
                }
                return false;
            }

            bool OnRoad(const glm::dvec2& p) const
            {
                for (const FCompiledFeature& c : compiled_)
                {
                    if (c.src->type != FTerrainFeature::EType::Road) continue;
                    if (c.Rejects(p)) continue;
                    if (ClosestOnPolyline(c.pts, p).dist < c.src->width * 0.5) return true;
                }
                return false;
            }

            bool OnPad(const glm::dvec2& p) const
            {
                for (const FCompiledFeature& c : compiled_)
                {
                    const FTerrainFeature& f = *c.src;
                    if (f.type != FTerrainFeature::EType::Pad) continue;
                    glm::dvec2 local;
                    PadLocal(c, f, p, local);
                    if (std::abs(local.x) <= f.size.x * 0.5 && std::abs(local.y) <= f.size.y * 0.5) return true;
                }
                return false;
            }

        private:
            static void PadLocal(const FCompiledFeature& c, const FTerrainFeature& f,
                                 const glm::dvec2& p, glm::dvec2& outLocal)
            {
                const glm::dvec2 d = p - f.at;
                outLocal.x = d.x * c.cosR + d.y * c.sinR;
                outLocal.y = -d.x * c.sinR + d.y * c.cosR;
            }

            void CompileFeature(const FTerrainFeature& f, FCompiledFeature& c, size_t priorCount)
            {
                switch (f.type)
                {
                case FTerrainFeature::EType::Mountain:
                case FTerrainFeature::EType::Plateau:
                {
                    const double pad = f.radius * 1.3 + 2.0;
                    c.bboxMin = f.at - glm::dvec2(pad);
                    c.bboxMax = f.at + glm::dvec2(pad);
                    break;
                }
                case FTerrainFeature::EType::Lake:
                {
                    const double pad = f.radius * 1.4 + 2.0;
                    c.bboxMin = f.at - glm::dvec2(pad);
                    c.bboxMax = f.at + glm::dvec2(pad);
                    // Water level: just below the lowest rim of the pre-lake
                    // terrain so the basin actually holds the water.
                    double rim = 1e30;
                    for (int k = 0; k < 8; ++k)
                    {
                        const double ang = 2.0 * kPi * static_cast<double>(k) / 8.0;
                        const glm::dvec2 s = f.at + glm::dvec2(std::cos(ang), std::sin(ang)) * (f.radius * 0.85);
                        rim = std::min(rim, EvalHeight(s.x, s.y, priorCount));
                    }
                    c.waterLevel = rim - 0.12 * f.depth;
                    c.bedLevel = c.waterLevel - f.depth;
                    break;
                }
                case FTerrainFeature::EType::Ridge:
                case FTerrainFeature::EType::River:
                case FTerrainFeature::EType::Road:
                {
                    c.pts = ChaikinRefine(f.pts, 2);
                    glm::dvec2 lo(1e30), hi(-1e30);
                    for (const glm::dvec2& q : c.pts)
                    {
                        lo = glm::min(lo, q);
                        hi = glm::max(hi, q);
                    }
                    const double pad = std::max(f.width * 2.4, f.width * 0.5 + 3.0);
                    c.bboxMin = lo - glm::dvec2(pad);
                    c.bboxMax = hi + glm::dvec2(pad);

                    if (f.type == FTerrainFeature::EType::River)
                    {
                        // Water line: sample the pre-carve terrain along the path,
                        // clamp monotonically downstream, smooth, re-clamp.
                        c.level.resize(c.pts.size());
                        for (size_t i = 0; i < c.pts.size(); ++i)
                        {
                            c.level[i] = EvalHeight(c.pts[i].x, c.pts[i].y, priorCount);
                        }
                        for (size_t i = 1; i < c.level.size(); ++i)
                        {
                            c.level[i] = std::min(c.level[i], c.level[i - 1]);
                        }
                        BoxFilterInPlace(c.level, 2);
                        for (size_t i = 1; i < c.level.size(); ++i)
                        {
                            c.level[i] = std::min(c.level[i], c.level[i - 1]);
                        }
                        for (double& v : c.level)
                        {
                            v -= 0.35 * f.depth;
                        }
                    }
                    else if (f.type == FTerrainFeature::EType::Road)
                    {
                        c.level.resize(c.pts.size());
                        for (size_t i = 0; i < c.pts.size(); ++i)
                        {
                            c.level[i] = EvalHeight(c.pts[i].x, c.pts[i].y, priorCount);
                        }
                        BoxFilterInPlace(c.level, 2);
                        BoxFilterInPlace(c.level, 2);
                    }
                    break;
                }
                case FTerrainFeature::EType::Pad:
                {
                    const double rad = f.rot * kDeg2Rad;
                    c.cosR = std::cos(rad);
                    c.sinR = std::sin(rad);
                    const double pad = 0.5 * std::max(f.size.x, f.size.y) * 1.6 + 2.0;
                    c.bboxMin = f.at - glm::dvec2(pad);
                    c.bboxMax = f.at + glm::dvec2(pad);
                    double sum = EvalHeight(f.at.x, f.at.y, priorCount);
                    int count = 1;
                    for (int sx = -1; sx <= 1; sx += 2)
                    {
                        for (int sy = -1; sy <= 1; sy += 2)
                        {
                            const glm::dvec2 corner(sx * f.size.x * 0.35, sy * f.size.y * 0.35);
                            const glm::dvec2 world = f.at + glm::dvec2(corner.x * c.cosR - corner.y * c.sinR,
                                                                       corner.x * c.sinR + corner.y * c.cosR);
                            sum += EvalHeight(world.x, world.y, priorCount);
                            ++count;
                        }
                    }
                    c.padTarget = sum / static_cast<double>(count);
                    break;
                }
                }
            }

            void ApplyFeature(const FCompiledFeature& c, const glm::dvec2& p, double& h) const
            {
                const FTerrainFeature& f = *c.src;
                if (c.Rejects(p)) return;

                switch (f.type)
                {
                case FTerrainFeature::EType::Mountain:
                {
                    const double d = glm::length(p - f.at) / std::max(1e-6, f.radius);
                    if (d >= 1.0) return;
                    double profile = f.height * (1.0 - d * d) * (1.0 - d * d);
                    if (f.rugged > 0.0)
                    {
                        const double freq = 2.2 / std::max(1.0, f.radius);
                        profile *= 1.0 + Clamp01(f.rugged) * 0.45 * Fbm(c.seed, p.x * freq, p.y * freq, 3);
                        profile = std::max(0.0, profile);
                    }
                    const double k = std::max(0.4, 0.18 * f.height);
                    h = SmoothMax(h, spec_.baseHeight + profile, k);
                    return;
                }
                case FTerrainFeature::EType::Ridge:
                {
                    if (c.pts.size() < 2) return;
                    const double half = std::max(1e-6, f.width * 0.5);
                    const PolylineHit hit = ClosestOnPolyline(c.pts, p);
                    if (hit.dist >= half) return;
                    const double s = SmoothStep01(1.0 - hit.dist / half);
                    double profile = f.height * s;
                    const double freq = 3.0 / std::max(1.0, f.width);
                    profile *= 1.0 + 0.25 * Fbm(c.seed, p.x * freq, p.y * freq, 2);
                    profile = std::max(0.0, profile);
                    const double k = std::max(0.4, 0.15 * f.height);
                    h = SmoothMax(h, spec_.baseHeight + profile, k);
                    return;
                }
                case FTerrainFeature::EType::Plateau:
                {
                    const double d = glm::length(p - f.at);
                    if (d >= f.radius) return;
                    const double edge = std::max(1e-6, 0.30 * f.radius);
                    const double s = SmoothStep01((f.radius - d) / edge);
                    const double wobble = 1.0 + 0.05 * Fbm(c.seed, p.x * 0.15, p.y * 0.15, 2);
                    const double target = spec_.baseHeight + f.height * s * wobble;
                    h = SmoothMax(h, target, std::max(0.3, 0.10 * f.height));
                    return;
                }
                case FTerrainFeature::EType::Lake:
                {
                    const double d = glm::length(p - f.at);
                    if (d >= f.radius) return;
                    const double edge = std::max(1e-6, 0.35 * f.radius);
                    const double s = SmoothStep01((f.radius - d) / edge);
                    const double carved = h - s * (h - c.bedLevel);
                    h = SmoothMin(h, carved, 0.3);
                    return;
                }
                case FTerrainFeature::EType::River:
                {
                    if (c.pts.size() < 2) return;
                    const double half = std::max(1e-6, f.width * 0.5);
                    const double bank = half * 2.2;
                    const PolylineHit hit = ClosestOnPolyline(c.pts, p);
                    if (hit.dist >= bank) return;
                    const double wl = InterpAlong(c.level, hit);
                    const double bedDepth = 0.75 * f.depth;
                    const double bl = wl - bedDepth;
                    double target;
                    if (hit.dist <= half)
                    {
                        const double crossT = hit.dist / half;
                        target = bl + crossT * crossT * bedDepth * 0.45;
                    }
                    else
                    {
                        const double s = SmoothStep01((hit.dist - half) / (bank - half));
                        target = bl + bedDepth * 0.45 + s * (h - (bl + bedDepth * 0.45));
                    }
                    h = SmoothMin(h, target, 0.4);
                    return;
                }
                case FTerrainFeature::EType::Road:
                {
                    if (c.pts.size() < 2) return;
                    const double half = std::max(1e-6, f.width * 0.5);
                    const double apron = half * 2.0;
                    const PolylineHit hit = ClosestOnPolyline(c.pts, p);
                    if (hit.dist >= apron) return;
                    const double targetLevel = InterpAlong(c.level, hit);
                    // Roads cut and fill, but only up to a max fill depth:
                    // where the terrain drops further (a carved river), the
                    // gap stays open for a bridge instead of damming a ford.
                    const double maxFill = 0.9;
                    if (h < targetLevel - maxFill) return;
                    if (hit.dist <= half)
                    {
                        h = targetLevel;
                    }
                    else
                    {
                        const double s = SmoothStep01((hit.dist - half) / (apron - half));
                        h = targetLevel + s * (h - targetLevel);
                    }
                    return;
                }
                case FTerrainFeature::EType::Pad:
                {
                    glm::dvec2 local;
                    PadLocal(c, f, p, local);
                    const double hx = f.size.x * 0.5;
                    const double hy = f.size.y * 0.5;
                    const double apron = std::max(1.5, 0.35 * std::min(f.size.x, f.size.y));
                    const double dx = std::abs(local.x) - hx;
                    const double dy = std::abs(local.y) - hy;
                    const double outside = std::max(dx, dy);
                    if (outside >= apron) return;
                    if (outside <= 0.0)
                    {
                        h = c.padTarget;
                    }
                    else
                    {
                        const double s = SmoothStep01(outside / apron);
                        h = c.padTarget + s * (h - c.padTarget);
                    }
                    return;
                }
                }
            }

            const FTerrainSpec& spec_;
            double baseFreq_ = 0.01;
            int baseOctaves_ = 3;
            std::vector<FCompiledFeature> compiled_;
        };

        double Area2(const glm::dvec2& a, const glm::dvec2& b, const glm::dvec2& c)
        {
            return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
        }

        glm::dvec2 XY(const glm::dvec3& v)
        {
            return {v.x, v.y};
        }

        const char* BiomeMaterialName(ETerrainBiome biome)
        {
            switch (biome)
            {
            case ETerrainBiome::Grass: return "terrain_grass";
            case ETerrainBiome::GrassDark: return "terrain_grass_dark";
            case ETerrainBiome::DryGrass: return "terrain_dry_grass";
            case ETerrainBiome::Sand: return "terrain_sand";
            case ETerrainBiome::Rock: return "terrain_rock";
            case ETerrainBiome::RockHigh: return "terrain_rock_high";
            case ETerrainBiome::Snow: return "terrain_snow";
            case ETerrainBiome::Bed: return "terrain_bed";
            case ETerrainBiome::Road: return "terrain_road";
            case ETerrainBiome::Pad: return "terrain_pad";
            default: return "terrain";
            }
        }

        // ------------------------------------------------------------------
        // Value decoding helpers
        // ------------------------------------------------------------------
        bool ReadVec2(const Value& v, glm::dvec2& out)
        {
            if (v.type != Value::Type::Vec || v.vec.size() < 2) return false;
            out.x = v.vec[0].AsNumber(out.x);
            out.y = v.vec[1].AsNumber(out.y);
            return true;
        }

        bool ReadPoints(const Value& v, std::vector<glm::dvec2>& out)
        {
            if (v.type != Value::Type::Vec) return false;
            out.clear();
            for (const Value& p : v.vec)
            {
                glm::dvec2 q(0.0);
                if (!ReadVec2(p, q)) return false;
                out.push_back(q);
            }
            return out.size() >= 2;
        }

        void AppendKeyNumber(std::string& key, double v)
        {
            char buf[40];
            std::snprintf(buf, sizeof(buf), "%.17g,", v);
            key += buf;
        }
    } // namespace

    // ----------------------------------------------------------------------
    // FTerrainData queries
    // ----------------------------------------------------------------------
    int FTerrainData::CellIndexAt(double x, double y) const
    {
        if (spec.cells.x <= 0 || spec.cells.y <= 0) return -1;
        const double dx = spec.size.x / spec.cells.x;
        const double dy = spec.size.y / spec.cells.y;
        const int i = std::clamp(static_cast<int>(std::floor((x + spec.size.x * 0.5) / dx)), 0, spec.cells.x - 1);
        const int j = std::clamp(static_cast<int>(std::floor((y + spec.size.y * 0.5) / dy)), 0, spec.cells.y - 1);
        return j * spec.cells.x + i;
    }

    bool FTerrainData::LocateTriangle(double x, double y, glm::dvec3& a, glm::dvec3& b, glm::dvec3& c) const
    {
        if (verts.empty() || spec.cells.x <= 0 || spec.cells.y <= 0) return false;

        const double hx = spec.size.x * 0.5;
        const double hy = spec.size.y * 0.5;
        const double eps = 1e-9;
        const double qx = std::clamp(x, -hx + eps, hx - eps);
        const double qy = std::clamp(y, -hy + eps, hy - eps);
        const glm::dvec2 p(qx, qy);

        const double dx = spec.size.x / spec.cells.x;
        const double dy = spec.size.y / spec.cells.y;
        const int ci = std::clamp(static_cast<int>(std::floor((qx + hx) / dx)), 0, spec.cells.x - 1);
        const int cj = std::clamp(static_cast<int>(std::floor((qy + hy) / dy)), 0, spec.cells.y - 1);

        auto cellTris = [&](int i, int j, int tri, glm::dvec3& ta, glm::dvec3& tb, glm::dvec3& tc)
        {
            const glm::dvec3& va = verts[j * vertsX + i];
            const glm::dvec3& vb = verts[j * vertsX + i + 1];
            const glm::dvec3& vc = verts[(j + 1) * vertsX + i + 1];
            const glm::dvec3& vd = verts[(j + 1) * vertsX + i];
            if (cellDiagFlip[j * spec.cells.x + i] == 0)
            {
                if (tri == 0) { ta = va; tb = vb; tc = vc; }
                else { ta = va; tb = vc; tc = vd; }
            }
            else
            {
                if (tri == 0) { ta = va; tb = vb; tc = vd; }
                else { ta = vb; tb = vc; tc = vd; }
            }
        };

        auto insideTri = [&](const glm::dvec3& ta, const glm::dvec3& tb, const glm::dvec3& tc)
        {
            const double denom = Area2(XY(ta), XY(tb), XY(tc));
            if (std::abs(denom) < 1e-12) return false;
            const double w0 = Area2(p, XY(tb), XY(tc)) / denom;
            const double w1 = Area2(XY(ta), p, XY(tc)) / denom;
            const double w2 = Area2(XY(ta), XY(tb), p) / denom;
            const double tol = -1e-9;
            return w0 >= tol && w1 >= tol && w2 >= tol;
        };

        // The XY jitter is < half a cell, so the containing triangle is in the
        // clamped cell or one of its 8 neighbours.
        for (int ring = 0; ring <= 1; ++ring)
        {
            for (int oj = -ring; oj <= ring; ++oj)
            {
                for (int oi = -ring; oi <= ring; ++oi)
                {
                    if (ring == 1 && std::abs(oi) != 1 && std::abs(oj) != 1) continue;
                    const int i = ci + oi;
                    const int j = cj + oj;
                    if (i < 0 || j < 0 || i >= spec.cells.x || j >= spec.cells.y) continue;
                    for (int tri = 0; tri < 2; ++tri)
                    {
                        glm::dvec3 ta, tb, tc;
                        cellTris(i, j, tri, ta, tb, tc);
                        if (insideTri(ta, tb, tc))
                        {
                            a = ta;
                            b = tb;
                            c = tc;
                            return true;
                        }
                    }
                }
            }
        }

        // Degenerate fallback (should not happen): extrapolate from the cell's
        // first triangle so callers always get a finite answer.
        cellTris(ci, cj, 0, a, b, c);
        return true;
    }

    double FTerrainData::HeightAt(double x, double y) const
    {
        glm::dvec3 a, b, c;
        if (!LocateTriangle(x, y, a, b, c)) return spec.baseHeight;
        const double hx = spec.size.x * 0.5;
        const double hy = spec.size.y * 0.5;
        const glm::dvec2 p(std::clamp(x, -hx + 1e-9, hx - 1e-9), std::clamp(y, -hy + 1e-9, hy - 1e-9));
        const double denom = Area2(XY(a), XY(b), XY(c));
        if (std::abs(denom) < 1e-12) return a.z;
        const double w0 = Area2(p, XY(b), XY(c)) / denom;
        const double w1 = Area2(XY(a), p, XY(c)) / denom;
        const double w2 = 1.0 - w0 - w1;
        return w0 * a.z + w1 * b.z + w2 * c.z;
    }

    glm::dvec3 FTerrainData::NormalAt(double x, double y) const
    {
        glm::dvec3 a, b, c;
        if (!LocateTriangle(x, y, a, b, c)) return {0.0, 0.0, 1.0};
        glm::dvec3 n = glm::cross(b - a, c - a);
        const double len = glm::length(n);
        if (len < 1e-12) return {0.0, 0.0, 1.0};
        n /= len;
        return n.z < 0.0 ? -n : n;
    }

    void FTerrainData::InfoAt(double x, double y, double& outHeight, double& outSlopeDeg,
                              bool& outWater, uint8_t& outBiome) const
    {
        outHeight = HeightAt(x, y);
        const glm::dvec3 n = NormalAt(x, y);
        outSlopeDeg = std::acos(std::clamp(n.z, -1.0, 1.0)) / kDeg2Rad;
        const int cell = CellIndexAt(x, y);
        if (cell >= 0 && cell < static_cast<int>(cellFlags.size()))
        {
            outWater = (cellFlags[cell] & kFlagWater) != 0;
            outBiome = cellBiome[cell];
        }
        else
        {
            outWater = false;
            outBiome = static_cast<uint8_t>(ETerrainBiome::Grass);
        }
    }

    // ----------------------------------------------------------------------
    // Spec decoding
    // ----------------------------------------------------------------------
    bool ScadTerrain::DecodeSpec(const Value& value, FTerrainSpec& outSpec,
                                 std::string& outError, std::vector<std::string>& outWarnings)
    {
        outError.clear();
        outSpec = FTerrainSpec();

        if (value.type != Value::Type::Vec || value.vec.size() < 8)
        {
            outError = "TERR must be a list [\"gkterr1\", size, cells, seed, base, waterLevel, palette, features]";
            return false;
        }
        const Value& tag = value.vec[0];
        if (tag.type != Value::Type::Str || tag.str != "gkterr1")
        {
            outError = "TERR[0] must be the version tag \"gkterr1\"";
            return false;
        }

        if (!ReadVec2(value.vec[1], outSpec.size) || outSpec.size.x <= 0.0 || outSpec.size.y <= 0.0)
        {
            outError = "TERR size must be [sizeX, sizeY] with positive values";
            return false;
        }

        glm::dvec2 cellsRaw(50.0, 50.0);
        if (!ReadVec2(value.vec[2], cellsRaw))
        {
            outError = "TERR cells must be [cellsX, cellsY]";
            return false;
        }
        const int cx = static_cast<int>(std::lround(cellsRaw.x));
        const int cy = static_cast<int>(std::lround(cellsRaw.y));
        outSpec.cells.x = std::clamp(cx, 4, 256);
        outSpec.cells.y = std::clamp(cy, 4, 256);
        if (outSpec.cells.x != cx || outSpec.cells.y != cy)
        {
            outWarnings.push_back("cells clamped to [4..256] per axis");
        }

        outSpec.seed = static_cast<uint64_t>(static_cast<int64_t>(value.vec[3].AsNumber(0.0)));

        const Value& base = value.vec[4];
        if (base.type == Value::Type::Vec)
        {
            if (!base.vec.empty()) outSpec.baseHeight = base.vec[0].AsNumber(0.0);
            if (base.vec.size() > 1) outSpec.relief = std::max(0.0, base.vec[1].AsNumber(1.0));
            if (base.vec.size() > 2) outSpec.roughness = Clamp01(base.vec[2].AsNumber(0.5));
        }
        else if (base.IsNumber())
        {
            outSpec.baseHeight = base.num;
        }

        const Value& water = value.vec[5];
        if (water.IsNumber())
        {
            outSpec.hasWaterLevel = true;
            outSpec.waterLevel = water.num;
        }

        const Value& palette = value.vec[6];
        if (palette.type == Value::Type::Str && !palette.str.empty())
        {
            if (FindPalette(palette.str))
            {
                outSpec.palette = palette.str;
            }
            else
            {
                outWarnings.push_back("unknown palette '" + palette.str + "' (using temperate)");
            }
        }

        const Value& features = value.vec[7];
        if (features.type != Value::Type::Vec)
        {
            outError = "TERR features must be a list";
            return false;
        }
        for (const Value& fv : features.vec)
        {
            if (fv.type != Value::Type::Vec || fv.vec.empty() || fv.vec[0].type != Value::Type::Str)
            {
                outWarnings.push_back("feature entries must start with a type string; skipped one");
                continue;
            }
            const std::string& kind = fv.vec[0].str;
            FTerrainFeature f;
            auto num = [&](size_t i, double d) { return i < fv.vec.size() ? fv.vec[i].AsNumber(d) : d; };
            bool ok = true;

            if (kind == "mountain" || kind == "plateau")
            {
                f.type = kind == "mountain" ? FTerrainFeature::EType::Mountain : FTerrainFeature::EType::Plateau;
                ok = fv.vec.size() >= 4 && ReadVec2(fv.vec[1], f.at);
                f.radius = num(2, 0.0);
                f.height = num(3, 0.0);
                f.rugged = Clamp01(num(4, 0.0));
                ok = ok && f.radius > 0.0;
            }
            else if (kind == "lake")
            {
                f.type = FTerrainFeature::EType::Lake;
                ok = fv.vec.size() >= 4 && ReadVec2(fv.vec[1], f.at);
                f.radius = num(2, 0.0);
                f.depth = num(3, 0.0);
                ok = ok && f.radius > 0.0 && f.depth > 0.0;
            }
            else if (kind == "ridge")
            {
                f.type = FTerrainFeature::EType::Ridge;
                ok = fv.vec.size() >= 4 && ReadPoints(fv.vec[1], f.pts);
                f.width = num(2, 0.0);
                f.height = num(3, 0.0);
                ok = ok && f.width > 0.0;
            }
            else if (kind == "river")
            {
                f.type = FTerrainFeature::EType::River;
                ok = fv.vec.size() >= 4 && ReadPoints(fv.vec[1], f.pts);
                f.width = num(2, 0.0);
                f.depth = num(3, 0.0);
                ok = ok && f.width > 0.0 && f.depth > 0.0;
            }
            else if (kind == "road")
            {
                f.type = FTerrainFeature::EType::Road;
                ok = fv.vec.size() >= 3 && ReadPoints(fv.vec[1], f.pts);
                f.width = num(2, 0.0);
                ok = ok && f.width > 0.0;
            }
            else if (kind == "pad")
            {
                f.type = FTerrainFeature::EType::Pad;
                ok = fv.vec.size() >= 3 && ReadVec2(fv.vec[1], f.at) && ReadVec2(fv.vec[2], f.size);
                f.rot = num(3, 0.0);
                ok = ok && f.size.x > 0.0 && f.size.y > 0.0;
            }
            else
            {
                outWarnings.push_back("unknown terrain feature '" + kind + "'; skipped");
                continue;
            }

            if (!ok)
            {
                outWarnings.push_back("malformed terrain feature '" + kind + "'; skipped");
                continue;
            }
            outSpec.features.push_back(std::move(f));
        }

        return true;
    }

    std::string ScadTerrain::SpecCacheKey(const FTerrainSpec& spec)
    {
        std::string key;
        key.reserve(256);
        key += "gkterr1|";
        AppendKeyNumber(key, spec.size.x);
        AppendKeyNumber(key, spec.size.y);
        AppendKeyNumber(key, static_cast<double>(spec.cells.x));
        AppendKeyNumber(key, static_cast<double>(spec.cells.y));
        AppendKeyNumber(key, static_cast<double>(spec.seed));
        AppendKeyNumber(key, spec.baseHeight);
        AppendKeyNumber(key, spec.relief);
        AppendKeyNumber(key, spec.roughness);
        AppendKeyNumber(key, spec.hasWaterLevel ? 1.0 : 0.0);
        AppendKeyNumber(key, spec.waterLevel);
        key += spec.palette;
        key += '|';
        for (const FTerrainFeature& f : spec.features)
        {
            AppendKeyNumber(key, static_cast<double>(f.type));
            AppendKeyNumber(key, f.at.x);
            AppendKeyNumber(key, f.at.y);
            AppendKeyNumber(key, f.size.x);
            AppendKeyNumber(key, f.size.y);
            AppendKeyNumber(key, f.rot);
            AppendKeyNumber(key, f.radius);
            AppendKeyNumber(key, f.height);
            AppendKeyNumber(key, f.depth);
            AppendKeyNumber(key, f.width);
            AppendKeyNumber(key, f.rugged);
            for (const glm::dvec2& p : f.pts)
            {
                AppendKeyNumber(key, p.x);
                AppendKeyNumber(key, p.y);
            }
            key += ';';
        }
        return key;
    }

    // ----------------------------------------------------------------------
    // Build: heightfield -> jittered grid -> masks -> colored triangle soups
    // ----------------------------------------------------------------------
    std::shared_ptr<const FTerrainData> ScadTerrain::Build(const FTerrainSpec& spec)
    {
        auto data = std::make_shared<FTerrainData>();
        data->spec = spec;

        FFieldBuilder field(spec);
        field.Compile();

        const int cx = spec.cells.x;
        const int cy = spec.cells.y;
        const int vx = cx + 1;
        const int vy = cy + 1;
        const double dx = spec.size.x / cx;
        const double dy = spec.size.y / cy;
        const double hx = spec.size.x * 0.5;
        const double hy = spec.size.y * 0.5;
        const double jitterAmp = 0.35;
        const double jitterMargin = 0.75 * std::max(dx, dy);

        data->vertsX = vx;
        data->vertsY = vy;
        data->verts.resize(static_cast<size_t>(vx) * vy);

        // ---- Vertices (jittered XY + analytic height) ----
        const uint64_t jitterSeed = HashMix(spec.seed ^ 0x51AB44D1ULL);
        for (int j = 0; j < vy; ++j)
        {
            for (int i = 0; i < vx; ++i)
            {
                const double gx = -hx + i * dx;
                const double gy = -hy + j * dy;
                double jx = 0.0;
                double jy = 0.0;
                const bool border = i == 0 || j == 0 || i == vx - 1 || j == vy - 1;
                if (!border && !field.NearRoadOrPad(glm::dvec2(gx, gy), jitterMargin))
                {
                    jx = (Rand01(jitterSeed, i, j) - 0.5) * 2.0 * jitterAmp * dx;
                    jy = (Rand01(jitterSeed ^ 0x77E5C1ULL, i, j) - 0.5) * 2.0 * jitterAmp * dy;
                }
                const double px = gx + jx;
                const double py = gy + jy;
                data->verts[static_cast<size_t>(j) * vx + i] = {px, py, field.EvalHeight(px, py)};
            }
        }

        double minH = 1e30;
        double maxH = -1e30;
        for (const glm::dvec3& v : data->verts)
        {
            minH = std::min(minH, v.z);
            maxH = std::max(maxH, v.z);
        }
        data->minHeight = minH;
        data->maxHeight = maxH;

        // ---- Cell triangulation (seeded diagonal, validity-checked) ----
        data->cellDiagFlip.resize(static_cast<size_t>(cx) * cy, 0);
        const uint64_t diagSeed = HashMix(spec.seed ^ 0xD1A60000ULL);
        for (int j = 0; j < cy; ++j)
        {
            for (int i = 0; i < cx; ++i)
            {
                const glm::dvec2 a = XY(data->verts[static_cast<size_t>(j) * vx + i]);
                const glm::dvec2 b = XY(data->verts[static_cast<size_t>(j) * vx + i + 1]);
                const glm::dvec2 c = XY(data->verts[(static_cast<size_t>(j) + 1) * vx + i + 1]);
                const glm::dvec2 d = XY(data->verts[(static_cast<size_t>(j) + 1) * vx + i]);
                const double eps = 1e-9;
                const bool diagAcOk = Area2(a, b, c) > eps && Area2(a, c, d) > eps;
                const bool diagBdOk = Area2(a, b, d) > eps && Area2(b, c, d) > eps;
                uint8_t flip = static_cast<uint8_t>(Hash2(diagSeed, i, j) & 1u);
                if (flip == 0 && !diagAcOk && diagBdOk) flip = 1;
                if (flip == 1 && !diagBdOk && diagAcOk) flip = 0;
                data->cellDiagFlip[static_cast<size_t>(j) * cx + i] = flip;
            }
        }

        // ---- Cell semantics (water / road / pad / biome) ----
        const FPaletteDef& pal = *FindPalette(FindPalette(spec.palette) ? spec.palette : "temperate");
        data->cellFlags.resize(static_cast<size_t>(cx) * cy, 0);
        data->cellWater.resize(static_cast<size_t>(cx) * cy, 0.0);
        data->cellBiome.resize(static_cast<size_t>(cx) * cy, static_cast<uint8_t>(ETerrainBiome::Grass));

        const double span = std::max(0.0, maxH - spec.baseHeight);
        const bool snowEligible = span >= pal.minReliefForSnow;
        const double snowLine = spec.baseHeight + pal.snowFrac * span;
        const double rockHighZ = spec.baseHeight + 0.5 * span;
        const double dryLine = span >= 3.0 ? spec.baseHeight + pal.dryFrac * span : 1e30;
        const double patchFreq = 8.0 / std::max(1.0, std::min(spec.size.x, spec.size.y));
        const uint64_t patchSeed = HashMix(spec.seed ^ 0xC0FFEEULL);

        for (int j = 0; j < cy; ++j)
        {
            for (int i = 0; i < cx; ++i)
            {
                const size_t cell = static_cast<size_t>(j) * cx + i;
                const glm::dvec2 center(-hx + (i + 0.5) * dx, -hy + (j + 0.5) * dy);
                const double h = field.EvalHeight(center.x, center.y);

                uint8_t flags = 0;
                double level = 0.0;
                if (field.WaterAt(center, h, level))
                {
                    flags |= FTerrainData::kFlagWater;
                    data->cellWater[cell] = level;
                }
                if (field.OnRoad(center)) flags |= FTerrainData::kFlagRoad;
                if (field.OnPad(center)) flags |= FTerrainData::kFlagPad;
                data->cellFlags[cell] = flags;
            }
        }

        // ---- Per-face coloring + land mesh ----
        std::map<uint32_t, FTerrainData::ColoredTris> landByColor;
        auto colorKeyOf = [](const glm::vec4& c) -> uint32_t
        {
            auto q = [](float v) { return static_cast<uint32_t>(Clamp01(v) * 255.0f + 0.5f); };
            return (q(c.r) << 24) | (q(c.g) << 16) | (q(c.b) << 8) | q(c.a);
        };
        auto emitLand = [&](ETerrainBiome biome, const glm::dvec3& a, const glm::dvec3& b, const glm::dvec3& c)
        {
            const glm::vec4 color = pal.biomeColors[static_cast<size_t>(biome)];
            FTerrainData::ColoredTris& bucket = landByColor[colorKeyOf(color)];
            bucket.color = color;
            if (bucket.materialName.empty()) bucket.materialName = BiomeMaterialName(biome);
            bucket.tris.push_back(a);
            bucket.tris.push_back(b);
            bucket.tris.push_back(c);
        };

        auto cellHasWaterNeighbor = [&](int i, int j, double& outLevel)
        {
            for (int oj = -1; oj <= 1; ++oj)
            {
                for (int oi = -1; oi <= 1; ++oi)
                {
                    const int ni = i + oi;
                    const int nj = j + oj;
                    if (ni < 0 || nj < 0 || ni >= cx || nj >= cy) continue;
                    const size_t nc = static_cast<size_t>(nj) * cx + ni;
                    if (data->cellFlags[nc] & FTerrainData::kFlagWater)
                    {
                        outLevel = data->cellWater[nc];
                        return true;
                    }
                }
            }
            return false;
        };

        auto classifyFace = [&](int i, int j, const glm::dvec3& a, const glm::dvec3& b,
                                const glm::dvec3& c) -> ETerrainBiome
        {
            const size_t cell = static_cast<size_t>(j) * cx + i;
            const uint8_t flags = data->cellFlags[cell];
            if (flags & FTerrainData::kFlagWater) return ETerrainBiome::Bed;

            const glm::dvec3 centroid = (a + b + c) / 3.0;
            // Road and pad masks are sampled per face, like the natural
            // biomes below.  Using the cell-center flags here made both
            // triangles of every touched cell share one material, producing
            // visibly axis-aligned, square feature boundaries.
            const glm::dvec2 centroidXY(centroid.x, centroid.y);
            if (field.OnRoad(centroidXY)) return ETerrainBiome::Road;
            if (field.OnPad(centroidXY)) return ETerrainBiome::Pad;

            glm::dvec3 n = glm::cross(b - a, c - a);
            const double len = glm::length(n);
            double slopeDeg = 0.0;
            if (len > 1e-12)
            {
                n /= len;
                slopeDeg = std::acos(std::clamp(std::abs(n.z), 0.0, 1.0)) / kDeg2Rad;
            }
            if (slopeDeg > pal.rockSlopeDeg)
            {
                return centroid.z > rockHighZ ? ETerrainBiome::RockHigh : ETerrainBiome::Rock;
            }
            if (snowEligible && centroid.z > snowLine) return ETerrainBiome::Snow;

            double waterLevel = 0.0;
            if (cellHasWaterNeighbor(i, j, waterLevel) && centroid.z < waterLevel + 0.5)
            {
                return ETerrainBiome::Sand;
            }
            if (centroid.z > dryLine) return ETerrainBiome::DryGrass;

            const double patch = Fbm(patchSeed, centroid.x * patchFreq, centroid.y * patchFreq, 2);
            return patch > 0.15 ? ETerrainBiome::GrassDark : ETerrainBiome::Grass;
        };

        struct FClassifiedFace
        {
            size_t vertex[3]{};
            ETerrainBiome biome = ETerrainBiome::Grass;
        };
        std::vector<FClassifiedFace> faces;
        faces.reserve(static_cast<size_t>(cx) * cy * 2);

        for (int j = 0; j < cy; ++j)
        {
            for (int i = 0; i < cx; ++i)
            {
                const size_t cell = static_cast<size_t>(j) * cx + i;
                const size_t a = static_cast<size_t>(j) * vx + i;
                const size_t b = a + 1;
                const size_t d = static_cast<size_t>(j + 1) * vx + i;
                const size_t c = d + 1;
                size_t t0[3];
                size_t t1[3];
                if (data->cellDiagFlip[cell] == 0)
                {
                    t0[0] = a; t0[1] = b; t0[2] = c;
                    t1[0] = a; t1[1] = c; t1[2] = d;
                }
                else
                {
                    t0[0] = a; t0[1] = b; t0[2] = d;
                    t1[0] = b; t1[1] = c; t1[2] = d;
                }
                faces.push_back({{t0[0], t0[1], t0[2]},
                                 classifyFace(i, j, data->verts[t0[0]], data->verts[t0[1]], data->verts[t0[2]])});
                faces.push_back({{t1[0], t1[1], t1[2]},
                                 classifyFace(i, j, data->verts[t1[0]], data->verts[t1[1]], data->verts[t1[2]])});
            }
        }

        // Remove one-triangle material spikes without straightening the
        // low-poly boundary. A tip has three neighbors: one behind it and two
        // matching neighbors along its sides. Replacing only that strict
        // 2-to-1 case preserves ordinary diagonal miters and broad regions.
        std::vector<std::vector<size_t>> faceNeighbors(faces.size());
        std::map<std::pair<size_t, size_t>, size_t> edgeOwner;
        for (size_t faceIndex = 0; faceIndex < faces.size(); ++faceIndex)
        {
            const FClassifiedFace& face = faces[faceIndex];
            for (int edge = 0; edge < 3; ++edge)
            {
                const size_t v0 = face.vertex[edge];
                const size_t v1 = face.vertex[(edge + 1) % 3];
                const std::pair<size_t, size_t> key = std::minmax(v0, v1);
                const auto [it, inserted] = edgeOwner.emplace(key, faceIndex);
                if (!inserted)
                {
                    faceNeighbors[faceIndex].push_back(it->second);
                    faceNeighbors[it->second].push_back(faceIndex);
                }
            }
        }

        std::vector<ETerrainBiome> regularized;
        regularized.reserve(faces.size());
        for (const FClassifiedFace& face : faces) regularized.push_back(face.biome);
        bool changed = true;
        while (changed)
        {
            changed = false;
            for (size_t faceIndex = 0; faceIndex < faces.size(); ++faceIndex)
            {
                if (faceNeighbors[faceIndex].size() != 3) continue;

                const ETerrainBiome current = regularized[faceIndex];
                ETerrainBiome candidate = ETerrainBiome::Count;
                int candidateCount = 0;
                for (size_t neighbor : faceNeighbors[faceIndex])
                {
                    const ETerrainBiome neighborBiome = regularized[neighbor];
                    if (neighborBiome == current || neighborBiome == ETerrainBiome::Bed) continue;
                    int count = 0;
                    for (size_t other : faceNeighbors[faceIndex])
                    {
                        if (regularized[other] == neighborBiome) ++count;
                    }
                    if (count > candidateCount)
                    {
                        candidate = neighborBiome;
                        candidateCount = count;
                    }
                }
                if (current != ETerrainBiome::Bed && candidateCount >= 2)
                {
                    // This changes two disagreeing edges into one, so the
                    // total material-boundary length strictly decreases and
                    // the in-place relaxation cannot oscillate.
                    regularized[faceIndex] = candidate;
                    changed = true;
                }
            }
        }

        for (size_t faceIndex = 0; faceIndex < faces.size(); ++faceIndex)
        {
            FClassifiedFace& face = faces[faceIndex];
            face.biome = regularized[faceIndex];
            emitLand(face.biome, data->verts[face.vertex[0]], data->verts[face.vertex[1]],
                     data->verts[face.vertex[2]]);
        }

        for (int j = 0; j < cy; ++j)
        {
            for (int i = 0; i < cx; ++i)
            {
                const size_t cell = static_cast<size_t>(j) * cx + i;
                // Queries intentionally keep cell-center semantics even
                // though rendering classifies and regularizes per triangle.
                const uint8_t flags = data->cellFlags[cell];
                if (flags & FTerrainData::kFlagWater)
                    data->cellBiome[cell] = static_cast<uint8_t>(ETerrainBiome::Bed);
                else if (flags & FTerrainData::kFlagRoad)
                    data->cellBiome[cell] = static_cast<uint8_t>(ETerrainBiome::Road);
                else if (flags & FTerrainData::kFlagPad)
                    data->cellBiome[cell] = static_cast<uint8_t>(ETerrainBiome::Pad);
                else
                    data->cellBiome[cell] = static_cast<uint8_t>(faces[cell * 2].biome);
            }
        }

        // ---- Skirt + bottom cap (closed silhouette; not for CSG) ----
        {
            const double zBot = minH - std::max(2.0, 0.05 * std::min(spec.size.x, spec.size.y));
            FTerrainData::ColoredTris skirt;
            skirt.color = pal.skirtColor;
            skirt.materialName = "terrain_skirt";

            // Boundary loop, counter-clockwise seen from +Z.
            std::vector<glm::dvec3> loop;
            loop.reserve(2 * (vx + vy));
            for (int i = 0; i < vx; ++i) loop.push_back(data->verts[i]);                                    // south
            for (int j = 1; j < vy; ++j) loop.push_back(data->verts[static_cast<size_t>(j) * vx + vx - 1]); // east
            for (int i = vx - 2; i >= 0; --i) loop.push_back(data->verts[(static_cast<size_t>(vy) - 1) * vx + i]); // north
            for (int j = vy - 2; j >= 1; --j) loop.push_back(data->verts[static_cast<size_t>(j) * vx]);     // west
            loop.push_back(loop.front());

            for (size_t k = 0; k + 1 < loop.size(); ++k)
            {
                const glm::dvec3 t1 = loop[k];
                const glm::dvec3 t2 = loop[k + 1];
                const glm::dvec3 b1(t1.x, t1.y, zBot);
                const glm::dvec3 b2(t2.x, t2.y, zBot);
                skirt.tris.push_back(t1);
                skirt.tris.push_back(b1);
                skirt.tris.push_back(b2);
                skirt.tris.push_back(t1);
                skirt.tris.push_back(b2);
                skirt.tris.push_back(t2);
            }

            const glm::dvec3 c00(-hx, -hy, zBot);
            const glm::dvec3 c10(hx, -hy, zBot);
            const glm::dvec3 c11(hx, hy, zBot);
            const glm::dvec3 c01(-hx, hy, zBot);
            skirt.tris.push_back(c00);
            skirt.tris.push_back(c01);
            skirt.tris.push_back(c11);
            skirt.tris.push_back(c00);
            skirt.tris.push_back(c11);
            skirt.tris.push_back(c10);

            landByColor[colorKeyOf(skirt.color)] = std::move(skirt);
        }

        data->landGeom.reserve(landByColor.size());
        for (auto& entry : landByColor)
        {
            data->landGeom.push_back(std::move(entry.second));
        }

        // ---- Water (closed thin slab: top + bottom + boundary walls) ----
        // A single-sided sheet would trap refracted rays inside a dielectric
        // (black water); a closed volume stays safe if translucency returns.
        // The surface extends one cell ring past the water region so the slab
        // edge is buried inside the rising bank: the visible waterline is the
        // terrain/water intersection, not a dangling side wall.
        {
            FTerrainData::ColoredTris waterMesh;
            waterMesh.color = pal.waterColor;
            waterMesh.materialName = "terrain_water";
            const double slab = 0.3;

            // Extended region: water cells + their dry neighbours (borrowing the
            // highest adjacent water level).
            std::vector<double> extLevel(static_cast<size_t>(cx) * cy, -1e30);
            for (int j = 0; j < cy; ++j)
            {
                for (int i = 0; i < cx; ++i)
                {
                    const size_t cell = static_cast<size_t>(j) * cx + i;
                    if (data->cellFlags[cell] & FTerrainData::kFlagWater)
                    {
                        extLevel[cell] = data->cellWater[cell];
                        continue;
                    }
                    for (int oj = -1; oj <= 1; ++oj)
                    {
                        for (int oi = -1; oi <= 1; ++oi)
                        {
                            const int ni = i + oi;
                            const int nj = j + oj;
                            if (ni < 0 || nj < 0 || ni >= cx || nj >= cy) continue;
                            const size_t nc = static_cast<size_t>(nj) * cx + ni;
                            if (data->cellFlags[nc] & FTerrainData::kFlagWater)
                            {
                                extLevel[cell] = std::max(extLevel[cell], data->cellWater[nc]);
                            }
                        }
                    }
                }
            }

            auto inExt = [&](int ci, int cj)
            {
                if (ci < 0 || cj < 0 || ci >= cx || cj >= cy) return false;
                return extLevel[static_cast<size_t>(cj) * cx + ci] > -1e29;
            };

            auto cornerLevel = [&](int vi, int vj) -> double
            {
                double level = -1e30;
                for (int oj = -1; oj <= 0; ++oj)
                {
                    for (int oi = -1; oi <= 0; ++oi)
                    {
                        const int ci = vi + oi;
                        const int cj = vj + oj;
                        if (!inExt(ci, cj)) continue;
                        level = std::max(level, extLevel[static_cast<size_t>(cj) * cx + ci]);
                    }
                }
                return level > -1e29 ? level : 0.0;
            };

            auto pushTri = [&](const glm::dvec3& p0, const glm::dvec3& p1, const glm::dvec3& p2)
            {
                waterMesh.tris.push_back(p0);
                waterMesh.tris.push_back(p1);
                waterMesh.tris.push_back(p2);
            };
            // Outward-facing wall (tA->tB runs so that e x down points away
            // from the water region).
            auto pushWall = [&](const glm::dvec3& tA, const glm::dvec3& tB)
            {
                const glm::dvec3 bA(tA.x, tA.y, tA.z - slab);
                const glm::dvec3 bB(tB.x, tB.y, tB.z - slab);
                pushTri(tA, tB, bB);
                pushTri(tA, bB, bA);
            };

            for (int j = 0; j < cy; ++j)
            {
                for (int i = 0; i < cx; ++i)
                {
                    if (!inExt(i, j)) continue;
                    const size_t cell = static_cast<size_t>(j) * cx + i;
                    glm::dvec3 a = data->verts[static_cast<size_t>(j) * vx + i];
                    glm::dvec3 b = data->verts[static_cast<size_t>(j) * vx + i + 1];
                    glm::dvec3 c = data->verts[(static_cast<size_t>(j) + 1) * vx + i + 1];
                    glm::dvec3 d = data->verts[(static_cast<size_t>(j) + 1) * vx + i];
                    a.z = cornerLevel(i, j);
                    b.z = cornerLevel(i + 1, j);
                    c.z = cornerLevel(i + 1, j + 1);
                    d.z = cornerLevel(i, j + 1);
                    const glm::dvec3 ab(a.x, a.y, a.z - slab);
                    const glm::dvec3 bb(b.x, b.y, b.z - slab);
                    const glm::dvec3 cb(c.x, c.y, c.z - slab);
                    const glm::dvec3 db(d.x, d.y, d.z - slab);
                    if (data->cellDiagFlip[cell] == 0)
                    {
                        pushTri(a, b, c);
                        pushTri(a, c, d);
                        pushTri(ab, cb, bb);
                        pushTri(ab, db, cb);
                    }
                    else
                    {
                        pushTri(a, b, d);
                        pushTri(b, c, d);
                        pushTri(ab, db, bb);
                        pushTri(bb, db, cb);
                    }
                    if (!inExt(i, j - 1)) pushWall(b, a);     // south, outward -Y
                    if (!inExt(i, j + 1)) pushWall(d, c);     // north, outward +Y
                    if (!inExt(i - 1, j)) pushWall(a, d);     // west, outward -X
                    if (!inExt(i + 1, j)) pushWall(c, b);     // east, outward +X
                }
            }

            if (!waterMesh.tris.empty())
            {
                data->waterGeom.push_back(std::move(waterMesh));
            }
        }

        return data;
    }
} // namespace Assets::Scad
