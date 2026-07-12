#include "Modules/ScadLoader/FScadEvaluator.Detail.h"

namespace Assets::Scad
{
    // ----------------------------------------------------------------------
    // Value helpers (declared in FScadTypes.h)
    // ----------------------------------------------------------------------
    bool Value::IsTruthy() const
    {
        switch (type)
        {
        case Type::Bool: return boolean;
        case Type::Number: return num != 0.0;
        case Type::Str: return !str.empty();
        case Type::Vec: return !vec.empty();
        case Type::Range: return true;
        default: return false;
        }
    }

    double Value::AsNumber(double fallback) const
    {
        if (type == Type::Number) return num;
        if (type == Type::Bool) return boolean ? 1.0 : 0.0;
        return fallback;
    }

    bool Value::AsVec3(glm::dvec3& out) const
    {
        if (type != Type::Vec) return false;
        for (size_t i = 0; i < vec.size() && i < 3; ++i)
        {
            out[static_cast<int>(i)] = vec[i].AsNumber(out[static_cast<int>(i)]);
        }
        return true;
    }

    bool Value::AsVec4(glm::dvec4& out) const
    {
        if (type != Type::Vec) return false;
        for (size_t i = 0; i < vec.size() && i < 4; ++i)
        {
            out[static_cast<int>(i)] = vec[i].AsNumber(out[static_cast<int>(i)]);
        }
        return true;
    }

    bool ScadEvaluator::Evaluate(const Scope& mainTopLevel,
                                 const std::unordered_map<std::string, StmtPtr>& modules,
                                 const std::unordered_map<std::string, StmtPtr>& functions,
                                 const ScadLoadOptions& options,
                                 EvalResult& outResult,
                                 std::string& outError)
    {
        outError.clear();
        EvalDetail::Evaluator evaluator(modules, functions, options, outResult);
        evaluator.RunFlat(mainTopLevel);
        return true;
    }

    bool ScadEvaluator::EvaluateScene(const Scope& mainTopLevel,
                                      const std::unordered_map<std::string, StmtPtr>& modules,
                                      const std::unordered_map<std::string, StmtPtr>& functions,
                                      const ScadLoadOptions& options,
                                      SceneEvalResult& outResult,
                                      std::string& outError)
    {
        outError.clear();
        EvalDetail::Evaluator evaluator(modules, functions, options, outResult);
        evaluator.RunScene(mainTopLevel);
        return true;
    }
} // namespace Assets::Scad
