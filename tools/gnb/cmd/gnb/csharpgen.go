package main

import (
	"strconv"
	"strings"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/console"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/csharpgen"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/platform"
	"github.com/spf13/cobra"
)

func newCSharpGenCommand(ctx appContext) *cobra.Command {
	var check bool
	var refresh bool
	cmd := &cobra.Command{
		Use:   "csharpgen",
		Short: "Generate the managed binding layer from EngineApi.def.h and the reflection manifest",
		Long: "Expands src/Modules/NextDotNet/EngineApi.def.h into\n" +
			"assets/csharp/GkNext.Engine/Engine.g.cs, and src/Modules/NextDotNet/ReflectionManifest.json\n" +
			"into assets/csharp/GkNext.Engine/Components.g.cs. Both sources are authoritative and the\n" +
			"generated files must never be edited by hand.\n\n" +
			"The manifest is a committed snapshot of what the engine registered with entt::meta, so\n" +
			"generation and --check need no built binary. Use --refresh after changing reflection: it\n" +
			"runs gkNextRenderer --dump-reflection and then regenerates.",
		RunE: func(cmd *cobra.Command, args []string) error {
			if refresh {
				executable := platform.ExecutablePath(platform.BinDir(ctx.repoRoot, ctx.preset), "gkNextRenderer")
				if err := csharpgen.Refresh(ctx.repoRoot, executable); err != nil {
					return err
				}
				console.Success("refreshed %s", csharpgen.ManifestPath)
			}

			result, err := csharpgen.Run(ctx.repoRoot, check)
			if err != nil {
				return err
			}
			console.Label("bindings", csharpgen.DefPath)
			console.Label("entries", strconv.Itoa(result.EntryCount))
			console.Label("reflection", csharpgen.ManifestPath)
			console.Label("properties", strconv.Itoa(result.PropertyCount))
			if check {
				console.Success("%s and %s are up to date", csharpgen.OutputPath, csharpgen.ComponentsOutputPath)
				return nil
			}
			if result.Changed {
				console.Success("wrote %s", strings.Join(result.Outputs, ", "))
			} else {
				console.Info("%s and %s already up to date", csharpgen.OutputPath, csharpgen.ComponentsOutputPath)
			}
			return nil
		},
	}
	cmd.Flags().BoolVar(&check, "check", false, "fail instead of writing when a generated file is stale")
	cmd.Flags().BoolVar(&refresh, "refresh", false, "re-dump the reflection manifest from the built gkNextRenderer first")
	return cmd
}
