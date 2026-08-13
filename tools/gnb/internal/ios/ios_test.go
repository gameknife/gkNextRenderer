package ios

import (
	"encoding/json"
	"errors"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func TestDeviceBuildConstants(t *testing.T) {
	if preset != "ios-device" {
		t.Fatalf("preset = %q, want ios-device", preset)
	}
	if target != "gkNextRenderer" {
		t.Fatalf("target = %q, want gkNextRenderer", target)
	}
}

func TestParseProvisioningProfile(t *testing.T) {
	team, err := parseProvisioningProfile([]byte(`<?xml version="1.0" encoding="UTF-8"?>
<plist version="1.0"><dict>
    <key>TeamName</key><string>Example Studio</string>
    <key>TeamIdentifier</key><array><string>ABCDE12345</string></array>
</dict></plist>`))
	if err != nil {
		t.Fatalf("parseProvisioningProfile() error = %v", err)
	}
	if want := (Team{Name: "Example Studio", ID: "ABCDE12345"}); team != want {
		t.Fatalf("parseProvisioningProfile() = %#v, want %#v", team, want)
	}
}

func TestDiscoverTeamsDeduplicatesAndSorts(t *testing.T) {
	dir := t.TempDir()
	for _, name := range []string{"one.mobileprovision", "two.mobileprovision", "invalid.mobileprovision"} {
		if err := os.WriteFile(filepath.Join(dir, name), nil, 0o600); err != nil {
			t.Fatal(err)
		}
	}

	teams, warnings := discoverTeams([]string{dir}, func(path string) (Team, error) {
		switch filepath.Base(path) {
		case "one.mobileprovision":
			return Team{Name: "Zeta", ID: "ZZZZZ99999"}, nil
		case "two.mobileprovision":
			return Team{Name: "Alpha", ID: "AAAAA11111"}, nil
		default:
			return Team{}, errors.New("invalid profile")
		}
	})
	if len(warnings) != 1 {
		t.Fatalf("warnings = %d, want 1", len(warnings))
	}
	if want := []Team{{Name: "Alpha", ID: "AAAAA11111"}, {Name: "Zeta", ID: "ZZZZZ99999"}}; !equalTeams(teams, want) {
		t.Fatalf("teams = %#v, want %#v", teams, want)
	}
}

func TestReadArtifact(t *testing.T) {
	repoRoot := t.TempDir()
	bundlePath := filepath.Join(repoRoot, "signed.app")
	if err := os.Mkdir(bundlePath, 0o755); err != nil {
		t.Fatal(err)
	}
	manifestPath := filepath.Join(repoRoot, "out", "build", preset, "ios-app-"+artifactConfiguration+".json")
	if err := os.MkdirAll(filepath.Dir(manifestPath), 0o755); err != nil {
		t.Fatal(err)
	}
	data, err := json.Marshal(Artifact{BundlePath: bundlePath, BundleID: "com.example.app"})
	if err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(manifestPath, data, 0o600); err != nil {
		t.Fatal(err)
	}

	artifact, err := ReadArtifact(repoRoot)
	if err != nil {
		t.Fatalf("ReadArtifact() error = %v", err)
	}
	if want := (Artifact{BundlePath: bundlePath, BundleID: "com.example.app"}); artifact != want {
		t.Fatalf("ReadArtifact() = %#v, want %#v", artifact, want)
	}
}

func TestRunOnMacVerifiesBeforeLaunching(t *testing.T) {
	repoRoot := writeArtifact(t)
	launched := false
	_, err := runOnMac(repoRoot, func(string) error { return errors.New("unsigned") }, mkdirCopy, changedHash, func(string) error {
		launched = true
		return nil
	})
	if err == nil || !strings.Contains(err.Error(), "signature is not valid") {
		t.Fatalf("runOnMac() error = %v, want signature error", err)
	}
	if launched {
		t.Fatal("runOnMac() launched an app with an invalid signature")
	}
}

// Launching the bare bundle is what macOS kills with SIGKILL (code 9); only the
// staged wrapper may reach Launch Services.
func TestRunOnMacLaunchesStagedWrapperNotBareBundle(t *testing.T) {
	repoRoot := writeArtifact(t)
	var copiedTo, launchedPath string
	artifact, err := runOnMac(repoRoot,
		func(string) error { return nil },
		func(sourcePath, destinationPath string) error {
			copiedTo = destinationPath
			return mkdirCopy(sourcePath, destinationPath)
		},
		changedHash,
		func(path string) error {
			launchedPath = path
			return nil
		})
	if err != nil {
		t.Fatalf("runOnMac() error = %v", err)
	}
	if launchedPath == artifact.BundlePath {
		t.Fatal("runOnMac() launched the bare bundle, which macOS kills for code signing")
	}
	wantWrapper := filepath.Join(filepath.Dir(artifact.BundlePath), wrapperDirName, "gkNextRenderer.app")
	if launchedPath != wantWrapper {
		t.Fatalf("launch path = %q, want %q", launchedPath, wantWrapper)
	}
	if artifact.WrapperPath != wantWrapper {
		t.Fatalf("artifact.WrapperPath = %q, want %q", artifact.WrapperPath, wantWrapper)
	}
	if want := filepath.Join(wantWrapper, "Wrapper", "gkNextRenderer.app"); copiedTo != want {
		t.Fatalf("copy destination = %q, want %q", copiedTo, want)
	}
}

func TestStageWrapperCreatesLaunchableLayout(t *testing.T) {
	dir := t.TempDir()
	bundlePath := filepath.Join(dir, "gkNextRenderer.app")
	if err := os.Mkdir(bundlePath, 0o755); err != nil {
		t.Fatal(err)
	}

	staged, err := stageWrapper(bundlePath, mkdirCopy, changedHash)
	if err != nil {
		t.Fatalf("stageWrapper() error = %v", err)
	}
	if !staged.Restaged {
		t.Fatal("stageWrapper() built a new wrapper but did not report it as restaged")
	}
	inner := filepath.Join(staged.WrapperPath, "Wrapper", "gkNextRenderer.app")
	if info, err := os.Stat(inner); err != nil || !info.IsDir() {
		t.Fatalf("wrapped bundle missing at %s: %v", inner, err)
	}
	link, err := os.Readlink(filepath.Join(staged.WrapperPath, "WrappedBundle"))
	if err != nil {
		t.Fatalf("read WrappedBundle symlink: %v", err)
	}
	if want := filepath.Join("Wrapper", "gkNextRenderer.app"); link != want {
		t.Fatalf("WrappedBundle -> %q, want %q", link, want)
	}
}

// A changed build replaces the old inner bundle, but preserves the outer
// wrapper that Launch Services identifies.
func TestStageWrapperReplacesInnerBundleAndPreservesOuterWrapper(t *testing.T) {
	dir := t.TempDir()
	bundlePath := filepath.Join(dir, "gkNextRenderer.app")
	if err := os.Mkdir(bundlePath, 0o755); err != nil {
		t.Fatal(err)
	}

	first, err := stageWrapper(bundlePath, mkdirCopy, changedHash)
	if err != nil {
		t.Fatalf("first stageWrapper() error = %v", err)
	}
	stalePath := filepath.Join(first.WrapperPath, "Wrapper", "gkNextRenderer.app", "stale.txt")
	if err := os.WriteFile(stalePath, []byte("stale"), 0o600); err != nil {
		t.Fatal(err)
	}
	outerSentinel := filepath.Join(first.WrapperPath, "outer-sentinel")
	if err := os.WriteFile(outerSentinel, []byte("preserve"), 0o600); err != nil {
		t.Fatal(err)
	}

	second, err := stageWrapper(bundlePath, mkdirCopy, changedHash)
	if err != nil {
		t.Fatalf("second stageWrapper() error = %v", err)
	}
	if second.WrapperPath != first.WrapperPath {
		t.Fatalf("wrapper path changed between runs: %q then %q", first.WrapperPath, second.WrapperPath)
	}
	if _, err := os.Stat(stalePath); !os.IsNotExist(err) {
		t.Fatalf("stale inner-bundle content survived restaging: %v", err)
	}
	if _, err := os.Stat(outerSentinel); err != nil {
		t.Fatalf("outer wrapper was replaced instead of preserved: %v", err)
	}
}

// Restaging an unchanged build would needlessly replace the inner bundle.
func TestStageWrapperReusesWrapperWhenBuildUnchanged(t *testing.T) {
	dir := t.TempDir()
	bundlePath := filepath.Join(dir, "gkNextRenderer.app")
	if err := os.Mkdir(bundlePath, 0o755); err != nil {
		t.Fatal(err)
	}

	sameHash := func(string) (string, error) { return "cdhash-1", nil }
	if _, err := stageWrapper(bundlePath, mkdirCopy, sameHash); err != nil {
		t.Fatalf("first stageWrapper() error = %v", err)
	}

	copied := false
	staged, err := stageWrapper(bundlePath, func(sourcePath, destinationPath string) error {
		copied = true
		return mkdirCopy(sourcePath, destinationPath)
	}, sameHash)
	if err != nil {
		t.Fatalf("second stageWrapper() error = %v", err)
	}
	if copied {
		t.Fatal("stageWrapper() restaged an unchanged build, which re-triggers Gatekeeper approval")
	}
	if staged.Restaged {
		t.Fatal("stageWrapper() reused the wrapper but reported it as restaged")
	}
	if _, err := os.Stat(filepath.Join(staged.WrapperPath, "WrappedBundle")); err != nil {
		t.Fatalf("reused wrapper is incomplete: %v", err)
	}
}

// An interrupted stage can leave the copy in place without the symlink; reusing
// that half-built wrapper would ship an incomplete app layout.
func TestStageWrapperRebuildsWhenSymlinkMissing(t *testing.T) {
	dir := t.TempDir()
	bundlePath := filepath.Join(dir, "gkNextRenderer.app")
	if err := os.Mkdir(bundlePath, 0o755); err != nil {
		t.Fatal(err)
	}

	sameHash := func(string) (string, error) { return "cdhash-1", nil }
	first, err := stageWrapper(bundlePath, mkdirCopy, sameHash)
	if err != nil {
		t.Fatalf("first stageWrapper() error = %v", err)
	}
	if err := os.Remove(filepath.Join(first.WrapperPath, "WrappedBundle")); err != nil {
		t.Fatal(err)
	}

	staged, err := stageWrapper(bundlePath, mkdirCopy, sameHash)
	if err != nil {
		t.Fatalf("second stageWrapper() error = %v", err)
	}
	if !staged.Restaged {
		t.Fatal("stageWrapper() reused an incomplete wrapper instead of rebuilding it")
	}
	if _, err := os.Lstat(filepath.Join(staged.WrapperPath, "WrappedBundle")); err != nil {
		t.Fatalf("WrappedBundle symlink was not restored: %v", err)
	}
}

func TestParseCDHash(t *testing.T) {
	t.Run("reads the hash", func(t *testing.T) {
		output := "Identifier=gknext.renderer\nCandidateCDHash sha256=aaaa\nCDHash=c8127a54a45fddf5551ff0b858803aa6a36caeff\nTeamIdentifier=N5XNVW358C\n"
		got, err := parseCDHash(output, "app")
		if err != nil {
			t.Fatalf("parseCDHash() error = %v", err)
		}
		if want := "c8127a54a45fddf5551ff0b858803aa6a36caeff"; got != want {
			t.Fatalf("parseCDHash() = %q, want %q", got, want)
		}
	})

	// An unsigned bundle reports no CDHash; treating that as a valid hash would
	// make an unsigned wrapper look up to date.
	t.Run("rejects output without a hash", func(t *testing.T) {
		if _, err := parseCDHash("Executable=app\nIdentifier=gknext.renderer\n", "app"); err == nil {
			t.Fatal("parseCDHash() accepted output that has no CDHash")
		}
	})
}

func mkdirCopy(_, destinationPath string) error {
	return os.MkdirAll(destinationPath, 0o755)
}

// changedHash reports a different hash per call so staging always rebuilds.
func changedHash(path string) (string, error) {
	return path + "-unique", nil
}

func TestReadArtifactRejectsMissingBundle(t *testing.T) {
	repoRoot := t.TempDir()
	manifestPath := filepath.Join(repoRoot, "out", "build", preset, "ios-app-"+artifactConfiguration+".json")
	if err := os.MkdirAll(filepath.Dir(manifestPath), 0o755); err != nil {
		t.Fatal(err)
	}
	data, err := json.Marshal(Artifact{BundlePath: filepath.Join(repoRoot, "missing.app")})
	if err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(manifestPath, data, 0o600); err != nil {
		t.Fatal(err)
	}

	if _, err := ReadArtifact(repoRoot); err == nil || !strings.Contains(err.Error(), "bundle not found") {
		t.Fatalf("ReadArtifact() error = %v, want missing bundle error", err)
	}
}

func writeArtifact(t *testing.T) string {
	t.Helper()
	repoRoot := t.TempDir()
	bundlePath := filepath.Join(repoRoot, "gkNextRenderer.app")
	if err := os.Mkdir(bundlePath, 0o755); err != nil {
		t.Fatal(err)
	}
	manifestPath := filepath.Join(repoRoot, "out", "build", preset, "ios-app-"+artifactConfiguration+".json")
	if err := os.MkdirAll(filepath.Dir(manifestPath), 0o755); err != nil {
		t.Fatal(err)
	}
	data, err := json.Marshal(Artifact{BundlePath: bundlePath, BundleID: "com.example.app"})
	if err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(manifestPath, data, 0o600); err != nil {
		t.Fatal(err)
	}
	return repoRoot
}

func equalTeams(got, want []Team) bool {
	if len(got) != len(want) {
		return false
	}
	for i := range got {
		if got[i] != want[i] {
			return false
		}
	}
	return true
}
