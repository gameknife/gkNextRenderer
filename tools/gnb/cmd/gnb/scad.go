package main

import (
	"crypto/sha256"
	"fmt"
	"io"
	"os"
	"path/filepath"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/console"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/platform"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/runner"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/scadcompose"
	"github.com/spf13/cobra"
)

func newScadCommand(ctx appContext) *cobra.Command {
	cmd := &cobra.Command{
		Use:   "scad",
		Short: "SCAD kit utilities (catalog + scene compose, see docs/designs/scad-scene-compose-design.md)",
	}
	cmd.AddCommand(newScadCatalogCommand(ctx))
	cmd.AddCommand(newScadComposeCommand(ctx))
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
		},
	}
	cmd.Flags().StringVar(&specPath, "spec", "", "scene spec JSON path")
	cmd.Flags().StringVarP(&outPath, "out", "o", "", "output .scad path (default assets/scad/gen/<name>.scad)")
	return cmd
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
