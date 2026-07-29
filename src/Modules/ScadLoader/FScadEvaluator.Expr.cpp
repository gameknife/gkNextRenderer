// FScadEvaluator.Expr.cpp — expression evaluation and the OpenSCAD builtin
// function table for the SCAD evaluator.
#include "Modules/ScadLoader/FScadEvaluator.Detail.h"

namespace Assets::Scad::EvalDetail
{
    Value Evaluator::EvalExpr(const ExprPtr& e)
    {
        if (!e) return Value();
        switch (e->kind)
        {
        case ExprKind::Number: return Value::MakeNumber(e->num);
        case ExprKind::Bool: return Value::MakeBool(e->boolean);
        case ExprKind::Str: return Value::MakeStr(e->str);
        case ExprKind::Undef: return Value();
        case ExprKind::VectorLit:
        {
            std::vector<Value> items;
            for (const ExprPtr& it : e->list) AppendElement(it, items);
            return MakeVector(std::move(items));
        }
        case ExprKind::RangeLit:
        {
            if (e->list.size() == 2)
            {
                return Value::MakeRange(EvalExpr(e->list[0]).AsNumber(0.0), 1.0, EvalExpr(e->list[1]).AsNumber(0.0));
            }
            if (e->list.size() == 3)
            {
                return Value::MakeRange(EvalExpr(e->list[0]).AsNumber(0.0),
                                        EvalExpr(e->list[1]).AsNumber(1.0),
                                        EvalExpr(e->list[2]).AsNumber(0.0));
            }
            return Value();
        }
        case ExprKind::Ident:
        {
            const Value* v = ctx_.Get(e->str);
            if (v) return *v;
            Warn("undefvar", "undefined variable '" + e->str + "'");
            return Value();
        }
        case ExprKind::Index:
        {
            const Value base = EvalExpr(e->list[0]);
            const int idx = static_cast<int>(EvalExpr(e->list[1]).AsNumber(0.0));
            if (base.type == Value::Type::Vec && idx >= 0 && idx < static_cast<int>(base.vec.size()))
            {
                return base.vec[idx];
            }
            return Value();
        }
        case ExprKind::Member:
        {
            const Value base = EvalExpr(e->list[0]);
            int idx = -1;
            if (e->str == "x") idx = 0;
            else if (e->str == "y") idx = 1;
            else if (e->str == "z") idx = 2;
            if (base.type == Value::Type::Vec && idx >= 0 && idx < static_cast<int>(base.vec.size()))
            {
                return base.vec[idx];
            }
            return Value();
        }
        case ExprKind::Unary: return EvalUnary(e);
        case ExprKind::Binary: return EvalBinary(e);
        case ExprKind::Cond:
            return EvalExpr(e->list[0]).IsTruthy() ? EvalExpr(e->list[1]) : EvalExpr(e->list[2]);
        case ExprKind::Call: return EvalCall(e);
        case ExprKind::CompFor:
        case ExprKind::CompLet:
        case ExprKind::CompIf:
        case ExprKind::CompEach:
            return Value(); // only meaningful inside a VectorLit (see AppendElement)
        }
        return Value();
    }

    void Evaluator::BindingValues(const Value& v, std::vector<Value>& out)
    {
        if (v.type == Value::Type::Range) EnumerateRange(v, out);
        else if (v.type == Value::Type::Vec) out = v.vec;
        else out.push_back(v);
    }

    void Evaluator::AppendElement(const ExprPtr& e, std::vector<Value>& out)
    {
        if (!e) return;
        switch (e->kind)
        {
        case ExprKind::CompFor:
        {
            if (e->list.empty()) return;

            std::function<void(size_t)> appendBinding = [&](size_t argIndex)
            {
                while (argIndex < e->args.size() && e->args[argIndex].name.empty())
                {
                    ++argIndex;
                }
                if (argIndex >= e->args.size())
                {
                    AppendElement(e->list[0], out);
                    return;
                }

                const CallArg& binding = e->args[argIndex];
                std::vector<Value> values;
                BindingValues(EvalExpr(binding.value), values);
                if (values.empty()) return;

                for (const Value& value : values)
                {
                    ctx_.Push();
                    ctx_.Set(binding.name, value);
                    appendBinding(argIndex + 1);
                    ctx_.Pop();
                }
            };

            appendBinding(0);
            return;
        }
        case ExprKind::CompLet:
        {
            ctx_.Push();
            for (const CallArg& a : e->args)
            {
                if (!a.name.empty()) ctx_.Set(a.name, EvalExpr(a.value));
            }
            if (!e->list.empty()) AppendElement(e->list[0], out);
            ctx_.Pop();
            return;
        }
        case ExprKind::CompIf:
        {
            if (e->list.size() >= 2 && EvalExpr(e->list[0]).IsTruthy()) AppendElement(e->list[1], out);
            else if (e->list.size() >= 3) AppendElement(e->list[2], out);
            return;
        }
        case ExprKind::CompEach:
        {
            if (e->list.empty()) return;
            const Value v = EvalExpr(e->list[0]);
            if (v.type == Value::Type::Vec) out.insert(out.end(), v.vec.begin(), v.vec.end());
            else if (v.type == Value::Type::Range) { std::vector<Value> vv; EnumerateRange(v, vv); out.insert(out.end(), vv.begin(), vv.end()); }
            else out.push_back(v);
            return;
        }
        default:
            out.push_back(EvalExpr(e));
            return;
        }
    }

    Value Evaluator::EvalUnary(const ExprPtr& e)
    {
        const Value v = EvalExpr(e->list[0]);
        if (e->str == "-")
        {
            if (v.type == Value::Type::Vec)
            {
                std::vector<Value> out;
                out.reserve(v.vec.size());
                for (const Value& c : v.vec) out.push_back(Value::MakeNumber(-c.AsNumber(0.0)));
                return MakeVector(std::move(out));
            }
            return Value::MakeNumber(-v.AsNumber(0.0));
        }
        if (e->str == "!") return Value::MakeBool(!v.IsTruthy());
        return v; // unary '+'
    }

    Value Evaluator::EvalBinary(const ExprPtr& e)
    {
        const std::string& op = e->str;
        const Value a = EvalExpr(e->list[0]);
        if (op == "&&") return Value::MakeBool(a.IsTruthy() && EvalExpr(e->list[1]).IsTruthy());
        if (op == "||") return Value::MakeBool(a.IsTruthy() || EvalExpr(e->list[1]).IsTruthy());

        const Value b = EvalExpr(e->list[1]);

        if (op == "==") return Value::MakeBool(ValuesEqual(a, b));
        if (op == "!=") return Value::MakeBool(!ValuesEqual(a, b));
        if (op == "<") return Value::MakeBool(a.AsNumber(0.0) < b.AsNumber(0.0));
        if (op == ">") return Value::MakeBool(a.AsNumber(0.0) > b.AsNumber(0.0));
        if (op == "<=") return Value::MakeBool(a.AsNumber(0.0) <= b.AsNumber(0.0));
        if (op == ">=") return Value::MakeBool(a.AsNumber(0.0) >= b.AsNumber(0.0));

        const bool aVec = a.type == Value::Type::Vec;
        const bool bVec = b.type == Value::Type::Vec;

        if (op == "+" || op == "-")
        {
            if (aVec && bVec)
            {
                std::vector<Value> out;
                const size_t n = std::min(a.vec.size(), b.vec.size());
                for (size_t i = 0; i < n; ++i)
                {
                    const double x = a.vec[i].AsNumber(0.0);
                    const double y = b.vec[i].AsNumber(0.0);
                    out.push_back(Value::MakeNumber(op == "+" ? x + y : x - y));
                }
                return MakeVector(std::move(out));
            }
            const double x = a.AsNumber(0.0);
            const double y = b.AsNumber(0.0);
            return Value::MakeNumber(op == "+" ? x + y : x - y);
        }
        if (op == "*")
        {
            if (aVec && !bVec) return ScaleVec(a, b.AsNumber(0.0));
            if (!aVec && bVec) return ScaleVec(b, a.AsNumber(0.0));
            return Value::MakeNumber(a.AsNumber(0.0) * b.AsNumber(0.0));
        }
        if (op == "/")
        {
            if (aVec && !bVec) return ScaleVec(a, 1.0 / b.AsNumber(1.0));
            const double denom = b.AsNumber(1.0);
            return Value::MakeNumber(denom != 0.0 ? a.AsNumber(0.0) / denom : 0.0);
        }
        if (op == "%")
        {
            return Value::MakeNumber(std::fmod(a.AsNumber(0.0), b.AsNumber(1.0)));
        }
        return Value();
    }

    Value Evaluator::MakeVector(std::vector<Value>&& values)
    {
        Value result = Value::MakeVec(std::move(values));
        result.cacheIdentity = nextValueIdentity_++;
        return result;
    }

    Value Evaluator::ScaleVec(const Value& v, double s)
    {
        std::vector<Value> out;
        out.reserve(v.vec.size());
        for (const Value& c : v.vec) out.push_back(Value::MakeNumber(c.AsNumber(0.0) * s));
        return MakeVector(std::move(out));
    }

    std::string Evaluator::NumToStr(double v)
    {
        if (v == static_cast<double>(static_cast<long long>(v)))
        {
            return std::to_string(static_cast<long long>(v));
        }
        std::string s = std::to_string(v);
        // Trim trailing zeros for a tidier echo output.
        while (!s.empty() && s.back() == '0') s.pop_back();
        if (!s.empty() && s.back() == '.') s.pop_back();
        return s;
    }

    std::string Evaluator::ValueToString(const Value& v)
    {
        switch (v.type)
        {
        case Value::Type::Number: return NumToStr(v.num);
        case Value::Type::Bool: return v.boolean ? "true" : "false";
        case Value::Type::Str: return v.str;
        case Value::Type::Vec:
        {
            std::string s = "[";
            for (size_t i = 0; i < v.vec.size(); ++i)
            {
                if (i) s += ", ";
                s += ValueToString(v.vec[i]);
            }
            return s + "]";
        }
        case Value::Type::Range:
            return "[" + NumToStr(v.rangeBegin) + " : " + NumToStr(v.rangeStep) + " : " + NumToStr(v.rangeEnd) + "]";
        default: return "undef";
        }
    }

    bool Evaluator::ValuesEqual(const Value& a, const Value& b)
    {
        if (a.type != b.type)
        {
            if ((a.type == Value::Type::Number || a.type == Value::Type::Bool) &&
                (b.type == Value::Type::Number || b.type == Value::Type::Bool))
            {
                return a.AsNumber(0.0) == b.AsNumber(1.0);
            }
            return false;
        }
        switch (a.type)
        {
        case Value::Type::Number: return a.num == b.num;
        case Value::Type::Bool: return a.boolean == b.boolean;
        case Value::Type::Str: return a.str == b.str;
        default: return false;
        }
    }

    Value Evaluator::EvalCall(const ExprPtr& e)
    {
        const std::string& name = e->str;

        StmtPtr fn = FindFunction(name);
        if (fn)
        {
            if (depth_ >= options_.maxRecursionDepth)
            {
                Warn("depth", "max recursion depth reached in function '" + name + "'");
                return Value();
            }
            ++depth_;
            ctx_.Push();
            BindParams(fn->params, e->args);
            const Value r = EvalExpr(fn->value);
            ctx_.Pop();
            --depth_;
            return r;
        }

        std::vector<Value> a;
        a.reserve(e->args.size());
        for (const CallArg& arg : e->args) a.push_back(EvalExpr(arg.value));
        return EvalBuiltinFunction(name, a);
    }

    Value Evaluator::EvalBuiltinFunction(const std::string& name, const std::vector<Value>& a)
    {
        auto num = [&](size_t i, double d = 0.0) { return i < a.size() ? a[i].AsNumber(d) : d; };

        if (name == "max")
        {
            double m = -std::numeric_limits<double>::infinity();
            for (const Value& v : a) m = std::max(m, v.AsNumber(m));
            return Value::MakeNumber(m);
        }
        if (name == "min")
        {
            double m = std::numeric_limits<double>::infinity();
            for (const Value& v : a) m = std::min(m, v.AsNumber(m));
            return Value::MakeNumber(m);
        }
        if (name == "abs") return Value::MakeNumber(std::abs(num(0)));
        if (name == "floor") return Value::MakeNumber(std::floor(num(0)));
        if (name == "ceil") return Value::MakeNumber(std::ceil(num(0)));
        if (name == "round") return Value::MakeNumber(std::round(num(0)));
        if (name == "sqrt") return Value::MakeNumber(std::sqrt(std::max(0.0, num(0))));
        if (name == "pow") return Value::MakeNumber(std::pow(num(0), num(1)));
        if (name == "exp") return Value::MakeNumber(std::exp(num(0)));
        if (name == "ln") return Value::MakeNumber(std::log(num(0)));
        if (name == "log") return Value::MakeNumber(std::log10(num(0)));
        if (name == "sign") { const double v = num(0); return Value::MakeNumber((v > 0) - (v < 0)); }
        if (name == "sin") return Value::MakeNumber(std::sin(num(0) * kDeg2Rad));
        if (name == "cos") return Value::MakeNumber(std::cos(num(0) * kDeg2Rad));
        if (name == "tan") return Value::MakeNumber(std::tan(num(0) * kDeg2Rad));
        if (name == "asin") return Value::MakeNumber(std::asin(num(0)) / kDeg2Rad);
        if (name == "acos") return Value::MakeNumber(std::acos(num(0)) / kDeg2Rad);
        if (name == "atan") return Value::MakeNumber(std::atan(num(0)) / kDeg2Rad);
        if (name == "atan2") return Value::MakeNumber(std::atan2(num(0), num(1)) / kDeg2Rad);
        if (name == "len")
        {
            if (!a.empty() && a[0].type == Value::Type::Vec) return Value::MakeNumber(static_cast<double>(a[0].vec.size()));
            if (!a.empty() && a[0].type == Value::Type::Str) return Value::MakeNumber(static_cast<double>(a[0].str.size()));
            return Value::MakeNumber(0.0);
        }
        if (name == "norm")
        {
            if (!a.empty() && a[0].type == Value::Type::Vec)
            {
                double s = 0.0;
                for (const Value& c : a[0].vec) s += c.AsNumber(0.0) * c.AsNumber(0.0);
                return Value::MakeNumber(std::sqrt(s));
            }
            return Value::MakeNumber(0.0);
        }
        if (name == "concat")
        {
            std::vector<Value> out;
            for (const Value& v : a)
            {
                if (v.type == Value::Type::Vec) out.insert(out.end(), v.vec.begin(), v.vec.end());
                else out.push_back(v);
            }
            return MakeVector(std::move(out));
        }
        if (name == "str")
        {
            std::string s;
            for (const Value& v : a) s += ValueToString(v);
            return Value::MakeStr(std::move(s));
        }
        if (name == "gk_terrain_height" || name == "gk_terrain_info")
        {
            if (a.empty())
            {
                Warn("terrain", name + "() requires (TERR, x, y)");
                return Value();
            }
            std::shared_ptr<const FTerrainData> data = TerrainFromValue(a[0], name.c_str());
            if (!data)
            {
                return Value();
            }
            const double x = num(1);
            const double y = num(2);
            if (name == "gk_terrain_height")
            {
                return Value::MakeNumber(data->HeightAt(x, y));
            }
            double h = 0.0;
            double slopeDeg = 0.0;
            bool waterFlag = false;
            uint8_t biome = 0;
            data->InfoAt(x, y, h, slopeDeg, waterFlag, biome);
            std::vector<Value> info;
            info.push_back(Value::MakeNumber(h));
            info.push_back(Value::MakeNumber(slopeDeg));
            info.push_back(Value::MakeNumber(waterFlag ? 1.0 : 0.0));
            info.push_back(Value::MakeNumber(static_cast<double>(biome)));
            return MakeVector(std::move(info));
        }

        Warn("unknownfn", "unknown function '" + name + "'");
        return Value();
    }
} // namespace Assets::Scad::EvalDetail
