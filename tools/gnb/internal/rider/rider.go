package rider

import (
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"sort"
	"strings"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/console"
)

// Launch starts JetBrains Rider with the repository's root CMake project.
// Rider is detached so gnb can return immediately after handing off the IDE.
func Launch(repoRoot string) (string, error) {
	cmakePath := filepath.Join(repoRoot, "CMakeLists.txt")
	if _, err := os.Stat(cmakePath); err != nil {
		return "", fmt.Errorf("root CMake project not found: %s: %w", cmakePath, err)
	}

	executable, err := FindExecutable()
	if err != nil {
		return "", err
	}
	if err := RemoveProjectMetadata(repoRoot); err != nil {
		return "", err
	}

	console.Command(executable, cmakePath)
	cmd := exec.Command(executable, cmakePath)
	cmd.Dir = repoRoot
	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr
	if err := cmd.Start(); err != nil {
		return "", fmt.Errorf("start Rider: %w", err)
	}
	_ = cmd.Process.Release()
	return executable, nil
}

// RemoveProjectMetadata clears Rider's generated project state before every
// launch. The current Rider version can fail to build this CMake project when
// stale metadata is present in the repository.
func RemoveProjectMetadata(repoRoot string) error {
	ideaPath := filepath.Join(repoRoot, ".idea")
	info, err := os.Lstat(ideaPath)
	if os.IsNotExist(err) {
		return nil
	}
	if err != nil {
		return fmt.Errorf("inspect Rider metadata %s: %w", ideaPath, err)
	}
	if !info.IsDir() {
		return fmt.Errorf("Rider metadata path is not a directory: %s", ideaPath)
	}
	if err := os.RemoveAll(ideaPath); err != nil {
		return fmt.Errorf("remove Rider metadata %s: %w", ideaPath, err)
	}
	return nil
}

// FindExecutable resolves Rider from PATH and the standard JetBrains install
// locations. The returned path is suitable for exec.Command.
func FindExecutable() (string, error) {
	for _, name := range pathCommandNames() {
		if executable, err := exec.LookPath(name); err == nil {
			return executable, nil
		}
	}

	for _, pattern := range installPatterns() {
		matches, err := filepath.Glob(pattern)
		if err != nil {
			continue
		}
		sort.Strings(matches)
		for index := len(matches) - 1; index >= 0; index-- {
			if info, err := os.Stat(matches[index]); err == nil && !info.IsDir() {
				return matches[index], nil
			}
		}
	}

	return "", fmt.Errorf("Rider executable not found; install Rider or add rider64/rider to PATH")
}

func pathCommandNames() []string {
	if runtime.GOOS == "windows" {
		return []string{"rider64.exe", "rider.exe", "rider64", "rider"}
	}
	return []string{"rider", "rider.sh", "rider64"}
}

func installPatterns() []string {
	patterns := make([]string, 0, 8)
	switch runtime.GOOS {
	case "windows":
		for _, root := range []string{os.Getenv("ProgramFiles"), os.Getenv("ProgramFiles(x86)")} {
			if root != "" {
				patterns = append(patterns, filepath.Join(root, "JetBrains", "JetBrains Rider*", "bin", "rider64.exe"))
			}
		}
		if localAppData := os.Getenv("LOCALAPPDATA"); localAppData != "" {
			patterns = append(patterns,
				filepath.Join(localAppData, "JetBrains", "Installations", "Rider*", "bin", "rider64.exe"),
				filepath.Join(localAppData, "JetBrains", "Toolbox", "apps", "Rider", "*", "*", "bin", "rider64.exe"),
			)
		}
	case "darwin":
		patterns = append(patterns,
			"/Applications/Rider.app/Contents/MacOS/rider",
			filepath.Join(os.Getenv("HOME"), "Applications", "Rider.app", "Contents", "MacOS", "rider"),
			filepath.Join(os.Getenv("HOME"), "Library", "Application Support", "JetBrains", "Toolbox", "apps", "Rider", "*", "*", "*", "Rider.app", "Contents", "MacOS", "rider"),
		)
	default:
		patterns = append(patterns,
			"/opt/jetbrains/rider*/bin/rider.sh",
			filepath.Join(os.Getenv("HOME"), ".local", "share", "JetBrains", "Installations", "Rider*", "bin", "rider.sh"),
		)
	}

	result := make([]string, 0, len(patterns))
	for _, pattern := range patterns {
		if strings.TrimSpace(pattern) != "" {
			result = append(result, pattern)
		}
	}
	return result
}
