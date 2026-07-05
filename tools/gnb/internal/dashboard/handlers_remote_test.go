package dashboard

import (
	"html/template"
	"net/http/httptest"
	"os"
	"path/filepath"
	"reflect"
	"strings"
	"testing"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/config"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/platform"
	"github.com/gameknife/gknextrenderer/tools/gnb/internal/remoteplay"
)

func setupRemoteServer(t *testing.T, repoRoot string, cfg config.Config) *Server {
	t.Helper()
	tpl, err := template.New("dashboard").
		Funcs(templateFuncs()).
		ParseFS(templateFS, "templates/*.html")
	if err != nil {
		t.Fatal(err)
	}
	return &Server{
		opts:  Options{RepoRoot: repoRoot, Preset: "windows", Config: cfg},
		tpl:   tpl,
		jobs:  NewJobManager(),
		chats: NewChatStore(),
	}
}

func TestHandleTabRemoteRendersRemoteLauncher(t *testing.T) {
	repoRoot := t.TempDir()
	remoteAsset := filepath.Join(repoRoot, "assets", "remote")
	if err := os.MkdirAll(remoteAsset, 0o755); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(remoteAsset, "index.html"), []byte("<!doctype html><title>stub remote</title>"), 0o644); err != nil {
		t.Fatal(err)
	}
	s := setupRemoteServer(t, repoRoot, config.Config{
		Targets: config.TargetsConfig{
			All: []string{"gkNextRenderer", "Packager", "gkNextEditor", "gkNextUnitTests"},
		},
	})

	req := httptest.NewRequest("GET", "/tab/remote", nil)
	req.SetPathValue("kind", "remote")
	rec := httptest.NewRecorder()

	s.handleTab(rec, req)

	if rec.Code != 200 {
		t.Fatalf("status = %d (%s), want 200", rec.Code, rec.Body.String())
	}
	body := rec.Body.String()
	if !strings.Contains(body, "启动 Remote") {
		t.Fatalf("rendered body missing remote action:\n%s", body)
	}
	if !strings.Contains(body, "内嵌串流") || !strings.Contains(body, "data-remote-frame") {
		t.Fatalf("rendered body missing embedded remote surface:\n%s", body)
	}
	if !strings.Contains(body, `name="bind" value="0.0.0.0"`) {
		t.Fatalf("rendered body missing bind default:\n%s", body)
	}
	if !strings.Contains(body, "http://127.0.0.1:8088") {
		t.Fatalf("rendered body missing loopback preview:\n%s", body)
	}
	if !strings.Contains(body, "gkNextRenderer") || !strings.Contains(body, "gkNextEditor") {
		t.Fatalf("rendered body missing runnable targets:\n%s", body)
	}
	if strings.Contains(body, "Packager") || strings.Contains(body, "gkNextUnitTests") {
		t.Fatalf("rendered body should exclude non-remote targets:\n%s", body)
	}
}

func TestHandleRemoteClientServesRepoAsset(t *testing.T) {
	repoRoot := t.TempDir()
	remoteAsset := filepath.Join(repoRoot, "assets", "remote")
	if err := os.MkdirAll(remoteAsset, 0o755); err != nil {
		t.Fatal(err)
	}
	const html = "<!doctype html><title>gkNext Remote Play</title><body>remote</body>"
	if err := os.WriteFile(filepath.Join(remoteAsset, "index.html"), []byte(html), 0o644); err != nil {
		t.Fatal(err)
	}
	s := setupRemoteServer(t, repoRoot, config.Config{})

	req := httptest.NewRequest("GET", "/remote/client", nil)
	rec := httptest.NewRecorder()

	s.handleRemoteClient(rec, req)

	if rec.Code != 200 {
		t.Fatalf("status = %d (%s), want 200", rec.Code, rec.Body.String())
	}
	if got := rec.Header().Get("Content-Type"); !strings.Contains(got, "text/html") {
		t.Fatalf("Content-Type = %q, want text/html", got)
	}
	if rec.Body.String() != html {
		t.Fatalf("body = %q, want %q", rec.Body.String(), html)
	}
}

func TestRemoteJobSpecBuildsDefaultRemoteArgs(t *testing.T) {
	repoRoot := t.TempDir()
	s := setupRemoteServer(t, repoRoot, config.Config{})

	spec, err := s.remoteJobSpec("gkNextRenderer", remoteplay.Options{
		Scene: "assets/models/playground.glb",
	}, []string{"--validation"})
	if err != nil {
		t.Fatalf("remoteJobSpec returned error: %v", err)
	}

	wantExe := platform.ExecutablePath(platform.BinDir(repoRoot, "windows"), "gkNextRenderer")
	wantArgs := remoteplay.RunArgs(remoteplay.Options{
		Bind:          "0.0.0.0",
		Encoder:       "auto",
		HttpPort:      8088,
		SignalingPort: 8089,
		Fps:           30,
		Scene:         "assets/models/playground.glb",
	}, []string{"--validation"})

	if spec.Kind != JobRemote || spec.Target != "gkNextRenderer" {
		t.Fatalf("spec identity = %+v, want remote gkNextRenderer", spec)
	}
	if spec.Name != wantExe || spec.WorkDir != platform.BinDir(repoRoot, "windows") {
		t.Fatalf("spec paths = (%q, %q), want (%q, %q)", spec.Name, spec.WorkDir, wantExe, platform.BinDir(repoRoot, "windows"))
	}
	if !reflect.DeepEqual(spec.Args, wantArgs) {
		t.Fatalf("spec args = %#v, want %#v", spec.Args, wantArgs)
	}
	if spec.AfterStart == nil {
		t.Fatal("spec.AfterStart = nil, want activation hook")
	}
}
