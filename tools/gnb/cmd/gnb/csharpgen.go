package main

import (
	"strconv"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/console"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/csharpgen"
	"github.com/spf13/cobra"
)

func newCSharpGenCommand(ctx appContext) *cobra.Command {
	var check bool
	cmd := &cobra.Command{
		Use:   "csharpgen",
		Short: "Generate the managed binding layer from EngineApi.def.h",
		Long: "Expands src/Modules/NextDotNet/EngineApi.def.h into\n" +
			"assets/csharp/GkNext.Engine/Engine.g.cs. The def file is the single source of truth for\n" +
			"the binding surface; the generated file must never be edited by hand.",
		RunE: func(cmd *cobra.Command, args []string) error {
			result, err := csharpgen.Run(ctx.repoRoot, check)
			if err != nil {
				return err
			}
			console.Label("bindings", csharpgen.DefPath)
			console.Label("entries", strconv.Itoa(result.EntryCount))
			if check {
				console.Success("%s is up to date", csharpgen.OutputPath)
				return nil
			}
			if result.Changed {
				console.Success("wrote %s", csharpgen.OutputPath)
			} else {
				console.Info("%s already up to date", csharpgen.OutputPath)
			}
			return nil
		},
	}
	cmd.Flags().BoolVar(&check, "check", false, "fail instead of writing when the generated file is stale")
	return cmd
}
