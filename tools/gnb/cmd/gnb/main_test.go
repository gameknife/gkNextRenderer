package main

import (
	"os"
	"path/filepath"
	"reflect"
	"strings"
	"testing"
	"time"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/config"
	"github.com/spf13/cobra"
)

func TestResolvePackagePresetUsesConfiguredDefault(t *testing.T) {
	cfg := config.Config{Package: config.PackageConfig{
		DefaultPreset: "default",
		Presets: map[string]config.PackagePresetConfig{
			"default": {
				Targets:             []string{"gkNextRenderer"},
				ArchiveName:         "renderer_{platform}_{version}.7z",
				AlwaysIncludeAssets: []string{"assets/models/playground.glb"},
			},
		},
	}}
	preset, err := resolvePackagePreset(cfg, "")
	if err != nil {
		t.Fatal(err)
	}
	if preset.Name != "default" || len(preset.Targets) != 1 || preset.Targets[0] != "gkNextRenderer" {
		t.Fatalf("resolved preset = %+v", preset)
	}
	if len(preset.AlwaysIncludeAssets) != 1 || preset.AlwaysIncludeAssets[0] != "assets/models/playground.glb" {
		t.Fatalf("resolved always-include assets = %v", preset.AlwaysIncludeAssets)
	}
}

func TestResolveIOSSkipCodeSignDefault(t *testing.T) {
	cmd := &cobra.Command{Use: "ios"}
	cmd.Flags().Bool("skip-codesign", true, "")
	cmd.Flags().Bool("codesign", false, "")

	got, err := resolveIOSSkipCodeSign(cmd, true, false)
	if err != nil {
		t.Fatalf("resolveIOSSkipCodeSign returned error: %v", err)
	}
	if !got {
		t.Fatalf("resolveIOSSkipCodeSign() = %v, want true", got)
	}
}

func TestResolveIOSSkipCodeSignWithCodesignFlag(t *testing.T) {
	cmd := &cobra.Command{Use: "ios"}
	skipCodeSign := true
	codeSign := false
	cmd.Flags().BoolVar(&skipCodeSign, "skip-codesign", true, "")
	cmd.Flags().BoolVar(&codeSign, "codesign", false, "")
	if err := cmd.ParseFlags([]string{"--codesign"}); err != nil {
		t.Fatalf("ParseFlags returned error: %v", err)
	}

	got, err := resolveIOSSkipCodeSign(cmd, skipCodeSign, codeSign)
	if err != nil {
		t.Fatalf("resolveIOSSkipCodeSign returned error: %v", err)
	}
	if got {
		t.Fatalf("resolveIOSSkipCodeSign() = %v, want false", got)
	}
}

func TestResolveIOSSkipCodeSignRejectsConflictingFlags(t *testing.T) {
	cmd := &cobra.Command{Use: "ios"}
	skipCodeSign := true
	codeSign := false
	cmd.Flags().BoolVar(&skipCodeSign, "skip-codesign", true, "")
	cmd.Flags().BoolVar(&codeSign, "codesign", false, "")
	if err := cmd.ParseFlags([]string{"--skip-codesign", "--codesign"}); err != nil {
		t.Fatalf("ParseFlags returned error: %v", err)
	}

	if _, err := resolveIOSSkipCodeSign(cmd, skipCodeSign, codeSign); err == nil {
		t.Fatal("resolveIOSSkipCodeSign() error = nil, want conflict error")
	}
}

func TestTyposCommandReportsMissingExecutable(t *testing.T) {
	t.Setenv("PATH", t.TempDir())

	err := newTyposCommand(appContext{repoRoot: t.TempDir()}).RunE(nil, nil)
	if err == nil || !strings.Contains(err.Error(), "typos is not installed") {
		t.Fatalf("expected missing typos error, got %v", err)
	}
}

func TestParseRunArgsPassesTargetArgsWithoutSeparator(t *testing.T) {
	opts, showHelp, err := parseRunArgs("windows", []string{"gkNextRenderer", "--help"})
	if err != nil {
		t.Fatalf("parseRunArgs returned error: %v", err)
	}
	if showHelp {
		t.Fatal("parseRunArgs showHelp = true, want false")
	}
	if opts.Target != "gkNextRenderer" {
		t.Fatalf("Target = %q, want gkNextRenderer", opts.Target)
	}
	if !reflect.DeepEqual(opts.Args, []string{"--help"}) {
		t.Fatalf("Args = %#v, want --help", opts.Args)
	}
}

func TestParseRunArgsKeepsRunFlagsBeforeTarget(t *testing.T) {
	opts, showHelp, err := parseRunArgs("windows", []string{"--dry-run", "--scene", "Demo.proc", "--present-mode=mailbox", "gkNextRenderer", "--help"})
	if err != nil {
		t.Fatalf("parseRunArgs returned error: %v", err)
	}
	if showHelp {
		t.Fatal("parseRunArgs showHelp = true, want false")
	}
	if !opts.DryRun {
		t.Fatal("DryRun = false, want true")
	}
	if opts.Target != "gkNextRenderer" {
		t.Fatalf("Target = %q, want gkNextRenderer", opts.Target)
	}
	if !reflect.DeepEqual(opts.Scenes, []string{"Demo.proc"}) {
		t.Fatalf("Scenes = %#v, want Demo.proc", opts.Scenes)
	}
	if !reflect.DeepEqual(opts.PresentModes, []string{"mailbox"}) {
		t.Fatalf("PresentModes = %#v, want mailbox", opts.PresentModes)
	}
	if !reflect.DeepEqual(opts.Args, []string{"--help"}) {
		t.Fatalf("Args = %#v, want --help", opts.Args)
	}
}

func TestParseRunArgsShowsRunHelpWithoutTarget(t *testing.T) {
	_, showHelp, err := parseRunArgs("windows", []string{"--help"})
	if err != nil {
		t.Fatalf("parseRunArgs returned error: %v", err)
	}
	if !showHelp {
		t.Fatal("parseRunArgs showHelp = false, want true")
	}
}

func TestShotRunArgsIncludesUI(t *testing.T) {
	got := shotRunArgs(60, true, []string{"--width=1280"})
	want := []string{
		"--agent-validation",
		"--agent-validation-frames=60",
		"--agent-validation-ui",
		"--width=1280",
	}
	if !reflect.DeepEqual(got, want) {
		t.Fatalf("shotRunArgs() = %#v, want %#v", got, want)
	}
}

func TestShotRunArgsKeepsDefaultCaptureClean(t *testing.T) {
	got := shotRunArgs(0, false, nil)
	want := []string{"--agent-validation"}
	if !reflect.DeepEqual(got, want) {
		t.Fatalf("shotRunArgs() = %#v, want %#v", got, want)
	}
}

func TestLoadValidateScriptHints(t *testing.T) {
	dir := t.TempDir()
	path := filepath.Join(dir, "smoke.agentscript.json")
	body := `{"name":"smoke","target":"ScadStudio","scene":"assets/scad/source/beer_cup.scad","viewport":{"width":1024,"height":768},"steps":[]}`
	if err := os.WriteFile(path, []byte(body), 0644); err != nil {
		t.Fatal(err)
	}
	hints := loadValidateScriptHints(path)
	if hints.Name != "smoke" || hints.Target != "ScadStudio" || hints.Scene != "assets/scad/source/beer_cup.scad" {
		t.Fatalf("hints = %+v", hints)
	}
	if hints.Viewport.Width != 1024 || hints.Viewport.Height != 768 {
		t.Fatalf("viewport = %+v", hints.Viewport)
	}
}

func TestRemoteRunArgsIncludesRemoteFlags(t *testing.T) {
	got := remoteRunArgs(remoteCmdOptions{
		Bind:          "0.0.0.0",
		Resolution:    "1280x720",
		Encoder:       "vulkan",
		HttpPort:      9000,
		SignalingPort: 9001,
		BitrateKbps:   6000,
		Fps:           60,
		ShowWindow:    true,
	}, []string{"--present-mode=0"})
	want := []string{
		"--remote",
		"--remote-bind=0.0.0.0",
		"--remote-http-port=9000",
		"--remote-port=9001",
		"--remote-bitrate=6000",
		"--remote-fps=60",
		"--remote-encoder=vulkan",
		"--remote-res=1280x720",
		"--remote-show-window",
		"--present-mode=0",
	}
	if !reflect.DeepEqual(got, want) {
		t.Fatalf("remoteRunArgs() = %#v, want %#v", got, want)
	}
}

func TestBuildRemoteAccessURLsWildcardIncludesLoopbackAndLan(t *testing.T) {
	got := buildRemoteAccessURLs("0.0.0.0", 8088, []string{"192.168.1.22", "10.0.0.9", "192.168.1.22"})
	want := []string{
		"http://127.0.0.1:8088",
		"http://localhost:8088",
		"http://10.0.0.9:8088",
		"http://192.168.1.22:8088",
	}
	if !reflect.DeepEqual(got, want) {
		t.Fatalf("buildRemoteAccessURLs() = %#v, want %#v", got, want)
	}
}

func TestBuildRemoteAccessURLSSpecificBindUsesSingleHost(t *testing.T) {
	got := buildRemoteAccessURLs("192.168.50.12", 8088, []string{"10.0.0.9"})
	want := []string{"http://192.168.50.12:8088"}
	if !reflect.DeepEqual(got, want) {
		t.Fatalf("buildRemoteAccessURLs() = %#v, want %#v", got, want)
	}
}

func TestTodoAddWithSpecTextCreatesLinkedSpec(t *testing.T) {
	dir := t.TempDir()
	specDir := filepath.Join(dir, ".spec")
	if err := os.MkdirAll(specDir, 0755); err != nil {
		t.Fatal(err)
	}
	todo := `# TODO

## Milestone: 测试  <!-- status: active -->

### 下一步

(暂无)

### 待规划

(暂无)

### 最近完成

(暂无)
`
	if err := os.WriteFile(filepath.Join(specDir, "TODO.md"), []byte(todo), 0644); err != nil {
		t.Fatal(err)
	}

	cmd := newTodoAddCommand(appContext{repoRoot: dir})
	cmd.SetArgs([]string{"-t", "feat", "测试任务", "--spec-text", "## 背景\n\n详细背景"})
	if err := cmd.Execute(); err != nil {
		t.Fatalf("todo add: %v", err)
	}

	todoAfter, err := os.ReadFile(filepath.Join(specDir, "TODO.md"))
	if err != nil {
		t.Fatal(err)
	}
	if !strings.Contains(string(todoAfter), "- [ ] `#00001` [FEAT] 测试任务 → specs/00001.md") {
		t.Fatalf("TODO missing linked spec:\n%s", todoAfter)
	}
	specBody, err := os.ReadFile(filepath.Join(specDir, "specs", "00001.md"))
	if err != nil {
		t.Fatal(err)
	}
	if !strings.Contains(string(specBody), "详细背景") {
		t.Fatalf("spec body missing detail:\n%s", specBody)
	}
}

func TestTodoNextWaitReturnsExistingTaskImmediately(t *testing.T) {
	dir := t.TempDir()
	writeTestTODO(t, dir, `- [ ] `+"`#00048`"+` [IDEA] 等待命令`)

	start := time.Now()
	out, err := runTodoNext(appContext{repoRoot: dir}, todoNextOptions{
		wait:    true,
		timeout: time.Second,
		poll:    10 * time.Millisecond,
	})
	if err != nil {
		t.Fatalf("runTodoNext: %v", err)
	}
	if !out.Found || out.ID != 48 {
		t.Fatalf("out = %+v, want found #00048", out)
	}
	if elapsed := time.Since(start); elapsed > 200*time.Millisecond {
		t.Fatalf("runTodoNext waited %s despite existing task", elapsed)
	}
}

func TestTodoNextWaitReturnsWhenTodoChanges(t *testing.T) {
	dir := t.TempDir()
	writeTestTODO(t, dir, "(暂无)")

	start := time.Now()
	writeDone := make(chan error, 1)
	go func() {
		time.Sleep(100 * time.Millisecond)
		writeDone <- writeTestTODOFile(dir, `- [ ] `+"`#00049`"+` [BUG] 新任务`)
	}()

	out, err := runTodoNext(appContext{repoRoot: dir}, todoNextOptions{
		wait:    true,
		timeout: 2 * time.Second,
		poll:    time.Second,
	})
	if err != nil {
		t.Fatalf("runTodoNext: %v", err)
	}
	if err := <-writeDone; err != nil {
		t.Fatalf("async TODO write: %v", err)
	}
	if !out.Found || out.ID != 49 {
		t.Fatalf("out = %+v, want found #00049", out)
	}
	if out.WaitedMillis <= 0 {
		t.Fatalf("WaitedMillis = %d, want > 0", out.WaitedMillis)
	}
	if elapsed := time.Since(start); elapsed >= 700*time.Millisecond {
		t.Fatalf("runTodoNext took %s; file notification did not beat 1s polling fallback", elapsed)
	}
}

func TestTodoNextWaitTimesOutWithoutTask(t *testing.T) {
	dir := t.TempDir()
	writeTestTODO(t, dir, "(暂无)")

	out, err := runTodoNext(appContext{repoRoot: dir}, todoNextOptions{
		wait:    true,
		timeout: 30 * time.Millisecond,
		poll:    10 * time.Millisecond,
	})
	if err != nil {
		t.Fatalf("runTodoNext: %v", err)
	}
	if out.Found {
		t.Fatalf("out = %+v, want no task", out)
	}
	if !out.TimedOut {
		t.Fatalf("TimedOut = false, want true; out = %+v", out)
	}
}

func TestDashboardRejectsBrowserAndNoOpenTogether(t *testing.T) {
	err := runDashboard(appContext{}, dashboardCmdOpts{Browser: true, NoOpen: true})
	if err == nil || !strings.Contains(err.Error(), "cannot be used together") {
		t.Fatalf("runDashboard error = %v, want conflicting flags error", err)
	}
}

func writeTestTODO(t *testing.T, repoRoot string, nextSection string) {
	t.Helper()
	if err := writeTestTODOFile(repoRoot, nextSection); err != nil {
		t.Fatal(err)
	}
}

func writeTestTODOFile(repoRoot string, nextSection string) error {
	specDir := filepath.Join(repoRoot, ".spec")
	if err := os.MkdirAll(specDir, 0755); err != nil {
		return err
	}
	todo := `# TODO

## Milestone: 测试  <!-- status: active -->

### 下一步

` + nextSection + `

### 待规划

(暂无)

### 最近完成

(暂无)
`
	return os.WriteFile(filepath.Join(specDir, "TODO.md"), []byte(todo), 0644)
}
