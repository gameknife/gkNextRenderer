package packager

import (
	"archive/zip"
	"bufio"
	"context"
	"fmt"
	"io"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"sync"
	"time"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/console"
)

// SmokeOptions controls how far `gnb smoke` goes.
type SmokeOptions struct {
	// Launch runs each target for real and waits for the scene-upload marker.
	// Requires a working Vulkan device, so CI leaves it off.
	Launch bool
	// LaunchTimeout bounds each real launch.
	LaunchTimeout time.Duration
	// Keep leaves the extracted staging directory in place for inspection.
	Keep bool
	// StagingDir overrides the extraction directory.
	StagingDir string
}

// requiredEntries are archive members that must exist for the package to be
// usable straight out of the zip.
var requiredEntries = []string{
	"README.txt",
	"LICENSE",
	"THIRD-PARTY-NOTICES.md",
	"assets/configs/cvar_default.json",
	"assets/shaders",
	"assets/textures",
	"assets/locale",
	"assets/fonts",
	"assets/scripts",
	"assets/remote",
}

// forbiddenSuffixes must never appear in a public package.
var forbiddenSuffixes = []string{".pdb", ".ilk", ".exp", ".lib", ".obj"}

// sceneReadyMarker is the engine log line that proves the runtime reached a
// usable state: the scene is parsed, its GPU resources are built and committed.
// Emitted by Engine::SceneLoad (Engine.SceneLoad.cpp).
const sceneReadyMarker = "committed scene"

// Smoke extracts a release archive into a clean directory and verifies that it
// is self-contained: required files present, no debug artifacts, and every
// shipped executable starts and resolves its dynamic dependencies.
func Smoke(archive string, opts SmokeOptions) error {
	if opts.LaunchTimeout <= 0 {
		opts.LaunchTimeout = 90 * time.Second
	}

	staging := opts.StagingDir
	if staging == "" {
		dir, err := os.MkdirTemp("", "gnb-smoke-")
		if err != nil {
			return err
		}
		staging = dir
	} else if err := os.MkdirAll(staging, 0o755); err != nil {
		return err
	}
	if !opts.Keep {
		defer os.RemoveAll(staging)
	}

	console.Header("package smoke test")
	console.Label("archive", archive)
	console.Label("staging", staging)

	names, err := extractArchive(archive, staging)
	if err != nil {
		return err
	}
	console.Info("extracted %d files", len(names))

	var failures []string
	failures = append(failures, checkForbidden(names)...)
	failures = append(failures, checkRequired(staging)...)

	for _, target := range releaseTargets {
		exe := filepath.Join(staging, "bin", target+platformExeExt())
		if _, statErr := os.Stat(exe); statErr != nil {
			failures = append(failures, fmt.Sprintf("missing executable bin/%s%s", target, platformExeExt()))
			continue
		}
		if launchErr := runHelp(exe, staging); launchErr != nil {
			failures = append(failures, fmt.Sprintf("%s --help failed: %v", target, launchErr))
			continue
		}
		console.Success("%s starts and prints usage", target)

		if !opts.Launch {
			continue
		}
		if launchErr := runUntilSceneReady(exe, staging, opts.LaunchTimeout); launchErr != nil {
			failures = append(failures, fmt.Sprintf("%s launch failed: %v", target, launchErr))
			continue
		}
		console.Success("%s reached '%s'", target, sceneReadyMarker)
	}

	if len(failures) > 0 {
		for _, failure := range failures {
			console.Error("%s", failure)
		}
		return fmt.Errorf("package smoke test failed with %d problem(s)", len(failures))
	}
	console.Success("package smoke test passed")
	return nil
}

func extractArchive(archive string, staging string) ([]string, error) {
	reader, err := zip.OpenReader(archive)
	if err != nil {
		return nil, err
	}
	defer reader.Close()

	names := make([]string, 0, len(reader.File))
	for _, file := range reader.File {
		name := filepath.ToSlash(file.Name)
		if strings.Contains(name, "..") {
			return nil, fmt.Errorf("archive contains unsafe path %q", name)
		}
		names = append(names, name)
		if file.FileInfo().IsDir() {
			continue
		}
		destination := filepath.Join(staging, filepath.FromSlash(name))
		if err := os.MkdirAll(filepath.Dir(destination), 0o755); err != nil {
			return nil, err
		}
		in, err := file.Open()
		if err != nil {
			return nil, err
		}
		out, err := os.OpenFile(destination, os.O_CREATE|os.O_TRUNC|os.O_WRONLY, file.Mode()|0o600)
		if err != nil {
			_ = in.Close()
			return nil, err
		}
		_, err = io.Copy(out, in)
		_ = in.Close()
		closeErr := out.Close()
		if err != nil {
			return nil, err
		}
		if closeErr != nil {
			return nil, closeErr
		}
	}
	return names, nil
}

func checkForbidden(names []string) []string {
	var failures []string
	for _, name := range names {
		lower := strings.ToLower(name)
		for _, suffix := range forbiddenSuffixes {
			if strings.HasSuffix(lower, suffix) {
				failures = append(failures, "package contains build artifact "+name)
				break
			}
		}
	}
	return failures
}

func checkRequired(staging string) []string {
	var failures []string
	for _, rel := range requiredEntries {
		if _, err := os.Stat(filepath.Join(staging, filepath.FromSlash(rel))); err != nil {
			failures = append(failures, "package is missing "+rel)
		}
	}
	return failures
}

// runHelp proves the executable loads: on Windows an unresolved DLL fails here,
// and on Linux a missing shared object does the same.
func runHelp(exe string, workDir string) error {
	ctx, cancel := context.WithTimeout(context.Background(), 60*time.Second)
	defer cancel()

	cmd := exec.CommandContext(ctx, exe, "--help")
	cmd.Dir = workDir
	output, err := cmd.CombinedOutput()
	if err != nil {
		return fmt.Errorf("%w (output: %s)", err, strings.TrimSpace(truncate(string(output), 400)))
	}
	if !strings.Contains(string(output), "--load-scene") {
		return fmt.Errorf("usage output did not list the expected options")
	}
	return nil
}

// runUntilSceneReady launches the target headless and waits for the engine to
// report a live scene, then shuts it down. Targets that exit on their own
// (benchmarks) are accepted as long as they exit cleanly.
func runUntilSceneReady(exe string, workDir string, timeout time.Duration) error {
	ctx, cancel := context.WithTimeout(context.Background(), timeout)
	defer cancel()

	cmd := exec.CommandContext(ctx, exe, "--hidden-window", "--width", "640", "--height", "360")
	cmd.Dir = workDir
	stdout, err := cmd.StdoutPipe()
	if err != nil {
		return err
	}
	cmd.Stderr = cmd.Stdout
	if err := cmd.Start(); err != nil {
		return err
	}

	ready := make(chan struct{})
	var tail strings.Builder
	var once sync.Once
	go func() {
		scanner := bufio.NewScanner(stdout)
		scanner.Buffer(make([]byte, 0, 64*1024), 1024*1024)
		for scanner.Scan() {
			line := scanner.Text()
			tail.WriteString(line)
			tail.WriteString("\n")
			if strings.Contains(line, sceneReadyMarker) {
				once.Do(func() { close(ready) })
			}
		}
	}()

	done := make(chan error, 1)
	go func() { done <- cmd.Wait() }()

	select {
	case <-ready:
		_ = cmd.Process.Kill()
		<-done
		return nil
	case waitErr := <-done:
		if waitErr == nil {
			return nil
		}
		return fmt.Errorf("%w (log tail: %s)", waitErr, truncate(lastLines(tail.String(), 12), 800))
	case <-ctx.Done():
		_ = cmd.Process.Kill()
		<-done
		return fmt.Errorf("timed out before '%s' (log tail: %s)", sceneReadyMarker, truncate(lastLines(tail.String(), 12), 800))
	}
}

func lastLines(text string, count int) string {
	lines := strings.Split(strings.TrimRight(text, "\n"), "\n")
	if len(lines) > count {
		lines = lines[len(lines)-count:]
	}
	return strings.Join(lines, " | ")
}

func truncate(text string, limit int) string {
	if len(text) <= limit {
		return text
	}
	return text[:limit] + "..."
}
