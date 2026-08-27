// FScadEvaluator.Geometry.cpp — 3D primitives, 2D profile collection,
// extrusions and instance-argument resolution for the SCAD evaluator.
#include "Modules/ScadLoader/FScadEvaluator.Detail.h"

namespace Assets::Scad::EvalDetail
{
    GeomList Evaluator::MakeGeom(TriSoup&& objSpace, const glm::dmat4& xform, const glm::dvec4& color, bool hasColor)
    {
        if (objSpace.empty())
            return {};
        ColoredSoup cs;
        cs.color = color;
        cs.hasColor = hasColor;
        if (!materialPropertiesStack_.empty())
        {
            cs.material = materialPropertiesStack_.back();
        }
        if (hasColor && !materialNameStack_.empty())
        {
            cs.materialName = materialNameStack_.back();
        }
        cs.groupName = CurrentGroupLabel();
        cs.groupInstanceId = CurrentGroupInstanceId();
        cs.soup.reserve(objSpace.size());
        const bool flipWinding = glm::determinant(glm::dmat3(xform)) < 0.0;
        for (size_t i = 0; i + 2 < objSpace.size(); i += 3)
        {
            const glm::dvec3 a = glm::dvec3(xform * glm::dvec4(objSpace[i + 0], 1.0));
            const glm::dvec3 b = glm::dvec3(xform * glm::dvec4(objSpace[i + 1], 1.0));
            const glm::dvec3 c = glm::dvec3(xform * glm::dvec4(objSpace[i + 2], 1.0));
            cs.soup.push_back(a);
            if (flipWinding)
            {
                cs.soup.push_back(c);
                cs.soup.push_back(b);
            }
            else
            {
                cs.soup.push_back(b);
                cs.soup.push_back(c);
            }
        }
        GeomList out;
        out.push_back(std::move(cs));
        return out;
    }

    GeomList Evaluator::EvalPrimitive(const Stmt& inst, const glm::dmat4& xform, const glm::dvec4& color, bool hasColor)
    {
        TriSoup soup;
        const std::string& name = inst.name;
        const double fn = ctx_.GetNumber("$fn", 0.0);
        const double fa = ctx_.GetNumber("$fa", 12.0);
        const double fs = ctx_.GetNumber("$fs", 2.0);

        if (name == "cube")
        {
            const Value sizeVal = Arg(inst, "size", 0);
            glm::dvec3 size(1.0);
            if (sizeVal.IsNumber())
                size = glm::dvec3(sizeVal.num);
            else
                sizeVal.AsVec3(size);
            const bool center = Arg(inst, "center", 1).IsTruthy();
            ScadGeometry::BuildCube(size, center, soup);
        }
        else if (name == "sphere")
        {
            const double r = ResolveRadius(inst, "r", "d", 1.0);
            ScadGeometry::BuildSphere(r, ScadGeometry::Fragments(r, fn, fa, fs), soup);
        }
        else if (name == "cylinder")
        {
            const double h = Arg(inst, "h", 0).AsNumber(1.0);
            double r1 = ResolveRadius(inst, "r1", "d1", -1.0);
            double r2 = ResolveRadius(inst, "r2", "d2", -1.0);
            const double r = ResolveRadius(inst, "r", "d", -1.0);
            if (r1 < 0.0)
                r1 = (r >= 0.0) ? r : 1.0;
            if (r2 < 0.0)
                r2 = (r >= 0.0) ? r : r1;
            const bool center = Arg(inst, "center", -1).IsTruthy();
            const double maxR = std::max(r1, r2);
            ScadGeometry::BuildCylinder(h, r1, r2, center, ScadGeometry::Fragments(maxR, fn, fa, fs), soup);
        }
        else if (name == "polyhedron")
        {
            std::vector<glm::dvec3> points;
            std::vector<std::vector<int>> faces;
            const Value ptsVal = Arg(inst, "points", 0);
            Value facesVal = Arg(inst, "faces", 1);
            if (facesVal.type == Value::Type::Undef)
                facesVal = Arg(inst, "triangles", -1);
            if (ptsVal.type == Value::Type::Vec)
            {
                for (const Value& p : ptsVal.vec)
                {
                    glm::dvec3 v(0.0);
                    p.AsVec3(v);
                    points.push_back(v);
                }
            }
            if (facesVal.type == Value::Type::Vec)
            {
                for (const Value& f : facesVal.vec)
                {
                    std::vector<int> face;
                    if (f.type == Value::Type::Vec)
                    {
                        for (const Value& iv : f.vec)
                        {
                            face.push_back(static_cast<int>(iv.AsNumber(0.0)));
                        }
                    }
                    faces.push_back(std::move(face));
                }
            }
            ScadGeometry::BuildPolyhedron(points, faces, soup);
        }

        return MakeGeom(std::move(soup), xform, color, hasColor);
    }

    std::shared_ptr<const FTerrainData> Evaluator::TerrainFromValue(const Value& value, const char* where)
    {
        if (value.cacheIdentity != 0)
        {
            auto identityFound = terrainIdentityCache_.find(value.cacheIdentity);
            if (identityFound != terrainIdentityCache_.end())
            {
                return identityFound->second;
            }
        }

        FTerrainSpec spec;
        std::string err;
        std::vector<std::string> warnings;
        if (!ScadTerrain::DecodeSpec(value, spec, err, warnings))
        {
            Warn("terrain", std::string(where) + ": " + err);
            return nullptr;
        }
        for (const std::string& w : warnings)
        {
            Warn("terrain", std::string(where) + ": " + w);
        }

        const std::string key = ScadTerrain::SpecCacheKey(spec);
        auto found = terrainCache_.find(key);
        if (found != terrainCache_.end())
        {
            if (value.cacheIdentity != 0)
            {
                terrainIdentityCache_.emplace(value.cacheIdentity, found->second);
            }
            return found->second;
        }
        std::shared_ptr<const FTerrainData> data = ScadTerrain::Build(spec);
        terrainCache_.emplace(key, data);
        if (value.cacheIdentity != 0)
        {
            terrainIdentityCache_.emplace(value.cacheIdentity, data);
        }
        return data;
    }

    GeomList Evaluator::EvalTerrain(const Stmt& inst, const glm::dmat4& xform, const glm::dvec4& color, bool hasColor)
    {
        (void)color;
        (void)hasColor;
        std::shared_ptr<const FTerrainData> data = TerrainFromValue(Arg(inst, "spec", 0), "gk_terrain");
        if (!data)
        {
            return {};
        }

        GeomList out;
        auto emit = [&](const FTerrainData::ColoredTris& src, bool water)
        {
            if (src.tris.empty())
                return;
            TriSoup soup = src.tris; // MakeGeom consumes; cached data stays intact
            GeomList part = MakeGeom(std::move(soup), xform, glm::dvec4(src.color), true);
            for (ColoredSoup& cs : part)
            {
                cs.faceted = true;
                cs.terrainWater = water;
                cs.materialName = src.materialName;
            }
            AppendMove(out, std::move(part));
        };
        for (const FTerrainData::ColoredTris& land : data->landGeom)
        {
            emit(land, false);
        }
        for (const FTerrainData::ColoredTris& water : data->waterGeom)
        {
            emit(water, true);
        }

        // Record the terrain payload for the loader (TerrainComponent). Skip
        // inside CSG evaluation: the geometry gets consumed by the boolean and
        // no longer matches the heightfield.
        if (sceneResult_ && suppressSceneNodes_ == 0)
        {
            SceneTerrain payload;
            payload.data = data;
            payload.xform = xform;
            if (SceneNodeBuild* owner = CurrentSceneOwner())
            {
                payload.ownerInstanceId = owner->instanceId;
            }
            else
            {
                payload.ownerInstanceId = topLevelFallbackInstanceId_;
            }
            sceneResult_->terrains.push_back(std::move(payload));
        }
        return out;
    }

    GeomList Evaluator::EvalLinearExtrude(const Stmt& inst, const glm::dmat4& xform, const glm::dvec4& color,
                                          bool hasColor)
    {
        const double height = Arg(inst, "height", 0).AsNumber(1.0);
        const bool center = Arg(inst, "center", -1).IsTruthy();
        const double fn = ctx_.GetNumber("$fn", 0.0);

        GeomList out;

        // text() children: shaped via FreeType into their own geometry.
        for (const StmtPtr& cptr : inst.children)
        {
            if (!cptr || cptr->kind != StmtKind::Instance || cptr->name != "text")
                continue;
            const Stmt& child = *cptr;
            if (!ScadText::Available())
            {
                Warn("text", "text() unavailable (no FreeType backend); skipped");
                continue;
            }
            const Value tv = Arg(child, "text", 0);
            const std::string str = (tv.type == Value::Type::Str) ? tv.str : std::string();
            const double tsize = Arg(child, "size", 1).AsNumber(10.0);
            const Value ha = Arg(child, "halign", -1);
            const Value va = Arg(child, "valign", -1);
            const std::string halign = (ha.type == Value::Type::Str) ? ha.str : "left";
            const std::string valign = (va.type == Value::Type::Str) ? va.str : "baseline";
            TriSoup soup;
            if (!str.empty() && ScadText::BuildText(str, tsize, halign, valign, height, center, fn, soup))
            {
                AppendMove(out, MakeGeom(std::move(soup), xform, color, hasColor));
            }
        }

        // 2D shape children (concave + holes + nested transforms) via earcut.
        std::vector<std::vector<glm::dvec2>> contours;
        Collect2D(inst.children, glm::dmat3(1.0), contours);
        if (!contours.empty())
        {
            TriSoup soup;
            if (ScadTess::Available())
            {
                ScadTess::ExtrudeEvenOdd(contours, height, center, soup);
            }
            else
            {
                for (const std::vector<glm::dvec2>& c : contours)
                {
                    ScadGeometry::ExtrudePolygon(c, height, center, soup);
                }
            }
            AppendMove(out, MakeGeom(std::move(soup), xform, color, hasColor));
        }
        return out;
    }

    glm::dmat3 Evaluator::Translate2D(double tx, double ty)
    {
        glm::dmat3 m(1.0);
        m[2] = glm::dvec3(tx, ty, 1.0);
        return m;
    }

    glm::dmat3 Evaluator::Scale2D(double sx, double sy)
    {
        glm::dmat3 m(1.0);
        m[0][0] = sx;
        m[1][1] = sy;
        return m;
    }

    glm::dmat3 Evaluator::Rot2D(double rad)
    {
        const double c = std::cos(rad);
        const double s = std::sin(rad);
        glm::dmat3 m(1.0);
        m[0] = glm::dvec3(c, s, 0.0);
        m[1] = glm::dvec3(-s, c, 0.0);
        return m;
    }

    glm::dvec2 Evaluator::Apply2D(const glm::dmat3& m, const glm::dvec2& p)
    {
        const glm::dvec3 r = m * glm::dvec3(p.x, p.y, 1.0);
        return glm::dvec2(r.x, r.y);
    }

    void Evaluator::Collect2D(const Scope& children, const glm::dmat3& m, std::vector<std::vector<glm::dvec2>>& out)
    {
        DefinitionFrameGuard definitionFrame(*this);
        RegisterLocalDefinitions(children);

        const double fn = ctx_.GetNumber("$fn", 0.0);
        const double fa = ctx_.GetNumber("$fa", 12.0);
        const double fs = ctx_.GetNumber("$fs", 2.0);

        for (const StmtPtr& sp : children)
        {
            if (!sp)
                continue;
            if (sp->kind == StmtKind::Assign)
            {
                ctx_.Set(sp->name, EvalExpr(sp->value));
                continue;
            }
            if (sp->kind != StmtKind::Instance)
                continue;
            const Stmt& c = *sp;
            if (c.modifiers.find('*') != std::string::npos)
                continue;

            if (c.name == "translate")
            {
                glm::dvec3 t(0.0);
                Arg(c, "v", 0).AsVec3(t);
                Collect2D(c.children, m * Translate2D(t.x, t.y), out);
            }
            else if (c.name == "scale")
            {
                glm::dvec3 s(1.0);
                const Value v = Arg(c, "v", 0);
                if (v.IsNumber())
                    s = glm::dvec3(v.num);
                else
                    v.AsVec3(s);
                Collect2D(c.children, m * Scale2D(s.x, s.y), out);
            }
            else if (c.name == "rotate")
            {
                const Value a0 = Arg(c, "a", 0);
                double deg = 0.0;
                if (a0.type == Value::Type::Vec)
                {
                    glm::dvec3 e(0.0);
                    a0.AsVec3(e);
                    deg = e.z;
                }
                else
                    deg = a0.AsNumber(0.0);
                Collect2D(c.children, m * Rot2D(deg * kDeg2Rad), out);
            }
            else if (c.name == "union" || c.name == "group")
            {
                Collect2D(c.children, m, out);
            }
            else if (c.name == "circle")
            {
                const double r = ResolveRadius(c, "r", "d", 1.0);
                const int frag = ScadGeometry::Fragments(r, fn, fa, fs);
                std::vector<glm::dvec2> poly;
                poly.reserve(frag);
                for (int i = 0; i < frag; ++i)
                {
                    const double a = 2.0 * kPi * static_cast<double>(i) / static_cast<double>(frag);
                    poly.push_back(Apply2D(m, glm::dvec2(r * std::cos(a), r * std::sin(a))));
                }
                out.push_back(std::move(poly));
            }
            else if (c.name == "square")
            {
                const Value sz = Arg(c, "size", 0);
                glm::dvec2 s(1.0, 1.0);
                if (sz.IsNumber())
                    s = glm::dvec2(sz.num);
                else
                {
                    glm::dvec3 v(1.0);
                    sz.AsVec3(v);
                    s = glm::dvec2(v.x, v.y);
                }
                const bool sc = Arg(c, "center", 1).IsTruthy();
                const glm::dvec2 o = sc ? -s * 0.5 : glm::dvec2(0.0);
                std::vector<glm::dvec2> poly = {Apply2D(m, o), Apply2D(m, glm::dvec2(o.x + s.x, o.y)),
                                                Apply2D(m, glm::dvec2(o.x + s.x, o.y + s.y)),
                                                Apply2D(m, glm::dvec2(o.x, o.y + s.y))};
                out.push_back(std::move(poly));
            }
            else if (c.name == "polygon")
            {
                const Value pts = Arg(c, "points", 0);
                if (pts.type == Value::Type::Vec)
                {
                    std::vector<glm::dvec2> all;
                    all.reserve(pts.vec.size());
                    for (const Value& p : pts.vec)
                    {
                        glm::dvec3 v(0.0);
                        p.AsVec3(v);
                        all.push_back(Apply2D(m, glm::dvec2(v.x, v.y)));
                    }
                    const Value paths = Arg(c, "paths", 1);
                    if (paths.type == Value::Type::Vec && !paths.vec.empty())
                    {
                        // Each path is an index loop into points (outer + holes).
                        for (const Value& path : paths.vec)
                        {
                            if (path.type != Value::Type::Vec)
                                continue;
                            std::vector<glm::dvec2> ring;
                            for (const Value& iv : path.vec)
                            {
                                const int idx = static_cast<int>(iv.AsNumber(0.0));
                                if (idx >= 0 && idx < static_cast<int>(all.size()))
                                    ring.push_back(all[idx]);
                            }
                            if (ring.size() >= 3)
                                out.push_back(std::move(ring));
                        }
                    }
                    else if (all.size() >= 3)
                    {
                        out.push_back(std::move(all));
                    }
                }
            }
            else
            {
                StmtPtr found = FindModule(c.name);
                if (found && depth_ < options_.maxRecursionDepth)
                {
                    ++depth_;
                    ctx_.Push();
                    BindParams(found->params, c.args);
                    Collect2D(found->body, m, out);
                    ctx_.Pop();
                    --depth_;
                }
            }
        }
    }

    GeomList Evaluator::EvalRotateExtrude(const Stmt& inst, const glm::dmat4& xform, const glm::dvec4& color,
                                          bool hasColor)
    {
        const double angle = Arg(inst, "angle", 0).AsNumber(360.0);
        const double fn = ctx_.GetNumber("$fn", 0.0);
        const double fa = ctx_.GetNumber("$fa", 12.0);
        const double fs = ctx_.GetNumber("$fs", 2.0);

        std::vector<std::vector<glm::dvec2>> profiles;
        Collect2D(inst.children, glm::dmat3(1.0), profiles);
        if (profiles.empty())
            return {};

        double maxR = 0.0;
        for (const auto& prof : profiles)
        {
            for (const glm::dvec2& p : prof)
                maxR = std::max(maxR, p.x);
        }
        const int base = ScadGeometry::Fragments(maxR, fn, fa, fs);
        const int segs = (angle >= 359.999)
            ? base
            : std::max(2, static_cast<int>(std::ceil(static_cast<double>(base) * angle / 360.0)));

        TriSoup soup;
        ScadGeometry::BuildRotateExtrude(profiles, angle, segs, soup);
        return MakeGeom(std::move(soup), xform, color, hasColor);
    }

    Value Evaluator::Arg(const Stmt& inst, const char* name, int posIndex)
    {
        for (const CallArg& a : inst.args)
        {
            if (a.name == name)
                return EvalExpr(a.value);
        }
        if (posIndex >= 0)
        {
            int p = 0;
            for (const CallArg& a : inst.args)
            {
                if (!a.name.empty())
                    continue;
                if (p == posIndex)
                    return EvalExpr(a.value);
                ++p;
            }
        }
        return Value();
    }

    double Evaluator::ResolveRadius(const Stmt& inst, const char* rName, const char* dName, double fallback)
    {
        for (const CallArg& a : inst.args)
        {
            if (a.name == rName)
                return EvalExpr(a.value).AsNumber(fallback);
        }
        for (const CallArg& a : inst.args)
        {
            if (a.name == dName)
                return EvalExpr(a.value).AsNumber(fallback * 2.0) * 0.5;
        }
        return fallback;
    }

    glm::dmat4 Evaluator::BuildRotate(const Stmt& inst)
    {
        const Value a0 = Arg(inst, "a", 0);
        if (a0.type == Value::Type::Vec)
        {
            glm::dvec3 deg(0.0);
            a0.AsVec3(deg);
            glm::dmat4 r(1.0);
            r = glm::rotate(r, deg.z * kDeg2Rad, glm::dvec3(0, 0, 1));
            r = glm::rotate(r, deg.y * kDeg2Rad, glm::dvec3(0, 1, 0));
            r = glm::rotate(r, deg.x * kDeg2Rad, glm::dvec3(1, 0, 0));
            return r;
        }
        if (a0.IsNumber())
        {
            glm::dvec3 axis(0, 0, 1);
            const Value v = Arg(inst, "v", 1);
            if (v.type == Value::Type::Vec)
                v.AsVec3(axis);
            if (glm::dot(axis, axis) < 1e-12)
                axis = glm::dvec3(0, 0, 1);
            return glm::rotate(glm::dmat4(1.0), a0.num * kDeg2Rad, glm::normalize(axis));
        }
        return glm::dmat4(1.0);
    }

    glm::dmat4 Evaluator::BuildMirror(const glm::dvec3& nrm)
    {
        const double len2 = glm::dot(nrm, nrm);
        if (len2 < 1e-12)
            return glm::dmat4(1.0);
        const glm::dvec3 n = nrm / std::sqrt(len2);
        glm::dmat4 m(1.0);
        for (int i = 0; i < 3; ++i)
        {
            for (int j = 0; j < 3; ++j)
            {
                m[j][i] = ((i == j) ? 1.0 : 0.0) - 2.0 * n[i] * n[j];
            }
        }
        return m;
    }

    glm::dmat4 Evaluator::BuildMultmatrix(const Stmt& inst)
    {
        const Value m = Arg(inst, "m", 0);
        if (m.type != Value::Type::Vec)
            return glm::dmat4(1.0);
        glm::dmat4 out(1.0);
        for (size_t row = 0; row < m.vec.size() && row < 4; ++row)
        {
            const Value& r = m.vec[row];
            if (r.type != Value::Type::Vec)
                continue;
            for (size_t col = 0; col < r.vec.size() && col < 4; ++col)
            {
                out[static_cast<int>(col)][static_cast<int>(row)] = r.vec[col].AsNumber(0.0);
            }
        }
        return out;
    }

    bool Evaluator::ResolveColor(const Stmt& inst, glm::dvec4& out)
    {
        const Value c = Arg(inst, "c", 0);
        if (c.type == Value::Type::Vec)
        {
            out = glm::dvec4(0.0, 0.0, 0.0, 1.0);
            c.AsVec4(out);
        }
        else if (c.type == Value::Type::Str)
        {
            out = NamedColor(c.str);
        }
        else
        {
            return false;
        }
        const Value alpha = Arg(inst, "alpha", 1);
        if (alpha.IsNumber())
            out.a = alpha.num;
        return true;
    }

    bool Evaluator::ResolveMaterialColor(const Stmt& inst, glm::dvec4& out)
    {
        const Value c = Arg(inst, "c", 0);
        if (c.type == Value::Type::Vec)
        {
            out = glm::dvec4(0.0, 0.0, 0.0, 1.0);
            c.AsVec4(out);
        }
        else if (c.type == Value::Type::Str)
        {
            out = NamedColor(c.str);
        }
        else
        {
            return false;
        }

        const Value alpha = Arg(inst, "alpha", -1);
        if (alpha.IsNumber())
        {
            out.a = alpha.num;
        }
        return true;
    }

    const ExprPtr& Evaluator::ColorArgExpr(const Stmt& inst) const
    {
        static const ExprPtr empty;
        int positional = 0;
        for (const CallArg& a : inst.args)
        {
            if (a.name == "c")
            {
                return a.value;
            }
            if (a.name.empty())
            {
                if (positional == 0)
                {
                    return a.value;
                }
                ++positional;
            }
        }
        return empty;
    }

    std::string Evaluator::MaterialNameFromExpr(const ExprPtr& expr) const
    {
        if (!expr)
        {
            return "";
        }

        switch (expr->kind)
        {
        case ExprKind::Ident:
        case ExprKind::Str:
            return expr->str;
        case ExprKind::Call:
            return expr->str;
        case ExprKind::Index:
        case ExprKind::Member:
            return expr->list.empty() ? std::string() : MaterialNameFromExpr(expr->list[0]);
        default:
            return "";
        }
    }

    std::string Evaluator::ResolveColorName(const Stmt& inst) const { return MaterialNameFromExpr(ColorArgExpr(inst)); }

    glm::dvec4 Evaluator::NamedColor(const std::string& nameIn)
    {
        std::string n;
        n.reserve(nameIn.size());
        for (char ch : nameIn)
            n.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
        if (n == "red")
            return {1, 0, 0, 1};
        if (n == "green")
            return {0, 0.5, 0, 1};
        if (n == "blue")
            return {0, 0, 1, 1};
        if (n == "white")
            return {1, 1, 1, 1};
        if (n == "black")
            return {0, 0, 0, 1};
        if (n == "gray" || n == "grey")
            return {0.5, 0.5, 0.5, 1};
        if (n == "yellow")
            return {1, 1, 0, 1};
        if (n == "orange")
            return {1, 0.65, 0, 1};
        if (n == "brown")
            return {0.65, 0.16, 0.16, 1};
        return {0.78, 0.78, 0.78, 1};
    }
} // namespace Assets::Scad::EvalDetail
