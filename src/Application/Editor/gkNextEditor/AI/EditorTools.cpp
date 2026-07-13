#include "AI/EditorTools.hpp"
#include "AI/EditorScriptExecutor.hpp"

#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Assets/Core/Scene.hpp"
#include "EditorContext.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Modules/NextAI/AI/IAITool.hpp"
#include "Modules/NextAI/AI/ToolRegistry.hpp"

#include <fmt/format.h>
#include <glm/gtc/quaternion.hpp>
#include <nlohmann/json.hpp>

#include <regex>
#include <sstream>
#include <string>
#include <vector>

using json = nlohmann::json;
using NextAI::EToolParamType;
using NextAI::FToolCallContext;
using NextAI::FToolParam;
using NextAI::FToolSchema;
using NextAI::IAITool;

namespace Editor
{
    namespace
    {
        // ---------- Arg helpers (loose JSON typing tolerance) ----------

        std::string ArgString(const json& args, const char* key, const std::string& fallback = "")
        {
            if (!args.is_object()) return fallback;
            auto it = args.find(key);
            if (it == args.end() || it->is_null()) return fallback;
            if (it->is_string()) return it->get<std::string>();
            if (it->is_number()) return std::to_string(it->get<double>());
            if (it->is_boolean()) return it->get<bool>() ? "true" : "false";
            return it->dump();
        }

        double ArgNumber(const json& args, const char* key, double fallback = 0.0)
        {
            if (!args.is_object()) return fallback;
            auto it = args.find(key);
            if (it == args.end() || it->is_null()) return fallback;
            if (it->is_number()) return it->get<double>();
            if (it->is_string())
            {
                try { return std::stod(it->get<std::string>()); }
                catch (...) { return fallback; }
            }
            return fallback;
        }

        int ArgInt(const json& args, const char* key, int fallback = 0)
        {
            return static_cast<int>(ArgNumber(args, key, fallback));
        }

        // Quote an EditorScript token: wrap in double quotes if it has spaces or special chars,
        // escape embedded backslashes and double quotes.
        std::string QuoteForScript(const std::string& s)
        {
            bool needsQuote = s.empty();
            for (char c : s)
            {
                if (c == ' ' || c == '\t' || c == '"' || c == '\\' || c == '#')
                {
                    needsQuote = true;
                    break;
                }
            }
            if (!needsQuote) return s;
            std::string out;
            out.reserve(s.size() + 2);
            out.push_back('"');
            for (char c : s)
            {
                if (c == '"' || c == '\\') out.push_back('\\');
                out.push_back(c);
            }
            out.push_back('"');
            return out;
        }

        // ---------- Execution + collection ----------

        struct FExecResult
        {
            std::string log;
            std::vector<FDeferredEditorAction> deferred;
            std::string error;
        };

        FExecResult RunScriptLine(FEditorScriptExecutor& executor, EditorContext* ctx,
                                  const std::string& line, bool defer)
        {
            FExecResult r;
            if (!ctx)
            {
                r.error = "editor context unavailable (no active frame)";
                return r;
            }
            // Drain pre-existing log so we only return entries produced by this call.
            // Deferred actions are intentionally NOT drained — FEditorAIService takes
            // them on its main-thread pump so the user-confirm UI can pick them up.
            (void)executor.TakeLog();
            executor.ExecuteScriptText(line, ctx, defer);
            auto entries = executor.TakeLog();
            std::ostringstream os;
            for (const auto& e : entries)
            {
                os << (e.isError ? "ERROR: " : "") << e.message << "\n";
            }
            r.log = os.str();
            if (r.log.empty()) r.log = "(no output)";
            return r;
        }

        std::string FormatExecResult(const FExecResult& r)
        {
            if (!r.error.empty()) return "ERROR: " + r.error;
            return r.log;
        }

        // ---------- Base mutation tool ----------

        class FScriptForwardingTool : public IAITool
        {
        public:
            FScriptForwardingTool(std::string name, std::string description, FToolSchema schema,
                                  FEditorScriptExecutor* executor, FEditorContextProvider provider, bool defer,
                                  bool highRisk = false)
                : name_(std::move(name)), description_(std::move(description)),
                  schema_(std::move(schema)), executor_(executor),
                  provider_(std::move(provider)), defer_(defer), highRisk_(highRisk) {}

            std::string Name() const override { return name_; }
            FToolSchema Schema() const override { return schema_; }
            bool RequiresMainThread() const override { return true; }
            bool IsHighRisk() const override { return highRisk_; }

            std::string Execute(const json& args, FToolCallContext&) override
            {
                if (!executor_) return "ERROR: editor script executor not bound";
                if (!provider_) return "ERROR: editor context provider not bound";
                std::string line = BuildLine(args);
                if (line.empty()) return "ERROR: failed to construct script line";
                EditorContext* ctx = provider_();
                FExecResult r = RunScriptLine(*executor_, ctx, line, defer_);
                return FormatExecResult(r);
            }

        protected:
            virtual std::string BuildLine(const json& args) = 0;

            std::string name_;
            std::string description_;
            FToolSchema schema_;
            FEditorScriptExecutor* executor_;
            FEditorContextProvider provider_;
            bool defer_;
            bool highRisk_;
        };

        // ---------- Mutation tools ----------

        FToolSchema MakeSchema(const std::string& name, const std::string& desc,
                                std::vector<FToolParam> params)
        {
            FToolSchema s;
            s.name = name;
            s.description = desc;
            s.params = std::move(params);
            return s;
        }

        class FSelectTool : public FScriptForwardingTool
        {
        public:
            FSelectTool(FEditorScriptExecutor* exec, FEditorContextProvider prov)
                : FScriptForwardingTool(
                      "scene_select", "Select a node by name or instanceId.",
                      MakeSchema("scene_select", "Select a node",
                                 {{"node", EToolParamType::String, "node name or instanceId", true}}),
                      exec, std::move(prov), false) {}
            std::string BuildLine(const json& args) override
            {
                std::string node = ArgString(args, "node");
                if (node.empty()) return {};
                return "select " + QuoteForScript(node);
            }
        };

        class FRenameTool : public FScriptForwardingTool
        {
        public:
            FRenameTool(FEditorScriptExecutor* exec, FEditorContextProvider prov)
                : FScriptForwardingTool(
                      "scene_rename", "Rename a node.",
                      MakeSchema("scene_rename", "Rename a node",
                                 {{"node", EToolParamType::String, "current name or id", true},
                                  {"new_name", EToolParamType::String, "new name", true}}),
                      exec, std::move(prov), false) {}
            std::string BuildLine(const json& args) override
            {
                std::string n = ArgString(args, "node");
                std::string nn = ArgString(args, "new_name");
                if (n.empty() || nn.empty()) return {};
                return fmt::format("rename {} {}", QuoteForScript(n), QuoteForScript(nn));
            }
        };

        class FDeleteTool : public FScriptForwardingTool
        {
        public:
            FDeleteTool(FEditorScriptExecutor* exec, FEditorContextProvider prov)
                : FScriptForwardingTool(
                      "scene_delete", "Delete a node.",
                      MakeSchema("scene_delete", "Delete a node",
                                 {{"node", EToolParamType::String, "name or id", true}}),
                      exec, std::move(prov), false) {}
            std::string BuildLine(const json& args) override
            {
                std::string n = ArgString(args, "node");
                if (n.empty()) return {};
                return "delete " + QuoteForScript(n);
            }
        };

        class FDuplicateTool : public FScriptForwardingTool
        {
        public:
            FDuplicateTool(FEditorScriptExecutor* exec, FEditorContextProvider prov)
                : FScriptForwardingTool(
                      "scene_duplicate", "Duplicate a node.",
                      MakeSchema("scene_duplicate", "Duplicate a node",
                                 {{"node", EToolParamType::String, "name or id", true}}),
                      exec, std::move(prov), false) {}
            std::string BuildLine(const json& args) override
            {
                std::string n = ArgString(args, "node");
                if (n.empty()) return {};
                return "duplicate " + QuoteForScript(n);
            }
        };

        class FTransformTool : public FScriptForwardingTool
        {
        public:
            FTransformTool(const char* op, FEditorScriptExecutor* exec, FEditorContextProvider prov)
                : FScriptForwardingTool(
                      std::string("scene_") + op,
                      fmt::format("Set node {} (absolute, world space).", op),
                      MakeSchema(std::string("scene_") + op, std::string("Set node ") + op,
                                 {{"node", EToolParamType::String, "name or id", true},
                                  {"x", EToolParamType::Number, "x", true},
                                  {"y", EToolParamType::Number, "y", true},
                                  {"z", EToolParamType::Number, "z", true}}),
                      exec, std::move(prov), false),
                  op_(op) {}
            std::string BuildLine(const json& args) override
            {
                std::string n = ArgString(args, "node");
                if (n.empty()) return {};
                return fmt::format("{} {} {:g} {:g} {:g}", op_, QuoteForScript(n),
                                   ArgNumber(args, "x"), ArgNumber(args, "y"), ArgNumber(args, "z"));
            }
        private:
            const char* op_;
        };

        class FSetPropertyTool : public FScriptForwardingTool
        {
        public:
            FSetPropertyTool(FEditorScriptExecutor* exec, FEditorContextProvider prov)
                : FScriptForwardingTool(
                      "scene_set_property",
                      "Set a component property on a node.",
                      MakeSchema("scene_set_property", "Set a component property",
                                 {{"node", EToolParamType::String, "name or id", true},
                                  {"component", EToolParamType::String, "component type name", true},
                                  {"property", EToolParamType::String, "property name", true},
                                  {"value", EToolParamType::String, "value as text (numbers, bools, vec3 like '1,2,3')", true}}),
                      exec, std::move(prov), false) {}
            std::string BuildLine(const json& args) override
            {
                std::string n = ArgString(args, "node");
                std::string c = ArgString(args, "component");
                std::string p = ArgString(args, "property");
                std::string v = ArgString(args, "value");
                if (n.empty() || c.empty() || p.empty()) return {};
                return fmt::format("set_property {} {} {} {}",
                                   QuoteForScript(n), QuoteForScript(c),
                                   QuoteForScript(p), QuoteForScript(v));
            }
        };

        class FRenamePatternTool : public FScriptForwardingTool
        {
        public:
            FRenamePatternTool(FEditorScriptExecutor* exec, FEditorContextProvider prov)
                : FScriptForwardingTool(
                      "scene_rename_pattern",
                      "Rename all nodes whose name contains `search` by replacing the match with `replace`.",
                      MakeSchema("scene_rename_pattern", "Batch rename",
                                 {{"search", EToolParamType::String, "substring to search", true},
                                  {"replace", EToolParamType::String, "replacement substring", true}}),
                      exec, std::move(prov), false) {}
            std::string BuildLine(const json& args) override
            {
                std::string s = ArgString(args, "search");
                std::string r = ArgString(args, "replace");
                if (s.empty()) return {};
                return fmt::format("rename_pattern {} {}", QuoteForScript(s), QuoteForScript(r));
            }
        };

        class FCVarTool : public FScriptForwardingTool
        {
        public:
            FCVarTool(FEditorScriptExecutor* exec, FEditorContextProvider prov)
                : FScriptForwardingTool(
                      "cvar",
                      "Run a CVar command (e.g. `r.bloom.quality 2`, `cvar.complete bloom`).",
                      MakeSchema("cvar", "CVar command",
                                 {{"command", EToolParamType::String, "cvar command line", true}}),
                      exec, std::move(prov), false) {}
            std::string BuildLine(const json& args) override
            {
                std::string c = ArgString(args, "command");
                if (c.empty()) return {};
                // Pass through verbatim; ExecCVar joins tokens after `cvar`.
                return "cvar " + c;
            }
        };

        class FActionTool : public FScriptForwardingTool
        {
        public:
            FActionTool(FEditorScriptExecutor* exec, FEditorContextProvider prov, bool defer)
                : FScriptForwardingTool(
                      "scene_action",
                      "Trigger an editor action: load_scene <ref> (HIGH-RISK, deferred for user confirm), "
                      "add_scene <ref>, focus_selected.",
                      MakeSchema("scene_action", "Editor action",
                                 {{"name", EToolParamType::String, "action name: load_scene | add_scene | focus_selected", true},
                                  {"arg", EToolParamType::String, "action argument (scene ref for load/add)", false}}),
                      exec, std::move(prov), defer, /*highRisk=*/false) {}
            std::string BuildLine(const json& args) override
            {
                std::string n = ArgString(args, "name");
                std::string a = ArgString(args, "arg");
                if (n.empty()) return {};
                if (a.empty()) return "action " + n;
                return fmt::format("action {} {}", n, QuoteForScript(a));
            }
        };

        class FRunEditorScriptTool : public IAITool
        {
        public:
            FRunEditorScriptTool(FEditorScriptExecutor* exec, FEditorContextProvider prov, bool defer)
                : executor_(exec), provider_(std::move(prov)), defer_(defer) {}
            std::string Name() const override { return "run_editor_script"; }
            FToolSchema Schema() const override
            {
                return MakeSchema("run_editor_script",
                    "Execute a multi-line EditorScript verbatim (one command per line). Escape hatch for batch ops.",
                    {{"script", EToolParamType::String, "EditorScript text", true}});
            }
            bool RequiresMainThread() const override { return true; }
            std::string Execute(const json& args, FToolCallContext&) override
            {
                if (!executor_ || !provider_) return "ERROR: not bound";
                EditorContext* ctx = provider_();
                std::string text = ArgString(args, "script");
                if (text.empty()) return "ERROR: script is required";
                FExecResult r = RunScriptLine(*executor_, ctx, text, defer_);
                return FormatExecResult(r);
            }
        private:
            FEditorScriptExecutor* executor_;
            FEditorContextProvider provider_;
            bool defer_;
        };

        class FRunJavaScriptTool : public IAITool
        {
        public:
            FRunJavaScriptTool(FEditorScriptExecutor* exec) : executor_(exec) {}
            std::string Name() const override { return "run_javascript"; }
            FToolSchema Schema() const override
            {
                return MakeSchema("run_javascript",
                    "Eval JavaScript via QuickJS (Editor.* helpers available). Escape hatch for complex logic.",
                    {{"code", EToolParamType::String, "JS source", true}});
            }
            bool RequiresMainThread() const override { return true; }
            std::string Execute(const json& args, FToolCallContext&) override
            {
                if (!executor_) return "ERROR: not bound";
                std::string code = ArgString(args, "code");
                if (code.empty()) return "ERROR: code is required";
                (void)executor_->TakeLog();
                executor_->EvalJavaScript(code);
                auto log = executor_->TakeLog();
                std::ostringstream os;
                for (const auto& e : log) os << (e.isError ? "ERROR: " : "") << e.message << "\n";
                return os.str().empty() ? "(no output)" : os.str();
            }
        private:
            FEditorScriptExecutor* executor_;
        };

        // ---------- Query tools (need scene access; not script-forwarding) ----------

        std::string FormatNodeRow(const Assets::Node& node)
        {
            const auto t = node.Translation();
            std::string comps;
            for (const auto& comp : node.GetComponents())
            {
                if (!comps.empty()) comps += ",";
                comps += std::string(comp->GetTypeName());
            }
            return fmt::format("[{}] {} pos=({:.2f},{:.2f},{:.2f}) comps=[{}]",
                               node.GetInstanceId(), node.GetName(),
                               t.x, t.y, t.z, comps.empty() ? "-" : comps);
        }

        class FListNodesTool : public IAITool
        {
        public:
            FListNodesTool(NextEngine* engine) : engine_(engine) {}
            std::string Name() const override { return "scene_list_nodes"; }
            FToolSchema Schema() const override
            {
                return MakeSchema("scene_list_nodes",
                    "List nodes in the current scene. Filter by name substring (case-sensitive).",
                    {{"pattern", EToolParamType::String, "name substring filter (optional)", false},
                     {"max", EToolParamType::Integer, "max rows (1-500, default 80)", false}});
            }
            bool RequiresMainThread() const override { return true; }
            std::string Execute(const json& args, FToolCallContext&) override
            {
                if (!engine_) return "ERROR: engine not bound";
                std::string pattern = ArgString(args, "pattern");
                int maxRows = ArgInt(args, "max", 80);
                if (maxRows < 1) maxRows = 1;
                if (maxRows > 500) maxRows = 500;
                auto& scene = engine_->GetScene();
                const auto& nodes = scene.Nodes();
                std::ostringstream os;
                int shown = 0;
                size_t totalMatch = 0;
                for (const auto& node : nodes)
                {
                    const std::string& name = node->GetName();
                    if (!pattern.empty() && name.find(pattern) == std::string::npos) continue;
                    ++totalMatch;
                    if (shown < maxRows)
                    {
                        os << FormatNodeRow(*node) << "\n";
                        ++shown;
                    }
                }
                if (totalMatch == 0) return "(no matching nodes)";
                if (static_cast<int>(totalMatch) > shown)
                {
                    os << fmt::format("... [{} more, total {}]\n", totalMatch - shown, totalMatch);
                }
                return os.str();
            }
        private:
            NextEngine* engine_;
        };

        class FGetNodeTool : public IAITool
        {
        public:
            FGetNodeTool(NextEngine* engine) : engine_(engine) {}
            std::string Name() const override { return "scene_get_node"; }
            FToolSchema Schema() const override
            {
                return MakeSchema("scene_get_node",
                    "Get details of a single node by name or instanceId.",
                    {{"node", EToolParamType::String, "name or id", true}});
            }
            bool RequiresMainThread() const override { return true; }
            std::string Execute(const json& args, FToolCallContext&) override
            {
                if (!engine_) return "ERROR: engine not bound";
                std::string ref = ArgString(args, "node");
                if (ref.empty()) return "ERROR: node is required";
                auto& scene = engine_->GetScene();
                Assets::Node* node = nullptr;
                bool isNumeric = !ref.empty() && std::all_of(ref.begin(), ref.end(),
                    [](unsigned char c) { return std::isdigit(c); });
                if (isNumeric)
                {
                    try { node = scene.GetNodeByInstanceId(static_cast<uint32_t>(std::stoul(ref))); }
                    catch (...) { node = nullptr; }
                }
                if (!node) node = scene.GetNode(ref);
                if (!node) return "(not found)";
                const auto euler = glm::degrees(glm::eulerAngles(node->Rotation()));
                const auto s = node->Scale();
                std::ostringstream os;
                os << FormatNodeRow(*node) << "\n";
                os << fmt::format("rotation_euler_deg=({:.1f},{:.1f},{:.1f})\n", euler.x, euler.y, euler.z);
                os << fmt::format("scale=({:.3f},{:.3f},{:.3f})\n", s.x, s.y, s.z);
                return os.str();
            }
        private:
            NextEngine* engine_;
        };

        class FGetSelectionTool : public IAITool
        {
        public:
            FGetSelectionTool(NextEngine* engine) : engine_(engine) {}
            std::string Name() const override { return "scene_get_selection"; }
            FToolSchema Schema() const override
            {
                return MakeSchema("scene_get_selection",
                    "Get the currently selected nodes (primary id + full selection list).", {});
            }
            bool RequiresMainThread() const override { return true; }
            std::string Execute(const json&, FToolCallContext&) override
            {
                if (!engine_) return "ERROR: engine not bound";
                auto& scene = engine_->GetScene();
                std::ostringstream os;
                const uint32_t primary = scene.GetSelectedId();
                os << "primary=";
                if (primary == static_cast<uint32_t>(-1)) os << "none";
                else os << primary;
                os << "\n";
                const auto& ids = scene.GetSelectedIds();
                if (ids.empty()) os << "(no other selection)\n";
                else
                {
                    for (uint32_t id : ids)
                    {
                        if (auto* n = scene.GetNodeByInstanceId(id))
                        {
                            os << FormatNodeRow(*n) << "\n";
                        }
                    }
                }
                return os.str();
            }
        private:
            NextEngine* engine_;
        };

        class FSceneSummaryTool : public IAITool
        {
        public:
            FSceneSummaryTool(NextEngine* engine) : engine_(engine) {}
            std::string Name() const override { return "scene_summary"; }
            FToolSchema Schema() const override
            {
                return MakeSchema("scene_summary",
                    "Compact statistics for the current scene (node count + component-type histogram).", {});
            }
            bool RequiresMainThread() const override { return true; }
            std::string Execute(const json&, FToolCallContext&) override
            {
                if (!engine_) return "ERROR: engine not bound";
                auto& scene = engine_->GetScene();
                const auto& nodes = scene.Nodes();
                std::map<std::string, int> histo;
                for (const auto& node : nodes)
                {
                    for (const auto& comp : node->GetComponents())
                    {
                        histo[std::string(comp->GetTypeName())]++;
                    }
                }
                std::ostringstream os;
                os << "total_nodes=" << nodes.size() << "\n";
                os << "selected_primary=";
                const uint32_t p = scene.GetSelectedId();
                if (p == static_cast<uint32_t>(-1)) os << "none\n"; else os << p << "\n";
                os << "component_histogram:\n";
                for (const auto& [type, count] : histo)
                {
                    os << "  " << type << " = " << count << "\n";
                }
                if (histo.empty()) os << "  (none)\n";
                return os.str();
            }
        private:
            NextEngine* engine_;
        };
    } // namespace

    void RegisterEditorTools(NextAI::FToolRegistry& registry, const FEditorToolsOptions& options)
    {
        NextEngine* engine = options.engine;
        FEditorScriptExecutor* exec = options.executor;
        const auto& prov = options.contextProvider;
        const bool defer = options.deferHighRiskActions;

        if (options.enableQueryTools && engine)
        {
            registry.Register(std::make_unique<FListNodesTool>(engine));
            registry.Register(std::make_unique<FGetNodeTool>(engine));
            registry.Register(std::make_unique<FGetSelectionTool>(engine));
            registry.Register(std::make_unique<FSceneSummaryTool>(engine));
        }

        if (options.enableMutationTools && exec && prov)
        {
            registry.Register(std::make_unique<FSelectTool>(exec, prov));
            registry.Register(std::make_unique<FRenameTool>(exec, prov));
            registry.Register(std::make_unique<FDeleteTool>(exec, prov));
            registry.Register(std::make_unique<FDuplicateTool>(exec, prov));
            registry.Register(std::make_unique<FTransformTool>("move", exec, prov));
            registry.Register(std::make_unique<FTransformTool>("rotate", exec, prov));
            registry.Register(std::make_unique<FTransformTool>("scale", exec, prov));
            registry.Register(std::make_unique<FSetPropertyTool>(exec, prov));
            registry.Register(std::make_unique<FRenamePatternTool>(exec, prov));
            registry.Register(std::make_unique<FCVarTool>(exec, prov));
        }

        if (options.enableActionTools && exec && prov)
        {
            registry.Register(std::make_unique<FActionTool>(exec, prov, defer));
        }

        if (options.enableEscapeHatches && exec && prov)
        {
            registry.Register(std::make_unique<FRunEditorScriptTool>(exec, prov, defer));
            registry.Register(std::make_unique<FRunJavaScriptTool>(exec));
        }
    }
}
