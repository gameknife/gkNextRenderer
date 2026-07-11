package main

import (
	"fmt"
	"io"
	"os"
	"path/filepath"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/console"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/platform"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/runner"
	"github.com/spf13/cobra"
)

func newScadCommand(ctx appContext) *cobra.Command {
	cmd := &cobra.Command{
		Use:   "scad",
		Short: "SCAD kit utilities (catalog generation, see docs/designs/scad-scene-compose-design.md)",
	}
	cmd.AddCommand(newScadCatalogCommand(ctx))
	return cmd
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
