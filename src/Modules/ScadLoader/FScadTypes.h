#pragma once

// ============================================================================
// FScadTypes.h - Shared data model for the OpenSCAD (.scad) loader.
//
// Layering (single direction): Loader -> Geometry -> Evaluator -> Parser ->
// Lexer -> Types. This header only holds plain data: load options, the runtime
// Value variant, and the AST node definitions produced by the parser.
//
// All numeric evaluation uses double (matching OpenSCAD). Coordinates stay in
// native SCAD space (right-handed, Z-up) until the loader bakes them into the
// engine (right-handed, Y-up) via a -90 deg rotation about X.
// ============================================================================

#include <memory>
#include <string>
#include <vector>

#include <glm/glm.hpp>

namespace Assets
{
    // Public options for the SCAD loader. Mirrors LDrawLoadOptions style.
    struct ScadLoadOptions
    {
        // Uniform world scale applied on top of the Z-up -> Y-up conversion.
        // Default 1.0: the sample treats "1 unit ~= 1 meter".
        float scadToWorldScale = 1.0f;

        // Append a neutral display floor under the model.
        bool addFloor = false;

        // Max module/function call recursion depth (guards runaway recursion).
        int maxRecursionDepth = 200;

        // Vertex-normal smoothing threshold. Adjacent faces within this dihedral
        // angle share a smoothed normal (curved primitives); sharper edges stay
        // faceted. 0 => flat shading.
        float smoothAngleDegrees = 35.0f;
    };

    namespace Scad
    {
        inline constexpr double kPi = 3.14159265358979323846;
        inline constexpr double kDeg2Rad = kPi / 180.0;

        // ------------------------------------------------------------------
        // Value: the runtime variant used during evaluation.
        // ------------------------------------------------------------------
        struct Value
        {
            enum class Type
            {
                Undef,
                Number,
                Bool,
                Str,
                Vec,
                Range
            };

            Type type = Type::Undef;
            double num = 0.0;
            bool boolean = false;
            std::string str;
            std::vector<Value> vec;
            double rangeBegin = 0.0;
            double rangeStep = 1.0;
            double rangeEnd = 0.0;

            static Value MakeNumber(double v)
            {
                Value r;
                r.type = Type::Number;
                r.num = v;
                return r;
            }
            static Value MakeBool(bool v)
            {
                Value r;
                r.type = Type::Bool;
                r.boolean = v;
                return r;
            }
            static Value MakeStr(std::string v)
            {
                Value r;
                r.type = Type::Str;
                r.str = std::move(v);
                return r;
            }
            static Value MakeVec(std::vector<Value> v)
            {
                Value r;
                r.type = Type::Vec;
                r.vec = std::move(v);
                return r;
            }
            static Value MakeRange(double b, double s, double e)
            {
                Value r;
                r.type = Type::Range;
                r.rangeBegin = b;
                r.rangeStep = s;
                r.rangeEnd = e;
                return r;
            }

            bool IsNumber() const { return type == Type::Number; }
            bool IsVec() const { return type == Type::Vec; }

            bool IsTruthy() const;
            double AsNumber(double fallback = 0.0) const;
            // Reads a vec2/vec3/vec4-like value into out (missing components stay as provided).
            bool AsVec3(glm::dvec3& out) const;
            bool AsVec4(glm::dvec4& out) const;
        };

        // ------------------------------------------------------------------
        // Expressions
        // ------------------------------------------------------------------
        enum class ExprKind
        {
            Number,
            Bool,
            Str,
            Undef,
            VectorLit,
            RangeLit, // list[0]=begin, (optional list[1]=step), last=end
            Ident,    // str = name
            Index,    // list[0]=base, list[1]=index
            Member,   // list[0]=base, str = member name (x/y/z)
            Unary,    // str = op ("-","!"), list[0]=operand
            Binary,   // str = op, list[0]=lhs, list[1]=rhs
            Cond,     // list[0]=cond, list[1]=then, list[2]=else
            Call,     // str = callee name, args = arguments
            // List-comprehension elements (only valid inside a VectorLit element list)
            CompFor,  // args = loop bindings, list[0] = body element
            CompLet,  // args = let bindings, list[0] = body element
            CompIf,   // list[0]=cond, list[1]=then element, list[2]=else element (optional)
            CompEach  // list[0] = expr evaluating to a vector to splice in
        };

        struct Expr;
        using ExprPtr = std::shared_ptr<Expr>;

        struct CallArg
        {
            std::string name; // empty => positional
            ExprPtr value;
        };

        struct Expr
        {
            ExprKind kind = ExprKind::Undef;
            int line = 0;

            double num = 0.0;
            bool boolean = false;
            std::string str; // ident name / string literal / operator / callee / member

            std::vector<ExprPtr> list; // sub-expressions (see ExprKind notes)
            std::vector<CallArg> args; // for Call
        };

        // ------------------------------------------------------------------
        // Statements
        // ------------------------------------------------------------------
        struct Stmt;
        using StmtPtr = std::shared_ptr<Stmt>;
        using Scope = std::vector<StmtPtr>;

        struct Param
        {
            std::string name;
            ExprPtr defaultValue; // may be null
        };

        enum class StmtKind
        {
            Assign,      // name = value;
            ModuleDef,   // module name(params) body
            FunctionDef, // function name(params) = value;
            Instance     // name(args) children   (builtins, control flow, user modules)
        };

        struct Stmt
        {
            StmtKind kind = StmtKind::Instance;
            int line = 0;

            std::string name;  // assign target / def name / instance (module/builtin/control) name
            ExprPtr value;     // assign value / function body

            std::vector<Param> params; // module/function params
            Scope body;                // module body

            std::vector<CallArg> args;  // instance args
            Scope children;             // instance children (then-branch for "if")
            Scope elseChildren;         // else-branch for "if"
            std::string modifiers;      // any of '*','!','#','%'
        };
    } // namespace Scad
} // namespace Assets
