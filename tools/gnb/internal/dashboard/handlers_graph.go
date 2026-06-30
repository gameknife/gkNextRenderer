package dashboard

import (
	"encoding/json"
	"net/http"
	"os"
	"path/filepath"
	"time"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/targetgraph"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/vcpkg"
)

type graphVM struct {
	Preset string
}

type graphDataResponse struct {
	targetgraph.Data
	GeneratedAt string `json:"generated_at"`
	DotPath     string `json:"dot_path"`
	Error       string `json:"error,omitempty"`
}

func (s *Server) buildGraphVM() graphVM {
	return graphVM{Preset: s.opts.Preset}
}

func (s *Server) handleGraphData(w http.ResponseWriter, r *http.Request) {
	w.Header().Set("Content-Type", "application/json")
	buildDir := filepath.Join(s.opts.RepoRoot, "out", "build", s.opts.Preset)
	dotPath := filepath.Join(buildDir, "graphs", "dashboard-targets.dot")
	refresh := r.URL.Query().Get("refresh") == "1"

	if _, err := os.Stat(filepath.Join(buildDir, "CMakeCache.txt")); err != nil {
		_ = json.NewEncoder(w).Encode(graphDataResponse{
			DotPath: dotPath,
			Error:   "build directory is not configured; run `gnb build --reconfigure` first",
		})
		return
	}

	if refresh || !fileExists(dotPath) {
		cmakePath, err := vcpkg.EnsureBundledCMake(s.opts.RepoRoot, s.opts.Config)
		if err != nil {
			_ = json.NewEncoder(w).Encode(graphDataResponse{DotPath: dotPath, Error: err.Error()})
			return
		}
		if err := os.MkdirAll(filepath.Dir(dotPath), 0o755); err != nil {
			_ = json.NewEncoder(w).Encode(graphDataResponse{DotPath: dotPath, Error: err.Error()})
			return
		}
		if err := targetgraph.GenerateCMakeGraph(s.opts.RepoRoot, cmakePath, buildDir, dotPath, false); err != nil {
			_ = json.NewEncoder(w).Encode(graphDataResponse{DotPath: dotPath, Error: err.Error()})
			return
		}
	}

	data, err := targetgraph.LoadData(dotPath)
	if err != nil {
		_ = json.NewEncoder(w).Encode(graphDataResponse{DotPath: dotPath, Error: err.Error()})
		return
	}

	generatedAt := ""
	if info, err := os.Stat(dotPath); err == nil {
		generatedAt = info.ModTime().Format(time.RFC3339)
	}
	_ = json.NewEncoder(w).Encode(graphDataResponse{
		Data:        data,
		GeneratedAt: generatedAt,
		DotPath:     dotPath,
	})
}

func fileExists(path string) bool {
	_, err := os.Stat(path)
	return err == nil
}
