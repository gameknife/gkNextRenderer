#include "Modules/ScadLoader/FScadParser.h"

namespace Assets::Scad
{
    namespace
    {
        class Parser
        {
        public:
            Parser(const std::vector<Token>& tokens) : tokens_(tokens) {}

            bool ParseProgram(Scope& outScope, std::string& outError,
                              std::vector<FScadTopLevelSpan>* outTopLevelSpans)
            {
                while (!Check(Tok::Eof) && !failed_)
                {
                    if (Check(Tok::Semicolon))
                    {
                        Advance();
                        continue;
                    }
                    const size_t firstToken = pos_;
                    StmtPtr s = ParseStatement();
                    if (failed_)
                    {
                        break;
                    }
                    if (s)
                    {
                        outScope.push_back(s);
                        if (outTopLevelSpans)
                        {
                            // pos_ now sits one past the statement's last token.
                            const size_t lastToken = pos_ > firstToken ? pos_ - 1 : firstToken;
                            FScadTopLevelSpan span;
                            span.begin = tokens_[firstToken].begin;
                            span.end = tokens_[lastToken].end;
                            span.line = tokens_[firstToken].line;
                            span.endLine = tokens_[lastToken].line;
                            outTopLevelSpans->push_back(span);
                        }
                    }
                }
                if (failed_)
                {
                    outError = error_;
                    return false;
                }
                return true;
            }

        private:
            const std::vector<Token>& tokens_;
            size_t pos_ = 0;
            bool failed_ = false;
            std::string error_;

            const Token& Peek(size_t offset = 0) const
            {
                const size_t idx = pos_ + offset;
                return idx < tokens_.size() ? tokens_[idx] : tokens_.back();
            }
            const Token& Cur() const { return Peek(0); }
            bool Check(Tok k) const { return Cur().kind == k; }
            const Token& Advance() { return tokens_[pos_ < tokens_.size() - 1 ? pos_++ : pos_]; }

            bool Accept(Tok k)
            {
                if (Check(k))
                {
                    Advance();
                    return true;
                }
                return false;
            }

            void Expect(Tok k, const char* what)
            {
                if (!Accept(k))
                {
                    Fail(std::string("expected ") + what);
                }
            }

            void Fail(const std::string& msg)
            {
                if (!failed_)
                {
                    failed_ = true;
                    error_ = msg + " at line " + std::to_string(Cur().line);
                }
            }

            bool IsKeyword(const Token& t, const char* kw) const
            {
                return t.kind == Tok::Ident && t.text == kw;
            }

            // ----------------------------------------------------------------
            // Statements
            // ----------------------------------------------------------------
            StmtPtr ParseStatement()
            {
                const Token& t = Cur();

                if (IsKeyword(t, "module"))
                {
                    return ParseModuleDef();
                }
                if (IsKeyword(t, "function"))
                {
                    return ParseFunctionDef();
                }

                // Assignment: IDENT '=' / $SPECIAL '=' (but not '==').
                if ((t.kind == Tok::Ident || t.kind == Tok::Special) && Peek(1).kind == Tok::Assign)
                {
                    return ParseAssignment();
                }

                return ParseInstantiationStatement();
            }

            StmtPtr ParseModuleDef()
            {
                auto s = std::make_shared<Stmt>();
                s->kind = StmtKind::ModuleDef;
                s->line = Cur().line;
                s->sourceOffset = Cur().begin;
                Advance(); // 'module'
                if (!Check(Tok::Ident))
                {
                    Fail("expected module name");
                    return nullptr;
                }
                s->name = Advance().text;
                Expect(Tok::LParen, "'('");
                s->params = ParseParams();
                Expect(Tok::RParen, "')'");
                s->body = ParseChild();
                return s;
            }

            StmtPtr ParseFunctionDef()
            {
                auto s = std::make_shared<Stmt>();
                s->kind = StmtKind::FunctionDef;
                s->line = Cur().line;
                s->sourceOffset = Cur().begin;
                Advance(); // 'function'
                if (!Check(Tok::Ident))
                {
                    Fail("expected function name");
                    return nullptr;
                }
                s->name = Advance().text;
                Expect(Tok::LParen, "'('");
                s->params = ParseParams();
                Expect(Tok::RParen, "')'");
                Expect(Tok::Assign, "'='");
                s->value = ParseExpr();
                Expect(Tok::Semicolon, "';'");
                return s;
            }

            StmtPtr ParseAssignment()
            {
                auto s = std::make_shared<Stmt>();
                s->kind = StmtKind::Assign;
                s->line = Cur().line;
                s->sourceOffset = Cur().begin;
                s->name = Advance().text; // ident or special
                Expect(Tok::Assign, "'='");
                s->value = ParseExpr();
                Expect(Tok::Semicolon, "';'");
                return s;
            }

            // modifiers* ( block | name(args) child | if(...) ... )
            StmtPtr ParseInstantiationStatement()
            {
                std::string modifiers;
                while (Check(Tok::Star) || Check(Tok::Not) || Check(Tok::Hash) || Check(Tok::Percent))
                {
                    switch (Cur().kind)
                    {
                    case Tok::Star: modifiers.push_back('*'); break;
                    case Tok::Not: modifiers.push_back('!'); break;
                    case Tok::Hash: modifiers.push_back('#'); break;
                    case Tok::Percent: modifiers.push_back('%'); break;
                    default: break;
                    }
                    Advance();
                }

                // Bare block => implicit-union group.
                if (Check(Tok::LBrace))
                {
                    auto s = std::make_shared<Stmt>();
                    s->kind = StmtKind::Instance;
                    s->name = "union";
                    s->line = Cur().line;
                    s->sourceOffset = Cur().begin;
                    s->modifiers = modifiers;
                    s->children = ParseChild();
                    return s;
                }

                if (!Check(Tok::Ident))
                {
                    Fail("expected module instantiation");
                    return nullptr;
                }

                auto s = std::make_shared<Stmt>();
                s->kind = StmtKind::Instance;
                s->line = Cur().line;
                s->sourceOffset = Cur().begin;
                s->name = Advance().text;
                s->modifiers = modifiers;

                if (s->name == "if")
                {
                    Expect(Tok::LParen, "'('");
                    CallArg cond;
                    cond.value = ParseExpr();
                    s->args.push_back(cond);
                    Expect(Tok::RParen, "')'");
                    s->children = ParseChild();
                    if (IsKeyword(Cur(), "else"))
                    {
                        Advance();
                        s->elseChildren = ParseChild();
                    }
                    return s;
                }

                Expect(Tok::LParen, "'('");
                s->args = ParseArgs();
                Expect(Tok::RParen, "')'");
                s->children = ParseChild();
                return s;
            }

            // A child is: ';' | '{' stmts '}' | single instantiation statement.
            Scope ParseChild()
            {
                Scope out;
                if (Accept(Tok::Semicolon))
                {
                    return out;
                }
                if (Accept(Tok::LBrace))
                {
                    while (!Check(Tok::RBrace) && !Check(Tok::Eof) && !failed_)
                    {
                        if (Accept(Tok::Semicolon))
                        {
                            continue;
                        }
                        StmtPtr s = ParseStatement();
                        if (failed_)
                        {
                            break;
                        }
                        if (s)
                        {
                            out.push_back(s);
                        }
                    }
                    Expect(Tok::RBrace, "'}'");
                    return out;
                }
                // Single chained child (transforms / nested instantiation).
                StmtPtr s = ParseInstantiationStatement();
                if (s)
                {
                    out.push_back(s);
                }
                return out;
            }

            std::vector<Param> ParseParams()
            {
                std::vector<Param> params;
                while (!Check(Tok::RParen) && !Check(Tok::Eof) && !failed_)
                {
                    Param p;
                    if (Check(Tok::Ident) || Check(Tok::Special))
                    {
                        p.name = Advance().text;
                    }
                    else
                    {
                        Fail("expected parameter name");
                        break;
                    }
                    if (Accept(Tok::Assign))
                    {
                        p.defaultValue = ParseExpr();
                    }
                    params.push_back(p);
                    if (!Accept(Tok::Comma))
                    {
                        break;
                    }
                }
                return params;
            }

            std::vector<CallArg> ParseArgs()
            {
                std::vector<CallArg> args;
                while (!Check(Tok::RParen) && !Check(Tok::Eof) && !failed_)
                {
                    CallArg a;
                    if ((Check(Tok::Ident) || Check(Tok::Special)) && Peek(1).kind == Tok::Assign)
                    {
                        a.name = Advance().text;
                        Advance(); // '='
                    }
                    a.value = ParseExpr();
                    args.push_back(a);
                    if (!Accept(Tok::Comma))
                    {
                        break;
                    }
                }
                return args;
            }

            // ----------------------------------------------------------------
            // Expressions (precedence climbing)
            // ----------------------------------------------------------------
            ExprPtr MakeExpr(ExprKind kind)
            {
                auto e = std::make_shared<Expr>();
                e->kind = kind;
                e->line = Cur().line;
                return e;
            }

            ExprPtr ParseExpr() { return ParseTernary(); }

            ExprPtr ParseTernary()
            {
                ExprPtr cond = ParseOr();
                if (Accept(Tok::Question))
                {
                    auto e = MakeExpr(ExprKind::Cond);
                    e->list.push_back(cond);
                    e->list.push_back(ParseExpr());
                    Expect(Tok::Colon, "':'");
                    e->list.push_back(ParseExpr());
                    return e;
                }
                return cond;
            }

            ExprPtr ParseOr()
            {
                ExprPtr lhs = ParseAnd();
                while (Check(Tok::OrOr))
                {
                    Advance();
                    lhs = MakeBinary("||", lhs, ParseAnd());
                }
                return lhs;
            }

            ExprPtr ParseAnd()
            {
                ExprPtr lhs = ParseEquality();
                while (Check(Tok::AndAnd))
                {
                    Advance();
                    lhs = MakeBinary("&&", lhs, ParseEquality());
                }
                return lhs;
            }

            ExprPtr ParseEquality()
            {
                ExprPtr lhs = ParseComparison();
                while (Check(Tok::EqEq) || Check(Tok::NotEq))
                {
                    const std::string op = Check(Tok::EqEq) ? "==" : "!=";
                    Advance();
                    lhs = MakeBinary(op, lhs, ParseComparison());
                }
                return lhs;
            }

            ExprPtr ParseComparison()
            {
                ExprPtr lhs = ParseAdditive();
                while (Check(Tok::Lt) || Check(Tok::Gt) || Check(Tok::Le) || Check(Tok::Ge))
                {
                    std::string op;
                    switch (Cur().kind)
                    {
                    case Tok::Lt: op = "<"; break;
                    case Tok::Gt: op = ">"; break;
                    case Tok::Le: op = "<="; break;
                    default: op = ">="; break;
                    }
                    Advance();
                    lhs = MakeBinary(op, lhs, ParseAdditive());
                }
                return lhs;
            }

            ExprPtr ParseAdditive()
            {
                ExprPtr lhs = ParseMultiplicative();
                while (Check(Tok::Plus) || Check(Tok::Minus))
                {
                    const std::string op = Check(Tok::Plus) ? "+" : "-";
                    Advance();
                    lhs = MakeBinary(op, lhs, ParseMultiplicative());
                }
                return lhs;
            }

            ExprPtr ParseMultiplicative()
            {
                ExprPtr lhs = ParseUnary();
                while (Check(Tok::Star) || Check(Tok::Slash) || Check(Tok::Percent))
                {
                    std::string op;
                    switch (Cur().kind)
                    {
                    case Tok::Star: op = "*"; break;
                    case Tok::Slash: op = "/"; break;
                    default: op = "%"; break;
                    }
                    Advance();
                    lhs = MakeBinary(op, lhs, ParseUnary());
                }
                return lhs;
            }

            ExprPtr ParseUnary()
            {
                if (Check(Tok::Minus) || Check(Tok::Not) || Check(Tok::Plus))
                {
                    const std::string op = Check(Tok::Not) ? "!" : (Check(Tok::Minus) ? "-" : "+");
                    Advance();
                    auto e = MakeExpr(ExprKind::Unary);
                    e->str = op;
                    e->list.push_back(ParseUnary());
                    return e;
                }
                return ParsePostfix();
            }

            ExprPtr ParsePostfix()
            {
                ExprPtr e = ParsePrimary();
                while (!failed_)
                {
                    if (Accept(Tok::LBracket))
                    {
                        auto idx = MakeExpr(ExprKind::Index);
                        idx->list.push_back(e);
                        idx->list.push_back(ParseExpr());
                        Expect(Tok::RBracket, "']'");
                        e = idx;
                    }
                    else if (Accept(Tok::Dot))
                    {
                        auto mem = MakeExpr(ExprKind::Member);
                        mem->list.push_back(e);
                        if (Check(Tok::Ident))
                        {
                            mem->str = Advance().text;
                        }
                        else
                        {
                            Fail("expected member name");
                        }
                        e = mem;
                    }
                    else
                    {
                        break;
                    }
                }
                return e;
            }

            ExprPtr ParsePrimary()
            {
                const Token& t = Cur();
                switch (t.kind)
                {
                case Tok::Number:
                {
                    auto e = MakeExpr(ExprKind::Number);
                    e->num = Advance().number;
                    return e;
                }
                case Tok::String:
                {
                    auto e = MakeExpr(ExprKind::Str);
                    e->str = Advance().text;
                    return e;
                }
                case Tok::LParen:
                {
                    Advance();
                    ExprPtr inner = ParseExpr();
                    Expect(Tok::RParen, "')'");
                    return inner;
                }
                case Tok::LBracket:
                    return ParseVectorOrRange();
                case Tok::Special:
                {
                    auto e = MakeExpr(ExprKind::Ident);
                    e->str = Advance().text;
                    return e;
                }
                case Tok::Ident:
                {
                    if (t.text == "true" || t.text == "false")
                    {
                        auto e = MakeExpr(ExprKind::Bool);
                        e->boolean = (t.text == "true");
                        Advance();
                        return e;
                    }
                    if (t.text == "undef")
                    {
                        Advance();
                        return MakeExpr(ExprKind::Undef);
                    }
                    // let(...) <expr> in expression position. This is the normal
                    // way an OpenSCAD *function* body names a subexpression it
                    // uses more than once; without it a rule library has to
                    // inline every shared term, which is both unreadable and
                    // recomputed on every use. Same node kind as the list
                    // comprehension form -- the evaluator distinguishes them by
                    // where it meets the node, not by kind.
                    if (t.text == "let" && Peek(1).kind == Tok::LParen)
                    {
                        Advance();
                        auto e = MakeExpr(ExprKind::CompLet);
                        Expect(Tok::LParen, "'('");
                        e->args = ParseArgs();
                        Expect(Tok::RParen, "')'");
                        // The body is a full expression: `let (a = 1) a + 2`
                        // binds over the whole sum, not just the first term.
                        e->list.push_back(ParseExpr());
                        return e;
                    }
                    const std::string name = Advance().text;
                    if (Accept(Tok::LParen))
                    {
                        auto e = MakeExpr(ExprKind::Call);
                        e->str = name;
                        e->args = ParseArgs();
                        Expect(Tok::RParen, "')'");
                        return e;
                    }
                    auto e = MakeExpr(ExprKind::Ident);
                    e->str = name;
                    return e;
                }
                default:
                    Fail("unexpected token in expression");
                    return MakeExpr(ExprKind::Undef);
                }
            }

            bool IsCompKeyword(const Token& t) const
            {
                return t.kind == Tok::Ident &&
                       (t.text == "for" || t.text == "let" || t.text == "if" || t.text == "each");
            }

            // A vector element: a normal expression or a list-comprehension generator.
            ExprPtr ParseVectorElement()
            {
                const Token& t = Cur();
                if (IsKeyword(t, "for") || IsKeyword(t, "let"))
                {
                    const bool isFor = (t.text == "for");
                    Advance();
                    auto e = MakeExpr(isFor ? ExprKind::CompFor : ExprKind::CompLet);
                    Expect(Tok::LParen, "'('");
                    e->args = ParseArgs();
                    Expect(Tok::RParen, "')'");
                    e->list.push_back(ParseVectorElement());
                    return e;
                }
                if (IsKeyword(t, "if"))
                {
                    Advance();
                    auto e = MakeExpr(ExprKind::CompIf);
                    Expect(Tok::LParen, "'('");
                    e->list.push_back(ParseExpr());
                    Expect(Tok::RParen, "')'");
                    e->list.push_back(ParseVectorElement());
                    if (IsKeyword(Cur(), "else"))
                    {
                        Advance();
                        e->list.push_back(ParseVectorElement());
                    }
                    return e;
                }
                if (IsKeyword(t, "each"))
                {
                    Advance();
                    auto e = MakeExpr(ExprKind::CompEach);
                    e->list.push_back(ParseVectorElement());
                    return e;
                }
                return ParseExpr();
            }

            ExprPtr ParseVectorOrRange()
            {
                Advance(); // '['
                if (Accept(Tok::RBracket))
                {
                    return MakeExpr(ExprKind::VectorLit); // empty vector
                }

                // Leading comprehension generator => comprehension vector.
                if (IsCompKeyword(Cur()))
                {
                    auto e = MakeExpr(ExprKind::VectorLit);
                    e->list.push_back(ParseVectorElement());
                    while (Accept(Tok::Comma))
                    {
                        if (Check(Tok::RBracket)) break;
                        e->list.push_back(ParseVectorElement());
                    }
                    Expect(Tok::RBracket, "']'");
                    return e;
                }

                ExprPtr first = ParseExpr();
                if (Accept(Tok::Colon))
                {
                    auto e = MakeExpr(ExprKind::RangeLit);
                    e->list.push_back(first);
                    ExprPtr second = ParseExpr();
                    if (Accept(Tok::Colon))
                    {
                        e->list.push_back(second);     // step
                        e->list.push_back(ParseExpr()); // end
                    }
                    else
                    {
                        e->list.push_back(second);     // end (step defaults to 1)
                    }
                    Expect(Tok::RBracket, "']'");
                    return e;
                }

                auto e = MakeExpr(ExprKind::VectorLit);
                e->list.push_back(first);
                while (Accept(Tok::Comma))
                {
                    if (Check(Tok::RBracket))
                    {
                        break; // trailing comma
                    }
                    e->list.push_back(ParseVectorElement());
                }
                Expect(Tok::RBracket, "']'");
                return e;
            }

            ExprPtr MakeBinary(const std::string& op, ExprPtr lhs, ExprPtr rhs)
            {
                auto e = std::make_shared<Expr>();
                e->kind = ExprKind::Binary;
                e->str = op;
                e->line = lhs ? lhs->line : Cur().line;
                e->list.push_back(lhs);
                e->list.push_back(rhs);
                return e;
            }
        };
    } // namespace

    bool ScadParser::Parse(const std::vector<Token>& tokens, Scope& outScope, std::string& outError,
                           std::vector<FScadTopLevelSpan>* outTopLevelSpans)
    {
        outScope.clear();
        outError.clear();
        if (outTopLevelSpans)
        {
            outTopLevelSpans->clear();
        }
        Parser parser(tokens);
        return parser.ParseProgram(outScope, outError, outTopLevelSpans);
    }
} // namespace Assets::Scad
