package llm

import (
	"fmt"
	"os"
	"path/filepath"
	"runtime"
	"strings"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/config"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/console"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/fetcher"
)

// EnsureBinaries downloads and extracts the configured llama.cpp prebuilt
// release if the server binary is not already present.
func EnsureBinaries(repoRoot string, cfg config.LLMConfig) error {
	layout := ResolveLayout(repoRoot, cfg)
	if _, err := os.Stat(layout.ServerBinary()); err == nil {
		return nil
	}
	url := LlamaArchiveURL(cfg.Llama)
	if url == "" {
		return fmt.Errorf("no llama.cpp binary URL configured for %s/%s", runtime.GOOS, runtime.GOARCH)
	}
	if err := os.MkdirAll(layout.BinDir, 0o755); err != nil {
		return err
	}
	tmp := filepath.Join(repoRoot, "external", ".download-"+filepath.Base(url))
	if err := fetcher.Download(url, tmp); err != nil {
		return err
	}
	defer os.Remove(tmp)
	if !strings.HasSuffix(strings.ToLower(url), ".zip") {
		return fmt.Errorf("unsupported llama.cpp archive (expected .zip): %s", url)
	}
	if err := fetcher.Unzip(tmp, layout.BinDir); err != nil {
		return err
	}
	if err := flattenLlamaLayout(layout.BinDir); err != nil {
		return err
	}
	if _, err := os.Stat(layout.ServerBinary()); err != nil {
		return fmt.Errorf("llama-server not found after extracting %s: %w", url, err)
	}
	if runtime.GOOS != "windows" {
		_ = os.Chmod(layout.ServerBinary(), 0o755)
		_ = os.Chmod(layout.CLIBinary(), 0o755)
	}
	console.Success("installed llama.cpp %s", filepath.Base(layout.BinDir))
	return nil
}

// EnsureModel downloads the active GGUF model if missing.
func EnsureModel(repoRoot string, cfg config.LLMConfig) error {
	return EnsureModelEntry(repoRoot, cfg, cfg.ActiveModel())
}

// EnsureModelEntry downloads a specific model entry if its GGUF is missing.
// Each model lives at its own path under models/<file>, so multiple variants
// can coexist on disk.
func EnsureModelEntry(repoRoot string, cfg config.LLMConfig, m config.ModelConfig) error {
	if m.ID == "" {
		return fmt.Errorf("no model configured (set [external.llm].active or add [[external.llm.models]])")
	}
	layout := ResolveLayout(repoRoot, cfg)
	if err := os.MkdirAll(layout.ModelsDir, 0o755); err != nil {
		return err
	}
	dst := layout.ModelPath(m)
	if _, err := os.Stat(dst); err == nil {
		return nil
	}
	if m.URL == "" {
		return fmt.Errorf("no model URL configured for %s", m.ID)
	}
	console.Info("downloading model %s (this may take a few minutes)", m.File)
	return fetcher.Download(m.URL, dst)
}

// flattenLlamaLayout moves binaries up one level when the zip extracts into a
// nested folder (some llama.cpp release archives put everything under a
// `build/bin/` or `llama-bXXXX-bin-*/` directory).
func flattenLlamaLayout(binDir string) error {
	if hasServer(binDir) {
		return nil
	}
	candidates := []string{
		filepath.Join(binDir, "build", "bin"),
	}
	entries, _ := os.ReadDir(binDir)
	for _, e := range entries {
		if e.IsDir() {
			candidates = append(candidates, filepath.Join(binDir, e.Name()))
			candidates = append(candidates, filepath.Join(binDir, e.Name(), "build", "bin"))
		}
	}
	for _, c := range candidates {
		if hasServer(c) {
			return moveAll(c, binDir)
		}
	}
	return nil
}

func hasServer(dir string) bool {
	name := "llama-server"
	if runtime.GOOS == "windows" {
		name += ".exe"
	}
	_, err := os.Stat(filepath.Join(dir, name))
	return err == nil
}

func moveAll(src, dst string) error {
	entries, err := os.ReadDir(src)
	if err != nil {
		return err
	}
	for _, e := range entries {
		from := filepath.Join(src, e.Name())
		to := filepath.Join(dst, e.Name())
		if err := os.Rename(from, to); err != nil {
			// Cross-volume or already-exists: skip silently rather than aborting.
			continue
		}
	}
	return nil
}
