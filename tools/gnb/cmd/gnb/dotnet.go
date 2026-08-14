package main

import (
	"fmt"
	"os"
	"path/filepath"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/cmakerun"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/console"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/csharpgen"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/dotnetsdk"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/platform"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/vcpkg"
	"github.com/spf13/cobra"
)

func newDotNetCommand(ctx appContext) *cobra.Command {
	root := &cobra.Command{
		Use:   "dotnet",
		Short: ".NET toolchain and managed scripting layer",
	}
	root.AddCommand(newDotNetSetupCommand(ctx))
	root.AddCommand(newDotNetStatusCommand(ctx))
	root.AddCommand(newDotNetBuildCommand(ctx))
	root.AddCommand(newDotNetProbeCommand(ctx))
	root.AddCommand(newDotNetCICommand(ctx))
	return root
}

func newDotNetSetupCommand(ctx appContext) *cobra.Command {
	var force bool
	cmd := &cobra.Command{
		Use:   "setup",
		Short: "Ensure a usable .NET SDK, downloading the pinned one if needed",
		Long: "Accepts an installed SDK at or above the pinned version so a 300 MB download is not\n" +
			"forced on developers who already have one. Use --force to install the pinned SDK into\n" +
			"external/dotnet regardless.",
		RunE: func(cmd *cobra.Command, args []string) error {
			toolchain, err := dotnetsdk.Ensure(ctx.repoRoot, ctx.cfg.External.DotNet, force)
			if err != nil {
				return err
			}
			printToolchain(toolchain)
			console.Success(".NET toolchain ready")
			return nil
		},
	}
	cmd.Flags().BoolVar(&force, "force", false, "install the pinned SDK into external/dotnet even if a system SDK would do")
	return cmd
}

func newDotNetStatusCommand(ctx appContext) *cobra.Command {
	return &cobra.Command{
		Use:   "status",
		Short: "Show which .NET toolchain would be used",
		RunE: func(cmd *cobra.Command, args []string) error {
			toolchain, err := dotnetsdk.Resolve(ctx.repoRoot, ctx.cfg.External.DotNet)
			if err != nil {
				return err
			}
			printToolchain(toolchain)
			if hostPack, err := dotnetsdk.HostPackNativeDir(toolchain); err == nil {
				console.Label("host pack", hostPack)
			} else {
				console.Warn("%s", err)
			}
			return nil
		},
	}
}

func newDotNetBuildCommand(ctx appContext) *cobra.Command {
	var outputDir string
	var configuration string
	var aot bool
	var nativeLib string
	cmd := &cobra.Command{
		Use:   "build",
		Short: "Publish the managed assemblies (assets/csharp)",
		RunE: func(cmd *cobra.Command, args []string) error {
			toolchain, err := dotnetsdk.Resolve(ctx.repoRoot, ctx.cfg.External.DotNet)
			if err != nil {
				return err
			}

			if outputDir == "" {
				outputDir = filepath.Join(ctx.repoRoot, "out", "build", "dotnet", backendDirName(aot))
			}

			options := dotnetsdk.PublishOptions{
				OutputDir:     outputDir,
				Configuration: configuration,
				Aot:           aot,
				NativeLib:     nativeLib,
			}
			if err := dotnetsdk.PublishBootstrap(ctx.repoRoot, toolchain, options); err != nil {
				return err
			}
			if !aot {
				// Under CoreCLR the game assembly is loaded from its own directory so it can be
				// swapped without touching the bootstrap output.
				gameOptions := options
				gameOptions.OutputDir = filepath.Join(outputDir, "game")
				if err := dotnetsdk.PublishGame(ctx.repoRoot, toolchain, gameOptions); err != nil {
					return err
				}
			}

			console.Success("managed assemblies published to %s", outputDir)
			return nil
		},
	}
	cmd.Flags().StringVar(&outputDir, "out", "", "output directory (default out/build/dotnet/<backend>)")
	cmd.Flags().StringVar(&configuration, "configuration", "Release", "Debug or Release")
	cmd.Flags().BoolVar(&aot, "aot", false, "publish through NativeAOT instead of producing IL for CoreCLR")
	cmd.Flags().StringVar(&nativeLib, "native-lib", "Shared", "AOT output shape: Shared or Static (iOS needs Static)")
	return cmd
}

func newDotNetProbe(ctx appContext, options dotnetsdk.ProbeOptions) error {
	toolchain, err := dotnetsdk.Resolve(ctx.repoRoot, ctx.cfg.External.DotNet)
	if err != nil {
		return err
	}
	printToolchain(toolchain)
	return dotnetsdk.RunProbe(ctx.repoRoot, toolchain, options)
}

func newDotNetProbeCommand(ctx appContext) *cobra.Command {
	var skipAot bool
	var configuration string
	cmd := &cobra.Command{
		Use:   "probe",
		Short: "Run the two-backend acceptance probe",
		Long: "Builds a standalone native host and runs the same C# under CoreCLR and NativeAOT,\n" +
			"checking that the two backends produce identical output, that a collectible load\n" +
			"context swaps game code, and that it is collected afterwards.",
		RunE: func(cmd *cobra.Command, args []string) error {
			if err := newDotNetProbe(ctx, dotnetsdk.ProbeOptions{
				Configuration: configuration,
				SkipAot:       skipAot,
			}); err != nil {
				return err
			}
			console.Success("dotnet probe passed")
			return nil
		},
	}
	cmd.Flags().BoolVar(&skipAot, "skip-aot", false, "run only the CoreCLR half (fast inner loop; not acceptance)")
	cmd.Flags().StringVar(&configuration, "configuration", "Release", "Debug or Release")
	return cmd
}

func printToolchain(toolchain dotnetsdk.Toolchain) {
	console.Label("dotnet", toolchain.Exe)
	console.Label("sdk", fmt.Sprintf("%s (%s)", toolchain.SDKVersion, toolchain.Source))
}

func backendDirName(aot bool) string {
	if aot {
		return "aot"
	}
	return "coreclr"
}

// newDotNetCICommand is the enforcement point for the rule in design section 3.4: AOT
// compatibility rots silently unless something builds it on every change. Running only the
// CoreCLR half is what "we will fix it before release" looks like in practice.
func newDotNetCICommand(ctx appContext) *cobra.Command {
	var targets []string
	cmd := &cobra.Command{
		Use:   "ci",
		Short: "Verify the managed layer under both backends",
		Long: "Checks that Engine.g.cs matches EngineApi.def.h, runs the standalone two-backend " +
			"probe, then builds the engine itself under CoreCLR and NativeAOT. The build is left " +
			"configured for CoreCLR, which is the default developers work against.",
		RunE: func(cmd *cobra.Command, args []string) error {
			if _, err := csharpgen.Run(ctx.repoRoot, true); err != nil {
				return err
			}
			console.Success("Engine.g.cs matches %s", csharpgen.DefPath)

			if err := newDotNetProbe(ctx, dotnetsdk.ProbeOptions{Configuration: "Release"}); err != nil {
				return err
			}
			console.Success("two-backend probe passed")

			for _, backend := range []string{"CoreCLR", "AOT"} {
				console.Info("building the engine with GK_DOTNET_BACKEND=%s", backend)
				if err := buildWithDotNetBackend(ctx, backend, targets); err != nil {
					return fmt.Errorf("%s engine build failed: %w", backend, err)
				}
				console.Success("%s engine build ok", backend)
			}

			// Leave the tree the way a developer expects to find it.
			console.Info("restoring the CoreCLR configuration")
			if err := buildWithDotNetBackend(ctx, "CoreCLR", targets); err != nil {
				return err
			}
			console.Success("dotnet ci passed")
			return nil
		},
	}
	cmd.Flags().StringSliceVar(&targets, "target", []string{"DotNetSandbox", "FlappyCSharp", "gkNextUnitTests"},
		"targets to build under each backend")
	return cmd
}

func buildWithDotNetBackend(ctx appContext, backend string, targets []string) error {
	if err := platform.EnsureMSVCEnvironment(); err != nil {
		return err
	}

	// Switching backend changes what an executable links against, and MSVC refuses to reuse the
	// incremental PDB across that change (LNK1207). Nothing else in the build knows the backend
	// moved, so drop the debug databases of the targets we are about to relink.
	binDir := platform.BinDir(ctx.repoRoot, ctx.preset)
	for _, target := range targets {
		pdb := filepath.Join(binDir, target+".pdb")
		if err := os.Remove(pdb); err != nil && !os.IsNotExist(err) {
			return err
		}
	}
	cmakePath, err := vcpkg.EnsureBundledCMake(ctx.repoRoot, ctx.cfg)
	if err != nil {
		return err
	}
	return cmakerun.BuildWithCMake(ctx.repoRoot, cmakePath, ctx.preset, cmakerun.BuildOptions{
		Targets:       targets,
		ConfigureArgs: []string{"-DGK_DOTNET_BACKEND=" + backend},
		Reconfigure:   true,
	})
}
