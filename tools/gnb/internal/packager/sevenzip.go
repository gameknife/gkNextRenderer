package packager

import (
	"fmt"
	"io"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"sort"
	"strings"
	"time"

	"github.com/gameknife/gknextrenderer/tools/gnb/internal/console"
)

func resolve7Zip() (string, error) {
	if configured := strings.TrimSpace(os.Getenv("GNB_7Z")); configured != "" {
		if info, err := os.Stat(configured); err == nil && !info.IsDir() {
			return configured, nil
		}
		return "", fmt.Errorf("GNB_7Z does not point to a file: %s", configured)
	}
	for _, name := range []string{"7zz", "7z", "7za"} {
		if found, err := exec.LookPath(name); err == nil {
			return found, nil
		}
	}
	if runtime.GOOS == "windows" {
		for _, base := range []string{os.Getenv("ProgramFiles"), os.Getenv("ProgramFiles(x86)")} {
			if base == "" {
				continue
			}
			candidate := filepath.Join(base, "7-Zip", "7z.exe")
			if info, err := os.Stat(candidate); err == nil && !info.IsDir() {
				return candidate, nil
			}
		}
	}
	return "", fmt.Errorf("7-Zip executable not found; install 7zz/7z/7za or set GNB_7Z")
}

func safeArchiveName(name string) (string, error) {
	name = strings.ReplaceAll(strings.TrimSpace(name), "\\", "/")
	clean := filepath.ToSlash(filepath.Clean(filepath.FromSlash(name)))
	if name == "" || clean == "." || strings.HasPrefix(name, "/") || filepath.IsAbs(filepath.FromSlash(name)) ||
		clean == ".." || strings.HasPrefix(clean, "../") || filepath.VolumeName(filepath.FromSlash(name)) != "" {
		return "", fmt.Errorf("unsafe archive path %q", name)
	}
	return clean, nil
}

func copyArchiveEntry(staging string, item entry) (int64, error) {
	name, err := safeArchiveName(item.name)
	if err != nil {
		return 0, err
	}
	info, err := os.Stat(item.source)
	if err != nil {
		return 0, err
	}
	destination := filepath.Join(staging, filepath.FromSlash(name))
	if err := os.MkdirAll(filepath.Dir(destination), 0o755); err != nil {
		return 0, err
	}
	in, err := os.Open(item.source)
	if err != nil {
		return 0, err
	}
	defer in.Close()
	out, err := os.OpenFile(destination, os.O_CREATE|os.O_TRUNC|os.O_WRONLY, info.Mode().Perm())
	if err != nil {
		return 0, err
	}
	size, copyErr := io.Copy(out, in)
	closeErr := out.Close()
	if copyErr != nil {
		return 0, copyErr
	}
	if closeErr != nil {
		return 0, closeErr
	}
	return size, nil
}

func write7zArchive(dst string, entries []entry) error {
	sevenZip, err := resolve7Zip()
	if err != nil {
		return err
	}
	dst, err = filepath.Abs(dst)
	if err != nil {
		return err
	}
	if err := os.MkdirAll(filepath.Dir(dst), 0o755); err != nil {
		return err
	}
	staging, err := os.MkdirTemp(filepath.Dir(dst), ".gnb-7z-stage-")
	if err != nil {
		return err
	}
	defer cleanup7zStaging(staging)

	seen := make(map[string]bool, len(entries))
	var total int64
	for _, item := range entries {
		name, nameErr := safeArchiveName(item.name)
		if nameErr != nil {
			return nameErr
		}
		if seen[name] {
			return fmt.Errorf("duplicate archive path %q", name)
		}
		seen[name] = true
		size, copyErr := copyArchiveEntry(staging, item)
		if copyErr != nil {
			return copyErr
		}
		total += size
	}
	if err := os.Remove(dst); err != nil && !os.IsNotExist(err) {
		return err
	}
	cmd := exec.Command(sevenZip, "a", "-t7z", "-mx=9", "-m0=lzma2", "-md=128m", "-mfb=273", "-ms=on", "-mmt=on", "-y", dst, ".")
	cmd.Dir = staging
	if output, runErr := cmd.CombinedOutput(); runErr != nil {
		return fmt.Errorf("7-Zip create failed: %w (output: %s)", runErr, strings.TrimSpace(string(output)))
	}
	archiveInfo, err := os.Stat(dst)
	if err != nil {
		return err
	}
	console.Success("package written: %s (%d files, %.1f MB -> %.1f MB)", dst, len(entries),
		float64(total)/(1024*1024), float64(archiveInfo.Size())/(1024*1024))
	return nil
}

func cleanup7zStaging(staging string) {
	var lastErr error
	for attempt := 0; attempt < 10; attempt++ {
		lastErr = os.RemoveAll(staging)
		if lastErr == nil {
			return
		}
		// Antivirus and DLL scanners can briefly retain a handle after 7z exits.
		time.Sleep(time.Duration(attempt+1) * 100 * time.Millisecond)
	}
	console.Warn("unable to remove temporary 7z staging directory %s: %v", staging, lastErr)
}

func list7zEntries(sevenZip, archive string) ([]string, error) {
	cmd := exec.Command(sevenZip, "l", "-slt", "-ba", archive)
	output, err := cmd.CombinedOutput()
	if err != nil {
		return nil, fmt.Errorf("7-Zip list failed: %w (output: %s)", err, strings.TrimSpace(string(output)))
	}
	var names []string
	for _, line := range strings.Split(strings.ReplaceAll(string(output), "\r\n", "\n"), "\n") {
		if !strings.HasPrefix(line, "Path = ") {
			continue
		}
		name, pathErr := safeArchiveName(strings.TrimPrefix(line, "Path = "))
		if pathErr != nil {
			return nil, pathErr
		}
		names = append(names, name)
	}
	if len(names) == 0 {
		return nil, fmt.Errorf("7-Zip archive contains no entries: %s", archive)
	}
	sort.Strings(names)
	return names, nil
}

func extract7zArchive(archive, staging string) ([]string, error) {
	sevenZip, err := resolve7Zip()
	if err != nil {
		return nil, err
	}
	names, err := list7zEntries(sevenZip, archive)
	if err != nil {
		return nil, err
	}
	cmd := exec.Command(sevenZip, "x", "-y", "-o"+staging, archive)
	if output, runErr := cmd.CombinedOutput(); runErr != nil {
		return nil, fmt.Errorf("7-Zip extract failed: %w (output: %s)", runErr, strings.TrimSpace(string(output)))
	}
	return names, nil
}
