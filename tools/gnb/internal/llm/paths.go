package llm

import (
	"path/filepath"
	"runtime"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/config"
)

// Layout describes the on-disk locations used by the LLM subsystem.
//
//	external/llm/
//	  llama.cpp-<version>/   -- extracted llama.cpp release (binaries + libs)
//	  models/<file>.gguf     -- downloaded GGUF model
//	  run/server.pid         -- PID file written when llama-server is running
type Layout struct {
	Root      string // external/llm
	BinDir    string // external/llm/llama.cpp-<ver>
	ModelsDir string // external/llm/models
	RunDir    string // external/llm/run
	PIDFile   string // external/llm/run/server.pid
}

func ResolveLayout(repoRoot string, cfg config.LLMConfig) Layout {
	root := filepath.Join(repoRoot, "external", "llm")
	return Layout{
		Root:      root,
		BinDir:    filepath.Join(root, "llama.cpp-"+cfg.Llama.Version),
		ModelsDir: filepath.Join(root, "models"),
		RunDir:    filepath.Join(root, "run"),
		PIDFile:   filepath.Join(root, "run", "server.pid"),
	}
}

// ModelPath returns the on-disk path for a specific model entry. Use
// cfg.ActiveModel() when you want the currently-selected one.
func (l Layout) ModelPath(m config.ModelConfig) string {
	return filepath.Join(l.ModelsDir, m.File)
}

func (l Layout) ServerBinary() string {
	name := "llama-server"
	if runtime.GOOS == "windows" {
		name += ".exe"
	}
	return filepath.Join(l.BinDir, name)
}

func (l Layout) CLIBinary() string {
	name := "llama-cli"
	if runtime.GOOS == "windows" {
		name += ".exe"
	}
	return filepath.Join(l.BinDir, name)
}

// LlamaArchiveURL picks the prebuilt llama.cpp release URL for the current
// platform. Prefers the Vulkan build on Windows/Linux since this project
// already ships a Vulkan SDK; falls back to CPU when the Vulkan URL is empty.
func LlamaArchiveURL(c config.LlamaConfig) string {
	switch runtime.GOOS {
	case "windows":
		if c.WindowsVulkan != "" {
			return c.WindowsVulkan
		}
		return c.WindowsCPU
	case "linux":
		if c.LinuxVulkan != "" {
			return c.LinuxVulkan
		}
		return c.LinuxCPU
	case "darwin":
		return c.MacOSArm64
	}
	return ""
}
