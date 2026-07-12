#include "Modules/ScadLoader/FScadText.h"

#include "Modules/ScadLoader/FScadTess.h"
#include "Engine/Utilities/FileHelper.hpp"

#ifndef GK_WITH_FREETYPE
#define GK_WITH_FREETYPE 0
#endif

#if GK_WITH_FREETYPE

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <vector>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H

namespace Assets::Scad
{
    namespace
    {
        using Pt = glm::dvec2;
        using Contour = std::vector<Pt>;

        struct DecomposeCtx
        {
            std::vector<Contour>* contours = nullptr;
            Contour current;
            double scale = 1.0;
            int bezSteps = 6;
        };

        int MoveTo(const FT_Vector* to, void* user)
        {
            auto* c = static_cast<DecomposeCtx*>(user);
            if (c->current.size() >= 3) c->contours->push_back(c->current);
            c->current.clear();
            c->current.emplace_back(static_cast<double>(to->x) * c->scale, static_cast<double>(to->y) * c->scale);
            return 0;
        }

        int LineTo(const FT_Vector* to, void* user)
        {
            auto* c = static_cast<DecomposeCtx*>(user);
            c->current.emplace_back(static_cast<double>(to->x) * c->scale, static_cast<double>(to->y) * c->scale);
            return 0;
        }

        int ConicTo(const FT_Vector* ctrl, const FT_Vector* to, void* user)
        {
            auto* c = static_cast<DecomposeCtx*>(user);
            if (c->current.empty()) return 0;
            const Pt p0 = c->current.back();
            const Pt p1(static_cast<double>(ctrl->x) * c->scale, static_cast<double>(ctrl->y) * c->scale);
            const Pt p2(static_cast<double>(to->x) * c->scale, static_cast<double>(to->y) * c->scale);
            for (int i = 1; i <= c->bezSteps; ++i)
            {
                const double t = static_cast<double>(i) / c->bezSteps;
                const double u = 1.0 - t;
                c->current.push_back(u * u * p0 + 2.0 * u * t * p1 + t * t * p2);
            }
            return 0;
        }

        int CubicTo(const FT_Vector* c1, const FT_Vector* c2, const FT_Vector* to, void* user)
        {
            auto* c = static_cast<DecomposeCtx*>(user);
            if (c->current.empty()) return 0;
            const Pt p0 = c->current.back();
            const Pt P1(static_cast<double>(c1->x) * c->scale, static_cast<double>(c1->y) * c->scale);
            const Pt P2(static_cast<double>(c2->x) * c->scale, static_cast<double>(c2->y) * c->scale);
            const Pt P3(static_cast<double>(to->x) * c->scale, static_cast<double>(to->y) * c->scale);
            for (int i = 1; i <= c->bezSteps; ++i)
            {
                const double t = static_cast<double>(i) / c->bezSteps;
                const double u = 1.0 - t;
                c->current.push_back(u * u * u * p0 + 3.0 * u * u * t * P1 + 3.0 * u * t * t * P2 + t * t * t * P3);
            }
            return 0;
        }

        FT_Library& Lib()
        {
            static FT_Library lib = nullptr;
            return lib;
        }
        FT_Face& Face()
        {
            static FT_Face face = nullptr;
            return face;
        }
        bool g_ready = false;
        std::once_flag g_initOnce;

        void EnsureInit()
        {
            std::call_once(g_initOnce, []()
            {
                if (FT_Init_FreeType(&Lib()) != 0) return;
                const std::string path = Utilities::FileHelper::GetPlatformFilePath("assets/fonts/DroidSansFallback.ttf");
                if (FT_New_Face(Lib(), path.c_str(), 0, &Face()) != 0) return;
                g_ready = true;
            });
        }

        std::vector<uint32_t> DecodeUtf8(const std::string& s)
        {
            std::vector<uint32_t> out;
            size_t i = 0;
            const size_t n = s.size();
            while (i < n)
            {
                const unsigned char c = static_cast<unsigned char>(s[i]);
                uint32_t cp = 0;
                int extra = 0;
                if (c < 0x80) { cp = c; extra = 0; }
                else if ((c >> 5) == 0x6) { cp = c & 0x1F; extra = 1; }
                else if ((c >> 4) == 0xE) { cp = c & 0x0F; extra = 2; }
                else if ((c >> 3) == 0x1E) { cp = c & 0x07; extra = 3; }
                else { ++i; continue; }
                ++i;
                for (int k = 0; k < extra && i < n; ++k, ++i)
                {
                    cp = (cp << 6) | (static_cast<unsigned char>(s[i]) & 0x3F);
                }
                out.push_back(cp);
            }
            return out;
        }
    } // namespace

    bool ScadText::Available()
    {
        EnsureInit();
        return g_ready;
    }

    bool ScadText::BuildText(const std::string& text,
                             double size,
                             const std::string& halign,
                             const std::string& valign,
                             double height,
                             bool center,
                             double fnSegments,
                             TriSoup& out)
    {
        EnsureInit();
        if (!g_ready) return false;

        FT_Face face = Face();
        const double unitsPerEm = face->units_per_EM ? static_cast<double>(face->units_per_EM) : 1000.0;
        const double scale = size / unitsPerEm;
        const int bezSteps = std::min(16, std::max(3, static_cast<int>(fnSegments > 0.0 ? fnSegments / 6.0 : 6.0)));

        const std::vector<uint32_t> cps = DecodeUtf8(text);
        std::vector<Contour> allContours;
        double penX = 0.0;
        for (uint32_t cp : cps)
        {
            if (FT_Load_Char(face, cp, FT_LOAD_NO_SCALE | FT_LOAD_NO_BITMAP) != 0) continue;
            FT_GlyphSlot g = face->glyph;
            if (g->format == FT_GLYPH_FORMAT_OUTLINE)
            {
                std::vector<Contour> glyphContours;
                DecomposeCtx ctx;
                ctx.contours = &glyphContours;
                ctx.scale = scale;
                ctx.bezSteps = bezSteps;
                FT_Outline_Funcs funcs{};
                funcs.move_to = MoveTo;
                funcs.line_to = LineTo;
                funcs.conic_to = ConicTo;
                funcs.cubic_to = CubicTo;
                funcs.shift = 0;
                funcs.delta = 0;
                if (FT_Outline_Decompose(&g->outline, &funcs, &ctx) == 0)
                {
                    if (ctx.current.size() >= 3) glyphContours.push_back(ctx.current);
                    for (Contour& c : glyphContours)
                    {
                        for (Pt& p : c) p.x += penX;
                        allContours.push_back(std::move(c));
                    }
                }
            }
            penX += static_cast<double>(g->advance.x) * scale;
        }
        if (allContours.empty()) return false;

        const double totalWidth = penX;
        double ox = 0.0;
        double oy = 0.0;
        if (halign == "center") ox = -totalWidth * 0.5;
        else if (halign == "right") ox = -totalWidth;
        const double ascent = static_cast<double>(face->ascender) * scale;
        const double descent = static_cast<double>(face->descender) * scale;
        if (valign == "top") oy = -ascent;
        else if (valign == "center") oy = -(ascent + descent) * 0.5;
        else if (valign == "bottom") oy = -descent;
        if (ox != 0.0 || oy != 0.0)
        {
            for (Contour& c : allContours)
            {
                for (Pt& p : c) { p.x += ox; p.y += oy; }
            }
        }

        ScadTess::ExtrudeEvenOdd(allContours, height, center, out);
        return true;
    }
} // namespace Assets::Scad

#else // GK_WITH_FREETYPE

namespace Assets::Scad
{
    bool ScadText::Available() { return false; }

    bool ScadText::BuildText(const std::string&, double, const std::string&, const std::string&,
                             double, bool, double, TriSoup&)
    {
        return false;
    }
} // namespace Assets::Scad

#endif // GK_WITH_FREETYPE
