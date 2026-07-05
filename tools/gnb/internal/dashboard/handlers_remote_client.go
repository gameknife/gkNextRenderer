package dashboard

import (
	"net/http"
	"os"
	"path/filepath"
)

func (s *Server) handleRemoteClient(w http.ResponseWriter, r *http.Request) {
	path := filepath.Join(s.opts.RepoRoot, "assets", "remote", "index.html")
	body, err := os.ReadFile(path)
	if err != nil {
		http.Error(w, "failed to load remote client: "+err.Error(), http.StatusInternalServerError)
		return
	}
	w.Header().Set("Content-Type", "text/html; charset=utf-8")
	w.Header().Set("Cache-Control", "no-store")
	w.WriteHeader(http.StatusOK)
	_, _ = w.Write(body)
}
