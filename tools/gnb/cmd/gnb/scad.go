package main

import (
	"context"
	"crypto/sha256"
	"errors"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"strings"
	"time"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/console"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/llm"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/platform"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/runner"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/scadcompose"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/scadgen"
	"github.com/spf13/cobra"
)

func newScadCommand(ctx appContext) *cobra.Command {
	cmd := &cobra.Command{
		Use:   "scad",
		Short: "SCAD kit utilities (catalog + scene compose, see docs/designs/scad-scene-compose-design.md)",
	}
	cmd.AddCommand(newScadCatalogCommand(ctx))
	cmd.AddCommand(newScadComposeCommand(ctx))
	cmd.AddCommand(newScadGenerateCommand(ctx))
	return cmd
}

func newScadGenerateCommand(ctx appContext) *cobra.Command {
	var name string
	var modelID string
	var repairs int
	var temperature float64
	var debug bool
	cmd := &cobra.Command{
		Use:   "generate <场景描述>",
		Short: "用本地 LLM 从自然语言生成 scene spec 并展开为 .scad",
		Long: "把 kit catalog 作为零件菜单注入 prompt，让本地 LLM（gnb llm 基建）输出 scene\n" +
			"spec JSON；compose 校验失败会把错误回喂给模型自修复。成功后写\n" +
			"assets/scad/specs/<name>.json + assets/scad/gen/<name>.scad（含 build assets 镜像）。",
		Args: cobra.MinimumNArgs(1),
		RunE: func(cmd *cobra.Command, args []string) error {
			request := strings.Join(args, " ")
			libDir := filepath.Join(ctx.repoRoot, "assets", "scad", "lib")
			catalog, err := scadcompose.LoadCatalog(filepath.Join(libDir, "catalog.json"))
			if err != nil {
				return err
			}
			menu, err := scadgen.BuildKitMenu(filepath.Join(libDir, "catalog.json"))
			if err != nil {
				return err
			}

			cfg, err := selectLLMModel(ctx.cfg.External.LLM, modelID)
			if err != nil {
				return err
			}
			srv := llm.NewServer(ctx.repoRoot, cfg)
			runCtx, cancel := context.WithTimeout(cmd.Context(), 15*time.Minute)
			defer cancel()
			info, err := srv.EnsureRunningOrReuse(runCtx)
			if err != nil {
				return err
			}
			client := llm.NewClient(info.BaseURL())
			chat := func(chatCtx context.Context, messages []scadgen.Message) (string, error) {
				chatMessages := make([]llm.ChatMessage, len(messages))
				for i, message := range messages {
					chatMessages[i] = llm.ChatMessage{Role: message.Role, Content: message.Content}
				}
				return client.Chat(chatCtx, llm.ChatRequest{
					Messages:    chatMessages,
					Temperature: temperature,
					MaxTokens:   4096,
				})
			}

			console.Info("generating spec via " + cfg.Active + " ...")
			outcome, err := scadgen.Generate(runCtx, chat, catalog, menu, request,
				scadgen.Options{MaxRepairs: repairs})
			if err != nil {
				var genErr *scadgen.GenerateError
				if debug && errors.As(err, &genErr) {
					dumpPath := filepath.Join(os.TempDir(), "gnb_scad_generate_transcript.txt")
					var dump strings.Builder
					for _, message := range genErr.Transcript {
						fmt.Fprintf(&dump, "===== %s =====\n%s\n\n", message.Role, message.Content)
					}
					if writeErr := os.WriteFile(dumpPath, []byte(dump.String()), 0o644); writeErr == nil {
						console.Warn("transcript: " + dumpPath)
					}
				}
				return err
			}
			for _, warning := range outcome.Warnings {
				console.Warn(warning)
			}
			specName := outcome.Spec.Name
			if name != "" {
				specName = name
			}
			console.Info(fmt.Sprintf("spec accepted after %d round(s): %s", outcome.Rounds, specName))

			specPath := filepath.Join(ctx.repoRoot, "assets", "scad", "specs", specName+".json")
			if err := os.MkdirAll(filepath.Dir(specPath), 0o755); err != nil {
				return err
			}
			if err := os.WriteFile(specPath, outcome.SpecJSON, 0o644); err != nil {
				return err
			}
			console.Info("spec: " + specPath)

			// Re-compose through the standard path so the .scad header carries the
			// real spec path + hash (identical to a manual `gnb scad compose`).
			return composeSpecFile(ctx, specPath, "")
		},
	}
	cmd.Flags().StringVar(&name, "name", "", "override the scene name chosen by the model")
	cmd.Flags().StringVar(&modelID, "model", "", "override the active LLM model")
	cmd.Flags().IntVar(&repairs, "repairs", 3, "max self-repair rounds after the first reply")
	cmd.Flags().Float64Var(&temperature, "temp", 0.4, "sampling temperature")
	cmd.Flags().BoolVar(&debug, "debug", false, "dump the chat transcript on failure")
	return cmd
}

func newScadComposeCommand(ctx appContext) *cobra.Command {
	var specPath string
	var outPath string
	cmd := &cobra.Command{
		Use:   "compose --spec <spec.json> [-o <out.scad>]",
		Short: "Expand a JSON scene spec into assets/scad/gen/<name>.scad",
		Long: "Validates the spec against assets/scad/lib/catalog.json (module names, kit\n" +
			"ownership, layout matrix shape, scaleClass mixing) and expands it into a plain\n" +
			"top-level .scad built on the kit_layout combinators. Deterministic: the same\n" +
			"spec always produces the same bytes. The result is mirrored into the build\n" +
			"assets so `gnb shot --scene <out>` works without a rebuild.",
		RunE: func(cmd *cobra.Command, args []string) error {
			if specPath == "" {
				return fmt.Errorf("--spec is required")
			}
			return composeSpecFile(ctx, specPath, outPath)
		},
	}
	cmd.Flags().StringVar(&specPath, "spec", "", "scene spec JSON path")
	cmd.Flags().StringVarP(&outPath, "out", "o", "", "output .scad path (default assets/scad/gen/<name>.scad)")
	return cmd
}

// composeSpecFile expands a spec file to .scad and mirrors it into the build
// assets. Shared by `gnb scad compose` and `gnb scad generate`.
func composeSpecFile(ctx appContext, specPath string, outPath string) error {
	spec, err := scadcompose.LoadSpec(specPath)
	if err != nil {
		return err
	}
	catalog, err := scadcompose.LoadCatalog(filepath.Join(ctx.repoRoot, "assets", "scad", "lib", "catalog.json"))
	if err != nil {
		return err
	}
	raw, err := os.ReadFile(specPath)
	if err != nil {
		return err
	}
	hash := fmt.Sprintf("%x", sha256.Sum256(raw))[:12]
	relSpec, relErr := filepath.Rel(ctx.repoRoot, specPath)
	if relErr != nil {
		relSpec = specPath
	}
	result, err := scadcompose.Compose(spec, catalog, filepath.ToSlash(relSpec), hash)
	if err != nil {
		return err
	}
	for _, warning := range result.Warnings {
		console.Warn(warning)
	}
	if outPath == "" {
		outPath = filepath.Join(ctx.repoRoot, "assets", "scad", "gen", spec.Name+".scad")
	}
	if err := os.MkdirAll(filepath.Dir(outPath), 0o755); err != nil {
		return err
	}
	if err := os.WriteFile(outPath, []byte(result.Source), 0o644); err != nil {
		return err
	}
	console.Info("composed: " + outPath)
	// Mirror into the build assets so gnb shot sees it without a rebuild.
	relOut, relErr := filepath.Rel(filepath.Join(ctx.repoRoot, "assets"), outPath)
	if relErr == nil && !filepath.IsAbs(relOut) && !isDotDot(relOut) {
		buildCopy := filepath.Join(filepath.Dir(platform.BinDir(ctx.repoRoot, ctx.preset)), "assets", relOut)
		if err := copyFileTo(outPath, buildCopy); err != nil {
			console.Warn("build-assets mirror failed: " + err.Error())
		}
	}
	console.Info(fmt.Sprintf("preview: gnb shot --scene assets/%s", filepath.ToSlash(relOut)))
	return nil
}

func isDotDot(rel string) bool {
	return rel == ".." || len(rel) >= 3 && rel[:3] == ".."+string(filepath.Separator)
}

func newScadCatalogCommand(ctx appContext) *cobra.Command {
	var fn int
	cmd := &cobra.Command{
		Use:   "catalog",
		Short: "Regenerate assets/scad/lib/catalog.json from the kit libraries",
		Long: "Runs the ScadCatalog tool (build it first: gnb build ScadCatalog) against the\n" +
			"source-tree kit libraries, then mirrors the catalog into the build assets so\n" +
			"already-built binaries (ScadLibrary etc.) pick it up without a rebuild.",
		RunE: func(cmd *cobra.Command, args []string) error {
			libDir := filepath.Join(ctx.repoRoot, "assets", "scad", "lib")
			outPath := filepath.Join(libDir, "catalog.json")
			runArgs := []string{"--lib", libDir, "--out", outPath}
			if fn > 0 {
				runArgs = append(runArgs, "--fn", fmt.Sprintf("%d", fn))
			}
			runArgs = append(runArgs, args...)
			if err := runner.Run(ctx.repoRoot, runner.Options{Target: "ScadCatalog", Preset: ctx.preset, Args: runArgs}); err != nil {
				return err
			}
			// Mirror into the build assets copy (assets are copied at build time;
			// a fresh catalog would otherwise be invisible until the next build).
			buildLib := filepath.Join(filepath.Dir(platform.BinDir(ctx.repoRoot, ctx.preset)), "assets", "scad", "lib")
			if err := copyFileTo(outPath, filepath.Join(buildLib, "catalog.json")); err != nil {
				console.Warn("catalog written but build-assets mirror failed: " + err.Error())
			} else {
				console.Info("catalog mirrored to " + filepath.Join(buildLib, "catalog.json"))
			}
			return nil
		},
	}
	cmd.Flags().IntVar(&fn, "fn", 0, "$fn used while evaluating modules (0 = tool default)")
	return cmd
}

func copyFileTo(src, dst string) error {
	if err := os.MkdirAll(filepath.Dir(dst), 0o755); err != nil {
		return err
	}
	in, err := os.Open(src)
	if err != nil {
		return err
	}
	defer in.Close()
	out, err := os.Create(dst)
	if err != nil {
		return err
	}
	defer out.Close()
	_, err = io.Copy(out, in)
	return err
}
