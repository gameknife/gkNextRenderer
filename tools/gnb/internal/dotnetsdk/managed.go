package dotnetsdk

import (
	"fmt"
	"os"
	"path/filepath"
	"runtime"
)

// Managed assembly names, matching assets/csharp/.
const (
	BootstrapAssembly = "GkNext.Bootstrap"
	GameAssembly      = "GkNext.Game"
)

// SourceDir is the managed source tree, the C# counterpart of the deleted assets/typescript.
func SourceDir(repoRoot string) string {
	return filepath.Join(repoRoot, "assets", "csharp")
}

// ProjectPath returns the .csproj for a managed assembly.
func ProjectPath(repoRoot string, assembly string) string {
	return filepath.Join(SourceDir(repoRoot), assembly, assembly+".csproj")
}

// PublishOptions controls a managed publish.
type PublishOptions struct {
	// OutputDir receives the published output.
	OutputDir string
	// Configuration is Debug or Release.
	Configuration string
	// Aot publishes through ILC instead of producing IL for CoreCLR.
	Aot bool
	// NativeLib is Shared or Static; AOT only. Static is what iOS requires.
	NativeLib string
	// GameVariant is a Phase 0 lever that changes the game assembly's observable behaviour so a
	// hot reload can be proven to have swapped code. Normal builds leave it empty.
	GameVariant string
}

// PublishBootstrap publishes GkNext.Bootstrap (and, under AOT, the game assembly linked into it).
func PublishBootstrap(repoRoot string, toolchain Toolchain, options PublishOptions) error {
	args := []string{
		"publish", ProjectPath(repoRoot, BootstrapAssembly),
		"-c", configurationOrDefault(options.Configuration),
		"-o", options.OutputDir,
		"--nologo",
	}

	if options.Aot {
		rid, err := HostRID()
		if err != nil {
			return err
		}
		args = append(args, "-r", rid, "-p:GkAot=true")
		if options.NativeLib != "" {
			args = append(args, "-p:GkNativeLib="+options.NativeLib)
		}
		if options.GameVariant != "" {
			args = append(args, "-p:GkGameVariant="+options.GameVariant)
		}
	}

	return toolchain.Run(SourceDir(repoRoot), args...)
}

// PublishGame publishes the reloadable game assembly on its own. CoreCLR only: under AOT the game
// is linked into the bootstrap library instead.
func PublishGame(repoRoot string, toolchain Toolchain, options PublishOptions) error {
	args := []string{
		"publish", ProjectPath(repoRoot, GameAssembly),
		"-c", configurationOrDefault(options.Configuration),
		"-o", options.OutputDir,
		"--nologo",
	}
	if options.GameVariant != "" {
		args = append(args, "-p:GkGameVariant="+options.GameVariant)
	}
	return toolchain.Run(SourceDir(repoRoot), args...)
}

// NativeBootstrapLibrary returns the library a native target links against to get GkNext_Bootstrap
// from an AOT publish: the import library on Windows, the shared object elsewhere.
func NativeBootstrapLibrary(publishDir string) (string, error) {
	var candidates []string
	switch runtime.GOOS {
	case "windows":
		candidates = []string{BootstrapAssembly + ".lib"}
	case "darwin":
		candidates = []string{"lib" + BootstrapAssembly + ".dylib", "lib" + BootstrapAssembly + ".a"}
	default:
		candidates = []string{"lib" + BootstrapAssembly + ".so", "lib" + BootstrapAssembly + ".a"}
	}

	for _, name := range candidates {
		path := filepath.Join(publishDir, name)
		if fileExists(path) {
			return path, nil
		}
	}
	return "", fmt.Errorf("no native bootstrap library in %s (looked for %v)", publishDir, candidates)
}

// NativeRuntimeFiles lists files that must sit next to an executable linking the AOT bootstrap.
// Empty on platforms where the linked artifact is self-contained.
func NativeRuntimeFiles(publishDir string) []string {
	if runtime.GOOS != "windows" {
		return nil
	}
	dll := filepath.Join(publishDir, BootstrapAssembly+".dll")
	if fileExists(dll) {
		return []string{dll}
	}
	return nil
}

// CopyFileInto copies a file into a directory, preserving its base name.
func CopyFileInto(source string, destinationDir string) error {
	data, err := os.ReadFile(source)
	if err != nil {
		return err
	}
	if err := os.MkdirAll(destinationDir, 0o755); err != nil {
		return err
	}
	return os.WriteFile(filepath.Join(destinationDir, filepath.Base(source)), data, 0o755)
}

func configurationOrDefault(configuration string) string {
	if configuration == "" {
		return "Release"
	}
	return configuration
}
